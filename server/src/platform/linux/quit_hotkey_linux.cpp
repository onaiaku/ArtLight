/**
 * @file src/platform/linux/quit_hotkey_linux.cpp
 * @brief The pure half of the Linux quit combo: key events in, yes or no out.
 *
 * See the header for why this is kept away from devices and privileges. Everything in this file
 * is answerable with two integers, which is why it can be tested exhaustively and why nothing
 * here should ever grow a dependency on the machine it is running on.
 *
 * The one rule worth stating plainly, because it is what the tests pin down: the combo completes
 * on the KEY GOING DOWN with every required modifier already held - not on a repeat, and not on
 * a release. A held key repeats many times a second, so firing on repeat would end a stream
 * dozens of times over from one press.
 */
#include "src/platform/linux/quit_hotkey_linux.h"

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
  }  // namespace

  matcher_t::matcher_t(combo_t combo):
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
