#pragma once

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "browser/source_frame.h"
#include "include/cef_client.h"
#include "include/cef_render_handler.h"

namespace asciiomium::browser {

// Minimal single-browser CEF client used by the CPU OSR correctness path.
// All CEF callbacks are invoked on CEF's UI thread. The diagnostic executable
// uses CefDoMessageLoopWork on its main thread, so Resize/Close are also called
// from that same thread.
class OsrClient final : public CefClient,
                        public CefRenderHandler,
                        public CefLifeSpanHandler,
                        public CefLoadHandler {
 public:
  OsrClient(std::shared_ptr<SourceFrameStore> frame_store,
            int initial_width,
            int initial_height);

  CefRefPtr<CefRenderHandler> GetRenderHandler() override { return this; }
  CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
  CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }

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

  void OnLoadEnd(CefRefPtr<CefBrowser> browser,
                 CefRefPtr<CefFrame> frame,
                 int http_status_code) override;
  void OnLoadError(CefRefPtr<CefBrowser> browser,
                   CefRefPtr<CefFrame> frame,
                   ErrorCode error_code,
                   const CefString& error_text,
                   const CefString& failed_url) override;

  void Resize(int width, int height);
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

 private:
  std::vector<DirtyRect> ConvertDirtyRects(const RectList& rects) const;

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

  IMPLEMENT_REFCOUNTING(OsrClient);
};

}  // namespace asciiomium::browser
