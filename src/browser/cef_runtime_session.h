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
  bool Initialize(const CefMainArgs& args, const CefSettings& settings) {
    initialized_ = CefInitialize(args, settings, nullptr, nullptr);
    profile_.SetCefActive(initialized_);
    return initialized_;
  }
  void BrowserCreationStarted(CefRefPtr<OsrClient> client) { client_ = client; }
  bool Shutdown() {
    if (!initialized_) return true;
    if (client_) {
      client_->CloseBrowser();
      const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
      while (!client_->closed() && std::chrono::steady_clock::now() < deadline) {
        CefDoMessageLoopWork();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
      }
      if (!client_->closed()) return false;
      client_ = nullptr;
    }
    CefShutdown();
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
