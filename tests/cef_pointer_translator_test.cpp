#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <iostream>

#include "browser/cef_pointer_translator.h"
#include "geometry/viewport_transform.h"
#include "include/internal/cef_types.h"
#include "input/pointer_event.h"

namespace {

int failures = 0;

void Require(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

asciiomium::geometry::ViewportTransform MakeStretch(int columns = 4) {
  asciiomium::geometry::ViewportConfig config;
  config.terminal_columns = columns;
  config.terminal_rows = 2;
  config.browser_width = 400;
  config.browser_height = 200;
  config.fit_mode = asciiomium::geometry::FitMode::Stretch;
  return asciiomium::geometry::ViewportTransform::Create(config);
}

void TestClickAndModifiers() {
  using namespace asciiomium::input;
  PointerEvent pointer;
  pointer.kind = PointerEventKind::ButtonPress;
  pointer.button = PointerButton::Left;
  pointer.column = 0;
  pointer.row = 0;
  pointer.modifiers = kModifierShift | kModifierControl;
  pointer.buttons = kPointerButtonLeft;

  const auto dispatch = asciiomium::browser::TranslatePointerEvent(
      pointer, MakeStretch(), 120);
  Require(dispatch.has_value(), "left press translates");
  if (!dispatch.has_value()) return;

  Require(dispatch->kind ==
              asciiomium::browser::CefPointerDispatchKind::Click,
          "left press becomes CEF click dispatch");
  Require(dispatch->button == PointerButton::Left && !dispatch->mouse_up,
          "left press preserves button/down state");
  Require(dispatch->event.x == 50 && dispatch->event.y == 50,
          "cell centre maps through authoritative transform");
  Require((dispatch->event.modifiers & EVENTFLAG_SHIFT_DOWN) != 0 &&
              (dispatch->event.modifiers & EVENTFLAG_CONTROL_DOWN) != 0 &&
              (dispatch->event.modifiers & EVENTFLAG_LEFT_MOUSE_BUTTON) != 0,
          "CEF modifiers include keyboard and pressed-button state");

  pointer.kind = PointerEventKind::ButtonRelease;
  pointer.buttons = kPointerButtonNone;
  const auto release = asciiomium::browser::TranslatePointerEvent(
      pointer, MakeStretch(), 120);
  Require(release.has_value() && release->mouse_up,
          "left release becomes mouse-up dispatch");
  if (release.has_value()) {
    Require((release->event.modifiers & EVENTFLAG_LEFT_MOUSE_BUTTON) == 0,
            "released button is absent from CEF modifier state");
  }
}

void TestMoveAndWheel() {
  using namespace asciiomium::input;
  PointerEvent move;
  move.kind = PointerEventKind::Move;
  move.button = PointerButton::Right;
  move.column = 3;
  move.row = 1;
  move.buttons = kPointerButtonRight;
  move.modifiers = kModifierAlt;

  const auto move_dispatch = asciiomium::browser::TranslatePointerEvent(
      move, MakeStretch(), 120);
  Require(move_dispatch.has_value() &&
              move_dispatch->kind ==
                  asciiomium::browser::CefPointerDispatchKind::Move,
          "motion translates to CEF mouse move");
  if (move_dispatch.has_value()) {
    Require(move_dispatch->event.x == 350 &&
                move_dispatch->event.y == 150,
            "motion maps final terminal cell centre");
    Require((move_dispatch->event.modifiers &
             EVENTFLAG_RIGHT_MOUSE_BUTTON) != 0 &&
                (move_dispatch->event.modifiers & EVENTFLAG_ALT_DOWN) != 0,
            "motion carries button and Alt flags");
  }

  PointerEvent wheel;
  wheel.kind = PointerEventKind::Wheel;
  wheel.column = 1;
  wheel.row = 1;
  wheel.wheel_y = -1;
  wheel.wheel_x = 1;
  const auto wheel_dispatch = asciiomium::browser::TranslatePointerEvent(
      wheel, MakeStretch(), 120);
  Require(wheel_dispatch.has_value() &&
              wheel_dispatch->kind ==
                  asciiomium::browser::CefPointerDispatchKind::Wheel,
          "wheel translates to CEF wheel dispatch");
  if (wheel_dispatch.has_value()) {
    Require(wheel_dispatch->delta_x == 120 &&
                wheel_dispatch->delta_y == -120,
            "logical wheel notches use configured CEF scale");
  }
}

void TestLetterboxAndResizeMapping() {
  using namespace asciiomium::input;
  asciiomium::geometry::ViewportConfig config;
  config.terminal_columns = 80;
  config.terminal_rows = 40;
  config.browser_width = 1600;
  config.browser_height = 900;
  config.cell_aspect = 0.5;
  config.fit_mode = asciiomium::geometry::FitMode::Contain;
  const auto contained =
      asciiomium::geometry::ViewportTransform::Create(config);

  PointerEvent pointer;
  pointer.kind = PointerEventKind::Move;
  pointer.column = 40;
  pointer.row = 8;
  Require(!asciiomium::browser::TranslatePointerEvent(
               pointer, contained, 120)
               .has_value(),
          "letterbox cell is rejected rather than fabricated");

  pointer.row = 9;
  Require(asciiomium::browser::TranslatePointerEvent(
              pointer, contained, 120)
              .has_value(),
          "first clickable contain row maps to browser");

  pointer.column = 0;
  pointer.row = 0;
  const auto before = asciiomium::browser::TranslatePointerEvent(
      pointer, MakeStretch(4), 120);
  const auto after = asciiomium::browser::TranslatePointerEvent(
      pointer, MakeStretch(8), 120);
  Require(before.has_value() && after.has_value() &&
              before->event.x == 50 && after->event.x == 25,
          "pointer mapping follows the supplied post-resize transform");
}

void TestOddCornerAndCentreMapping() {
  using namespace asciiomium::input;
  asciiomium::geometry::ViewportConfig config;
  config.terminal_columns = 5;
  config.terminal_rows = 3;
  config.browser_width = 501;
  config.browser_height = 301;
  config.fit_mode = asciiomium::geometry::FitMode::Stretch;
  const auto transform =
      asciiomium::geometry::ViewportTransform::Create(config);

  struct Case {
    int column;
    int row;
    int expected_x;
    int expected_y;
    const char* message;
  };
  const Case cases[] = {
      {0, 0, 50, 50, "odd top-left"},
      {4, 0, 450, 50, "odd top-right"},
      {0, 2, 50, 250, "odd bottom-left"},
      {4, 2, 450, 250, "odd bottom-right"},
      {2, 1, 250, 150, "odd centre"},
  };

  for (const auto& test : cases) {
    PointerEvent pointer;
    pointer.kind = PointerEventKind::Move;
    pointer.column = test.column;
    pointer.row = test.row;
    const auto dispatch = asciiomium::browser::TranslatePointerEvent(
        pointer, transform, 120);
    Require(dispatch.has_value(), test.message);
    if (dispatch.has_value()) {
      Require(dispatch->event.x == test.expected_x &&
                  dispatch->event.y == test.expected_y,
              test.message);
    }
  }
}

void TestInvalidTranslation() {
  using namespace asciiomium::input;
  PointerEvent click;
  click.kind = PointerEventKind::ButtonPress;
  click.button = PointerButton::None;
  click.column = 0;
  click.row = 0;
  Require(!asciiomium::browser::TranslatePointerEvent(
               click, MakeStretch(), 120)
               .has_value(),
          "button event without button is rejected");

  PointerEvent wheel;
  wheel.kind = PointerEventKind::Wheel;
  wheel.column = 0;
  wheel.row = 0;
  wheel.wheel_y = 1;
  Require(!asciiomium::browser::TranslatePointerEvent(
               wheel, MakeStretch(), 0)
               .has_value(),
          "non-positive wheel scale is rejected");
}

}  // namespace

int main() {
  TestClickAndModifiers();
  TestMoveAndWheel();
  TestLetterboxAndResizeMapping();
  TestOddCornerAndCentreMapping();
  TestInvalidTranslation();

  if (failures != 0) {
    std::cerr << failures << " CEF pointer translator assertion(s) failed\n";
    return 1;
  }

  std::cout << "CEF pointer translator tests passed\n";
  return 0;
}
