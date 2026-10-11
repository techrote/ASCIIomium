#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include "browser/cef_key_translator.h"

#include <cstdint>
#include <iomanip>
#include <sstream>

namespace asciiomium::browser {
namespace {

using asciiomium::input::HasModifier;
using asciiomium::input::KeyboardEvent;
using asciiomium::input::KeyboardEventKind;
using asciiomium::input::KeyCode;
using asciiomium::input::KeyModifiers;
using asciiomium::input::kModifierAlt;
using asciiomium::input::kModifierControl;
using asciiomium::input::kModifierShift;

std::uint32_t CefModifiers(KeyModifiers modifiers) {
  std::uint32_t result = EVENTFLAG_NONE;
  if (HasModifier(modifiers, kModifierShift)) result |= EVENTFLAG_SHIFT_DOWN;
  if (HasModifier(modifiers, kModifierControl)) result |= EVENTFLAG_CONTROL_DOWN;
  if (HasModifier(modifiers, kModifierAlt)) result |= EVENTFLAG_ALT_DOWN;
  return result;
}

int VirtualKeyForKey(KeyCode key) {
  switch (key) {
    case KeyCode::Enter: return VK_RETURN;
    case KeyCode::Tab: return VK_TAB;
    case KeyCode::Backspace: return VK_BACK;
    case KeyCode::Escape: return VK_ESCAPE;
    case KeyCode::ArrowUp: return VK_UP;
    case KeyCode::ArrowDown: return VK_DOWN;
    case KeyCode::ArrowLeft: return VK_LEFT;
    case KeyCode::ArrowRight: return VK_RIGHT;
    case KeyCode::Home: return VK_HOME;
    case KeyCode::End: return VK_END;
    case KeyCode::PageUp: return VK_PRIOR;
    case KeyCode::PageDown: return VK_NEXT;
    case KeyCode::Insert: return VK_INSERT;
    case KeyCode::Delete: return VK_DELETE;
    case KeyCode::F1: return VK_F1;
    case KeyCode::F2: return VK_F2;
    case KeyCode::F3: return VK_F3;
    case KeyCode::F4: return VK_F4;
    case KeyCode::F5: return VK_F5;
    case KeyCode::F6: return VK_F6;
    case KeyCode::F7: return VK_F7;
    case KeyCode::F8: return VK_F8;
    case KeyCode::F9: return VK_F9;
    case KeyCode::F10: return VK_F10;
    case KeyCode::F11: return VK_F11;
    case KeyCode::F12: return VK_F12;
    case KeyCode::Character:
    case KeyCode::None: return 0;
  }
  return 0;
}

int VirtualKeyForCharacter(char32_t codepoint) {
  if (codepoint >= U'a' && codepoint <= U'z') {
    return static_cast<int>('A' + (codepoint - U'a'));
  }
  if (codepoint >= U'A' && codepoint <= U'Z') {
    return static_cast<int>(codepoint);
  }
  if (codepoint >= U'0' && codepoint <= U'9') {
    return static_cast<int>(codepoint);
  }
  if (codepoint > 0xFFFF) {
    return 0;
  }

  const SHORT mapped =
      VkKeyScanExW(static_cast<WCHAR>(codepoint), GetKeyboardLayout(0));
  if (mapped == -1) {
    return 0;
  }
  return LOBYTE(mapped);
}

bool IsExtendedVirtualKey(int virtual_key) {
  switch (virtual_key) {
    case VK_INSERT:
    case VK_DELETE:
    case VK_HOME:
    case VK_END:
    case VK_PRIOR:
    case VK_NEXT:
    case VK_LEFT:
    case VK_RIGHT:
    case VK_UP:
    case VK_DOWN:
      return true;
    default:
      return false;
  }
}

int NativeKeyCode(int virtual_key,
                  KeyModifiers modifiers,
                  cef_key_event_type_t type) {
  if (virtual_key == 0) {
    return 0;
  }

  const UINT scan = MapVirtualKeyExW(
      static_cast<UINT>(virtual_key), MAPVK_VK_TO_VSC_EX,
      GetKeyboardLayout(0));
  int native = 1;  // repeat count; physical repeat state is not transported.
  native |= static_cast<int>(scan & 0xFFu) << 16;
  if ((scan & 0xFF00u) != 0 || IsExtendedVirtualKey(virtual_key)) {
    native |= 1 << 24;
  }
  if (HasModifier(modifiers, kModifierAlt)) {
    native |= 1 << 29;
  }
  if (type == KEYEVENT_KEYUP) {
    native |= static_cast<int>(0xC0000000u);
  }
  return native;
}

CefKeyEvent KeyEvent(cef_key_event_type_t type,
                     int virtual_key,
                     KeyModifiers modifiers,
                     char16_t character = 0,
                     char16_t unmodified = 0) {
  CefKeyEvent result;
  result.type = type;
  result.modifiers = CefModifiers(modifiers);
  result.windows_key_code = virtual_key;
  result.native_key_code = NativeKeyCode(virtual_key, modifiers, type);
  result.is_system_key = HasModifier(modifiers, kModifierAlt) ? 1 : 0;
  result.character = character;
  result.unmodified_character = unmodified;
  return result;
}

void AppendUtf16Char(std::vector<CefKeyEvent>* output,
                     char32_t codepoint,
                     KeyModifiers modifiers) {
  auto append = [&](char16_t unit) {
    CefKeyEvent event = KeyEvent(KEYEVENT_CHAR, static_cast<int>(unit),
                                 modifiers, unit, unit);
    event.native_key_code = 0;
    event.is_system_key = 0;
    output->push_back(event);
  };

  if (codepoint <= 0xFFFF) {
    append(static_cast<char16_t>(codepoint));
    return;
  }

  codepoint -= 0x10000;
  append(static_cast<char16_t>(0xD800 + (codepoint >> 10)));
  append(static_cast<char16_t>(0xDC00 + (codepoint & 0x3FF)));
}

char16_t CharacterForKey(KeyCode key) {
  switch (key) {
    case KeyCode::Enter: return u'\r';
    case KeyCode::Tab: return u'\t';
    case KeyCode::Backspace: return u'\b';
    default: return 0;
  }
}

}  // namespace

std::vector<CefKeyEvent> TranslateKeyboardEvent(const KeyboardEvent& event) {
  std::vector<CefKeyEvent> output;
  if (event.kind == KeyboardEventKind::Unsupported) {
    return output;
  }

  if (event.kind == KeyboardEventKind::Text) {
    // The terminal transport already performed keyboard-layout/IME-to-text
    // conversion. Do not fabricate a physical key transition for ordinary
    // Unicode text. Alt-prefixed printable input is treated as a shortcut
    // instead, because emitting CHAR as well would type a duplicate/unwanted
    // character for common browser Alt chords.
    if (HasModifier(event.modifiers, kModifierAlt) ||
        HasModifier(event.modifiers, kModifierControl)) {
      const int vk = VirtualKeyForCharacter(event.codepoint);
      if (vk != 0) {
        output.push_back(
            KeyEvent(KEYEVENT_RAWKEYDOWN, vk, event.modifiers));
        output.push_back(KeyEvent(KEYEVENT_KEYUP, vk, event.modifiers));
      }
      return output;
    }

    AppendUtf16Char(&output, event.codepoint, event.modifiers);
    return output;
  }

  int virtual_key = VirtualKeyForKey(event.key);
  if (event.key == KeyCode::Character) {
    virtual_key = VirtualKeyForCharacter(event.codepoint);
  }
  if (virtual_key == 0) {
    return output;
  }

  output.push_back(KeyEvent(KEYEVENT_RAWKEYDOWN, virtual_key, event.modifiers));

  const char16_t control_character = CharacterForKey(event.key);
  if (control_character != 0 && event.modifiers == 0) {
    output.push_back(KeyEvent(KEYEVENT_CHAR,
                             static_cast<int>(control_character),
                             event.modifiers,
                             control_character,
                             control_character));
  }

  output.push_back(KeyEvent(KEYEVENT_KEYUP, virtual_key, event.modifiers));
  return output;
}

std::string DescribeCefKeyEvent(const CefKeyEvent& event) {
  std::ostringstream out;
  switch (event.type) {
    case KEYEVENT_RAWKEYDOWN: out << "RAWKEYDOWN"; break;
    case KEYEVENT_KEYDOWN: out << "KEYDOWN"; break;
    case KEYEVENT_KEYUP: out << "KEYUP"; break;
    case KEYEVENT_CHAR: out << "CHAR"; break;
    default: out << "UNKNOWN"; break;
  }
  out << "(vk=" << event.windows_key_code
      << ",mods=0x" << std::hex << std::uppercase << event.modifiers
      << std::dec;
  if (event.character != 0) {
    out << ",char=U+" << std::hex << std::uppercase << std::setfill('0')
        << std::setw(4) << static_cast<unsigned>(event.character) << std::dec;
  }
  out << ')';
  return out.str();
}

}  // namespace asciiomium::browser
