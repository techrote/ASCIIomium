#include "browser/osr_client.h"

#include "browser/cef_pointer_translator.h"
#include "browser/profile_session.h"

#include <algorithm>
#include <utility>

#include "browser/fixture_observation.h"
#include "include/cef_browser.h"
#include "include/cef_frame.h"

namespace asciiomium::browser {

OsrClient::OsrClient(std::shared_ptr<SourceFrameStore> frame_store,
                     int initial_width,
                     int initial_height,
                     BrowserRuntimeOptions runtime)
    : frame_store_(std::move(frame_store)),
      width_(std::max(1, initial_width)),
      height_(std::max(1, initial_height)),
      runtime_(std::move(runtime)) {}

void OsrClient::GetViewRect(CefRefPtr<CefBrowser>, CefRect& rect) {
  rect = CefRect(0, 0, width_.load(std::memory_order_relaxed),
                 height_.load(std::memory_order_relaxed));
}

bool OsrClient::GetRootScreenRect(CefRefPtr<CefBrowser>, CefRect& rect) {
  GetViewRect(nullptr, rect);
  return true;
}

bool OsrClient::GetScreenPoint(CefRefPtr<CefBrowser>,
                               int view_x,
                               int view_y,
                               int& screen_x,
                               int& screen_y) {
  screen_x = view_x;
  screen_y = view_y;
  return true;
}

bool OsrClient::GetScreenInfo(CefRefPtr<CefBrowser>, CefScreenInfo& screen_info) {
  const int width = width_.load(std::memory_order_relaxed);
  const int height = height_.load(std::memory_order_relaxed);
  screen_info.device_scale_factor = 1.0F;
  screen_info.depth = 32;
  screen_info.depth_per_component = 8;
  screen_info.is_monochrome = false;
  screen_info.rect = CefRect(0, 0, width, height);
  screen_info.available_rect = screen_info.rect;
  return true;
}

void OsrClient::OnPopupShow(CefRefPtr<CefBrowser>, bool show) {
  frame_store_->SetPopupVisible(show);
}

void OsrClient::OnPopupSize(CefRefPtr<CefBrowser>, const CefRect& rect) {
  frame_store_->SetPopupBounds(
      DirtyRect{rect.x, rect.y, rect.width, rect.height});
}

void OsrClient::OnPaint(CefRefPtr<CefBrowser>,
                        PaintElementType type,
                        const RectList& dirty_rects,
                        const void* buffer,
                        int width,
                        int height) {
  const auto converted = ConvertDirtyRects(dirty_rects);
  if (type == PET_VIEW) {
    // CEF documents CPU OnPaint as width*height*4 bytes, BGRA channel order,
    // tightly packed rows and an upper-left origin. Keep that representation
    // unchanged here; conversion belongs to the renderer boundary.
    frame_store_->UpdateView(buffer, width, height, converted);
  } else if (type == PET_POPUP) {
    frame_store_->UpdatePopup(buffer, width, height, converted);
  }
}

void OsrClient::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
  browser_ = browser;
  browser_->GetHost()->SetFocus(true);
  browser_created_.store(true, std::memory_order_release);
  if (closing_) browser_->GetHost()->CloseBrowser(true);
}

void OsrClient::OnBeforeClose(CefRefPtr<CefBrowser>) {
  browser_ = nullptr;
  closed_.store(true, std::memory_order_release);
}

void OsrClient::ResetMainNavigationState() {
  load_complete_.store(false, std::memory_order_release);
  load_error_code_.store(0, std::memory_order_release);
  http_status_code_.store(0, std::memory_order_release);
  {
    std::lock_guard lock(load_error_mutex_);
    load_error_text_.clear();
    failed_url_.clear();
  }
  std::lock_guard lock(display_mutex_);
  status_.notice.clear();
  ++status_.revision;
}

void OsrClient::OnLoadStart(CefRefPtr<CefBrowser>,
                            CefRefPtr<CefFrame> frame,
                            cef_transition_type_t) {
  if (frame->IsMain()) ResetMainNavigationState();
}

void OsrClient::OnLoadEnd(CefRefPtr<CefBrowser>,
                          CefRefPtr<CefFrame> frame,
                          int http_status_code) {
  if (!frame->IsMain()) {
    return;
  }
  http_status_code_.store(http_status_code, std::memory_order_release);
  load_complete_.store(true, std::memory_order_release);
}

void OsrClient::OnLoadError(CefRefPtr<CefBrowser>,
                            CefRefPtr<CefFrame> frame,
                            ErrorCode error_code,
                            const CefString& error_text,
                            const CefString& failed_url) {
  if (!frame->IsMain() || error_code == ERR_ABORTED) {
    return;
  }
  load_error_code_.store(static_cast<int>(error_code),
                         std::memory_order_release);
  {
    std::lock_guard lock(load_error_mutex_);
    load_error_text_ = error_text.ToString();
    failed_url_ = failed_url.ToString();
  }
  Notice("Navigation failed (network/TLS error " +
         std::to_string(static_cast<int>(error_code)) + "). Use Ctrl+Alt+O to open another URL.");
}

void OsrClient::OnTitleChange(CefRefPtr<CefBrowser>,
                              const CefString& title) {
  std::lock_guard lock(display_mutex_);
  main_title_ = title.ToString().substr(0, 4096);
  status_.title = main_title_;
  ++status_.revision;
  // Titles may subsequently change (focus/scroll), so retain both signals.
  pointer_click_center_observed_ |=
      main_title_.find("pointer-click-center") != std::string::npos;
  pointer_scroll_observed_ |=
      main_title_.find("pointer-scroll") != std::string::npos;
}

bool OsrClient::OnConsoleMessage(CefRefPtr<CefBrowser>,
                                 cef_log_severity_t,
                                 const CefString& message,
                                 const CefString&,
                                 int) {
  const std::string text = message.ToString();
  if (!runtime_.fixture_observations || !text.starts_with(kFixtureObservationPrefix) ||
      Status().url != runtime_.fixture_url) {
    return true;  // Never leak arbitrary page console contents to a log.
  }
  std::lock_guard lock(display_mutex_);
  if (text.size() <= 16384) latest_fixture_observation_ = text;
  return true;
}

bool OsrClient::Resize(int width, int height) {
  const int next_width = std::max(1, width);
  const int next_height = std::max(1, height);
  const int previous_width = width_.load(std::memory_order_relaxed);
  const int previous_height = height_.load(std::memory_order_relaxed);
  if (previous_width == next_width && previous_height == next_height) {
    return false;
  }

  width_.store(next_width, std::memory_order_relaxed);
  height_.store(next_height, std::memory_order_relaxed);
  if (browser_) {
    browser_->GetHost()->NotifyScreenInfoChanged();
    browser_->GetHost()->WasResized();
  }
  return true;
}

void OsrClient::SetFocus(bool focused) {
  if (browser_) {
    browser_->GetHost()->SetFocus(focused);
  }
}

std::size_t OsrClient::SendKeyEvents(
    const std::vector<CefKeyEvent>& events) {
  if (!browser_) {
    return 0;
  }
  for (const auto& event : events) {
    browser_->GetHost()->SendKeyEvent(event);
  }
  return events.size();
}

bool OsrClient::SendPointerEvent(const CefPointerDispatch& dispatch) {
  if (!browser_) {
    return false;
  }

  auto host = browser_->GetHost();
  switch (dispatch.kind) {
    case CefPointerDispatchKind::Move:
      host->SendMouseMoveEvent(dispatch.event, false);
      return true;

    case CefPointerDispatchKind::Click: {
      // Windowless browsers need explicit focus when receiving mouse presses.
      if (!dispatch.mouse_up) {
        host->SetFocus(true);
      }
      CefBrowserHost::MouseButtonType button = MBT_LEFT;
      switch (dispatch.button) {
        case asciiomium::input::PointerButton::Left:
          button = MBT_LEFT;
          break;
        case asciiomium::input::PointerButton::Middle:
          button = MBT_MIDDLE;
          break;
        case asciiomium::input::PointerButton::Right:
          button = MBT_RIGHT;
          break;
        case asciiomium::input::PointerButton::None:
          return false;
      }
      host->SendMouseClickEvent(dispatch.event, button, dispatch.mouse_up,
                                dispatch.click_count);
      return true;
    }

    case CefPointerDispatchKind::Wheel:
      host->SendMouseWheelEvent(dispatch.event, dispatch.delta_x,
                                dispatch.delta_y);
      return true;
  }
  return false;
}

std::string OsrClient::MainTitle() const {
  std::lock_guard lock(display_mutex_);
  return main_title_;
}

bool OsrClient::PointerClickCenterObserved() const {
  std::lock_guard lock(display_mutex_);
  return pointer_click_center_observed_;
}

bool OsrClient::PointerScrollObserved() const {
  std::lock_guard lock(display_mutex_);
  return pointer_scroll_observed_;
}

std::string OsrClient::LatestFixtureObservation() const {
  std::lock_guard lock(display_mutex_);
  return latest_fixture_observation_;
}

bool OsrClient::ExecuteMainFrameJavaScript(std::string_view code) {
  if (!browser_) {
    return false;
  }
  const auto frame = browser_->GetMainFrame();
  if (!frame) {
    return false;
  }
  frame->ExecuteJavaScript(std::string(code), frame->GetURL(), 0);
  return true;
}

void OsrClient::CloseBrowser() {
  closing_ = true;
  // Cancel using a detached bounded list: CEF may deliver callbacks reentrantly.
  const auto downloads = std::move(downloads_);
  downloads_.clear();
  for (const auto& [id, download] : downloads) {
    if (download.callback) download.callback->Cancel();
  }
  if (browser_) {
    browser_->GetHost()->CloseBrowser(true);
  }
}

BrowserStatus OsrClient::Status() const {
  std::lock_guard lock(display_mutex_);
  return status_;
}

void OsrClient::Notice(std::string_view message) {
  std::lock_guard lock(display_mutex_);
  status_.notice = TerminalSafeText(message, 2048);
  ++status_.revision;
}

bool OsrClient::Navigate(const BrowserCommand& command) {
  if (!browser_ || closing_) return false;
  switch (command.action) {
    case BrowserAction::Open:
      if (!IsAllowedNavigation(command.argument)) return false;
      if (const auto frame = browser_->GetMainFrame()) frame->LoadURL(command.argument);
      else return false;
      break;
    case BrowserAction::Back: browser_->GoBack(); break;
    case BrowserAction::Forward: browser_->GoForward(); break;
    case BrowserAction::Reload: browser_->Reload(); break;
    case BrowserAction::HardReload: browser_->ReloadIgnoreCache(); break;
    case BrowserAction::Stop: browser_->StopLoad(); break;
    default: return false;
  }
  return true;
}

void OsrClient::OnLoadingStateChange(CefRefPtr<CefBrowser>, bool loading,
                                    bool back, bool forward) {
  {
    std::lock_guard lock(display_mutex_);
    status_.loading = loading;
    status_.can_go_back = back;
    status_.can_go_forward = forward;
    ++status_.revision;
  }
}

void OsrClient::OnAddressChange(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame,
                                 const CefString& url) {
  if (!frame->IsMain()) return;
  std::lock_guard lock(display_mutex_);
  status_.url = url.ToString().substr(0, 8192);
  ++status_.revision;
}

bool OsrClient::OnBeforeBrowse(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame,
    CefRefPtr<CefRequest> request, bool, bool) {
  if (closing_) return true;
  if (frame->IsMain() && !IsAllowedNavigation(request->GetURL().ToString())) {
    {
      std::lock_guard lock(display_mutex_);
      ++status_.denied_protocols;
    }
    Notice("Unsupported navigation/protocol blocked; external applications are never launched.");
    return true;
  }
  // Loading-state notifications describe the aggregate browser load. A new
  // main navigation may replace an already loading/failed request without a
  // new false-to-true edge. Reset at accepted navigation and commit boundaries
  // as well, so its predecessor's failure cannot survive a successful page.
  // Never reset on a denied/subframe request or OnLoadEnd: a failed committed
  // navigation can legitimately deliver OnLoadError followed by OnLoadEnd.
  if (frame->IsMain()) ResetMainNavigationState();
  return false;
}

bool OsrClient::OnOpenURLFromTab(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>,
    const CefString&, cef_window_open_disposition_t disposition, bool) {
  if (disposition == CEF_WOD_CURRENT_TAB) return false;
  {
    std::lock_guard lock(display_mutex_);
    ++status_.denied_popups;
  }
  Notice("New window blocked. Use open URL in the current browser.");
  return true;
}

bool OsrClient::OnBeforePopup(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, int,
    const CefString&, const CefString&, cef_window_open_disposition_t, bool,
    const CefPopupFeatures&, CefWindowInfo&, CefRefPtr<CefClient>&,
    CefBrowserSettings&, CefRefPtr<CefDictionaryValue>&, bool*) {
  {
    std::lock_guard lock(display_mutex_);
    ++status_.denied_popups;
  }
  Notice("Popup blocked. This version has one browser; open its URL explicitly.");
  return true;
}

CefRefPtr<CefResourceRequestHandler> OsrClient::GetResourceRequestHandler(
    CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, CefRefPtr<CefRequest>,
    bool, bool, const CefString&, bool&) { return this; }

void OsrClient::OnProtocolExecution(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>,
    CefRefPtr<CefRequest>, bool& allow_os_execution) {
  allow_os_execution = false;
  {
    std::lock_guard lock(display_mutex_);
    ++status_.denied_protocols;
  }
  Notice("External protocol blocked; no operating-system application was launched.");
}

bool OsrClient::OnCertificateError(CefRefPtr<CefBrowser>, cef_errorcode_t,
    const CefString&, CefRefPtr<CefSSLInfo>, CefRefPtr<CefCallback>) {
  {
    std::lock_guard lock(display_mutex_);
    ++status_.denied_certificates;
  }
  Notice("TLS certificate rejected. Certificate verification cannot be bypassed.");
  return false;
}

bool OsrClient::GetAuthCredentials(CefRefPtr<CefBrowser>, const CefString&, bool,
    const CefString&, int, const CefString&, const CefString&,
    CefRefPtr<CefAuthCallback>) {
  Notice("HTTP/proxy authentication dialog unsupported; request cancelled.");
  return false;
}

bool OsrClient::OnSelectClientCertificate(CefRefPtr<CefBrowser>, bool,
    const CefString&, int, const X509CertificateList&,
    CefRefPtr<CefSelectClientCertificateCallback> callback) {
  callback->Select(nullptr);
  Notice("Client certificate selection unsupported; no certificate supplied.");
  return true;
}

bool OsrClient::OnRequestMediaAccessPermission(CefRefPtr<CefBrowser>,
    CefRefPtr<CefFrame>, const CefString&, uint32_t,
    CefRefPtr<CefMediaAccessCallback> callback) {
  callback->Cancel();
  {
    std::lock_guard lock(display_mutex_);
    ++status_.denied_permissions;
    ++status_.denied_media;
  }
  Notice("Camera/microphone/screen capture permission denied.");
  return true;
}

bool OsrClient::OnShowPermissionPrompt(CefRefPtr<CefBrowser>, uint64_t,
    const CefString&, uint32_t, CefRefPtr<CefPermissionPromptCallback> callback) {
  callback->Continue(CEF_PERMISSION_RESULT_DENY);
  {
    std::lock_guard lock(display_mutex_);
    ++status_.denied_permissions;
  }
  Notice("Browser permission denied (including location, notifications and clipboard prompts).");
  return true;
}

bool OsrClient::CanDownload(CefRefPtr<CefBrowser>, const CefString&, const CefString&) {
  if (!closing_ && !runtime_.downloads.empty() && downloads_.size() < 4) return true;
  {
    std::lock_guard lock(display_mutex_);
    ++status_.denied_downloads;
  }
  Notice("Download blocked. Opt in at launch with --downloads DIR (maximum four active downloads).");
  return false;
}

bool OsrClient::OnBeforeDownload(CefRefPtr<CefBrowser>,
    CefRefPtr<CefDownloadItem> item, const CefString&,
    CefRefPtr<CefBeforeDownloadCallback> callback) {
  if (!CanDownload(nullptr, {}, {})) return true;
  try {
    const auto path = AllocateDownload(runtime_.downloads);
    downloads_.emplace(item->GetId(), Download{path, nullptr});
    callback->Continue(path.wstring(), false);
    Notice("Download started in the configured destination (unique directory, download.bin).");
  } catch (const std::exception&) {
    Notice("Download cancelled: destination unavailable.");
  }
  return true;
}

void OsrClient::OnDownloadUpdated(CefRefPtr<CefBrowser>,
    CefRefPtr<CefDownloadItem> item, CefRefPtr<CefDownloadItemCallback> callback) {
  if (closing_) { callback->Cancel(); return; }
  const auto found = downloads_.find(item->GetId());
  if (found == downloads_.end()) return;
  found->second.callback = callback;
  if (item->IsComplete()) {
    const auto path = CefString(found->second.path.wstring()).ToString();
    {
      std::lock_guard lock(display_mutex_);
      ++status_.completed_downloads;
      status_.last_download = path;
    }
    Notice("Download complete: " + path);
    downloads_.erase(found);
  } else if (item->IsCanceled() || item->IsInterrupted()) {
    Notice("Download cancelled/interrupted; an incomplete file may remain in the configured destination.");
    downloads_.erase(found);
  }
}

bool OsrClient::OnJSDialog(CefRefPtr<CefBrowser>, const CefString&, JSDialogType,
    const CefString&, const CefString&, CefRefPtr<CefJSDialogCallback>, bool& suppress) {
  suppress = true;
  {
    std::lock_guard lock(display_mutex_);
    ++status_.denied_dialogs;
  }
  Notice("JavaScript dialog suppressed; modal prompts are unsupported.");
  return false;
}

bool OsrClient::OnBeforeUnloadDialog(CefRefPtr<CefBrowser>, const CefString&,
    bool, CefRefPtr<CefJSDialogCallback> callback) {
  callback->Continue(true, {});
  Notice("Leaving page; before-unload confirmation is unavailable.");
  return true;
}

bool OsrClient::OnFileDialog(CefRefPtr<CefBrowser>, FileDialogMode,
    const CefString&, const CefString&, const std::vector<CefString>&,
    const std::vector<CefString>&, const std::vector<CefString>&,
    CefRefPtr<CefFileDialogCallback> callback) {
  callback->Cancel();
  {
    std::lock_guard lock(display_mutex_);
    ++status_.denied_dialogs;
  }
  Notice("File picker unsupported; no local file was selected.");
  return true;
}

std::string OsrClient::load_error_text() const {
  std::lock_guard lock(load_error_mutex_);
  return load_error_text_;
}

std::string OsrClient::failed_url() const {
  std::lock_guard lock(load_error_mutex_);
  return failed_url_;
}

std::vector<DirtyRect> OsrClient::ConvertDirtyRects(
    const RectList& rects) const {
  std::vector<DirtyRect> converted;
  converted.reserve(rects.size());
  for (const auto& rect : rects) {
    converted.push_back(DirtyRect{rect.x, rect.y, rect.width, rect.height});
  }
  return converted;
}

}  // namespace asciiomium::browser
