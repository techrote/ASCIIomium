#include "terminal/terminal_input_reader.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <stdexcept>
#include <system_error>

namespace asciiomium::terminal {

struct TerminalInputReader::State {
  State(HANDLE handle, const TerminalConsoleApi& operations)
      : input(handle), api(operations) {}

  HANDLE input;
  TerminalConsoleApi api;
  std::array<char, kCapacity> bytes{};
  std::size_t offset = 0;
  std::size_t available = 0;
  bool stop = false;
  DWORD read_error = ERROR_SUCCESS;
  std::atomic_bool finished{true};
  std::mutex mutex;
  std::condition_variable changed;
};

TerminalInputReader::TerminalInputReader(HANDLE input,
                                         const TerminalConsoleApi& api)
    : state_(std::make_shared<State>(input, api)) {}

TerminalInputReader::~TerminalInputReader() {
  const auto drain = StopAndJoin();
  if (!drain.joined) FatalDrainFailure(drain.error);
}

void TerminalInputReader::Start() {
  std::unique_lock lock(state_->mutex);
  if (worker_.joinable()) throw std::logic_error("terminal reader already started");
  if (state_->stop) throw std::logic_error("terminal reader is stopping");
  if (!state_->api.read || !state_->api.cancel_synchronous_io) {
    throw std::invalid_argument("terminal reader requires read/cancel operations");
  }
  state_->finished.store(false, std::memory_order_release);
  try {
    // The worker owns its state independently; it never refers to a session or
    // its locks. The input handle and injected API context still must outlive
    // the worker, which normal teardown proves by joining it.
    worker_ = std::thread([state = state_] { Run(state); });
  } catch (...) {
    state_->finished.store(true, std::memory_order_release);
    throw;
  }
}

void TerminalInputReader::RequestStop() noexcept {
  {
    std::lock_guard lock(state_->mutex);
    state_->stop = true;
  }
  state_->changed.notify_all();
}

bool TerminalInputReader::finished() const noexcept {
  return state_->finished.load(std::memory_order_acquire);
}

std::string TerminalInputReader::TakeAvailable(std::size_t max_bytes) {
  std::unique_lock lock(state_->mutex);
  if (state_->stop || max_bytes == 0) return {};
  if (state_->read_error != ERROR_SUCCESS) {
    throw std::system_error(static_cast<int>(state_->read_error),
                            std::system_category(), "ReadFile(terminal input)");
  }
  const auto count = std::min(max_bytes, state_->available);
  std::string result(state_->bytes.data() + state_->offset, count);
  state_->offset += count;
  state_->available -= count;
  if (state_->available == 0) {
    state_->offset = 0;
    lock.unlock();
    state_->changed.notify_all();
  }
  return result;
}

void TerminalInputReader::Run(const std::shared_ptr<State>& state) noexcept {
  std::unique_lock lock(state->mutex);
  for (;;) {
    state->changed.wait(lock, [&] { return state->stop || state->available == 0; });
    if (state->stop) break;
    lock.unlock();

    // Read directly, without Wait/Peek gates. The console can hold a cached
    // UTF-8 suffix even when no new input record signals its handle.
    DWORD read = 0;
    const BOOL ok = state->api.read(state->api.context, state->input,
                                    state->bytes.data(),
                                    static_cast<DWORD>(state->bytes.size()), &read);
    DWORD error = ERROR_SUCCESS;
    if (!ok) {
      error = state->api.last_error(state->api.context);
      if (error == ERROR_SUCCESS) error = ERROR_GEN_FAILURE;
    } else if (read == 0) {
      error = ERROR_HANDLE_EOF;
    } else if (read > state->bytes.size()) {
      error = ERROR_INVALID_DATA;
    }

    lock.lock();
    if (state->stop) break;
    if (error != ERROR_SUCCESS) {
      state->read_error = error;
      break;
    }
    state->offset = 0;
    state->available = read;
  }
  state->finished.store(true, std::memory_order_release);
  lock.unlock();
  state->changed.notify_all();
}

InputDrainResult TerminalInputReader::StopAndJoin(
    std::chrono::milliseconds timeout) noexcept {
  timeout = std::clamp(timeout, std::chrono::milliseconds(0),
                       std::chrono::milliseconds(2000));
  RequestStop();
  if (!worker_.joinable()) return {};
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  DWORD cancellation_error = ERROR_SUCCESS;
  while (!finished()) {
    // CancelSynchronousIo only requests cancellation. ERROR_NOT_FOUND can be
    // the interval after stop was observed by the caller but before ReadFile
    // entered the kernel, so repeat until actual completion or the deadline.
    if (!state_->api.cancel_synchronous_io(
            state_->api.context, reinterpret_cast<HANDLE>(worker_.native_handle()))) {
      const DWORD error = state_->api.last_error(state_->api.context);
      if (error != ERROR_NOT_FOUND && cancellation_error == ERROR_SUCCESS) {
        cancellation_error = error == ERROR_SUCCESS ? ERROR_GEN_FAILURE : error;
      }
    }
    std::unique_lock lock(state_->mutex);
    if (state_->finished.load(std::memory_order_acquire)) break;
    const auto now = std::chrono::steady_clock::now();
    if (now >= deadline) {
      return {false, cancellation_error == ERROR_SUCCESS ? ERROR_TIMEOUT
                                                        : cancellation_error};
    }
    state_->changed.wait_until(
        lock, std::min(deadline, now + std::chrono::milliseconds(10)),
        [&] { return state_->finished.load(std::memory_order_acquire); });
  }
  // The completion flag is published after the final read and before the
  // thread returns. Bound the remaining native thread lifetime before join;
  // completion of ReadFile alone does not prove thread exit.
  const auto remaining = std::max(std::chrono::milliseconds(0),
      std::chrono::duration_cast<std::chrono::milliseconds>(
          deadline - std::chrono::steady_clock::now()));
  const DWORD wait = WaitForSingleObject(
      reinterpret_cast<HANDLE>(worker_.native_handle()),
      static_cast<DWORD>(remaining.count()));
  if (wait != WAIT_OBJECT_0) {
    DWORD error = wait == WAIT_FAILED ? GetLastError() : ERROR_TIMEOUT;
    if (error == ERROR_SUCCESS) error = ERROR_GEN_FAILURE;
    return {false, error};
  }
  worker_.join();
  return {};
}

void TerminalInputReader::FatalDrainFailure(DWORD error) noexcept {
  std::fprintf(stderr,
      "Terminal input reader did not stop (OS error %lu); terminal cleanup is "
      "incomplete, input restoration not confirmed. Terminating with active "
      "reader state intact.\n", static_cast<unsigned long>(error));
  std::fflush(stderr);
  std::_Exit(kFatalDrainExitCode);
}

}  // namespace asciiomium::terminal
