#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <future>
#include <iostream>
#include <mutex>
#include <string>
#include <system_error>
#include <thread>

#include "terminal/terminal_input_reader.h"

namespace {
using namespace asciiomium::terminal;
using namespace std::chrono_literals;
int failures = 0;
thread_local DWORD fake_error = ERROR_SUCCESS;

void Require(bool condition, const char* message) {
  if (!condition) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}

template <typename Predicate>
void Until(Predicate predicate, const char* message) {
  const auto deadline = std::chrono::steady_clock::now() + 3s;
  while (!predicate()) {
    if (std::chrono::steady_clock::now() >= deadline) {
      std::cerr << "FAIL: deadline: " << message << std::endl;
      std::_Exit(2);
    }
    std::this_thread::yield();
  }
}

struct Input {
  std::mutex mutex;
  std::condition_variable changed;
  std::string pending;
  std::size_t offset = 0;
  std::atomic_uint read_calls{0};
  std::atomic_uint cancel_calls{0};
  bool reading = false;
  bool cancelled = false;
  bool release = false;
  bool race_before_read = false;
  bool ignore_cancel = false;
  bool finish_on_cancel_failure = false;
  bool eof = false;
  DWORD read_failure = ERROR_SUCCESS;

  TerminalConsoleApi api{};

  Input() {
    api.context = this;
    api.last_error = [](void*) noexcept { return fake_error; };
    api.read = [](void* context, HANDLE, void* bytes, DWORD capacity,
                  DWORD* count) noexcept -> BOOL {
      auto& self = *static_cast<Input*>(context);
      self.read_calls.fetch_add(1);
      std::unique_lock lock(self.mutex);
      if (self.race_before_read) {
        self.changed.wait(lock, [&] { return self.cancel_calls.load() > 0; });
      }
      self.reading = true;
      self.changed.wait(lock, [&] {
        return self.cancelled || self.release || self.eof ||
               self.read_failure != ERROR_SUCCESS || self.offset < self.pending.size();
      });
      self.reading = false;
      if (self.cancelled || self.release) {
        fake_error = ERROR_OPERATION_ABORTED;
        return FALSE;
      }
      if (self.read_failure != ERROR_SUCCESS) {
        fake_error = self.read_failure;
        return FALSE;
      }
      *count = self.eof ? 0 : static_cast<DWORD>(
          std::min<std::size_t>(capacity, self.pending.size() - self.offset));
      std::memcpy(bytes, self.pending.data() + self.offset, *count);
      self.offset += *count;
      return TRUE;
    };
    api.cancel_synchronous_io = [](void* context, HANDLE) noexcept -> BOOL {
      auto& self = *static_cast<Input*>(context);
      self.cancel_calls.fetch_add(1);
      std::lock_guard lock(self.mutex);
      self.changed.notify_all();
      if (self.finish_on_cancel_failure) {
        self.release = true;
        fake_error = ERROR_ACCESS_DENIED;
        return FALSE;
      }
      if (self.ignore_cancel) return TRUE;
      if (!self.reading) {
        fake_error = ERROR_NOT_FOUND;
        return FALSE;
      }
      self.cancelled = true;
      return TRUE;
    };
  }

  void Append(std::string bytes) {
    std::lock_guard lock(mutex);
    pending += bytes;
    changed.notify_all();
  }
  bool Reading() {
    std::lock_guard lock(mutex);
    return reading;
  }
  void Release() {
    std::lock_guard lock(mutex);
    release = true;
    changed.notify_all();
  }
};

void TestNonblockingByteStream() {
  Input input;
  TerminalInputReader reader(nullptr, input.api);
  reader.Start();
  Until([&] { return input.Reading(); }, "blocked read starts");
  for (int i = 0; i < 100; ++i) {
    Require(reader.TakeAvailable(3).empty(), "UI poll does not wait for blocked OS read");
  }
  const std::string expected = "x\xC2\xA3\xE2\x82\xAC\xF0\x9F\x98\x80"
                               "\x1b[A\x1bOP\x1b[<0;3;4M\x03";
  input.Append(expected);
  std::string received;
  unsigned fragment = 0;
  Until([&] {
    received += reader.TakeAvailable(1 + fragment++ % 3);
    return received.size() >= expected.size();
  }, "fragmented UTF-8/VT byte delivery");
  Require(received == expected, "mailbox preserves every UTF-8/CSI/SS3/mouse byte");
  Until([&] { return input.read_calls.load() >= 2 && input.Reading(); },
        "reader immediately resumes without an input-event gate");
  Require(reader.StopAndJoin().joined, "idle blocked read cancels and joins");
  Require(input.cancel_calls.load() > 0, "blocked read uses cancellation");
}

void TestBoundAndBackpressure() {
  Input input;
  const std::string expected(TerminalInputReader::kCapacity * 3 + 17, 'q');
  input.Append(expected);
  TerminalInputReader reader(nullptr, input.api);
  reader.Start();
  std::string received;
  Until([&] { received += reader.TakeAvailable(1); return !received.empty(); },
        "first full mailbox is published");
  Require(input.read_calls.load() == 1,
          "worker cannot read another chunk while mailbox has unread bytes");
  Until([&] {
    const auto bytes = reader.TakeAvailable(1000000);
    Require(bytes.size() <= TerminalInputReader::kCapacity,
            "one poll cannot exceed fixed mailbox capacity");
    received += bytes;
    return received.size() >= expected.size();
  }, "backpressured stream drains");
  Require(received == expected, "backpressure does not discard bytes");
  Require(reader.StopAndJoin().joined, "backpressured reader joins");

  Input full;
  full.Append(std::string(TerminalInputReader::kCapacity, 'z'));
  TerminalInputReader waiting(nullptr, full.api);
  waiting.Start();
  Until([&] { return waiting.TakeAvailable(1) == "z"; }, "nonempty mailbox wait");
  Require(waiting.StopAndJoin().joined && full.read_calls.load() == 1,
          "stop wakes mailbox backpressure without issuing another read");
}

void TestCancelEntryRaceAndCompletion() {
  Input racing;
  racing.race_before_read = true;
  TerminalInputReader reader(nullptr, racing.api);
  reader.Start();
  Until([&] { return racing.read_calls.load() == 1; }, "read paused before kernel entry");
  Require(reader.StopAndJoin().joined && racing.cancel_calls.load() >= 2,
          "ERROR_NOT_FOUND before read entry is retried until actual completion");

  Input failed_cancel;
  failed_cancel.finish_on_cancel_failure = true;
  TerminalInputReader completed(nullptr, failed_cancel.api);
  completed.Start();
  Until([&] { return failed_cancel.Reading(); }, "read before failed cancel");
  Require(completed.StopAndJoin().joined,
          "completion wins over a failed cancellation request");

  Input ignored;
  ignored.ignore_cancel = true;
  TerminalInputReader retained(nullptr, ignored.api);
  retained.Start();
  Until([&] { return ignored.Reading(); }, "uncancellable read starts");
  const auto timed_out = retained.StopAndJoin(30ms);
  Require(!timed_out.joined && timed_out.error == ERROR_TIMEOUT && !retained.finished(),
          "successful cancel request alone cannot claim a completed drain");
  // State and buffer must still exist after the failed drain. Release the fake
  // driver explicitly, then join; the session-level fatal case is a child test.
  ignored.Release();
  Require(retained.StopAndJoin().joined, "failed drain retains worker until real completion");
}

void TestReadErrors() {
  for (bool eof : {false, true}) {
    Input input;
    input.eof = eof;
    input.read_failure = eof ? ERROR_SUCCESS : ERROR_READ_FAULT;
    TerminalInputReader reader(nullptr, input.api);
    reader.Start();
    Until([&] { return reader.finished(); }, "read error completion");
    bool caught = false;
    try { (void)reader.TakeAvailable(1); }
    catch (const std::system_error& error) {
      caught = error.code().value() ==
          static_cast<int>(eof ? ERROR_HANDLE_EOF : ERROR_READ_FAULT);
    }
    Require(caught && input.read_calls.load() == 1,
            "read failure/EOF is surfaced without a hot retry loop");
    Require(reader.StopAndJoin().joined, "failed reader still joins");
  }
}
}  // namespace

int main() {
  TestNonblockingByteStream();
  TestBoundAndBackpressure();
  TestCancelEntryRaceAndCompletion();
  TestReadErrors();
  if (failures) return 1;
  std::cout << "Terminal input mailbox/cancellation tests passed\n";
  return 0;
}
