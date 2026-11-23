#pragma once
#include <filesystem>
#include <string>
#include <vector>

namespace asciiomium::browser {
inline constexpr wchar_t kPrivateWorkerReportFilename[] = L".asciiomium-worker-report.json";
// Internal inherited-pipe protocol. It carries only a root created and owned
// by the supervisor; there is no arbitrary-path cleanup command. Call after
// CefExecuteProcess has returned for the main/browser process, before parsing
// application CLI options. The one-use channel environment is removed before
// CEF can launch descendants; user argument values are never reinterpreted.
void InitializePrivateWorker();
[[nodiscard]] const std::filesystem::path& PrivateWorkerRoot();
[[nodiscard]] std::vector<std::wstring> ProcessArguments();
// Launches this same executable in the current console, waits for the worker
// and its Windows job descendants, removes only its own private root, then
// finalizes the worker's machine-generated report. No CEF initialization here.
int RunPrivateProcess(const std::filesystem::path& report,
                      const std::vector<std::wstring>& arguments);
}  // namespace asciiomium::browser
