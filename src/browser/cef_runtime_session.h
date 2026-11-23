#pragma once
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include "browser/cef_profile.h"
#include "browser/osr_client.h"

namespace asciiomium::browser {
// Declare after ProfileSession and before other CEF/terminal owners. On an
// exception those owners release/restore before this final cleanup guard.
class CefRuntimeSession {
 public:
  explicit CefRuntimeSession(ProfileSession& profile) : profile_(profile) {}
  ~CefRuntimeSession() {
    if (!Shutdown()) {
      std::fputs("CEF shutdown timed out; terminating without deleting active private data.\n", stderr);
      std::_Exit(7);
    }
  }
  bool Initialize(const CefMainArgs& args, const CefSettings& settings,
                  CefRefPtr<CefApp> app = nullptr) {
    initialized_ = CefInitialize(args, settings, app, nullptr);
    profile_.SetCefActive(initialized_);
    return initialized_;
  }
  void BrowserCreationStarted(CefRefPtr<OsrClient> client) { client_ = client; }
  bool Shutdown(CallbackStatus* callback_status = nullptr) {
    if (!initialized_) return true;
    std::shared_ptr<const CallbackState> callbacks;
    if (client_) {
      callbacks = client_->callback_state();
      client_->CloseBrowser();
      const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
      while (!client_->closed() && std::chrono::steady_clock::now() < deadline) {
        CefDoMessageLoopWork();
        // Creation may have completed inside the pump. Close only after CEF's
        // OnAfterCreated continuation has unwound; close can be synchronous.
        client_->CloseBrowser();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
      }
      if (callback_status) *callback_status = client_->callback_status();
      if (!client_->closed()) return false;
      client_ = nullptr;
    }
    CefShutdown();
    // Resource-request callbacks may run on IO after OnBeforeClose. Keep only
    // the small observer alive, and take final counters after CEF stops them.
    if (callback_status && callbacks) *callback_status = callbacks->Snapshot();
    initialized_ = false;
    profile_.SetCefActive(false);
    return true;
  }
 private:
  ProfileSession& profile_;
  bool initialized_ = false;
  CefRefPtr<OsrClient> client_;
};
}  // namespace asciiomium::browser
