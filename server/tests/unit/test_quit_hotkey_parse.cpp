/**
 * @file tests/unit/test_quit_hotkey_parse.cpp
 * @brief The quit combo's parser, pinned down against the values people actually type.
 *
 * Two failure modes are worth more attention than the happy path, and both are here. The first
 * is the near-miss that silently means something else - a modifier spelled "Control" that is
 * quietly ignored would turn a three-key combo into a two-key one, which is a security-ish
 * surprise rather than a typo. The second is a bad value taking the server down with it, which
 * is why every rejection here is asserted to come back as a sentence and not an exception.
 */
#include <gtest/gtest.h>

#include "src/quit_hotkey_parse.h"

using quit_hotkey::parse;

TEST(QuitHotkey, ParsesTheDefaultCombo) {
  const auto parsed = parse("Ctrl+Alt+Shift+O");

  ASSERT_TRUE(parsed.ok) << parsed.error;
  EXPECT_EQ(parsed.key, 'O');
  EXPECT_TRUE(parsed.ctrl && parsed.alt && parsed.shift);
}

TEST(QuitHotkey, RejectsJunkWithoutThrowing) {
  const auto parsed = parse("Ctrl+Bananaphone");

  EXPECT_FALSE(parsed.ok);
  EXPECT_FALSE(parsed.error.empty()) << "a rejection must say why";
}

TEST(QuitHotkey, ModifierOrderDoesNotMatter) {
  const auto parsed = parse("Shift+Alt+Ctrl+O");

  ASSERT_TRUE(parsed.ok) << parsed.error;
  EXPECT_EQ(parsed.key, 'O');
  EXPECT_TRUE(parsed.ctrl && parsed.alt && parsed.shift);
}

TEST(QuitHotkey, ModifierNamesAreCaseInsensitive) {
  const auto parsed = parse("ctrl+ALT+shift+o");

  ASSERT_TRUE(parsed.ok) << parsed.error;
  EXPECT_EQ(parsed.key, 'O') << "the key is reported upper-case";
  EXPECT_TRUE(parsed.ctrl && parsed.alt && parsed.shift);
}

TEST(QuitHotkey, FewerModifiersIsAValidCombination) {
  const auto parsed = parse("Ctrl+Alt+O");

  ASSERT_TRUE(parsed.ok) << parsed.error;
  EXPECT_TRUE(parsed.ctrl);
  EXPECT_TRUE(parsed.alt);
  EXPECT_FALSE(parsed.shift);
}

TEST(QuitHotkey, DigitsAndFunctionKeysAreKeys) {
  ASSERT_TRUE(parse("Ctrl+Alt+Shift+7").ok);
  EXPECT_EQ(parse("Ctrl+Alt+Shift+7").key, '7');

  ASSERT_TRUE(parse("Ctrl+Alt+Shift+F12").ok);
  ASSERT_TRUE(parse("Ctrl+Alt+Shift+F24").ok);
  EXPECT_FALSE(parse("Ctrl+Alt+Shift+F25").ok) << "F25 does not exist";
}

TEST(QuitHotkey, AKeyWithNoModifiersIsStillACombination) {
  const auto parsed = parse("O");

  ASSERT_TRUE(parsed.ok) << parsed.error;
  EXPECT_EQ(parsed.key, 'O');
  EXPECT_FALSE(parsed.ctrl || parsed.alt || parsed.shift);
}

TEST(QuitHotkey, RejectsAnEmptyValue) {
  EXPECT_FALSE(parse("").ok);
  EXPECT_FALSE(parse("   ").ok);
}

TEST(QuitHotkey, RejectsModifiersWithNoKey) {
  const auto parsed = parse("Ctrl+Alt+Shift");

  EXPECT_FALSE(parsed.ok) << "a combination with no key can never be pressed";
  EXPECT_FALSE(parsed.error.empty());
}

TEST(QuitHotkey, RejectsAnUnknownModifierRatherThanIgnoringIt) {
  const auto parsed = parse("Ctrl+Alt+Shift+Super+O");

  EXPECT_FALSE(parsed.ok)
      << "silently dropping Super would register a different combination than was asked for";
}

TEST(QuitHotkey, RejectsAnUnknownKey) {
  EXPECT_FALSE(parse("Ctrl+Alt+Shift+Bananaphone").ok);
  EXPECT_FALSE(parse("Ctrl+Alt+Shift+F0").ok);
}

TEST(QuitHotkey, ToleratesStraySpaces) {
  const auto parsed = parse(" Ctrl + Alt + Shift + O ");

  ASSERT_TRUE(parsed.ok) << parsed.error;
  EXPECT_EQ(parsed.key, 'O');
  EXPECT_TRUE(parsed.ctrl && parsed.alt && parsed.shift);
}
