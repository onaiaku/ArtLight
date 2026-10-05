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

// --- The translation from a person's combo to evdev codes -------------------------------------
//
// Also pure, and also worth pinning down: the letters' evdev codes are not in alphabetical
// order and the function keys are split into three runs across the range, so a mistake here
// would be a combo that quietly never matches - the exact failure that costs an afternoon.

namespace {
  quit_hotkey::combo_t bare(char key, int function_key = 0) {
    quit_hotkey::combo_t combo;
    combo.key = key;
    combo.function_key = function_key;
    combo.ctrl = false;
    combo.alt = false;
    combo.shift = false;
    return combo;
  }

  unsigned int resolved_key(const quit_hotkey::combo_t &combo) {
    const auto resolved = quit_hotkey::resolve(combo);
    EXPECT_TRUE(resolved.has_value());
    return resolved.has_value() ? resolved->key : 0u;
  }
}  // namespace

TEST(ComboResolve, TheShippedComboResolvesToTheKeyO) {
  const auto resolved = quit_hotkey::resolve(quit_hotkey::combo_t{});

  ASSERT_TRUE(resolved.has_value());
  EXPECT_EQ(resolved->key, 24u) << "KEY_O";
  EXPECT_TRUE(resolved->ctrl && resolved->alt && resolved->shift);
}

TEST(ComboResolve, LettersUseTheKernelsCodesNotArithmetic) {
  EXPECT_EQ(resolved_key(bare('A')), 30u) << "KEY_A";
  EXPECT_EQ(resolved_key(bare('Z')), 44u) << "KEY_Z";
  EXPECT_EQ(resolved_key(bare('Q')), 16u) << "KEY_Q; the letters are not sequential in evdev";
}

TEST(ComboResolve, ZeroIsTheLastDigitKeyNotTheFirst) {
  EXPECT_EQ(resolved_key(bare('1')), 2u) << "KEY_1";
  EXPECT_EQ(resolved_key(bare('7')), 8u) << "KEY_7";
  EXPECT_EQ(resolved_key(bare('0')), 11u) << "KEY_0 sits after KEY_9";
}

TEST(ComboResolve, FunctionKeysCoverAllThreeKernelRuns) {
  EXPECT_EQ(resolved_key(bare('O', 1)), 59u) << "KEY_F1";
  EXPECT_EQ(resolved_key(bare('O', 10)), 68u) << "KEY_F10, the last of the first run";
  EXPECT_EQ(resolved_key(bare('O', 11)), 87u) << "KEY_F11, a gap then the second run";
  EXPECT_EQ(resolved_key(bare('O', 12)), 88u) << "KEY_F12";
  EXPECT_EQ(resolved_key(bare('O', 13)), 183u) << "KEY_F13, a long gap then the third run";
  EXPECT_EQ(resolved_key(bare('O', 24)), 194u) << "KEY_F24, the last one";
}

TEST(ComboResolve, AKeyWithNoEvdevCodeResolvesToNothing) {
  EXPECT_FALSE(quit_hotkey::resolve(bare('!')).has_value())
      << "the caller must keep its old setting rather than watch for an impossible key";
}
