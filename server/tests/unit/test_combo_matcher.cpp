/**
 * @file tests/unit/test_combo_matcher.cpp
 * @brief The Linux quit combo, pinned down without a keyboard.
 *
 * The matcher is the part of the Linux trigger that must not be wrong, and the part that can be
 * wrong invisibly: a combo that never fires and a combo that fires at the wrong moment both look
 * like "nothing happened" from the sofa. So the near-misses are tested as carefully as the hit -
 * Ctrl+Alt+O without Shift, the right key with the wrong modifiers, and a release with no press
 * before it, which is what a device arriving mid-stream looks like.
 */
#include <gtest/gtest.h>

#include "src/platform/linux/quit_hotkey_linux.h"

using quit_hotkey::event_value;
// A namespace needs an alias, not a using-declaration: `key` is a namespace, not a name.
namespace key = quit_hotkey::key;
using quit_hotkey::matcher_t;

namespace {
  constexpr int press = static_cast<int>(event_value::press);
  constexpr int release = static_cast<int>(event_value::release);
  constexpr int repeat = static_cast<int>(event_value::repeat);

  /// Hold every modifier the default combo asks for, in the order a person would.
  void hold_all(matcher_t &m) {
    m.feed(key::left_ctrl, press);
    m.feed(key::left_alt, press);
    m.feed(key::left_shift, press);
  }
}  // namespace

TEST(ComboMatcher, TheExactComboFires) {
  matcher_t m;
  hold_all(m);

  EXPECT_TRUE(m.feed(key::o, press));
}

TEST(ComboMatcher, CtrlAltOWithoutShiftDoesNotFire) {
  matcher_t m;
  m.feed(key::left_ctrl, press);
  m.feed(key::left_alt, press);

  EXPECT_FALSE(m.feed(key::o, press)) << "two of three modifiers must not end a stream";
}

TEST(ComboMatcher, CtrlAltShiftWithTheWrongKeyDoesNotFire) {
  matcher_t m;
  hold_all(m);

  EXPECT_FALSE(m.feed(25, press)) << "KEY_P is not the combo";
}

TEST(ComboMatcher, AKeyUpAloneDoesNotFire) {
  matcher_t m;
  hold_all(m);

  EXPECT_FALSE(m.feed(key::o, release)) << "a device arriving mid-stream sends releases first";
}

TEST(ComboMatcher, ReleasingAModifierStopsItFiring) {
  matcher_t m;
  hold_all(m);
  m.feed(key::left_shift, release);

  EXPECT_FALSE(m.feed(key::o, press)) << "Shift was let go, so the combo is no longer held";
}

TEST(ComboMatcher, RightHandModifiersCountTheSame) {
  matcher_t m;
  m.feed(key::right_ctrl, press);
  m.feed(key::right_alt, press);
  m.feed(key::right_shift, press);

  EXPECT_TRUE(m.feed(key::o, press)) << "which side a modifier is on is not a different combo";
}

TEST(ComboMatcher, RepeatsDoNotFireTwice) {
  matcher_t m;
  hold_all(m);

  EXPECT_TRUE(m.feed(key::o, press));
  EXPECT_FALSE(m.feed(key::o, repeat)) << "a held key repeats; one press must end one stream";
  EXPECT_FALSE(m.feed(key::o, repeat));
}

TEST(ComboMatcher, ResetForgetsEveryModifier) {
  matcher_t m;
  hold_all(m);
  m.reset();

  EXPECT_FALSE(m.feed(key::o, press)) << "a returned keyboard sends no releases; reset is the fix";
}

TEST(ComboMatcher, StateNamesWhatIsHeldForTheLog) {
  matcher_t m;
  EXPECT_EQ(m.state(), "no modifiers held");

  m.feed(key::left_ctrl, press);
  m.feed(key::left_shift, press);

  EXPECT_EQ(m.state(), "ctrl shift held");
}
