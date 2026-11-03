#pragma once

#include <atomic>
#include <cstddef>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "browser/source_frame.h"
#include "browser/browser_commands.h"
#include "browser/callback_state.h"
#include "include/cef_client.h"
#include "include/cef_dialog_handler.h"
#include "include/cef_display_handler.h"
#include "include/cef_download_handler.h"
#include "include/cef_jsdialog_handler.h"
#include "include/cef_permission_handler.h"
#include "include/cef_render_handler.h"
#include "include/cef_request_handler.h"

namespace asciiomium::browser {

struct CefPointerDispatch;

struct BrowserRuntimeOptions {
  std::filesystem::path downloads;
  bool fixture_observations = false;
  std::string fixture_url;
};

struct BrowserStatus {
  std::string url;
  std::string title;
  std::string notice;
  std::string last_download;
  bool loading = false;
  bool can_go_back = false;
  bool can_go_forward = false;
  bool url_truncated = false;
  bool title_truncated = false;
  std::uint64_t revision = 0;
  std::uint64_t denied_popups = 0;
  std::uint64_t denied_permissions = 0;
  std::uint64_t denied_media = 0;
  std::uint64_t denied_downloads = 0;
  std::uint64_t completed_downloads = 0;
  std::uint64_t denied_protocols = 0;
  std::uint64_t denied_certificates = 0;
  std::uint64_t denied_dialogs = 0;
};

// Minimal single-browser CEF client used by the CPU OSR correctness path.
// Browser/render callbacks use CEF's UI thread. Resource/authentication callbacks
// may use CEF's IO thread and retain only synchronized state. The executables
// pump UI work on their main thread; Resize/Close use that same thread. CEF may
// retain resource-request callbacks after OnBeforeClose, so the client continues
// owning shared frame/diagnostic state until its final reference is released.
class OsrClient final : public CefClient,
                        public CefRenderHandler,
                        public CefLifeSpanHandler,
                        public CefLoadHandler,
                        public CefDisplayHandler,
                        public CefRequestHandler,
                        public CefResourceRequestHandler,
                        public CefPermissionHandler,
                        public CefDownloadHandler,
                        public CefJSDialogHandler,
                        public CefDialogHandler {
 public:
  OsrClient(std::shared_ptr<SourceFrameStore> frame_store,
            int initial_width,
            int initial_height,
            BrowserRuntimeOptions runtime = {});

  CefRefPtr<CefRenderHandler> GetRenderHandler() override { return this; }
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
  CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }
  CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
  CefRefPtr<CefRequestHandler> GetRequestHandler() override { return this; }
  CefRefPtr<CefPermissionHandler> GetPermissionHandler() override { return this; }
  CefRefPtr<CefDownloadHandler> GetDownloadHandler() override { return this; }
  CefRefPtr<CefJSDialogHandler> GetJSDialogHandler() override { return this; }
  CefRefPtr<CefDialogHandler> GetDialogHandler() override { return this; }

  void OnLoadingStateChange(CefRefPtr<CefBrowser>, bool, bool, bool) override;
  void OnAddressChange(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>,
                       const CefString&) override;
  bool OnBeforeBrowse(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>,
                      CefRefPtr<CefRequest>, bool, bool) override;
  bool OnOpenURLFromTab(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>,
                        const CefString&, cef_window_open_disposition_t,
                        bool) override;
  bool OnBeforePopup(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, int,
                     const CefString&, const CefString&,
                     cef_window_open_disposition_t, bool,
                     const CefPopupFeatures&, CefWindowInfo&,
                     CefRefPtr<CefClient>&, CefBrowserSettings&,
                     CefRefPtr<CefDictionaryValue>&, bool*) override;
  CefRefPtr<CefResourceRequestHandler> GetResourceRequestHandler(
      CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>, CefRefPtr<CefRequest>,
      bool, bool, const CefString&, bool&) override;
  void OnProtocolExecution(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>,
                           CefRefPtr<CefRequest>, bool&) override;
  bool OnCertificateError(CefRefPtr<CefBrowser>, cef_errorcode_t,
                          const CefString&, CefRefPtr<CefSSLInfo>,
                          CefRefPtr<CefCallback>) override;
  bool GetAuthCredentials(CefRefPtr<CefBrowser>, const CefString&, bool,
                          const CefString&, int, const CefString&,
                          const CefString&, CefRefPtr<CefAuthCallback>) override;
  bool OnSelectClientCertificate(CefRefPtr<CefBrowser>, bool, const CefString&,
      int, const X509CertificateList&,
      CefRefPtr<CefSelectClientCertificateCallback>) override;
  bool OnRequestMediaAccessPermission(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame>,
      const CefString&, uint32_t, CefRefPtr<CefMediaAccessCallback>) override;
  bool OnShowPermissionPrompt(CefRefPtr<CefBrowser>, uint64_t, const CefString&,
      uint32_t, CefRefPtr<CefPermissionPromptCallback>) override;
  bool CanDownload(CefRefPtr<CefBrowser>, const CefString&, const CefString&) override;
  bool OnBeforeDownload(CefRefPtr<CefBrowser>, CefRefPtr<CefDownloadItem>,
      const CefString&, CefRefPtr<CefBeforeDownloadCallback>) override;
  void OnDownloadUpdated(CefRefPtr<CefBrowser>, CefRefPtr<CefDownloadItem>,
      CefRefPtr<CefDownloadItemCallback>) override;
  bool OnJSDialog(CefRefPtr<CefBrowser>, const CefString&, JSDialogType,
      const CefString&, const CefString&, CefRefPtr<CefJSDialogCallback>, bool&) override;
  bool OnBeforeUnloadDialog(CefRefPtr<CefBrowser>, const CefString&, bool,
      CefRefPtr<CefJSDialogCallback>) override;
  bool OnFileDialog(CefRefPtr<CefBrowser>, FileDialogMode, const CefString&,
      const CefString&, const std::vector<CefString>&,
      const std::vector<CefString>&, const std::vector<CefString>&,
      CefRefPtr<CefFileDialogCallback>) override;

  [[nodiscard]] BrowserStatus Status() const;
  void Notice(std::string_view message) noexcept;
  [[nodiscard]] bool Navigate(const BrowserCommand& command);

  void GetViewRect(CefRefPtr<CefBrowser> browser, CefRect& rect) override;
  bool GetRootScreenRect(CefRefPtr<CefBrowser> browser, CefRect& rect) override;
  bool GetScreenPoint(CefRefPtr<CefBrowser> browser,
                      int view_x,
                      int view_y,
                      int& screen_x,
                      int& screen_y) override;
  bool GetScreenInfo(CefRefPtr<CefBrowser> browser,
                     CefScreenInfo& screen_info) override;
  void OnPopupShow(CefRefPtr<CefBrowser> browser, bool show) override;
  void OnPopupSize(CefRefPtr<CefBrowser> browser, const CefRect& rect) override;
  void OnPaint(CefRefPtr<CefBrowser> browser,
               PaintElementType type,
               const RectList& dirty_rects,
               const void* buffer,
               int width,
               int height) override;

  void OnAfterCreated(CefRefPtr<CefBrowser> browser) override;
  void OnBeforeClose(CefRefPtr<CefBrowser> browser) override;

  void OnLoadStart(CefRefPtr<CefBrowser> browser,
                   CefRefPtr<CefFrame> frame,
                   cef_transition_type_t transition_type) override;
  void OnLoadEnd(CefRefPtr<CefBrowser> browser,
                 CefRefPtr<CefFrame> frame,
                 int http_status_code) override;
  void OnLoadError(CefRefPtr<CefBrowser> browser,
                   CefRefPtr<CefFrame> frame,
                   ErrorCode error_code,
                   const CefString& error_text,
                   const CefString& failed_url) override;
  void OnTitleChange(CefRefPtr<CefBrowser> browser,
                     const CefString& title) override;
  bool OnConsoleMessage(CefRefPtr<CefBrowser> browser,
                        cef_log_severity_t level,
                        const CefString& message,
                        const CefString& source,
                        int line) override;

  // Returns true only when the requested CEF viewport actually changes.
  bool Resize(int width, int height);
  void SetFocus(bool focused);
  [[nodiscard]] std::size_t SendKeyEvents(
      const std::vector<CefKeyEvent>& events);
  [[nodiscard]] bool SendPointerEvent(const CefPointerDispatch& dispatch);
  [[nodiscard]] std::string MainTitle() const;
  [[nodiscard]] bool PointerClickCenterObserved() const;
  [[nodiscard]] bool PointerScrollObserved() const;
  [[nodiscard]] std::string LatestFixtureObservation() const;
  [[nodiscard]] bool ExecuteMainFrameJavaScript(std::string_view code);
  void CloseBrowser();

  [[nodiscard]] bool browser_created() const noexcept {
    return browser_created_.load(std::memory_order_acquire);
  }
  [[nodiscard]] bool load_complete() const noexcept {
    return load_complete_.load(std::memory_order_acquire);
  }
  [[nodiscard]] bool closed() const noexcept {
    return closed_.load(std::memory_order_acquire);
  }
  [[nodiscard]] int load_error_code() const noexcept {
    return load_error_code_.load(std::memory_order_acquire);
  }
  [[nodiscard]] int http_status_code() const noexcept {
    return http_status_code_.load(std::memory_order_acquire);
  }
  [[nodiscard]] std::string load_error_text() const;
  [[nodiscard]] std::string failed_url() const;
  [[nodiscard]] bool callback_failed() const noexcept {
    return callback_state_->failed();
  }
  [[nodiscard]] CallbackStatus callback_status() const noexcept {
    return callback_state_->Snapshot();
  }
  // Retain only scalar failure state through CefShutdown, not a CEF object.
  [[nodiscard]] std::shared_ptr<const CallbackState> callback_state() const noexcept {
    return callback_state_;
  }

 private:
  void ResetMainNavigationState();
  std::vector<DirtyRect> ConvertDirtyRects(const RectList& rects,
                                         int width, int height) const;

  std::shared_ptr<SourceFrameStore> frame_store_;
  std::atomic_int width_;
  std::atomic_int height_;
  CefRefPtr<CefBrowser> browser_;
  std::atomic_bool browser_created_{false};
  std::atomic_bool load_complete_{false};
  std::atomic_bool closed_{false};
  std::atomic_int load_error_code_{0};
  std::atomic_int http_status_code_{0};
  mutable std::mutex load_error_mutex_;
  std::string load_error_text_;
  std::string failed_url_;
  mutable std::mutex display_mutex_;
  bool pointer_click_center_observed_ = false;
  bool pointer_scroll_observed_ = false;
  std::string latest_fixture_observation_;
  BrowserRuntimeOptions runtime_;
  BrowserStatus status_;
  std::shared_ptr<CallbackState> callback_state_;
  bool closing_ = false;
  struct Download {
    std::filesystem::path path;
    CefRefPtr<CefDownloadItemCallback> callback;
  };
  std::map<uint32_t, Download> downloads_;  // UI thread only; at most four.

  IMPLEMENT_REFCOUNTING(OsrClient);
};

}  // namespace asciiomium::browser
