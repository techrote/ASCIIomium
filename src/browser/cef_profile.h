#pragma once
#include "browser/profile_session.h"
#include "include/cef_app.h"

namespace asciiomium::browser {
inline void ConfigureProfile(const ProfileSession& profile, CefSettings& settings) {
  CefString(&settings.root_cache_path) = profile.root().wstring();
  if (!profile.private_mode()) CefString(&settings.cache_path) = profile.root().wstring();
  settings.persist_session_cookies = !profile.private_mode();
  // Browser URLs, credentials and arbitrary console text must not create a
  // default debug.log beside the executable. Explicit reports are redacted.
  settings.log_severity = LOGSEVERITY_DISABLE;
}
}  // namespace asciiomium::browser
