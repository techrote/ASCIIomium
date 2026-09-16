#include "browser/osr_client.h"

#include <algorithm>
#include <utility>

#include "include/cef_browser.h"
#include "include/cef_frame.h"

namespace asciiomium::browser {

OsrClient::OsrClient(std::shared_ptr<SourceFrameStore> frame_store,
                     int initial_width,
                     int initial_height)
    : frame_store_(std::move(frame_store)),
      width_(std::max(1, initial_width)),
      height_(std::max(1, initial_height)) {}

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
  browser_created_.store(true, std::memory_order_release);
}

void OsrClient::OnBeforeClose(CefRefPtr<CefBrowser>) {
  browser_ = nullptr;
  closed_.store(true, std::memory_order_release);
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
  if (!frame->IsMain()) {
    return;
  }

  load_error_code_.store(static_cast<int>(error_code),
                         std::memory_order_release);
  {
    std::lock_guard lock(load_error_mutex_);
    load_error_text_ = error_text.ToString();
    failed_url_ = failed_url.ToString();
  }
}

void OsrClient::Resize(int width, int height) {
  width_.store(std::max(1, width), std::memory_order_relaxed);
  height_.store(std::max(1, height), std::memory_order_relaxed);
  if (browser_) {
    browser_->GetHost()->NotifyScreenInfoChanged();
    browser_->GetHost()->WasResized();
  }
}

void OsrClient::CloseBrowser() {
  if (browser_) {
    browser_->GetHost()->CloseBrowser(true);
  }
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
