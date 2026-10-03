/**
 * @file src/usbip_input_policy.cpp
 * @brief Definitions for the portable USB/IP importer policies.
 */
#include "usbip_input_policy.h"

#include <cstddef>

namespace input::usbip {
  namespace {
    // A real busid is a bus number and a port number, both small. The cap exists so that a
    // pathological value from the network is refused before it reaches an argument vector.
    constexpr std::size_t kMaxBusidLength = 16;

    // Length of the "(1234:abcd)" form, which is the only parenthesised thing we trust.
    constexpr std::size_t kVidPidLength = 11;

    constexpr bool is_digit(const char c) {
      return c >= '0' && c <= '9';
    }

    constexpr bool is_hex(const char c) {
      return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
    }

    constexpr char to_lower(const char c) {
      return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
    }

    std::string_view trim(std::string_view text) {
      while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r')) {
        text.remove_prefix(1);
      }
      while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) {
        text.remove_suffix(1);
      }
      return text;
    }

    bool starts_with(const std::string_view text, const std::string_view prefix) {
      return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
    }

    /**
     * @brief Pull "(1234:abcd)" out of a line, lowercased. Empty when there is none.
     *
     * The four hex digits on each side are checked exactly rather than merely looking for a
     * colon inside brackets, because product names contain brackets of their own:
     * "[Viper Ultimate (Wireless)]" must not be read as a device id.
     */
    std::string extract_vid_pid(const std::string_view text) {
      for (std::size_t i = 0; i + kVidPidLength <= text.size(); ++i) {
        if (text[i] != '(' || text[i + 5] != ':' || text[i + 10] != ')') {
          continue;
        }

        bool ok = true;
        for (std::size_t k = 1; k <= 4 && ok; ++k) {
          ok = is_hex(text[i + k]);
        }
        for (std::size_t k = 6; k <= 9 && ok; ++k) {
          ok = is_hex(text[i + k]);
        }
        if (!ok) {
          continue;
        }

        std::string out;
        out.reserve(9);
        for (std::size_t k = 1; k <= 4; ++k) {
          out.push_back(to_lower(text[i + k]));
        }
        out.push_back(':');
        for (std::size_t k = 6; k <= 9; ++k) {
          out.push_back(to_lower(text[i + k]));
        }
        return out;
      }
      return {};
    }

    /// Drop a trailing "(1234:abcd)" from a description — the id is kept separately.
    std::string strip_trailing_vid_pid(const std::string_view text) {
      std::string_view s = trim(text);
      if (s.size() >= kVidPidLength && s.back() == ')') {
        const std::string_view tail = s.substr(s.size() - kVidPidLength);
        if (tail.front() == '(' && extract_vid_pid(tail).size() == 9) {
          s = trim(s.substr(0, s.size() - kVidPidLength));
        }
      }
      return std::string(s);
    }

    /// "Razer USA, Ltd : RC30-0305 ..." -> vendor and product.
    void split_vendor_product(const std::string_view text, Device &device) {
      const std::string_view s = trim(text);
      const auto separator = s.find(" : ");
      if (separator == std::string_view::npos) {
        device.product = strip_trailing_vid_pid(s);
        return;
      }
      device.vendor = std::string(trim(s.substr(0, separator)));
      device.product = strip_trailing_vid_pid(s.substr(separator + 3));
    }

    bool is_header_underline(const std::string_view line) {
      if (line.size() < 3) {
        return false;
      }
      for (const char c : line) {
        if (c != '=') {
          return false;
        }
      }
      return true;
    }

    /// Gather up to three lines that the tool itself called an error.
    void collect_error_lines(const std::string_view text, std::string &out, std::size_t &count) {
      std::size_t cursor = 0;
      while (cursor < text.size() && count < 3) {
        const auto newline = text.find('\n', cursor);
        const std::size_t stop = (newline == std::string_view::npos) ? text.size() : newline;
        const std::string_view candidate = trim(text.substr(cursor, stop - cursor));
        if (candidate.find("error:") != std::string_view::npos) {
          if (!out.empty()) {
            out += " | ";
          }
          out += std::string(candidate);
          ++count;
        }
        cursor = (newline == std::string_view::npos) ? text.size() : newline + 1;
      }
    }

    /**
     * @brief Refuse to read past a line we cannot place.
     *
     * When the tool named its own errors, those are the detail — all of them, joined. Picking
     * one would mean guessing which sentence a human needs, and in the captured failure the
     * line that matters is the second one: "open vhci_driver (is vhci_hcd loaded?)" says what
     * to do, while the line before it only says something went wrong. With no such line, the
     * detail is simply the row that defeated the parse.
     */
    DeviceList unreadable(const std::string_view line, const std::string_view rest) {
      DeviceList result;
      result.outcome = ListOutcome::Unreadable;
      result.detail = std::string(line);

      std::string errors;
      std::size_t count = 0;
      collect_error_lines(line, errors, count);
      collect_error_lines(rest, errors, count);
      if (!errors.empty()) {
        result.detail = errors;
      }

      return result;
    }
  }  // namespace

  bool is_valid_busid(const std::string_view busid) {
    if (busid.empty() || busid.size() > kMaxBusidLength) {
      return false;
    }

    // The first character must be a digit. This is what makes a leading '-' impossible, and
    // a leading '-' is what would turn a busid into an option usbip acts on.
    if (!is_digit(busid.front())) {
      return false;
    }

    std::size_t dashes = 0;
    for (const char c : busid) {
      if (c == '-') {
        ++dashes;
        continue;
      }
      if (!is_digit(c)) {
        return false;
      }
    }

    if (dashes != 1) {
      return false;
    }

    // The single separator has to sit between the two runs, not at the end. A trailing '-'
    // passes the digit test above and would otherwise be accepted.
    const auto separator = busid.find('-');
    return separator + 1 < busid.size();
  }

  DeviceList parse_device_list(const std::string_view output) {
    if (trim(output).empty()) {
      // Deliberately not NothingOffered. We cannot tell those apart without a clock and an
      // exit code, and guessing here is how a sleeping exporter becomes "nothing to share".
      DeviceList result;
      result.outcome = ListOutcome::NoOutput;
      return result;
    }

    DeviceList result;
    std::vector<Device> devices;
    bool saw_any_line = false;
    // True while the device just opened still owes us its description line. The Linux shape
    // puts the vendor on the following row; the Windows shape puts it on the busid row.
    bool wants_description = false;
    bool have_current = false;

    std::size_t cursor = 0;
    while (cursor <= output.size()) {
      const auto newline = output.find('\n', cursor);
      const std::size_t stop = (newline == std::string_view::npos) ? output.size() : newline;
      const std::string_view line = trim(output.substr(cursor, stop - cursor));
      cursor = (newline == std::string_view::npos) ? output.size() + 1 : newline + 1;

      if (line.empty()) {
        continue;
      }
      saw_any_line = true;

      // The Windows tool's banner, both of its rows. Recognising it is what lets an empty
      // result mean "nothing is offered" rather than "this build did not understand".
      if (line == "Exportable USB devices" || is_header_underline(line)) {
        continue;
      }

      // Linux shape: " - busid 3-10 (8087:0033)"
      if (starts_with(line, "- busid")) {
        const std::string_view rest = trim(line.substr(7));
        const auto space = rest.find_first_of(" \t");
        const std::string_view busid = (space == std::string_view::npos) ? rest : rest.substr(0, space);
        if (!is_valid_busid(busid)) {
          return unreadable(line, output.substr(cursor));
        }
        devices.push_back(Device{});
        devices.back().busid = std::string(busid);
        devices.back().vid_pid = extract_vid_pid(rest);
        wants_description = true;
        have_current = true;
        continue;
      }

      const auto separator = line.find(':');
      if (separator != std::string_view::npos) {
        const std::string_view head = trim(line.substr(0, separator));
        const std::string_view tail = line.substr(separator + 1);

        // Windows shape: the busid sits before the separator, on the device's own row.
        if (is_valid_busid(head)) {
          devices.push_back(Device{});
          devices.back().busid = std::string(head);
          devices.back().vid_pid = extract_vid_pid(tail);
          split_vendor_product(tail, devices.back());
          wants_description = false;
          have_current = true;
          continue;
        }

        if (wants_description && have_current) {
          // Linux shape: the vendor and product arrive on the row AFTER the busid, indented,
          // carrying a colon of their own. That is why the whole line is used here rather than
          // the part after its separator — splitting on that would throw the vendor away.
          Device &current = devices.back();
          split_vendor_product(head.empty() ? tail : line, current);
          if (current.vid_pid.empty()) {
            current.vid_pid = extract_vid_pid(line);
          }
          wants_description = false;
          continue;
        }

        if (head.empty() && have_current) {
          // A detail row hanging off the device above it.
          devices.back().interfaces.emplace_back(trim(tail));
          continue;
        }
      }

      // Bytes came back that we cannot place. That is not an empty list, and it is not a
      // device list either — say so rather than reporting a fact we do not have.
      return unreadable(line, output.substr(cursor));
    }

    if (!saw_any_line) {
      result.outcome = ListOutcome::NoOutput;
      return result;
    }

    if (!devices.empty()) {
      result.outcome = ListOutcome::Devices;
      result.devices = std::move(devices);
      return result;
    }

    // Every non-empty line was understood — a line we could not place returned early above, and
    // an entirely empty document never reached this point — so the tool answered the question and
    // its answer is that it has nothing to offer. That is a fact, not a guess. The banner checks
    // above are what make it safe, which is why an unrecognised line must return early rather
    // than being skipped.
    result.outcome = ListOutcome::NothingOffered;
    return result;
  }

  std::string describe(const ListOutcome outcome) {
    switch (outcome) {
      case ListOutcome::Devices:
        return "the exporter is offering devices";
      case ListOutcome::NothingOffered:
        return "the exporter is offering nothing";
      case ListOutcome::NoOutput:
        return "no output came back - the exporter may be asleep or unreachable";
      case ListOutcome::Unreadable:
        return "output came back that this build could not read";
    }
    return "unknown";
  }
}  // namespace input::usbip
