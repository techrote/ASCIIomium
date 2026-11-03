#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

#include "app/build_info.h"
#include "browser/browser_commands.h"
#include "browser/cef_pointer_translator.h"
#include "browser/cef_runtime_session.h"
#include "browser/private_process.h"
#include "browser/runtime_app.h"
#include "include/cef_request_context.h"
#include "include/cef_values.h"

namespace {
using namespace asciiomium::browser;
using Clock = std::chrono::steady_clock;

void WriteIdentityFields(std::ostream& out) {
  const auto info = asciiomium::GetBuildInfo();
  out << "\"source_revision\":\"" << info.source_revision
      << "\",\"source_tree\":\"" << info.source_tree
      << "\",\"source_identity_available\":" << (info.source_identity_available ? "true" : "false")
      << ",\"source_dirty\":" << (info.source_dirty ? "true" : "false")
      << ",\"build_configuration\":\"" << info.build_type
      << "\",\"cef_compile_version\":\"" << info.cef_compile_version
      << "\",\"cef_runtime_matches_headers\":" << (info.cef_runtime_matches_headers ? "true" : "false");
}

// Synthetic devices make the denial test independent of runner hardware.
// There is deliberately no fake-UI/permission-grant switch. Production live
// browsing never uses this probe-only app.
class ProbeApp final : public CefApp {
 public:
  void OnBeforeCommandLineProcessing(const CefString&,
      CefRefPtr<CefCommandLine> command_line) override {
    ApplyRuntimeCommandLinePolicy(command_line);
    command_line->AppendSwitch("use-fake-device-for-media-stream");
  }
 private:
  IMPLEMENT_REFCOUNTING(ProbeApp);
};

bool LoopbackOrigin(std::string_view origin) {
  constexpr std::string_view http = "http://127.0.0.1:";
  constexpr std::string_view https = "https://127.0.0.1:";
  if (origin.starts_with(http)) origin.remove_prefix(http.size());
  else if (origin.starts_with(https)) origin.remove_prefix(https.size());
  else return false;
  unsigned port = 0;
  const auto parsed = std::from_chars(origin.data(), origin.data() + origin.size(), port);
  return !origin.empty() && parsed.ec == std::errc{} &&
         parsed.ptr == origin.data() + origin.size() && port > 0 && port <= 65535;
}

void Require(bool condition, const std::string& message) {
  if (!condition) throw std::runtime_error(message);
}

bool PumpUntil(CefRefPtr<OsrClient> client, const std::function<bool()>& condition,
               int milliseconds = 12000) {
  const auto deadline = Clock::now() + std::chrono::milliseconds(milliseconds);
  while (Clock::now() < deadline) {
    CefDoMessageLoopWork();
    if (client->callback_failed()) return false;
    if (condition()) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
  return false;
}

void Click(CefRefPtr<OsrClient> client) {
  CefPointerDispatch event;
  event.kind = CefPointerDispatchKind::Move;
  event.event.x = 40;
  event.event.y = 30;
  Require(client->SendPointerEvent(event), "pointer move failed");
  event.kind = CefPointerDispatchKind::Click;
  event.button = asciiomium::input::PointerButton::Left;
  event.click_count = 1;
  event.mouse_up = false;
  Require(client->SendPointerEvent(event), "pointer down failed");
  // Separate the gesture edges across real browser pump iterations.
  PumpUntil(client, [] { return false; }, 60);
  event.mouse_up = true;
  Require(client->SendPointerEvent(event), "pointer up failed");
}

int Probe(int argc, char** argv, const CefMainArgs& main_args, CefRefPtr<CefApp> app) {
  std::filesystem::path profile_path, downloads, default_downloads, report;
  std::string origin, scenario, run_id;
  bool private_mode = false;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--private") { private_mode = true; continue; }
    Require(i + 1 < argc, "missing probe argument");
    const std::string value = argv[++i];
    if (arg == "--origin") origin = value;
    else if (arg == "--scenario") scenario = value;
    else if (arg == "--run-id") run_id = value;
    else if (arg == "--profile") profile_path = value;
    else if (arg == "--downloads") downloads = value;
    else if (arg == "--default-download-directory") default_downloads = value;
    else if (arg == "--report") report = value;
    else throw std::runtime_error("unknown probe argument");
  }
  if (!PrivateWorkerRoot().empty()) report = PrivateWorkerRoot() / kPrivateWorkerReportFilename;
  Require(LoopbackOrigin(origin),
          "qualification probe is restricted to loopback fixtures");
  Require(!scenario.empty() && !report.empty(), "scenario and report required");
  constexpr std::array<std::string_view, 13> scenarios{
      "storage-write", "storage-read", "storage-empty", "navigation", "policies",
      "download-deny", "download-allow", "tls", "hold", "lifecycle-ready",
      "lifecycle-before-created", "lifecycle-navigation-requested", "lifecycle-held-load"};
  Require(std::find(scenarios.begin(), scenarios.end(), std::string_view(scenario)) != scenarios.end(),
          "unknown probe scenario");
  if (scenario == "lifecycle-held-load") {
    Require(!run_id.empty() && run_id.size() <= 64 &&
            run_id.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-_") == std::string::npos,
            "held-load requires a bounded ASCII run id");
  }
  ProfileSession profile(profile_path, private_mode, ASCIIOMIUM_SOURCE_DIR);
  CefRuntimeSession session(profile);
  CefSettings settings;
  settings.no_sandbox = true;
  settings.windowless_rendering_enabled = true;
  ConfigureProfile(profile, settings);
  Require(session.Initialize(main_args, settings, app), "CefInitialize failed");
  auto frames = std::make_shared<SourceFrameStore>();
  BrowserRuntimeOptions runtime;
  runtime.downloads = downloads;
  CefRefPtr<OsrClient> client(new OsrClient(frames, 640, 360, runtime));
  CefWindowInfo window;
  window.SetAsWindowless(nullptr);
  CefBrowserSettings browser_settings;
  browser_settings.windowless_frame_rate = 30;
  std::string path;
  if (scenario.starts_with("storage-")) path = "/storage?mode=" + scenario.substr(8);
  else if (scenario.starts_with("download-")) path = "/download-page";
  else if (scenario == "policies") path = "/policies";
  else if (scenario == "lifecycle-held-load") path = "/lifecycle-held/" + run_id;
  else path = "/a";
  Require(CefBrowserHost::CreateBrowser(window, client, origin + path,
      browser_settings, nullptr, nullptr), "CreateBrowser failed");
  session.BrowserCreationStarted(client);
  bool passed = false;
  std::string failure;
  int verified_transitions = 0;
  std::string lifecycle_phase;
  bool phase_reached = false;
  try {
    auto wait_page = [&](std::string_view prefix, std::string_view previous = {}) {
      Require(PumpUntil(client, [&] {
        const auto title = client->MainTitle();
        return client->load_complete() && !client->Status().loading &&
               title.starts_with(prefix) && (previous.empty() || title != previous);
      }), "page marker timeout: " + std::string(prefix));
      ++verified_transitions;
    };
    auto navigate = [&](const std::string& text) {
      const auto command = BrowserCommands::Parse(text);
      Require(command.error.empty() && client->Navigate(command), "navigation command rejected");
    };
    if (scenario.starts_with("lifecycle-")) {
      lifecycle_phase = scenario.substr(10);
      if (lifecycle_phase == "before-created") {
        Require(!client->browser_created(), "browser creation unexpectedly completed before the first pump");
      } else if (lifecycle_phase == "held-load") {
        Require(PumpUntil(client, [&] {
          return client->MainTitle() == "LOADING:" + run_id &&
                 client->Status().loading && !client->load_complete();
        }), "held response did not reach an incomplete committed document");
        ++verified_transitions;
      } else {
        Require(lifecycle_phase == "ready" || lifecycle_phase == "navigation-requested",
                "unknown lifecycle phase");
        wait_page("PAGE:a:");
        if (lifecycle_phase == "navigation-requested") {
          // Shutdown follows this accepted request with no intervening pump.
          // This proves the queued-navigation path, not a network-start event.
          navigate("open " + origin + "/b");
        }
      }
      phase_reached = true;
    } else if (scenario.starts_with("storage-")) {
      Require(PumpUntil(client, [&] {
        return client->MainTitle().starts_with("PASS:") || client->MainTitle().starts_with("FAIL:");
      }), "storage page did not complete asynchronous IndexedDB transaction");
      Require(client->MainTitle() == "PASS:" + scenario, "storage fixture failed");
      ++verified_transitions;
    } else if (scenario == "navigation") {
      wait_page("PAGE:a:");
      navigate("open " + origin + "/b"); wait_page("PAGE:b:");
      navigate("back"); wait_page("PAGE:a:");
      Require(client->Status().can_go_forward, "forward history missing");
      navigate("forward"); wait_page("PAGE:b:");
      auto before = client->MainTitle();
      navigate("reload"); wait_page("PAGE:b:", before);
      before = client->MainTitle();
      navigate("hard-reload"); wait_page("PAGE:b:", before);
      navigate("open " + origin + "/slow");
      Require(PumpUntil(client, [&] { return client->Status().loading; }), "slow load never started");
      PumpUntil(client, [] { return false; }, 150);
      navigate("stop");
      Require(PumpUntil(client, [&] { return !client->Status().loading; }), "stop did not end loading");
      Require(client->load_error_code() == 0, "StopLoad incorrectly treated ERR_ABORTED as fatal");
      navigate("open " + origin + "/redirect"); wait_page("PAGE:a:");
      navigate("open " + origin + "/network-error");
      Require(PumpUntil(client, [&] { return client->load_error_code() != 0; }), "network failure not reported");
      // A distinct page marker proves recovery did not accept retained title
      // state from an earlier visit to /b while Chromium schedules navigation.
      navigate("open " + origin + "/recovered"); wait_page("PAGE:recovered:");
      Require(client->Status().url == origin + "/recovered" &&
              client->http_status_code() == 200,
              "recovery did not commit the expected successful page");
      Require(client->load_error_code() == 0 && client->load_error_text().empty() &&
              client->failed_url().empty(), "new navigation retained stale error");
    } else if (scenario == "policies") {
      wait_page("READY:policies");
      Require(PumpUntil(client, [&] { return frames->SnapshotState().view_generation > 0; }), "policy page has no paint");
      Click(client);
      Require(PumpUntil(client, [&] {
        return client->MainTitle().starts_with("PASS:") || client->MainTitle().starts_with("FAIL:");
      }), "policy JS promises did not settle");
      Require(client->MainTitle() == "PASS:policies", "page observed a policy grant or popup");
      Require(client->Status().denied_popups > 0, "window.open denial did not pass through host policy");
      Require(client->Status().denied_permissions > 0, "permissions not exercised");
      Require(client->Status().denied_media > 0, "media request did not reach explicit host denial");
    } else if (scenario.starts_with("download-")) {
      wait_page("READY:download");
      if (!default_downloads.empty()) {
        // Bound Chromium's fallback directory so a broken default-deny policy
        // cannot pass by writing somewhere outside the evidence inventory.
        const auto value = CefValue::Create();
        value->SetString(std::filesystem::absolute(default_downloads).wstring());
        CefString error;
        Require(CefRequestContext::GetGlobalContext()->SetPreference(
            "download.default_directory", value, error), "cannot bound default download directory");
      }
      Require(PumpUntil(client, [&] { return frames->SnapshotState().view_generation > 0; }), "download page has no paint");
      Click(client);
      const bool allowed = scenario == "download-allow";
      Require(PumpUntil(client, [&] { const auto state = client->Status();
        return allowed ? state.completed_downloads > 0 : state.denied_downloads > 0;
      }), "download policy result missing");
      if (!allowed) {
        PumpUntil(client, [] { return false; }, 300);
        Require(client->Status().completed_downloads == 0, "denied download completed");
      }
      // Python additionally checks actual file contents and no writes on denial.
    } else if (scenario == "tls") {
      Require(PumpUntil(client, [&] { return client->load_error_code() != 0; }), "invalid TLS certificate was not rejected");
      Require(!client->MainTitle().starts_with("PAGE:"), "untrusted HTTPS page executed");
      Require(client->Status().denied_certificates > 0, "certificate policy callback not exercised");
    } else if (scenario == "hold") {
      wait_page("PAGE:a:");
      // A bounded helper process lets Python verify real process ownership.
      std::ofstream(report.string() + ".ready") << "ready";
      PumpUntil(client, [] { return false; }, 6000);
    } else throw std::runtime_error("unknown scenario");
    passed = !client->callback_failed();
  } catch (const std::exception& error) { failure = error.what(); }
  const auto status = client->Status();
  if (!lifecycle_phase.empty()) {
    // Preserve the reached phase even if shutdown later exhausts its deadline.
    // The private supervisor finalizes cleanup only after the process tree exits.
    if (report.has_parent_path()) std::filesystem::create_directories(report.parent_path());
    std::ofstream checkpoint(report);
    Require(static_cast<bool>(checkpoint), "cannot open lifecycle checkpoint");
    checkpoint << "{\"schema\":\"asciiomium-runtime-probe-v1\",\"scenario\":\"" << scenario
        << "\",\"passed\":false,\"closed_cleanly\":false,\"profile_cleanup\":false"
        << ",\"profile_cleanup_deferred\":true,\"profile_cleanup_error\":0,\"profile_cleanup_attempts\":0"
        << ",\"private\":" << (private_mode ? "true" : "false")
        << ",\"probe_stage\":\"before-shutdown\",\"lifecycle_phase\":\"" << lifecycle_phase
        << "\",\"phase_reached\":" << (phase_reached ? "true" : "false") << ',';
    checkpoint << "\"callbacks\":";
    WriteCallbackStatusJson(checkpoint, client->callback_status());
    checkpoint << ',';
    WriteIdentityFields(checkpoint);
    checkpoint << "}\n";
    checkpoint.close();
    Require(static_cast<bool>(checkpoint), "cannot finish lifecycle checkpoint");
  }
  client = nullptr;
  CallbackStatus callbacks;
  Require(session.Shutdown(&callbacks), "browser failed to close");
  passed = passed && !callbacks.failed();
  const bool cleaned = profile.Finish();
  Require(cleaned, "profile cleanup failed");
  if (report.has_parent_path()) std::filesystem::create_directories(report.parent_path());
  std::ofstream out(report);
  Require(static_cast<bool>(out), "cannot open probe report");
  out << "{\"schema\":\"asciiomium-runtime-probe-v1\",\"scenario\":\"" << scenario
      << "\",\"passed\":" << (passed ? "true" : "false")
      << ",\"probe_stage\":\"complete\",\"lifecycle_phase\":\"" << lifecycle_phase
      << "\",\"phase_reached\":" << (phase_reached ? "true" : "false")
      << ",\"closed_cleanly\":true,\"profile_cleanup\":" << (profile.cleanup_deferred() ? "false" : "true")
      << ",\"profile_cleanup_deferred\":" << (profile.cleanup_deferred() ? "true" : "false")
      << ",\"profile_cleanup_error\":" << profile.cleanup_error_code()
      << ",\"private\":" << (private_mode ? "true" : "false")
      << ",\"profile_cleanup_attempts\":" << profile.cleanup_attempts()
      << ",\"verified_page_transitions\":" << verified_transitions
      << ",\"denied_popups\":" << status.denied_popups
      << ",\"denied_permissions\":" << status.denied_permissions
      << ",\"denied_media\":" << status.denied_media
      << ",\"denied_downloads\":" << status.denied_downloads
      << ",\"completed_downloads\":" << status.completed_downloads
      << ",\"denied_certificates\":" << status.denied_certificates
      << ",\"callbacks\":";
  WriteCallbackStatusJson(out, callbacks);
  out << ',';
  WriteIdentityFields(out);
  out << "}\n";
  out.close();
  Require(static_cast<bool>(out), "cannot finish probe report");
  if (!failure.empty()) std::cerr << "runtime probe: " << failure << '\n';
  if (callbacks.failed()) std::cerr << "runtime probe callback failure: "
      << CallbackFailureName(callbacks.first_failure) << '\n';
  return passed ? 0 : 1;
}
}  // namespace

int main(int argc, char** argv) {
  CefMainArgs args(GetModuleHandle(nullptr));
  CefRefPtr<CefApp> app(new ProbeApp);
  const int child = CefExecuteProcess(args, app, nullptr);
  if (child >= 0) return child;
  try {
    if (argc == 2 && std::string_view(argv[1]) == "--identity") {
      std::cout << "{\"schema\":\"asciiomium-runtime-probe-identity-v1\",";
      WriteIdentityFields(std::cout);
      std::cout << "}\n";
      return 0;
    }
    InitializePrivateWorker();
    bool private_mode = false;
    std::filesystem::path report;
    for (int i = 1; i < argc; ++i) {
      if (std::string_view(argv[i]) == "--private") private_mode = true;
      if (std::string_view(argv[i]) == "--report" && i + 1 < argc) report = argv[++i];
    }
    if (private_mode && PrivateWorkerRoot().empty()) return RunPrivateProcess(report, ProcessArguments());
    return Probe(argc, argv, args, app);
  }
  catch (const std::exception& error) { std::cerr << "runtime probe: " << error.what() << '\n'; return 1; }
}
