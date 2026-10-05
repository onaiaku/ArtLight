/**
 * @file src/quit_hotkey_parse.h
 * @brief Reading the quit combo out of a config file, forgivingly.
 *
 * The combo is stored as a person would write it - "Ctrl+Alt+Shift+Q" - because it gets read by
 * a person: in the config file, in the web UI, in the log line that says what the server thinks
 * it was told. That costs a parser, and this is it.
 *
 * The one rule that matters: a bad value must never stop the server. A typo in a config file is
 * a person's mistake, not a reason for their stream host to refuse to start, so a value that
 * cannot be understood is reported rather than thrown, and the caller falls back to the default
 * and says so. Everything here is therefore pure - a string in, a decision out - which is also
 * what makes the awkward inputs cheap to test.
 *
 * This is the SHARED form of a combination. Windows wants a virtual-key code and Linux wants an
 * evdev code, and neither of those belongs in a config file; each platform resolves this into
 * its own form at the point where it needs one. There is deliberately only one combo_t.
 */
#pragma once

#include <string>

namespace quit_hotkey {
  /**
   * @brief A combination, as a person would describe it.
   */
  struct combo_t {
    char key = 'Q';        ///< 'A'-'Z' or '0'-'9'. Always upper-case. Meaningless if function_key.
    int function_key = 0;  ///< 1-24 when the key is a function key, otherwise 0.
    bool ctrl = true;      ///< Whether either Ctrl must be held.
    bool alt = true;       ///< Whether either Alt must be held.
    bool shift = true;     ///< Whether either Shift must be held.

    /// The shipped combination, in the form a person would write it.
    ///
    /// Q, because that is the key the client already uses to leave a stream. This feature exists
    /// for the moment the keyboard has moved and the client can no longer see that combination,
    /// so the host has to watch the one the user will actually reach for - not a second,
    /// unfamiliar key they would have to be told about. O is the client's "open stream settings",
    /// which is why watching it would give one keystroke two different meanings depending on
    /// where the keyboard happens to be.
    static std::string default_value();
  };

  /**
   * @brief What parsing produced, understood or not.
   *
   * `error` is in words and is never empty when `ok` is false, because it is going into the log
   * next to the value that was rejected - and "input_quit_hotkey: 'Ctrl+Bananaphone' is not a
   * key" is a sentence somebody can act on.
   */
  struct parse_result_t {
    bool ok = false;
    char key = 'Q';
    int function_key = 0;
    bool ctrl = false;
    bool alt = false;
    bool shift = false;
    std::string error;
  };

  /**
   * @brief Parse a combination such as "Ctrl+Alt+Shift+Q".
   *
   * Forgiving about the things people vary and strict about the things that matter. Modifier
   * order does not matter, and modifier names are case-insensitive, because "ctrl+alt+q" and
   * "Alt+Ctrl+Q" are the same request. The key must be one this program can register: a letter
   * A-Z, a digit 0-9, or a function key F1-F24. Anything else - an empty value, an unknown
   * modifier, a key that is not a key, or a string with no key at all - comes back not-ok with
   * a sentence naming the offending part.
   *
   * An unknown modifier is rejected rather than ignored on purpose: quietly dropping "Super"
   * from "Ctrl+Alt+Shift+Super+Q" would register a different combination than was asked for,
   * which is a worse outcome than refusing to use that setting.
   */
  parse_result_t parse(const std::string &value);

  /**
   * @brief The combination a successful parse produced.
   *
   * Only meaningful when `ok` is true. This exists because the two types answer different
   * questions: parse_result_t says whether a value was understood and, if not, why; combo_t is
   * the combination itself, which is what each platform's resolve() takes. Keeping them apart
   * means the error text cannot be mistaken for part of the combination.
   */
  combo_t to_combo(const parse_result_t &parsed);
}  // namespace quit_hotkey
