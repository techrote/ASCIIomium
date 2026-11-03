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

#include "terminal/utf16_input_encoder.h"

namespace asciiomium::terminal {

struct TerminalInputReader::State {
  State(HANDLE handle, const TerminalConsoleApi& operations)
      : caller_input(handle), api(operations) {}

  HANDLE caller_input;
  HANDLE owned_input = INVALID_HANDLE_VALUE;
  TerminalConsoleApi api;
  std::array<char, kCapacity> bytes{};
  std::size_t offset = 0;
  std::size_t available = 0;
  bool stop = false;
  DWORD read_error = ERROR_SUCCESS;
  std::atomic_bool finished{true};
  std::atomic_bool input_released{true};
  std::mutex mutex;
  std::condition_variable changed;
};

TerminalInputReader::TerminalInputReader(HANDLE input,
                                         const TerminalConsoleApi& api)
    : state_(std::make_shared<State>(input, api)) {}

TerminalInputReader::~TerminalInputReader() {
  const auto drain = StopAndJoin();
  if (!drain.succeeded()) FatalDrainFailure(drain);
}

void TerminalInputReader::Start() {
  std::unique_lock lock(state_->mutex);
  if (worker_.joinable()) throw std::logic_error("terminal reader already started");
  if (state_->stop) throw std::logic_error("terminal reader is stopping");
  if (state_->owned_input != INVALID_HANDLE_VALUE) {
    throw std::logic_error("terminal reader already owns an input handle");
  }
  if (!state_->api.read_utf16 || !state_->api.cancel_synchronous_io ||
      !state_->api.open_input_reader || !state_->api.close_input_reader) {
    throw std::invalid_argument("terminal reader requires read/cancel/open/close operations");
  }
  const HANDLE input = state_->api.open_input_reader(
      state_->api.context, state_->caller_input);
  if (!input || input == INVALID_HANDLE_VALUE) {
    DWORD error = state_->api.last_error(state_->api.context);
    if (error == ERROR_SUCCESS) error = ERROR_GEN_FAILURE;
    throw std::system_error(static_cast<int>(error), std::system_category(),
                            "CreateFileW(CONIN$ reader)");
  }
  state_->owned_input = input;
  state_->input_released.store(false, std::memory_order_release);
  state_->finished.store(false, std::memory_order_release);
  try {
    // The worker owns its state independently; it never refers to a session or
    // its locks. The owned input handle and injected API context outlive the
    // joined worker. The caller's handle stays open throughout restoration.
    worker_ = std::thread([state = state_] { Run(state); });
  } catch (...) {
    state_->finished.store(true, std::memory_order_release);
    // Keep the opened handle owned until StopAndJoin/constructor rollback can
    // report its close result. Do not hide a close failure behind this error.
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

bool TerminalInputReader::input_released() const noexcept {
  return state_->input_released.load(std::memory_order_acquire);
}

std::string TerminalInputReader::TakeAvailable(std::size_t max_bytes) {
  std::unique_lock lock(state_->mutex);
  if (state_->stop || max_bytes == 0) return {};
  if (state_->read_error != ERROR_SUCCESS) {
    throw std::system_error(static_cast<int>(state_->read_error),
                            std::system_category(), "ReadConsoleW(terminal input)");
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
  std::array<wchar_t, kReadUnits> units{};
  Utf16InputEncoder encoder;
  // Each new UTF-16 unit needs at most three bytes, plus at most three for an
  // unmatched high surrogate carried from the previous read. Valid pairs use
  // fewer bytes. The final Append also has its full six-byte span available.
  static_assert(3 * (kReadUnits + 1) <= kCapacity);
  static_assert(3 * kReadUnits + Utf16InputEncoder::kMaxUnitBytes <= kCapacity);
  std::unique_lock lock(state->mutex);
  for (;;) {
    state->changed.wait(lock, [&] { return state->stop || state->available == 0; });
    if (state->stop) break;
    lock.unlock();

    // Never mix high-level reads with event reads/readiness gates or flush
    // retained typeahead. Explicit W input avoids the host's ANSI conversion.
    // Initialize the count: some console cancellation paths return TRUE
    // without writing it. Stop is checked before any result is published.
    DWORD read = 0;
    const BOOL ok = state->api.read_utf16(state->api.context, state->owned_input,
                                         units.data(),
                                         static_cast<DWORD>(units.size()), &read);
    DWORD error = ERROR_SUCCESS;
    if (!ok) {
      error = state->api.last_error(state->api.context);
      if (error == ERROR_SUCCESS) error = ERROR_GEN_FAILURE;
    } else if (read == 0) {
      error = ERROR_HANDLE_EOF;
    } else if (read > units.size()) {
      error = ERROR_INVALID_DATA;
    }

    lock.lock();
    if (state->stop) break;
    if (error != ERROR_SUCCESS) {
      state->read_error = error;
      break;
    }
    std::size_t size = 0;
    for (DWORD i = 0; i < read; ++i) {
      size += encoder.Append(static_cast<std::uint16_t>(units[i]),
          std::span<char, Utf16InputEncoder::kMaxUnitBytes>(
              state->bytes.data() + size, Utf16InputEncoder::kMaxUnitBytes));
    }
    state->offset = 0;
    state->available = size;
    // A read containing only a high surrogate has no encoded bytes yet. It
    // still made native progress; read again for its partner without a gate.
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
  if (!worker_.joinable()) return CloseInput();
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  DWORD cancellation_error = ERROR_SUCCESS;
  while (!finished()) {
    // CancelSynchronousIo only requests cancellation. ERROR_NOT_FOUND can be
    // the interval after stop was observed by the caller but before ReadConsoleW
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
      return {false, false, cancellation_error == ERROR_SUCCESS ? ERROR_TIMEOUT
                                                               : cancellation_error};
    }
    state_->changed.wait_until(
        lock, std::min(deadline, now + std::chrono::milliseconds(10)),
        [&] { return state_->finished.load(std::memory_order_acquire); });
  }
  // The completion flag is published after the final read and before the
  // thread returns. Bound the remaining native thread lifetime before join;
  // completion of ReadConsoleW alone does not prove thread exit.
  const auto remaining = std::max(std::chrono::milliseconds(0),
      std::chrono::duration_cast<std::chrono::milliseconds>(
          deadline - std::chrono::steady_clock::now()));
  const DWORD wait = WaitForSingleObject(
      reinterpret_cast<HANDLE>(worker_.native_handle()),
      static_cast<DWORD>(remaining.count()));
  if (wait != WAIT_OBJECT_0) {
    DWORD error = wait == WAIT_FAILED ? GetLastError() : ERROR_TIMEOUT;
    if (error == ERROR_SUCCESS) error = ERROR_GEN_FAILURE;
    return {false, false, error};
  }
  worker_.join();
  return CloseInput();
}

InputDrainResult TerminalInputReader::CloseInput() noexcept {
  if (state_->owned_input == INVALID_HANDLE_VALUE) return {};
  // Called only after join (or when thread creation never succeeded), without
  // callback/output/state gates. A separate CONIN$ object can dispose of a
  // host-side waiter that outlived client-side cancellation. Never close the
  // caller's handle or reopen/flush the shared console input buffer.
  if (!state_->api.close_input_reader(state_->api.context, state_->owned_input)) {
    DWORD error = state_->api.last_error(state_->api.context);
    if (error == ERROR_SUCCESS) error = ERROR_GEN_FAILURE;
    return {true, false, error};
  }
  state_->owned_input = INVALID_HANDLE_VALUE;
  state_->input_released.store(true, std::memory_order_release);
  return {};
}

void TerminalInputReader::FatalDrainFailure(InputDrainResult result) noexcept {
  if (!result.joined) {
    std::fprintf(stderr,
        "Terminal input reader did not stop (OS error %lu); terminal cleanup is "
        "incomplete, input restoration not confirmed. Terminating with active "
        "reader state intact.\n", static_cast<unsigned long>(result.error));
  } else {
    std::fprintf(stderr,
        "Terminal input reader has no live worker thread, but its owned console handle "
        "could not be closed (OS error %lu); terminal cleanup is incomplete, "
        "input restoration not confirmed. Terminating with handle ownership "
        "retained.\n", static_cast<unsigned long>(result.error));
  }
  std::fflush(stderr);
  std::_Exit(kFatalDrainExitCode);
}

}  // namespace asciiomium::terminal
