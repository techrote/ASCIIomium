#pragma once
#include "include/cef_app.h"

namespace asciiomium::browser {
inline void ApplyRuntimeCommandLinePolicy(CefRefPtr<CefCommandLine> command_line) {
  // The pinned CEF Chrome runtime enables a log destination even when the
  // settings severity is DISABLE (that maps to a FATAL minimum). Stop Chrome's
  // destination selection before startup opens chrome_debug.log. Applies to
  // the browser and every renderer/GPU/utility child process.
  command_line->AppendSwitch("disable-logging");
}

class RuntimeApp final : public CefApp {
 public:
  void OnBeforeCommandLineProcessing(const CefString&,
      CefRefPtr<CefCommandLine> command_line) override {
    ApplyRuntimeCommandLinePolicy(command_line);
  }
 private:
  IMPLEMENT_REFCOUNTING(RuntimeApp);
};
}  // namespace asciiomium::browser
