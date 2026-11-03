#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <thread>

#include "terminal/terminal_session.h"

// Cleanup is noexcept and must not allocate, including mouse-disable output.
// Keep the rejection local to the test thread so unrelated CRT work is not a
// source of nondeterminism. The fake console below uses fixed-size storage.
thread_local bool reject_allocations = false;

void* operator new(std::size_t size) {
  if (reject_allocations) throw std::bad_alloc();
  if (void* memory = std::malloc(std::max<std::size_t>(1, size))) return memory;
  throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }

namespace {

using namespace asciiomium::terminal;
using namespace std::chrono_literals;
int failures = 0;

void Require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

[[noreturn]] void DeadlineFailure(const char* message) {
  std::cerr << "FAIL: bounded wait expired: " << message << std::endl;
  // Do not let a future destructor turn a detected deadlock into a hung test.
  std::_Exit(2);
}

template <typename T>
T Await(std::future<T>& future, const char* message) {
  if (future.wait_for(3s) != std::future_status::ready) DeadlineFailure(message);
  return future.get();
}

template <typename Predicate>
void AwaitCondition(Predicate predicate, const char* message) {
  const auto deadline = std::chrono::steady_clock::now() + 3s;
  while (!predicate()) {
    if (std::chrono::steady_clock::now() >= deadline) DeadlineFailure(message);
    std::this_thread::yield();
  }
}

class Gate {
 public:
  void Pause() noexcept {
    std::unique_lock lock(mutex_);
    entered_ = true;
    changed_.notify_all();
    if (!changed_.wait_for(lock, 3s, [&] { return released_; })) {
      DeadlineFailure("release injected console call");
    }
  }
  void WaitForEntry() {
    std::unique_lock lock(mutex_);
    if (!changed_.wait_for(lock, 3s, [&] { return entered_; })) {
      DeadlineFailure("enter injected console call");
    }
  }
  void Release() {
    std::lock_guard lock(mutex_);
    released_ = true;
    changed_.notify_all();
  }

 private:
  std::mutex mutex_;
  std::condition_variable changed_;
  bool entered_ = false;
  bool released_ = false;
};

enum class Operation {
  GetInputMode, GetOutputMode, GetInputPage, GetOutputPage,
  SetInputMode, SetOutputMode, SetInputPage, SetOutputPage,
  InstallHandler, RemoveHandler, Geometry, Write,
};

struct FakeConsole {
  static constexpr DWORD kInputMode = ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT |
                                     ENABLE_PROCESSED_INPUT;
  static constexpr DWORD kOutputMode = ENABLE_PROCESSED_OUTPUT;
  static constexpr UINT kInputPage = 437;
  static constexpr UINT kOutputPage = 1252;
  static HANDLE Input() { return reinterpret_cast<HANDLE>(std::uintptr_t{1}); }
  static HANDLE Output() { return reinterpret_cast<HANDLE>(std::uintptr_t{2}); }

  DWORD input_mode = kInputMode;
  DWORD output_mode = kOutputMode;
  UINT input_page = kInputPage;
  UINT output_page = kOutputPage;
  PHANDLER_ROUTINE handler = nullptr;
  std::array<Operation, 256> operations{};
  std::array<char, 4096> output{};
  std::size_t calls = 0;
  std::size_t output_size = 0;
  std::array<bool, 256> fail{};
  bool mutate_before_failure = false;
  std::size_t zero_write = 256;
  DWORD write_limit = 0x7fffffffu;
  std::size_t pause_call = 256;
  Gate gate;
  std::atomic<DWORD> error{ERROR_SUCCESS};
  std::mutex log_mutex;
  TerminalConsoleApi api;

  FakeConsole() : api{
      this,
      [](void* context, HANDLE handle, DWORD* value) noexcept -> BOOL {
        auto& self = *static_cast<FakeConsole*>(context);
        const auto index = self.Record(handle == Input()
            ? Operation::GetInputMode : Operation::GetOutputMode);
        if (self.Fail(index)) return FALSE;
        *value = handle == Input() ? self.input_mode : self.output_mode;
        return TRUE;
      },
      [](void* context) noexcept -> UINT {
        auto& self = *static_cast<FakeConsole*>(context);
        const auto index = self.Record(Operation::GetInputPage);
        return self.Fail(index) ? 0 : self.input_page;
      },
      [](void* context) noexcept -> UINT {
        auto& self = *static_cast<FakeConsole*>(context);
        const auto index = self.Record(Operation::GetOutputPage);
        return self.Fail(index) ? 0 : self.output_page;
      },
      [](void* context, HANDLE handle, DWORD value) noexcept -> BOOL {
        auto& self = *static_cast<FakeConsole*>(context);
        const auto index = self.Record(handle == Input()
            ? Operation::SetInputMode : Operation::SetOutputMode);
        if (self.mutate_before_failure) {
          (handle == Input() ? self.input_mode : self.output_mode) = value;
        }
        if (self.Fail(index)) return FALSE;
        (handle == Input() ? self.input_mode : self.output_mode) = value;
        return TRUE;
      },
      [](void* context, UINT value) noexcept -> BOOL {
        auto& self = *static_cast<FakeConsole*>(context);
        const auto index = self.Record(Operation::SetInputPage);
        if (self.Fail(index)) return FALSE;
        self.input_page = value;
        return TRUE;
      },
      [](void* context, UINT value) noexcept -> BOOL {
        auto& self = *static_cast<FakeConsole*>(context);
        const auto index = self.Record(Operation::SetOutputPage);
        if (self.Fail(index)) return FALSE;
        self.output_page = value;
        return TRUE;
      },
      [](void* context, PHANDLER_ROUTINE handler, BOOL add) noexcept -> BOOL {
        auto& self = *static_cast<FakeConsole*>(context);
        const auto index = self.Record(add ? Operation::InstallHandler
                                           : Operation::RemoveHandler);
        self.Pause(index);
        if (self.Fail(index)) return FALSE;
        self.handler = add ? handler : nullptr;
        return TRUE;
      },
      [](void* context, HANDLE, CONSOLE_SCREEN_BUFFER_INFO* info) noexcept -> BOOL {
        auto& self = *static_cast<FakeConsole*>(context);
        const auto index = self.Record(Operation::Geometry);
        if (self.Fail(index)) return FALSE;
        *info = {};
        info->srWindow = {0, 0, 79, 23};
        return TRUE;
      },
      [](void* context, HANDLE, const void* data, DWORD size,
         DWORD* written) noexcept -> BOOL {
        auto& self = *static_cast<FakeConsole*>(context);
        const auto index = self.Record(Operation::Write);
        if (self.Fail(index)) return FALSE;
        *written = index == self.zero_write ? 0 : std::min(size, self.write_limit);
        {
          std::lock_guard lock(self.log_mutex);
          if (*written > self.output.size() - self.output_size) std::abort();
          std::memcpy(self.output.data() + self.output_size, data, *written);
          self.output_size += *written;
        }
        self.Pause(index);
        return TRUE;
      },
      [](void* context) noexcept -> DWORD {
        return static_cast<FakeConsole*>(context)->error.load();
      },
      [](void*, HANDLE, wchar_t*, DWORD, DWORD* read) noexcept -> BOOL {
        *read = 0;  // Transport errors are exercised by terminal_input_tests.
        return TRUE;
      },
      [](void*, HANDLE) noexcept -> BOOL { return TRUE; }} {}

  std::size_t Record(Operation operation) noexcept {
    std::lock_guard lock(log_mutex);
    if (calls == operations.size()) std::abort();
    operations[calls] = operation;
    return calls++;
  }
  bool Fail(std::size_t index) noexcept {
    if (!fail[index]) return false;
    error.store(static_cast<DWORD>(500 + index));
    return true;
  }
  void Pause(std::size_t index) noexcept {
    if (index == pause_call) gate.Pause();
  }
  void ResetLog() {
    calls = 0;
    output_size = 0;
    fail.fill(false);
    error.store(ERROR_SUCCESS);
  }
  bool Baseline() const {
    return input_mode == kInputMode && output_mode == kOutputMode &&
           input_page == kInputPage && output_page == kOutputPage;
  }
  std::string Bytes() {
    std::lock_guard lock(log_mutex);
    return std::string(output.data(), output_size);
  }
};

TerminalSessionOptions FullOptions() {
  TerminalSessionOptions options;
  options.raw_keyboard_input = true;
  options.mouse_tracking = MouseTrackingMode::AnyEvent;
  return options;
}

std::string CleanupBytes() {
  return std::string(kDisableSgrMouse) + kDisableAnyEventMouse + kResetSgr +
         kShowCursor + kLeaveAlternateScreen;
}

bool SameResult(TerminalRestoreResult left, TerminalRestoreResult right) {
  return left.completed == right.completed &&
         left.failed_steps == right.failed_steps &&
         left.first_error == right.first_error;
}

void TestRollbackAndRetryAcquisition() {
  // Fail every acquisition operation, from the first query through mouse
  // enable output. Every possible mutation is owned before its setter call.
  for (std::size_t failed = 0; failed < 12; ++failed) {
    FakeConsole console;
    console.fail[failed] = true;
    bool caught = false;
    try {
      TerminalSession session(FullOptions(), console.Input(), console.Output(),
                              console.api);
    } catch (const TerminalSessionAcquisitionError& error) {
      caught = true;
      Require(error.restoration().succeeded(),
              "partial acquisition preserves successful rollback evidence");
    }
    Require(caught, "injected acquisition error is surfaced");
    Require(console.Baseline(), "partial acquisition restores owned console state");
    Require(console.handler == nullptr, "partial acquisition removes its handler");

    // An exception must also release the process-wide ownership slot.
    console.ResetLog();
    TerminalSession next(FullOptions(), console.Input(), console.Output(), console.api);
    Require(next.Restore().succeeded(), "reacquire after startup failure");
  }

  for (std::size_t failed : {std::size_t{4}, std::size_t{5}}) {
    FakeConsole mutated;
    mutated.mutate_before_failure = true;
    mutated.fail[failed] = true;
    bool caught = false;
    try {
      TerminalSession session(FullOptions(), mutated.Input(), mutated.Output(), mutated.api);
    } catch (const TerminalSessionAcquisitionError& error) {
      caught = true;
      Require(error.restoration().succeeded(), "mutating setter failure has a rollback result");
    }
    Require(caught && mutated.Baseline(),
            "mode setter that mutates before returning FALSE is rolled back");
  }

  FakeConsole console;
  console.fail[9] = true;   // Geometry fails after all console mutations.
  console.fail[10] = true;  // Rollback input code-page restore also fails.
  try {
    TerminalSession session(FullOptions(), console.Input(), console.Output(), console.api);
    Require(false, "startup plus rollback fault throws");
  } catch (const TerminalSessionAcquisitionError& error) {
    Require(error.restoration().completed && !error.restoration().succeeded() &&
                error.restoration().failed_steps ==
                    static_cast<std::uint32_t>(TerminalRestoreFailure::InputCodePage),
            "constructor exception retains failed rollback, not headless success");
    Require(error.restoration().first_error == 510,
            "constructor rollback retains immediate OS error");
  }
  Require(console.output_page == console.kOutputPage &&
              console.input_mode == console.kInputMode &&
              console.output_mode == console.kOutputMode && console.handler == nullptr,
          "rollback continues after a restoration failure");

  for (bool partial_mouse_enable : {false, true}) {
    FakeConsole partial;
    partial.write_limit = 3;
    const auto visual_chunks = (BuildAcquireSequence(true, true).size() + 2) / 3;
    partial.fail[10 + (partial_mouse_enable ? visual_chunks : 0) + 1] = true;
    bool caught = false;
    try {
      TerminalSession session(FullOptions(), partial.Input(), partial.Output(), partial.api);
    } catch (const TerminalSessionAcquisitionError& error) {
      caught = true;
      Require(error.restoration().succeeded(),
              "partially written acquire sequence is rolled back");
    }
    Require(caught && partial.Baseline() && partial.handler == nullptr,
            "partial visual/mouse enable failure restores console and handler");
    const auto bytes = partial.Bytes();
    const auto expected = partial_mouse_enable ? CleanupBytes()
        : std::string(kResetSgr) + kShowCursor + kLeaveAlternateScreen;
    Require(bytes.size() >= expected.size() && bytes.ends_with(expected),
            "partial visual/mouse enable receives its full matching cleanup");
  }
}

void TestRestoreFailuresAndNoAllocation() {
  constexpr std::array<TerminalRestoreFailure, 10> expected{
      TerminalRestoreFailure::MouseTracking, TerminalRestoreFailure::MouseTracking,
      TerminalRestoreFailure::Sgr, TerminalRestoreFailure::Cursor,
      TerminalRestoreFailure::AlternateScreen, TerminalRestoreFailure::InputCodePage,
      TerminalRestoreFailure::OutputCodePage, TerminalRestoreFailure::InputMode,
      TerminalRestoreFailure::OutputMode, TerminalRestoreFailure::ControlHandler};
  for (std::size_t failed = 0; failed < expected.size(); ++failed) {
    FakeConsole console;
    TerminalSession session(FullOptions(), console.Input(), console.Output(), console.api);
    const auto callback = console.handler;
    console.ResetLog();
    console.fail[failed] = true;
    reject_allocations = true;
    const auto result = session.Restore();
    reject_allocations = false;
    Require(result.completed && !result.succeeded(), "failed cleanup is not success");
    Require(result.failed_steps == static_cast<std::uint32_t>(expected[failed]) &&
                result.first_error == 500 + failed,
            "failed cleanup step and immediate OS error are retained");
    Require(console.calls == expected.size(), "one cleanup fault does not skip later steps");
    const auto repeated = session.Restore();
    Require(SameResult(result, repeated) && console.calls == expected.size(),
            "completed failure result is stable without replaying VT cleanup");
    Require(callback(CTRL_C_EVENT) == FALSE,
            "late callback sees no session even if handler removal failed");
  }

  FakeConsole console;
  TerminalSession session(FullOptions(), console.Input(), console.Output(), console.api);
  console.ResetLog();
  console.zero_write = 0;
  const auto result = session.Restore();
  Require(result.completed && result.failed_steps ==
              static_cast<std::uint32_t>(TerminalRestoreFailure::MouseTracking) &&
              result.first_error == ERROR_WRITE_FAULT && console.calls == 10,
          "zero-progress cleanup write records failure and continues");
}

void TestWriteFailureRollback() {
  for (bool zero_progress : {false, true}) {
    FakeConsole console;
    TerminalSession session(FullOptions(), console.Input(), console.Output(), console.api);
    console.ResetLog();
    console.write_limit = 3;
    if (zero_progress) console.zero_write = 1;
    else console.fail[1] = true;
    bool caught = false;
    try { session.Write("PAYLOAD"); }
    catch (const std::exception&) { caught = true; }
    Require(caught, "partial output failure reaches the caller");
    Require(session.Restore().succeeded() && console.Baseline(),
            "partial output failure does not prevent terminal restoration");
    Require(console.Bytes() == "PAY" + CleanupBytes(),
            "cleanup follows accepted prefix after a partial write failure");
  }
}

void TestSuccessfulAcquisitionUnwind() {
  struct StartupFailure {};
  FakeConsole console;
  bool acquired = false;
  bool caught = false;
  PHANDLER_ROUTINE callback = nullptr;
  try {
    TerminalSession session(FullOptions(), console.Input(), console.Output(), console.api);
    acquired = true;
    callback = console.handler;
    console.ResetLog();
    throw StartupFailure{};
  } catch (const StartupFailure&) {
    caught = true;
    Require(console.Baseline() && console.handler == nullptr,
            "outer startup-error handler observes restored modes/pages and no handler");
    Require(console.Bytes() == CleanupBytes(),
            "successful session unwinds visual/mouse state before the startup error is handled");
  }
  Require(acquired && caught, "startup exception occurs only after successful acquisition");
  Require(callback && callback(CTRL_CLOSE_EVENT) == FALSE,
          "callback cannot access the unwound terminal session");
  TerminalSession next(FullOptions(), console.Input(), console.Output(), console.api);
  Require(next.Restore().succeeded() && console.Baseline(),
          "a new terminal session can acquire after successful-session unwinding");
}

void TestWriteVersusRestore() {
  FakeConsole console;
  TerminalSession session(FullOptions(), console.Input(), console.Output(), console.api);
  console.ResetLog();
  console.write_limit = 3;
  console.pause_call = 0;
  auto writing = std::async(std::launch::async, [&] { session.Write("PAYLOAD"); });
  console.gate.WaitForEntry();
  auto restoring = std::async(std::launch::async, [&] { return session.Restore(); });
  AwaitCondition([&] { return session.stop_requested(); }, "restoration requested");
  auto late_write = std::async(std::launch::async, [&] {
    try { session.Write("LATE"); }
    catch (const std::logic_error&) { return true; }
    return false;
  });
  Require(restoring.wait_for(0ms) != std::future_status::ready,
          "restoration waits for the already accepted payload");
  console.gate.Release();
  Await(writing, "drain accepted write");
  Require(Await(restoring, "finish restoration").succeeded(), "ordered restoration succeeds");
  Require(Await(late_write, "reject late output"), "writes after restoration request are rejected");
  Require(console.Bytes() == "PAYLOAD" + CleanupBytes(),
          "every partial payload chunk precedes cleanup and no payload follows it");
}

void TestConcurrentRestore() {
  FakeConsole console;
  TerminalSession session(FullOptions(), console.Input(), console.Output(), console.api);
  console.ResetLog();
  console.pause_call = 0;
  auto first = std::async(std::launch::async, [&] { return session.Restore(); });
  console.gate.WaitForEntry();
  std::atomic_bool second_entering{false};
  auto second = std::async(std::launch::async, [&] {
    second_entering.store(true);
    return session.Restore();
  });
  AwaitCondition([&] { return second_entering.load(); }, "second restore dispatched");
  Require(second.wait_for(50ms) != std::future_status::ready,
          "competing Restore cannot report completion while cleanup is paused");
  console.gate.Release();
  const auto a = Await(first, "first restore completion");
  const auto b = Await(second, "second restore completion");
  Require(a.succeeded() && SameResult(a, b) && console.calls == 10,
          "concurrent Restore waits and returns one completed result");
}

void TestCallbackPinsLifetime() {
  FakeConsole console;
  auto session = std::make_unique<TerminalSession>(
      FullOptions(), console.Input(), console.Output(), console.api);
  const auto callback = console.handler;
  Require(callback(CTRL_C_EVENT) == TRUE && session->stop_requested(),
          "Ctrl+C requests graceful stop without restoration");
  Require(!console.Baseline(), "Ctrl+C leaves cleanup to normal teardown");
  console.ResetLog();
  console.pause_call = 0;
  auto handling = std::async(std::launch::async, [&] { return callback(CTRL_CLOSE_EVENT); });
  console.gate.WaitForEntry();
  std::atomic_bool destruction_entering{false};
  auto destroying = std::async(std::launch::async, [&] {
    destruction_entering.store(true);
    session.reset();
  });
  AwaitCondition([&] { return destruction_entering.load(); }, "destructor dispatched");
  Require(destroying.wait_for(50ms) != std::future_status::ready,
          "destruction waits while the callback uses the session");
  console.gate.Release();
  Require(Await(handling, "close callback completion") == FALSE,
          "close callback preserves Windows termination handling");
  Await(destroying, "destructor callback drain");
  Require(console.Baseline() && console.calls == 10,
          "callback and destructor share one terminal cleanup");
  Require(callback(CTRL_CLOSE_EVENT) == FALSE,
          "callback dispatched after destruction cannot access the old session");
}

void TestCallbackDuringHandlerRemoval() {
  FakeConsole console;
  TerminalSession session(FullOptions(), console.Input(), console.Output(), console.api);
  const auto callback = console.handler;
  console.ResetLog();
  console.pause_call = 9;  // All terminal cleanup has finished; unregister is paused.
  auto restoring = std::async(std::launch::async, [&] { return session.Restore(); });
  console.gate.WaitForEntry();
  auto handling = std::async(std::launch::async, [&] { return callback(CTRL_CLOSE_EVENT); });
  Require(Await(handling, "callback while unregister is blocked") == FALSE,
          "handler removal holds no gate needed by a callback");
  bool rejected = false;
  try {
    TerminalSession next(FullOptions(), console.Input(), console.Output(), console.api);
  } catch (const std::runtime_error&) { rejected = true; }
  Require(rejected, "new acquisition cannot race old handler removal");
  console.gate.Release();
  Require(Await(restoring, "unregister completion").succeeded(),
          "normal restoration completes after callback and removal");
}

}  // namespace

int main() {
  TestRollbackAndRetryAcquisition();
  TestRestoreFailuresAndNoAllocation();
  TestWriteFailureRollback();
  TestSuccessfulAcquisitionUnwind();
  TestWriteVersusRestore();
  TestConcurrentRestore();
  TestCallbackPinsLifetime();
  TestCallbackDuringHandlerRemoval();
  if (failures) {
    std::cerr << failures << " terminal lifecycle assertion(s) failed\n";
    return 1;
  }
  std::cout << "Terminal lifecycle fault and interleaving tests passed\n";
  return 0;
}
