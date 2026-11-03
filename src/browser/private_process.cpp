#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>

#include "browser/private_process.h"
#include "browser/profile_session.h"
#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cwchar>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <thread>

namespace asciiomium::browser {
namespace {
constexpr wchar_t kWorkerChannel[] = L"ASCIIOMIUM_PRIVATE_WORKER_CHANNEL";
std::filesystem::path worker_root;

class Handle {
 public:
  explicit Handle(HANDLE value = nullptr) : value_(value) {}
  ~Handle() { reset(); }
  Handle(const Handle&) = delete;
  Handle& operator=(const Handle&) = delete;
  HANDLE get() const { return value_; }
  void reset(HANDLE value = nullptr) {
    if (value_ && value_ != INVALID_HANDLE_VALUE) CloseHandle(value_);
    value_ = value;
  }
 private:
  HANDLE value_;
};

void Check(bool okay, const char* message) {
  if (!okay) throw std::runtime_error(std::string(message) + " (Windows error " +
                                    std::to_string(GetLastError()) + ")");
}

class SupervisorControl {
 public:
  SupervisorControl() { Check(SetConsoleCtrlHandler(Handler, TRUE) != FALSE, "cannot protect private supervisor control handling"); }
  ~SupervisorControl() { SetConsoleCtrlHandler(Handler, FALSE); }
 private:
  static BOOL WINAPI Handler(DWORD event) {
    // The worker receives the same console signal and performs graceful
    // terminal/CEF teardown. The parent must remain alive to reap and clean.
    return event == CTRL_C_EVENT || event == CTRL_BREAK_EVENT;
  }
};

std::vector<wchar_t> WorkerEnvironment(HANDLE channel) {
  auto* environment = GetEnvironmentStringsW();
  Check(environment != nullptr, "cannot copy private worker environment");
  struct Environment { wchar_t* value; ~Environment() { FreeEnvironmentStringsW(value); } } owner{environment};
  const std::wstring prefix = std::wstring(kWorkerChannel) + L"=";
  std::vector<std::wstring> entries;
  for (auto* entry = environment; *entry; entry += std::wcslen(entry) + 1) {
    const std::wstring value(entry);
    if (value.size() >= prefix.size() && CompareStringOrdinal(value.data(), static_cast<int>(prefix.size()),
        prefix.data(), static_cast<int>(prefix.size()), TRUE) == CSTR_EQUAL) continue;
    entries.push_back(value);
  }
  entries.push_back(prefix + std::to_wstring(reinterpret_cast<std::uintptr_t>(channel)));
  std::sort(entries.begin(), entries.end(), [](const auto& a, const auto& b) {
    return CompareStringOrdinal(a.c_str(), -1, b.c_str(), -1, TRUE) == CSTR_LESS_THAN;
  });
  std::vector<wchar_t> result;
  for (const auto& entry : entries) {
    result.insert(result.end(), entry.begin(), entry.end());
    result.push_back(L'\0');
  }
  result.push_back(L'\0');
  return result;
}

void Transfer(HANDLE pipe, void* bytes, DWORD count, bool writing) {
  auto* position = static_cast<unsigned char*>(bytes);
  while (count != 0) {
    DWORD transferred = 0;
    const bool okay = writing ? WriteFile(pipe, position, count, &transferred, nullptr)
                               : ReadFile(pipe, position, count, &transferred, nullptr);
    Check(okay && transferred > 0, "private worker pipe transfer failed");
    position += transferred;
    count -= transferred;
  }
}

std::wstring Quote(std::wstring_view value) {
  std::wstring result = L"\"";
  std::size_t slashes = 0;
  for (wchar_t ch : value) {
    if (ch == L'\\') { ++slashes; continue; }
    result.append(slashes * (ch == L'"' ? 2 : 1), L'\\');
    slashes = 0;
    if (ch == L'"') result += L'\\';
    result += ch;
  }
  result.append(slashes * 2, L'\\');
  result += L'"';
  return result;
}

std::filesystem::path Executable() {
  std::wstring path(32768, L'\0');
  const auto length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
  Check(length > 0 && length < path.size(), "cannot locate private worker executable");
  path.resize(length);
  return path;
}

bool JobEmpty(HANDLE job, int milliseconds) noexcept {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
  do {
    JOBOBJECT_BASIC_ACCOUNTING_INFORMATION status{};
    if (!QueryInformationJobObject(job, JobObjectBasicAccountingInformation,
                                  &status, sizeof(status), nullptr)) return false;
    if (status.ActiveProcesses == 0) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  } while (std::chrono::steady_clock::now() < deadline);
  return false;
}

// Declared after the profile owner, so all exits stop/reap this private process
// tree before the profile owner can attempt deletion.
class WorkerLifetime {
 public:
  WorkerLifetime(HANDLE job, HANDLE process, ProfileSession& profile)
      : job_(job), process_(process), profile_(profile) { profile_.SetCefActive(true); }
  ~WorkerLifetime() {
    if (active_) {
      TerminateJobObject(job_, 11);
      WaitForSingleObject(process_, 5000);
      if (JobEmpty(job_, 5000)) profile_.SetCefActive(false);
    }
  }
  void Reaped() { active_ = false; profile_.SetCefActive(false); }
 private:
  HANDLE job_;
  HANDLE process_;
  ProfileSession& profile_;
  bool active_ = true;
};

void ReplaceOnce(std::string& body, std::string_view from, std::string_view to) {
  const auto position = body.find(from);
  if (position == std::string::npos || body.find(from, position + from.size()) != std::string::npos)
    throw std::runtime_error("private worker report missing unique finalization field");
  body.replace(position, from.size(), to);
}

std::string ReadWorkerReport(const std::filesystem::path& path) {
  std::error_code error;
  const auto size = std::filesystem::file_size(path, error);
  if (error || size == 0 || size > 4 * 1024 * 1024)
    throw std::runtime_error("private worker report missing or exceeds bounded size");
  std::ifstream input(path, std::ios::binary);
  std::string body(static_cast<std::size_t>(size), '\0');
  input.read(body.data(), static_cast<std::streamsize>(body.size()));
  if (!input || body.front() != '{') throw std::runtime_error("cannot read private worker report");
  return body;
}
}  // namespace

const std::filesystem::path& PrivateWorkerRoot() { return worker_root; }

void InitializePrivateWorker() {
  wchar_t encoded[64]{};
  const auto count = GetEnvironmentVariableW(kWorkerChannel, encoded, 64);
  if (count == 0) return;
  if (!worker_root.empty() || count >= 64) throw std::invalid_argument("invalid private worker channel");
  Check(SetEnvironmentVariableW(kWorkerChannel, nullptr) != FALSE, "cannot clear private worker channel");
  std::string argument;
  for (DWORD i = 0; i < count; ++i) {
    if (encoded[i] < L'0' || encoded[i] > L'9') throw std::invalid_argument("invalid private worker channel");
    argument += static_cast<char>(encoded[i]);
  }
  std::uintptr_t value = 0;
  const auto parsed = std::from_chars(argument.data(), argument.data() + argument.size(), value);
  if (parsed.ec != std::errc{} || parsed.ptr != argument.data() + argument.size() || value == 0)
    throw std::invalid_argument("invalid private worker channel");
  Handle input(reinterpret_cast<HANDLE>(value));
  Check(GetFileType(input.get()) == FILE_TYPE_PIPE, "private worker requires an inherited pipe");
  Check(SetHandleInformation(input.get(), HANDLE_FLAG_INHERIT, 0) != FALSE,
        "cannot restrict private channel inheritance");
  DWORD length = 0;
  Transfer(input.get(), &length, sizeof(length), false);
  if (length == 0 || length > 32767) throw std::invalid_argument("invalid private worker root length");
  std::wstring root(length, L'\0');
  Transfer(input.get(), root.data(), length * sizeof(wchar_t), false);
  worker_root = root;
  if (!worker_root.is_absolute() || !std::filesystem::is_directory(worker_root))
    throw std::invalid_argument("private worker root does not exist");
}

std::vector<std::wstring> ProcessArguments() {
  int count = 0;
  auto** values = CommandLineToArgvW(GetCommandLineW(), &count);
  Check(values != nullptr && count > 0, "cannot parse private worker arguments");
  std::vector<std::wstring> result;
  for (int i = 1; i < count; ++i) result.emplace_back(values[i]);
  LocalFree(values);
  return result;
}

int RunPrivateProcess(const std::filesystem::path& report,
                      const std::vector<std::wstring>& arguments) {
  if (!worker_root.empty()) throw std::logic_error("private worker cannot supervise another session");
  SupervisorControl console_control;
  ProfileSession profile({}, true);
  const auto internal_report = profile.root() / kPrivateWorkerReportFilename;

  SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
  HANDLE read_raw = nullptr, write_raw = nullptr;
  Check(CreatePipe(&read_raw, &write_raw, &security, 65536) != FALSE, "cannot create private worker channel");
  Handle read_pipe(read_raw), write_pipe(write_raw);
  Check(SetHandleInformation(write_pipe.get(), HANDLE_FLAG_INHERIT, 0) != FALSE,
        "cannot restrict private channel inheritance");
  std::array<Handle, 3> standard;
  std::array<HANDLE, 4> inherited{read_pipe.get(), nullptr, nullptr, nullptr};
  const std::array<DWORD, 3> ids{STD_INPUT_HANDLE, STD_OUTPUT_HANDLE, STD_ERROR_HANDLE};
  for (std::size_t i = 0; i < ids.size(); ++i) {
    HANDLE duplicate = nullptr;
    const auto original = GetStdHandle(ids[i]);
    if (original && original != INVALID_HANDLE_VALUE) {
      Check(DuplicateHandle(GetCurrentProcess(), original, GetCurrentProcess(), &duplicate,
                            0, TRUE, DUPLICATE_SAME_ACCESS) != FALSE, "cannot inherit private worker console");
    } else {
      duplicate = CreateFileW(L"NUL", i == 0 ? GENERIC_READ : GENERIC_WRITE,
          FILE_SHARE_READ | FILE_SHARE_WRITE, &security, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
      Check(duplicate != INVALID_HANDLE_VALUE, "cannot provide private worker standard handle");
    }
    standard[i].reset(duplicate);
    inherited[i + 1] = duplicate;
  }
  SIZE_T attribute_size = 0;
  InitializeProcThreadAttributeList(nullptr, 1, 0, &attribute_size);
  Check(attribute_size > 0, "cannot size private worker handle list");
  std::vector<unsigned char> attribute_memory(attribute_size);
  STARTUPINFOEXW startup{};
  startup.StartupInfo.cb = sizeof(startup);
  startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
  startup.StartupInfo.hStdInput = inherited[1];
  startup.StartupInfo.hStdOutput = inherited[2];
  startup.StartupInfo.hStdError = inherited[3];
  startup.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attribute_memory.data());
  Check(InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &attribute_size) != FALSE,
        "cannot initialize private worker handle list");
  struct Attributes {
    LPPROC_THREAD_ATTRIBUTE_LIST value;
    ~Attributes() { DeleteProcThreadAttributeList(value); }
  } attributes{startup.lpAttributeList};
  Check(UpdateProcThreadAttribute(startup.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                  inherited.data(), sizeof(inherited), nullptr, nullptr) != FALSE,
        "cannot restrict inherited private worker handles");
  Handle job(CreateJobObjectW(nullptr, nullptr));
  Check(job.get() != nullptr, "cannot create private session job");
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
  limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
  Check(SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation, &limits, sizeof(limits)) != FALSE,
        "cannot configure private session process ownership");
  const auto executable = Executable();
  std::wstring command = Quote(executable.wstring());
  for (const auto& value : arguments) command += L" " + Quote(value);
  if (command.size() >= 32767) throw std::invalid_argument("private worker command line is too long");
  auto environment = WorkerEnvironment(read_pipe.get());
  PROCESS_INFORMATION created{};
  Check(CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE,
                       CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT | CREATE_UNICODE_ENVIRONMENT, environment.data(), nullptr,
                       &startup.StartupInfo, &created) != FALSE, "cannot start private worker");
  Handle process(created.hProcess), thread(created.hThread);
  if (!AssignProcessToJobObject(job.get(), process.get())) {
    const auto error = GetLastError();
    TerminateProcess(process.get(), 11);
    WaitForSingleObject(process.get(), 5000);
    SetLastError(error);
    Check(false, "cannot own private worker process tree");
  }
  WorkerLifetime lifetime(job.get(), process.get(), profile);
  read_pipe.reset();
  Check(ResumeThread(thread.get()) != static_cast<DWORD>(-1), "cannot resume private worker");
  thread.reset();
  auto root = profile.root().wstring();
  if (root.size() > 32760) throw std::invalid_argument("private root path exceeds worker channel bound");
  DWORD root_length = static_cast<DWORD>(root.size());
  Transfer(write_pipe.get(), &root_length, sizeof(root_length), true);
  Transfer(write_pipe.get(), root.data(), root_length * sizeof(wchar_t), true);
  write_pipe.reset();
  Check(WaitForSingleObject(process.get(), INFINITE) == WAIT_OBJECT_0, "cannot wait for private worker");
  DWORD worker_exit = 1;
  Check(GetExitCodeProcess(process.get(), &worker_exit) != FALSE, "cannot inspect private worker exit");
  bool forced_descendants = false;
  if (!JobEmpty(job.get(), 2000)) {
    forced_descendants = true;
    Check(TerminateJobObject(job.get(), 11) != FALSE, "cannot stop remaining private descendants");
    if (!JobEmpty(job.get(), 5000)) throw std::runtime_error("private descendants remain active; root retained");
  }
  lifetime.Reaped();
  std::string body, report_error;
  try { body = ReadWorkerReport(internal_report); }
  catch (const std::exception& error) { report_error = error.what(); }
  const bool cleaned = profile.Finish();
  int result = static_cast<int>(worker_exit);
  if (forced_descendants && result == 0) result = 11;
  if (!cleaned) {
    std::cerr << "Private supervisor cleanup failed (OS error " << profile.cleanup_error_code() << ").\n";
    if (result == 0) result = 10;
  }
  if (!report_error.empty()) {
    std::cerr << report_error << '\n';
    if (result == 0) result = 9;
  } else {
    try {
    ReplaceOnce(body, "\"profile_cleanup\":false", cleaned ? "\"profile_cleanup\":true" : "\"profile_cleanup\":false");
    ReplaceOnce(body, "\"profile_cleanup_deferred\":true", "\"profile_cleanup_deferred\":false");
    ReplaceOnce(body, "\"profile_cleanup_attempts\":0", "\"profile_cleanup_attempts\":" + std::to_string(profile.cleanup_attempts()));
    ReplaceOnce(body, "\"profile_cleanup_error\":0", "\"profile_cleanup_error\":" + std::to_string(profile.cleanup_error_code()));
    body.insert(1, "\"private_supervisor\":{\"worker_exit\":" + std::to_string(worker_exit) +
        ",\"process_tree_reaped\":true,\"forced_descendants\":" + (forced_descendants ? "true" : "false") +
        ",\"profile_removed\":" + (cleaned ? "true" : "false") + "},");
    if (!report.empty()) {
      if (report.has_parent_path()) std::filesystem::create_directories(report.parent_path());
      std::ofstream output(report, std::ios::binary);
      output << body;
      output.close();
      if (!output) throw std::runtime_error("cannot write finalized private report");
    }
    } catch (const std::exception& error) {
      std::cerr << "Private report finalization failed: " << error.what() << '\n';
      if (result == 0) result = 9;
    }
  }
  return result;
}
}  // namespace asciiomium::browser
