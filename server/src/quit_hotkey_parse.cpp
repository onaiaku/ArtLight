/**
 * @file src/quit_hotkey_parse.cpp
 * @brief The parser, which must never be the reason a server fails to start.
 *
 * Nothing here throws and nothing here logs. A rejected value comes back as a result carrying a
 * sentence, and the decision about what to do about it - fall back and carry on - belongs to the
 * caller, where the config and the log are.
 */
#include "src/quit_hotkey_parse.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace quit_hotkey {
  namespace {
    std::string trim(const std::string &value) {
      const auto first = value.find_first_not_of(" \t\r\n");
      if (first == std::string::npos) {
        return {};
      }
      const auto last = value.find_last_not_of(" \t\r\n");
      return value.substr(first, last - first + 1);
    }

    std::string upper(const std::string &value) {
      std::string out = value;
      std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
      });
      return out;
    }

    std::vector<std::string> split_on_plus(const std::string &value) {
      std::vector<std::string> parts;
      std::string current;
      for (const char c : value) {
        if (c == '+') {
          parts.push_back(current);
          current.clear();
        } else {
          current += c;
        }
      }
      parts.push_back(current);
      return parts;
    }
  }  // namespace

  std::string combo_t::default_value() {
    return "Ctrl+Alt+Shift+O";
  }

  parse_result_t parse(const std::string &value) {
    parse_result_t result;

    const std::string trimmed = trim(value);
    if (trimmed.empty()) {
      result.error = "the value is empty";
      return result;
    }

    bool have_key = false;

    for (const auto &raw_part : split_on_plus(trimmed)) {
      const std::string part = upper(trim(raw_part));

      if (part.empty()) {
        result.error = "there is an empty part between two '+' signs";
        return result;
      }

      if (part == "CTRL") {
        result.ctrl = true;
        continue;
      }
      if (part == "ALT") {
        result.alt = true;
        continue;
      }
      if (part == "SHIFT") {
        result.shift = true;
        continue;
      }

      // Everything else has to be the key itself, and there can only be one.
      if (have_key) {
        result.error = "'" + part + "' is a second key; a combination has exactly one";
        return result;
      }

      if (part.size() == 1 && (std::isupper(static_cast<unsigned char>(part[0])) || std::isdigit(static_cast<unsigned char>(part[0])))) {
        result.key = part[0];
        have_key = true;
        continue;
      }

      // A function key: F followed by 1-24. "F" on its own is the letter F, not a function key,
      // which is why this needs the digits to be present and in range.
      if (part.size() > 1 && part[0] == 'F') {
        const std::string digits = part.substr(1);
        if (std::all_of(digits.begin(), digits.end(), [](unsigned char c) { return std::isdigit(c); })) {
          const int number = std::stoi(digits);
          if (number >= 1 && number <= 24) {
            result.function_key = number;
            have_key = true;
            continue;
          }
          result.error = "'" + part + "' is not a function key; this accepts F1 to F24";
          return result;
        }
      }

      result.error = "'" + part + "' is not a key this can register";
      return result;
    }

    if (!have_key) {
      result.error = "there is no key, only modifiers; that combination can never be pressed";
      return result;
    }

    result.ok = true;
    return result;
  }
}  // namespace quit_hotkey
