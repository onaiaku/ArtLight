/**
 * @file src/platform/linux/quit_hotkey_linux.cpp
 * @brief The pure half of the Linux quit combo: key events in, yes or no out.
 *
 * See the header for why this is kept away from devices and privileges. Everything in this file
 * is answerable with two integers, which is why it can be tested exhaustively and why nothing
 * here should ever grow a dependency on the machine it is running on.
 *
 * Two things are worth stating plainly because the tests pin them down. First, the combo
 * completes on the KEY GOING DOWN with every required modifier already held - not on a repeat,
 * and not on a release: a held key repeats many times a second, so firing on repeat would end
 * one stream dozens of times over. Second, the translation from a person's "Ctrl+Alt+Shift+Q"
 * to an evdev code happens here and nowhere else, because those numbers are a Linux detail with
 * no business in a config file.
 */
#include "src/platform/linux/quit_hotkey_linux.h"

#include <cctype>

namespace quit_hotkey {
  namespace {
    bool is_ctrl(unsigned int code) {
      return code == key::left_ctrl || code == key::right_ctrl;
    }

    bool is_alt(unsigned int code) {
      return code == key::left_alt || code == key::right_alt;
    }

    bool is_shift(unsigned int code) {
      return code == key::left_shift || code == key::right_shift;
    }

    // <linux/input-event-codes.h>, A-Z. The evdev codes for the letters are NOT in alphabetical
    // order: this is a translation table, not arithmetic. The tests below check it against the
    // header's values rather than against anybody's eye.
    constexpr unsigned int kLetters[26] = {
        30, 48, 46, 32, 18, 33, 34, 35, 23, 36, 37, 38, 50,
        49, 24, 25, 16, 19, 31, 20, 22, 47, 17, 45, 21, 44};

    // '0'-'9'. Key 0 is the last of the top row, not the first.
    constexpr unsigned int kDigits[10] = {11, 2, 3, 4, 5, 6, 7, 8, 9, 10};

    /// F1-F24, which the kernel splits into three runs rather than one.
    unsigned int function_code(int number) {
      if (number <= 10) {
        return 58 + static_cast<unsigned int>(number);
      }
      if (number <= 12) {
        return 76 + static_cast<unsigned int>(number);
      }
      return 170 + static_cast<unsigned int>(number);
    }
  }  // namespace

  std::optional<resolved_combo_t> resolve(const combo_t &combo) {
    unsigned int code = 0;

    if (combo.function_key >= 1 && combo.function_key <= 24) {
      code = function_code(combo.function_key);
    } else {
      const auto c = static_cast<char>(std::toupper(static_cast<unsigned char>(combo.key)));
      if (c >= 'A' && c <= 'Z') {
        code = kLetters[c - 'A'];
      } else if (c >= '0' && c <= '9') {
        code = kDigits[c - '0'];
      }
    }

    if (code == 0) {
      // No evdev code for this key. The caller keeps its previous setting and says so, rather
      // than waiting for a key nobody can press.
      return std::nullopt;
    }

    resolved_combo_t out;
    out.key = code;
    out.ctrl = combo.ctrl;
    out.alt = combo.alt;
    out.shift = combo.shift;
    return out;
  }

  matcher_t::matcher_t(resolved_combo_t combo):
      combo_(combo) {
  }

  void matcher_t::reset() {
    ctrl_ = false;
    alt_ = false;
    shift_ = false;
  }

  bool matcher_t::feed(unsigned int code, int value) {
    const auto event = static_cast<event_value>(value);

    // The left and right variants of a modifier are the same modifier to a person pressing it,
    // and a keyboard that reports one rather than the other is not a different keyboard. Only a
    // release lets go; press and repeat both mean held.
    const bool held = event != event_value::release;

    if (is_ctrl(code)) {
      ctrl_ = held;
      return false;
    }

    if (is_alt(code)) {
      alt_ = held;
      return false;
    }

    if (is_shift(code)) {
      shift_ = held;
      return false;
    }

    if (code != combo_.key || event != event_value::press) {
      // Either not our key, or our key doing something other than going down. A release is the
      // user letting go after a match, and a repeat is the same press arriving again.
      return false;
    }

    const bool ctrl_ok = ctrl_ == combo_.ctrl || !combo_.ctrl;
    const bool alt_ok = alt_ == combo_.alt || !combo_.alt;
    const bool shift_ok = shift_ == combo_.shift || !combo_.shift;

    return ctrl_ok && alt_ok && shift_ok;
  }

  std::string matcher_t::state() const {
    std::string held;
    if (ctrl_) {
      held += "ctrl ";
    }
    if (alt_) {
      held += "alt ";
    }
    if (shift_) {
      held += "shift ";
    }
    if (held.empty()) {
      return "no modifiers held";
    }
    return held + "held";
  }
}  // namespace quit_hotkey
