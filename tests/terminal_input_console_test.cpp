#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "terminal/terminal_session.h"

namespace {
using namespace asciiomium::terminal;
using namespace std::chrono_literals;

void Require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

void PrintBytes(const char* name, std::string_view bytes) noexcept {
  constexpr std::size_t limit = 96;
  std::fprintf(stderr, "%s_count=%zu %s_hex=", name, bytes.size(), name);
  for (std::size_t i = 0; i < std::min(bytes.size(), limit); ++i) {
    std::fprintf(stderr, "%02X", static_cast<unsigned char>(bytes[i]));
  }
  if (bytes.size() > limit) std::fputs("[truncated]", stderr);
  std::fputc('\n', stderr);
}

class Handle {
 public:
  explicit Handle(HANDLE value = INVALID_HANDLE_VALUE) : value_(value) {}
  ~Handle() { Close(); }
  Handle(const Handle&) = delete;
  Handle& operator=(const Handle&) = delete;
  HANDLE get() const { return value_; }
  void Close() {
    if (value_ && value_ != INVALID_HANDLE_VALUE) CloseHandle(value_);
    value_ = INVALID_HANDLE_VALUE;
  }
 private:
  HANDLE value_;
};

void DescribeConsoleOwner() noexcept {
  DWORD process_id = 0;
  const HWND window = GetConsoleWindow();
  const DWORD thread_id = window ? GetWindowThreadProcessId(window, &process_id) : 0;
  const DWORD owner_error = thread_id ? ERROR_SUCCESS : (window ? GetLastError() : ERROR_NOT_FOUND);
  Handle process(process_id ? OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, process_id) : nullptr);
  const DWORD open_error = process.get() ? ERROR_SUCCESS : (process_id ? GetLastError() : owner_error);
  std::array<wchar_t, 1024> path{};
  DWORD length = static_cast<DWORD>(path.size());
  const BOOL found = process.get() && QueryFullProcessImageNameW(process.get(), 0, path.data(), &length);
  const DWORD image_error = found ? ERROR_SUCCESS : (process.get() ? GetLastError() : open_error);
  std::fprintf(stderr, "CONSOLE OWNER: pid=%lu owner_error=%lu open_error=%lu image_error=%lu image=",
      static_cast<unsigned long>(process_id), static_cast<unsigned long>(owner_error),
      static_cast<unsigned long>(open_error), static_cast<unsigned long>(image_error));
  // This is best-effort context, not a qualification condition. Keep stderr
  // byte-oriented and bounded even if the image path contains non-ASCII text.
  if (found) {
    for (DWORD i = 0; i < std::min<DWORD>(length, static_cast<DWORD>(path.size())); ++i) {
      const auto value = static_cast<unsigned int>(path[i]);
      std::fputc(value <= 0x7fu ? static_cast<int>(value) : '?', stderr);
    }
  }
  std::fputc('\n', stderr);
  std::fflush(stderr);
}

struct Console {
  Handle input{CreateFileW(L"CONIN$", GENERIC_READ | GENERIC_WRITE,
      FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr)};
  Handle output{CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE,
      FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr)};
  DWORD input_mode = 0, output_mode = 0;
  UINT input_page = 0, output_page = 0;

  Console() {
    // This console belongs only to the test child. Establish a valid cooked
    // baseline so VT mode and UTF-8 setters must change state on every runner.
    Require(SetConsoleMode(input.get(), ENABLE_PROCESSED_INPUT | ENABLE_LINE_INPUT |
                                         ENABLE_ECHO_INPUT | ENABLE_EXTENDED_FLAGS) &&
            SetConsoleMode(output.get(), ENABLE_PROCESSED_OUTPUT | ENABLE_WRAP_AT_EOL_OUTPUT) &&
            SetConsoleCP(437) && SetConsoleOutputCP(437),
            "isolated console test modes");
    input_page = GetConsoleCP();
    output_page = GetConsoleOutputCP();
    Require(GetConsoleMode(input.get(), &input_mode) &&
            GetConsoleMode(output.get(), &output_mode) && input_page && output_page,
            "isolated real console baseline");
  }
  ~Console() {
    SetConsoleCP(input_page);
    SetConsoleOutputCP(output_page);
    SetConsoleMode(input.get(), input_mode);
    SetConsoleMode(output.get(), output_mode);
  }
  bool Restored() const {
    DWORD in = 0, out = 0;
    return GetConsoleMode(input.get(), &in) && GetConsoleMode(output.get(), &out) &&
           in == input_mode && out == output_mode &&
           GetConsoleCP() == input_page && GetConsoleOutputCP() == output_page;
  }
};

template <typename Predicate>
void Until(Predicate predicate, const char* message) {
  const auto deadline = std::chrono::steady_clock::now() + 5s;
  while (!predicate()) {
    Require(std::chrono::steady_clock::now() < deadline, message);
    std::this_thread::sleep_for(1ms);
  }
}

void QueueText(HANDLE input, std::u16string_view text) {
  std::vector<INPUT_RECORD> records(text.size());
  for (std::size_t i = 0; i < text.size(); ++i) {
    records[i].EventType = KEY_EVENT;
    records[i].Event.KeyEvent.bKeyDown = TRUE;
    records[i].Event.KeyEvent.wRepeatCount = 1;
    records[i].Event.KeyEvent.wVirtualKeyCode = VK_PACKET;
    records[i].Event.KeyEvent.uChar.UnicodeChar = static_cast<wchar_t>(text[i]);
  }
  DWORD written = 0;
  const BOOL ok = WriteConsoleInputW(input, records.data(), static_cast<DWORD>(records.size()), &written);
  const DWORD error = ok ? ERROR_SUCCESS : GetLastError();
  std::fprintf(stderr, "INPUT FIXTURE: utf16_units=%zu written=%lu error=%lu utf16_prefix=",
      text.size(), static_cast<unsigned long>(written), static_cast<unsigned long>(error));
  for (std::size_t i = 0; i < std::min<std::size_t>(text.size(), 32); ++i) {
    std::fprintf(stderr, "%04X", static_cast<unsigned int>(text[i]));
  }
  if (text.size() > 32) std::fputs("[truncated]", stderr);
  std::fputc('\n', stderr);
  std::fflush(stderr);
  Require(ok && written == records.size(),
          "WriteConsoleInputW text fixture");
}

TerminalSessionOptions Options() {
  TerminalSessionOptions options;
  options.use_alternate_screen = false;
  options.hide_cursor = false;
  options.raw_keyboard_input = true;
  return options;
}

struct NativeReads {
  struct CompletedRead {
    unsigned call = 0;
    DWORD requested = 0, count = 0, error = ERROR_SUCCESS;
    BOOL succeeded = FALSE;
    std::array<char, 32> prefix{};
    std::size_t prefix_size = 0;
  };

  const char* case_name;
  DWORD read_limit;
  std::atomic_uint calls{0}, cancels{0};
  std::atomic<DWORD> last_count{0};
  std::atomic_bool active{false};
  std::atomic<PHANDLER_ROUTINE> handler{nullptr};
  TerminalConsoleApi api = Win32TerminalConsoleApi();
  // Record the actual native output before the mailbox sees it. This trace is
  // test-only, fixed-capacity, and never performs an input-event read or flush.
  std::mutex evidence_mutex;
  std::array<CompletedRead, 16> completed_reads{};
  std::size_t completed_count = 0, native_bytes = 0;

  explicit NativeReads(const char* name, DWORD limit = TerminalInputReader::kCapacity)
      : case_name(name), read_limit(limit) {
    std::fprintf(stderr, "INPUT CASE: %s native_read_limit=%lu\n", case_name,
                 static_cast<unsigned long>(read_limit));
    std::fflush(stderr);
    api.context = this;
    api.read = [](void* context, HANDLE input, void* bytes, DWORD size,
                  DWORD* count) noexcept -> BOOL {
      auto& self = *static_cast<NativeReads*>(context);
      const unsigned call = self.calls.fetch_add(1) + 1;
      self.active.store(true);
      const DWORD requested = std::min(size, self.read_limit);
      const BOOL ok = ReadFile(input, bytes, requested, count, nullptr);
      const DWORD error = ok ? ERROR_SUCCESS : GetLastError();
      {
        std::lock_guard lock(self.evidence_mutex);
        if (self.completed_count < self.completed_reads.size()) {
          auto& sample = self.completed_reads[self.completed_count];
          sample.call = call;
          sample.requested = requested;
          sample.count = ok ? *count : 0;
          sample.error = error;
          sample.succeeded = ok;
          sample.prefix_size = std::min<std::size_t>(sample.count, sample.prefix.size());
          std::copy_n(static_cast<const char*>(bytes), sample.prefix_size, sample.prefix.data());
        }
        ++self.completed_count;
        if (ok) self.native_bytes += *count;
      }
      self.active.store(false);
      self.last_count.store(ok ? *count : 0);
      SetLastError(error);
      return ok;
    };
    api.cancel_synchronous_io = [](void* context, HANDLE thread) noexcept -> BOOL {
      static_cast<NativeReads*>(context)->cancels.fetch_add(1);
      return CancelSynchronousIo(thread);
    };
    api.set_control_handler = [](void* context, PHANDLER_ROUTINE handler,
                                 BOOL add) noexcept -> BOOL {
      const BOOL ok = SetConsoleCtrlHandler(handler, add);
      const DWORD error = ok ? ERROR_SUCCESS : GetLastError();
      if (ok) static_cast<NativeReads*>(context)->handler.store(add ? handler : nullptr);
      SetLastError(error);
      return ok;
    };
  }

  void Describe(const Console& console, std::string_view expected,
                std::string_view received) noexcept {
    std::array<CompletedRead, 16> samples{};
    std::size_t count = 0, byte_count = 0;
    {
      std::lock_guard lock(evidence_mutex);
      samples = completed_reads;
      count = completed_count;
      byte_count = native_bytes;
    }
    DWORD input_mode = 0, output_mode = 0;
    const BOOL input_ok = GetConsoleMode(console.input.get(), &input_mode);
    const DWORD input_error = input_ok ? ERROR_SUCCESS : GetLastError();
    const BOOL output_ok = GetConsoleMode(console.output.get(), &output_mode);
    const DWORD output_error = output_ok ? ERROR_SUCCESS : GetLastError();
    std::fprintf(stderr,
        "INPUT FAILURE: case=%s native_read_limit=%lu calls=%u completed=%zu "
        "native_bytes=%zu active=%d cancels=%u\n",
        case_name, static_cast<unsigned long>(read_limit), calls.load(), count,
        byte_count, static_cast<int>(active.load()), cancels.load());
    std::fprintf(stderr,
        "console_input_mode=0x%08lX input_mode_error=%lu console_input_cp=%u "
        "console_output_mode=0x%08lX output_mode_error=%lu console_output_cp=%u\n",
        static_cast<unsigned long>(input_mode), static_cast<unsigned long>(input_error),
        GetConsoleCP(), static_cast<unsigned long>(output_mode),
        static_cast<unsigned long>(output_error), GetConsoleOutputCP());
    PrintBytes("expected", expected);
    PrintBytes("received", received);
    std::size_t prefix = 0;
    while (prefix < std::min(expected.size(), received.size()) && expected[prefix] == received[prefix]) {
      ++prefix;
    }
    std::fprintf(stderr, "matching_prefix_bytes=%zu\n", prefix);
    for (std::size_t i = 0; i < std::min(count, samples.size()); ++i) {
      const auto& sample = samples[i];
      std::fprintf(stderr, "native_call=%u requested=%lu ok=%d returned=%lu error=%lu ",
          sample.call, static_cast<unsigned long>(sample.requested), static_cast<int>(sample.succeeded),
          static_cast<unsigned long>(sample.count), static_cast<unsigned long>(sample.error));
      PrintBytes("native_prefix", std::string_view(sample.prefix.data(), sample.prefix_size));
    }
    if (count > samples.size()) std::fprintf(stderr, "native_samples_omitted=%zu\n", count - samples.size());
    std::fflush(stderr);
  }
};

void ReadExactBytes(TerminalSession& session, const Console& console, NativeReads& trace,
                    std::string_view expected, std::size_t max_bytes, bool vary_limit = false) {
  std::string received;
  received.reserve(expected.size() + max_bytes);
  const auto deadline = std::chrono::steady_clock::now() + 5s;
  unsigned fragment = 0;
  try {
    for (;;) {
      received += session.ReadInputAvailable(vary_limit ? 1 + fragment++ % max_bytes : max_bytes);
      // A wrong short prefix is already a mismatch. Do not disguise native
      // replacement/NUL bytes as a timeout waiting for the UTF-8 byte count.
      Require(received.size() <= expected.size() &&
              std::equal(received.begin(), received.end(), expected.begin()),
              "native byte stream differs from expected UTF-8/VT bytes");
      if (received.size() == expected.size()) return;
      Require(std::chrono::steady_clock::now() < deadline,
              "native byte stream is incomplete without another key");
      std::this_thread::sleep_for(1ms);
    }
  } catch (...) {
    trace.Describe(console, expected, received);
    throw;
  }
}

void TestRetainedRecordsAndUtf8(Console& console) {
  // These records predate raw/VT acquisition. A signalled input handle does
  // not guarantee ReadFile can produce a byte after it discards such records.
  std::array<INPUT_RECORD, 3> records{};
  records[0].EventType = FOCUS_EVENT;
  records[0].Event.FocusEvent.bSetFocus = TRUE;
  records[1].EventType = MENU_EVENT;
  records[1].Event.MenuEvent.dwCommandId = 1;
  records[2].EventType = KEY_EVENT;
  records[2].Event.KeyEvent.bKeyDown = FALSE;
  records[2].Event.KeyEvent.wRepeatCount = 1;
  records[2].Event.KeyEvent.wVirtualKeyCode = VK_SHIFT;
  DWORD written = 0;
  Require(WriteConsoleInputW(console.input.get(), records.data(),
      static_cast<DWORD>(records.size()), &written) && written == records.size(),
      "queue retained non-character records");
  Require(WaitForSingleObject(console.input.get(), 0) == WAIT_OBJECT_0,
          "retained non-character input signals the handle");

  NativeReads trace("retained-records/mixed-utf8-vt");
  TerminalSession session(Options(), console.input.get(), console.output.get(), trace.api);
  Until([&] { return trace.active.load(); }, "native ReadFile starts");
  unsigned heartbeat = 0;
  for (; heartbeat < 100; ++heartbeat) {
    Require(session.ReadInputAvailable(3).empty(), "non-character records produced bytes");
  }
  Require(heartbeat == 100, "main loop keeps progressing without an injected key");

  // Retained text, BMP, surrogate-pair Unicode and literal VT sequences must
  // all pass through ReadFile+CP_UTF8; no low-level event read may clear its
  // internal converted-byte cache. UI drains intentionally split UTF-8/CSI.
  QueueText(console.input.get(), u"x\u00A3\u20AC\U0001F600\x1b[A\x1bOP\x1b[<0;3;4M\x03");
  const std::string expected = "x\xC2\xA3\xE2\x82\xAC\xF0\x9F\x98\x80"
                               "\x1b[A\x1bOP\x1b[<0;3;4M\x03";
  ReadExactBytes(session, console, trace, expected, 3, true);
  Until([&] { return trace.calls.load() >= 2 && trace.active.load(); },
        "native reader returns to idle blocked read");
  Require(session.Restore().succeeded(), "native blocked-read cancellation/restoration");
  Require(trace.cancels.load() > 0 && console.Restored(),
          "native cancellation drains before exact mode/code-page restore");
}

void TestMutatingSetterFailure(Console& console) {
  for (HANDLE target : {console.input.get(), console.output.get()}) {
    struct Mutation { HANDLE target; bool fired = false; } mutation{target};
    auto api = Win32TerminalConsoleApi();
    api.context = &mutation;
    api.set_mode = [](void* context, HANDLE handle, DWORD mode) noexcept -> BOOL {
      auto& state = *static_cast<Mutation*>(context);
      const BOOL result = SetConsoleMode(handle, mode);
      if (result && handle == state.target && !state.fired) {
        state.fired = true;
        SetLastError(ERROR_INVALID_PARAMETER);
        return FALSE;  // Failure injection after a genuine console mutation.
      }
      return result;
    };
    bool caught = false;
    try {
      TerminalSession session(Options(), console.input.get(), console.output.get(), api);
    } catch (const TerminalSessionAcquisitionError& error) {
      caught = true;
      Require(error.restoration().succeeded(), "mutating native setter rollback result");
    }
    Require(caught && mutation.fired && console.Restored(),
            "setter failure after mutation left console changes behind");
  }
}

void TestPreAcquisitionTypeahead(Console& console) {
  NativeReads trace("pre-acquisition-typeahead");
  QueueText(console.input.get(), u"retained:\u00A3\u20AC\U0001F600");
  TerminalSession session(Options(), console.input.get(), console.output.get(), trace.api);
  const std::string expected = "retained:\xC2\xA3\xE2\x82\xAC\xF0\x9F\x98\x80";
  ReadExactBytes(session, console, trace, expected, 1);
  Require(session.Restore().succeeded() && console.Restored(), "typeahead session restore");
}

void TestNativeUtf8ReadBoundaries(Console& console) {
  for (DWORD limit : {1u, 2u, 3u}) {
    NativeReads trace("native-utf8-read-boundary", limit);
    TerminalSession session(Options(), console.input.get(), console.output.get(), trace.api);
    Until([&] { return trace.active.load(); }, "native short read starts");
    QueueText(console.input.get(), u"\u00A3\u20AC\U0001F600");
    const std::string expected = "\xC2\xA3\xE2\x82\xAC\xF0\x9F\x98\x80";
    ReadExactBytes(session, console, trace, expected, 3);
    Require(trace.calls.load() >= (expected.size() + limit - 1) / limit,
            "actual short ReadFile calls lost or changed converted UTF-8 suffixes");
    Require(session.Restore().succeeded() && console.Restored(),
            "native short-read session restoration");
  }
}

void TestEmergencyDefersInputRestore(Console& console) {
  NativeReads trace("emergency-deferred-input-restore");
  TerminalSession session(Options(), console.input.get(), console.output.get(), trace.api);
  Until([&] { return trace.active.load(); }, "native read active before emergency callback");
  const auto handler = trace.handler.load();
  Require(handler && handler(CTRL_CLOSE_EVENT) == FALSE,
          "direct production close callback completes without joining the reader");
  DWORD mode = 0;
  Require(trace.cancels.load() == 0 && GetConsoleMode(console.input.get(), &mode) &&
              mode != console.input_mode && GetConsoleCP() == CP_UTF8,
          "emergency callback must defer input conversion/mode while read is live");
  Require(session.Restore().succeeded() && console.Restored(),
          "normal teardown finishes deferred input restoration after cancellation");
}

void TestRestoreFailureAfterMutation(Console& console) {
  struct Failure { HANDLE input; bool armed = false; } failure{console.input.get()};
  auto api = Win32TerminalConsoleApi();
  api.context = &failure;
  api.set_mode = [](void* context, HANDLE handle, DWORD mode) noexcept -> BOOL {
    auto& state = *static_cast<Failure*>(context);
    const BOOL ok = SetConsoleMode(handle, mode);
    if (ok && state.armed && handle == state.input) {
      SetLastError(ERROR_INVALID_PARAMETER);
      return FALSE;
    }
    return ok;
  };
  TerminalSession session(Options(), console.input.get(), console.output.get(), api);
  failure.armed = true;
  const auto result = session.Restore();
  Require(result.completed && !result.succeeded() &&
              result.failed_steps == static_cast<std::uint32_t>(TerminalRestoreFailure::InputMode) &&
              result.first_error == ERROR_INVALID_PARAMETER && console.Restored(),
          "setter FALSE remains a recorded failure even when mode bits were restored");
  Require(!session.Restore().succeeded(), "repeated restore retains native injected failure");
}

void TestMailboxStop(Console& console) {
  QueueText(console.input.get(), std::u16string(TerminalInputReader::kCapacity * 2, u'q'));
  NativeReads trace("mailbox-backpressure-stop");
  TerminalSession session(Options(), console.input.get(), console.output.get(), trace.api);
  Until([&] { return session.ReadInputAvailable(1) == "q"; },
        "native mailbox becomes nonempty");
  Require(trace.calls.load() == 1 && trace.last_count.load() > 1,
          "native reader must wait behind its unread mailbox");
  Require(session.Restore().succeeded() && console.Restored(),
          "mailbox backpressure is woken and drained before restoration");
  // Do not flush the remaining OS typeahead. The isolated console is destroyed
  // with this child process; production never clears another input consumer's data.
}

void FatalDrainChild(Console& console) {
  struct Uncancellable {
    HANDLE input;
    HANDLE entered;
    HANDLE never;
    bool restoring = false;
  } state{console.input.get(), CreateEventW(nullptr, TRUE, FALSE, nullptr),
           CreateEventW(nullptr, TRUE, FALSE, nullptr)};
  Require(state.entered && state.never, "fatal fixture events");
  auto api = Win32TerminalConsoleApi();
  api.context = &state;
  api.read = [](void* context, HANDLE, void*, DWORD, DWORD*) noexcept -> BOOL {
    auto& value = *static_cast<Uncancellable*>(context);
    SetEvent(value.entered);
    WaitForSingleObject(value.never, INFINITE);
    return FALSE;
  };
  api.cancel_synchronous_io = [](void*, HANDLE) noexcept -> BOOL {
    SetLastError(ERROR_ACCESS_DENIED);
    return FALSE;
  };
  api.set_mode = [](void* context, HANDLE handle, DWORD mode) noexcept -> BOOL {
    auto& value = *static_cast<Uncancellable*>(context);
    if (value.restoring && handle == value.input) {
      std::fputs("UNSAFE_INPUT_RESTORE\n", stderr);
      std::fflush(stderr);
    }
    return SetConsoleMode(handle, mode);
  };
  api.set_input_code_page = [](void* context, UINT page) noexcept -> BOOL {
    if (static_cast<Uncancellable*>(context)->restoring) {
      std::fputs("UNSAFE_INPUT_RESTORE\n", stderr);
      std::fflush(stderr);
    }
    return SetConsoleCP(page);
  };
  TerminalSession session(Options(), console.input.get(), console.output.get(), api);
  Require(WaitForSingleObject(state.entered, 5000) == WAIT_OBJECT_0,
          "uncancellable read entered");
  state.restoring = true;
  std::fputs("FATAL_DRAIN_ARMED\n", stderr);
  std::fflush(stderr);
  (void)session.Restore();
  // Do not unwind: a destructor's later fatal drain could otherwise hide an
  // incorrect normal return from the explicit restoration under test.
  std::fputs("UNEXPECTED_DRAIN_RETURN\n", stderr);
  std::fflush(stderr);
  std::_Exit(92);
}

int Child(bool fatal) {
  Console console;
  DescribeConsoleOwner();
  if (fatal) FatalDrainChild(console);
  TestRetainedRecordsAndUtf8(console);
  TestPreAcquisitionTypeahead(console);
  TestNativeUtf8ReadBoundaries(console);
  TestMutatingSetterFailure(console);
  TestEmergencyDefersInputRestore(console);
  TestRestoreFailureAfterMutation(console);
  TestMailboxStop(console);
  std::cout << "Real console retained-input/UTF-8/cancellation tests passed\n";
  return 0;
}

struct ChildResult {
  DWORD code;
  std::string text;
  std::filesystem::path log_path;
};

std::string ChildEvidence(const ChildResult& result) {
  const auto path = result.log_path.u8string();
  return "exit=" + std::to_string(result.code) + ", log=" +
      std::string(reinterpret_cast<const char*>(path.data()), path.size()) +
      "\n" + (result.text.empty() ? "(no child output)\n" : result.text);
}

ChildResult RunChild(bool fatal) {
  std::array<wchar_t, 32768> executable{};
  const DWORD length = GetModuleFileNameW(nullptr, executable.data(),
                                         static_cast<DWORD>(executable.size()));
  Require(length > 0 && length < executable.size(), "child executable path");
  std::array<wchar_t, MAX_PATH> directory{}, log_path{};
  const DWORD temp_length = GetTempPathW(static_cast<DWORD>(directory.size()), directory.data());
  Require(temp_length && temp_length < directory.size() &&
              GetTempFileNameW(directory.data(), L"ait", 0, log_path.data()),
          "child evidence file");
  SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
  Handle log(CreateFileW(log_path.data(), GENERIC_WRITE,
      FILE_SHARE_READ | FILE_SHARE_WRITE, &security, CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, nullptr));
  Handle input(CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           &security, OPEN_EXISTING, 0, nullptr));
  Require(log.get() != INVALID_HANDLE_VALUE && input.get() != INVALID_HANDLE_VALUE,
          "child standard handles");
  std::wstring command = L"\"" + std::wstring(executable.data()) + L"\" --child " +
                         (fatal ? L"fatal" : L"normal");
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  startup.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
  startup.wShowWindow = SW_HIDE;
  startup.hStdInput = input.get();
  startup.hStdOutput = startup.hStdError = log.get();
  PROCESS_INFORMATION process{};
  Require(CreateProcessW(executable.data(), command.data(), nullptr, nullptr, TRUE,
      CREATE_NEW_CONSOLE, nullptr, nullptr, &startup, &process), "start isolated console child");
  Handle child(process.hProcess), thread(process.hThread);
  thread.Close();
  const DWORD wait = WaitForSingleObject(child.get(), fatal ? 10000 : 30000);
  const DWORD wait_error = wait == WAIT_FAILED ? GetLastError() : ERROR_SUCCESS;
  BOOL terminated = TRUE;
  DWORD terminate_error = ERROR_SUCCESS, termination_wait = WAIT_OBJECT_0;
  DWORD termination_wait_error = ERROR_SUCCESS;
  if (wait != WAIT_OBJECT_0) {
    terminated = TerminateProcess(child.get(), 91);
    terminate_error = terminated ? ERROR_SUCCESS : GetLastError();
    termination_wait = WaitForSingleObject(child.get(), 5000);
    if (termination_wait == WAIT_FAILED) termination_wait_error = GetLastError();
  }
  DWORD code = STILL_ACTIVE;
  const BOOL got_exit_code = GetExitCodeProcess(child.get(), &code);
  const DWORD exit_error = got_exit_code ? ERROR_SUCCESS : GetLastError();
  log.Close();
  std::ifstream stream(std::filesystem::path(log_path.data()), std::ios::binary);
  std::array<char, 8192> bytes{};
  stream.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  std::string text(bytes.data(), static_cast<std::size_t>(stream.gcount()));
  if (stream.peek() != std::char_traits<char>::eof()) text += "\n(child output truncated)\n";
  stream.close();
  ChildResult result{code, std::move(text), std::filesystem::path(log_path.data())};
  if (wait != WAIT_OBJECT_0 || !got_exit_code) {
    throw std::runtime_error("isolated console child did not finish normally: wait=" +
        std::to_string(wait) + ", wait_error=" + std::to_string(wait_error) +
        ", terminated=" + std::to_string(terminated) +
        ", terminate_error=" + std::to_string(terminate_error) +
        ", termination_wait=" + std::to_string(termination_wait) +
        ", termination_wait_error=" + std::to_string(termination_wait_error) +
        ", exit_error=" + std::to_string(exit_error) + "\n" + ChildEvidence(result));
  }
  return result;
}
}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc == 3 && std::string_view(argv[1]) == "--child") {
      return Child(std::string_view(argv[2]) == "fatal");
    }
    const auto normal = RunChild(false);
    if (normal.code != 0) throw std::runtime_error(ChildEvidence(normal));
    DeleteFileW(normal.log_path.c_str());
    const auto fatal = RunChild(true);
    if (fatal.code != TerminalInputReader::kFatalDrainExitCode ||
        fatal.text.find("FATAL_DRAIN_ARMED") == std::string::npos ||
        fatal.text.find("terminal cleanup is incomplete") == std::string::npos ||
        fatal.text.find("UNSAFE_INPUT_RESTORE") != std::string::npos) {
      throw std::runtime_error("fatal drain must preserve live state and defer input restore\n" +
                               ChildEvidence(fatal));
    }
    DeleteFileW(fatal.log_path.c_str());
    std::cout << normal.text << "Fatal input drain child exited with incomplete-cleanup diagnostic\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
