#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <future>
#include <iostream>
#include <mutex>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>

#include "terminal/terminal_input_reader.h"
#include "terminal/utf16_input_encoder.h"

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
  std::u16string pending;
  std::size_t offset = 0;
  DWORD read_limit = static_cast<DWORD>(TerminalInputReader::kReadUnits);
  std::atomic_uint read_calls{0};
  std::atomic_uint cancel_calls{0};
  std::atomic_bool counts_initialized{true};
  bool reading = false;
  bool cancelled = false;
  bool release = false;
  bool race_before_read = false;
  bool ignore_cancel = false;
  bool finish_on_cancel_failure = false;
  bool eof = false;
  bool untouched_count = false;
  bool oversized_count = false;
  bool cancel_returns_true = false;
  DWORD read_failure = ERROR_SUCCESS;

  TerminalConsoleApi api{};

  Input() {
    api.context = this;
    api.last_error = [](void*) noexcept { return fake_error; };
    api.read_utf16 = [](void* context, HANDLE, wchar_t* units, DWORD capacity,
                        DWORD* count) noexcept -> BOOL {
      auto& self = *static_cast<Input*>(context);
      self.read_calls.fetch_add(1);
      if (*count != 0) self.counts_initialized.store(false);
      std::unique_lock lock(self.mutex);
      if (self.race_before_read) {
        self.changed.wait(lock, [&] { return self.cancel_calls.load() > 0; });
      }
      self.reading = true;
      self.changed.wait(lock, [&] {
        return self.cancelled || self.release || self.eof || self.untouched_count ||
               self.oversized_count ||
               self.read_failure != ERROR_SUCCESS || self.offset < self.pending.size();
      });
      self.reading = false;
      if (self.cancelled || self.release) {
        fake_error = ERROR_OPERATION_ABORTED;
        // Real ReadConsoleW cancellation can return TRUE with count untouched.
        return self.cancel_returns_true ? TRUE : FALSE;
      }
      if (self.read_failure != ERROR_SUCCESS) {
        fake_error = self.read_failure;
        return FALSE;
      }
      if (self.untouched_count) return TRUE;
      if (self.oversized_count) { *count = capacity + 1; return TRUE; }
      *count = self.eof ? 0 : static_cast<DWORD>(std::min<std::size_t>(
          std::min(capacity, self.read_limit), self.pending.size() - self.offset));
      for (DWORD i = 0; i < *count; ++i) {
        units[i] = static_cast<wchar_t>(self.pending[self.offset + i]);
      }
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

  void Append(std::u16string_view units) {
    std::lock_guard lock(mutex);
    pending += units;
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
  void FailRead(DWORD error) {
    std::lock_guard lock(mutex);
    read_failure = error;
    changed.notify_all();
  }
};

std::string Encode(std::u16string_view units) {
  Utf16InputEncoder encoder;
  std::string result;
  for (char16_t unit : units) {
    // Guard both ends of the maximum per-unit output, including malformed
    // high+BMP input that emits six bytes in one operation.
    std::array<char, Utf16InputEncoder::kMaxUnitBytes + 2> bytes{};
    bytes.front() = '\x55';
    bytes.back() = '\x66';
    const auto size = encoder.Append(static_cast<std::uint16_t>(unit),
        std::span<char, Utf16InputEncoder::kMaxUnitBytes>(
            bytes.data() + 1, Utf16InputEncoder::kMaxUnitBytes));
    Require(size <= Utf16InputEncoder::kMaxUnitBytes &&
                bytes.front() == '\x55' && bytes.back() == '\x66',
            "UTF-16 encoding stays within its fixed per-unit output");
    result.append(bytes.data() + 1, size);
  }
  return result;
}

void TestUtf16ScalarAndRecovery() {
  std::u16string ascii;
  std::string ascii_bytes;
  for (char16_t value = 0; value < 128; ++value) {
    ascii.push_back(value);
    ascii_bytes.push_back(static_cast<char>(value));
  }
  Require(Encode(ascii) == ascii_bytes,
          "ASCII including NUL, ESC, CR/LF and Ctrl-C remains byte-exact");
  Require(Encode(u"\u0080\u07ff\u0800\ud7ff\ue000\uffff\U00010000\U0010ffff") ==
              "\xc2\x80\xdf\xbf\xe0\xa0\x80\xed\x9f\xbf\xee\x80\x80"
              "\xef\xbf\xbf\xf0\x90\x80\x80\xf4\x8f\xbf\xbf",
          "UTF-8 length and Unicode scalar boundaries encode exactly");
  const std::string replacement = "\xef\xbf\xbd";
  Require(Encode(std::u16string{0xdc00, u'A'}) == replacement + "A",
          "isolated low surrogate cannot swallow following ASCII");
  Require(Encode(std::u16string{0xd800, u'\x1b', u'[', u'A'}) == replacement + "\x1b[A",
          "unmatched high surrogate preserves the following VT sequence");
  Require(Encode(std::u16string{0xd800, 0xd83d, 0xde00}) ==
              replacement + "\xf0\x9f\x98\x80",
          "a second high surrogate can still start a valid pair");
  Require(Encode(std::u16string{0xdbff, 0x0800}) == replacement + "\xe0\xa0\x80",
          "malformed carry plus three-byte BMP uses its full six-byte output");
  Require(Encode(std::u16string{0xd800}).empty(),
          "a trailing high surrogate waits instead of inventing an early replacement");
}

void ReadExpected(TerminalInputReader& reader, std::string_view expected,
                  const char* message) {
  std::string received;
  unsigned fragment = 0;
  Until([&] {
    received += reader.TakeAvailable(1 + fragment++ % 3);
    return received.size() >= expected.size();
  }, message);
  Require(received == expected, message);
}

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
  input.Append(u"x\u00a3\u20ac\U0001f600\x1b[A\x1bOP\x1b[<0;3;4M\x03");
  ReadExpected(reader, expected, "mailbox preserves every UTF-8/CSI/SS3/mouse byte");
  Until([&] { return input.read_calls.load() >= 2 && input.Reading(); },
        "reader immediately resumes without an input-event gate");
  Require(reader.StopAndJoin().joined, "idle blocked read cancels and joins");
  Require(input.cancel_calls.load() > 0, "blocked read uses cancellation");
  Require(input.counts_initialized.load(), "every native result count starts at zero");
}

void TestSurrogateReadBoundaries() {
  for (DWORD limit : {1u, 2u, 3u}) {
    Input input;
    input.read_limit = limit;
    input.Append(u"\u00a3\u20ac\U0001f600\x1b[A");
    TerminalInputReader reader(nullptr, input.api);
    reader.Start();
    ReadExpected(reader, "\xc2\xa3\xe2\x82\xac\xf0\x9f\x98\x80\x1b[A",
                 "one/two/three-unit native reads preserve split pairs and VT");
    Require(reader.StopAndJoin().joined, "short UTF-16 reader joins");
  }

  Input boundary;
  boundary.Append(std::u16string(TerminalInputReader::kReadUnits - 1, u'q') +
                  u"\U0001f600\x1bOP");
  TerminalInputReader split(nullptr, boundary.api);
  split.Start();
  ReadExpected(split, std::string(TerminalInputReader::kReadUnits - 1, 'q') +
                     "\xf0\x9f\x98\x80\x1bOP",
               "surrogate at the end of the maximum native read retains its partner");
  Require(split.StopAndJoin().joined, "boundary reader joins");

  Input carried;
  carried.Append(std::u16string{0xd800});
  TerminalInputReader maximum(nullptr, carried.api);
  maximum.Start();
  Until([&] { return carried.read_calls.load() == 2 && carried.Reading(); },
        "a high-only read continues without false EOF or publication");
  Require(maximum.TakeAvailable(10).empty(), "high-only read has no UTF-8 bytes yet");
  carried.Append(std::u16string(TerminalInputReader::kReadUnits, 0x0800));
  std::string expected = "\xef\xbf\xbd";
  for (std::size_t i = 0; i < TerminalInputReader::kReadUnits; ++i) {
    expected += "\xe0\xa0\x80";
  }
  ReadExpected(maximum, expected, "maximum BMP expansion plus malformed carry stays exact");
  Require(expected.size() == 3075 && maximum.StopAndJoin().joined,
          "maximum native output fits the fixed mailbox");

  Input stopped_carry;
  stopped_carry.Append(std::u16string{0xd83d});
  TerminalInputReader stopped(nullptr, stopped_carry.api);
  stopped.Start();
  Until([&] { return stopped_carry.read_calls.load() == 2 && stopped_carry.Reading(); },
        "stop fixture retains a high surrogate while awaiting its partner");
  Require(stopped.StopAndJoin().joined && stopped.TakeAvailable(10).empty(),
          "stop discards a pending half without emitting replacement input");

  Input failed_carry;
  failed_carry.Append(std::u16string{0xd83d});
  TerminalInputReader failed(nullptr, failed_carry.api);
  failed.Start();
  Until([&] { return failed_carry.read_calls.load() == 2 && failed_carry.Reading(); },
        "error fixture retains a high surrogate while awaiting its partner");
  failed_carry.FailRead(ERROR_READ_FAULT);
  Until([&] { return failed.finished(); }, "read failure terminates a pending surrogate");
  bool caught = false;
  try { (void)failed.TakeAvailable(10); }
  catch (const std::system_error& error) {
    caught = error.code().value() == static_cast<int>(ERROR_READ_FAULT);
  }
  Require(caught && failed.StopAndJoin().joined,
          "transport failure remains an error instead of invented replacement input");
}

void TestBoundAndBackpressure() {
  Input input;
  const std::string expected(TerminalInputReader::kCapacity * 3 + 17, 'q');
  input.Append(std::u16string(expected.size(), u'q'));
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
  full.Append(std::u16string(TerminalInputReader::kReadUnits, u'z'));
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

  Input cancelled_true;
  cancelled_true.cancel_returns_true = true;
  cancelled_true.Append(u"before-cancel");
  TerminalInputReader successful_cancel(nullptr, cancelled_true.api);
  successful_cancel.Start();
  ReadExpected(successful_cancel, "before-cancel", "input before TRUE cancellation");
  Until([&] { return cancelled_true.read_calls.load() == 2 && cancelled_true.Reading(); },
        "TRUE/untouched-count cancellation fixture is blocked");
  Require(successful_cancel.StopAndJoin().joined && cancelled_true.counts_initialized.load(),
          "stop accepts actual completion even when cancellation returns TRUE without a count");

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
  for (int kind = 0; kind < 4; ++kind) {
    Input input;
    input.eof = kind == 1;
    input.untouched_count = kind == 2;
    input.oversized_count = kind == 3;
    input.read_failure = kind == 0 ? ERROR_READ_FAULT : ERROR_SUCCESS;
    const DWORD expected = kind == 0 ? ERROR_READ_FAULT :
                           kind == 3 ? ERROR_INVALID_DATA : ERROR_HANDLE_EOF;
    TerminalInputReader reader(nullptr, input.api);
    reader.Start();
    Until([&] { return reader.finished(); }, "read error completion");
    bool caught = false;
    try { (void)reader.TakeAvailable(1); }
    catch (const std::system_error& error) {
      caught = error.code().value() == static_cast<int>(expected);
    }
    Require(caught && input.read_calls.load() == 1,
            "read failure/EOF/untouched or invalid count stops without a hot retry loop");
    Require(reader.StopAndJoin().joined, "failed reader still joins");
  }
}
}  // namespace

int main() {
  TestUtf16ScalarAndRecovery();
  TestNonblockingByteStream();
  TestSurrogateReadBoundaries();
  TestBoundAndBackpressure();
  TestCancelEntryRaceAndCompletion();
  TestReadErrors();
  if (failures) return 1;
  std::cout << "Terminal UTF-16/UTF-8 input mailbox/cancellation tests passed\n";
  return 0;
}
