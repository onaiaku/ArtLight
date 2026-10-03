/**
 * @file src/usbip_input_policy.cpp
 * @brief Definitions for the portable USB/IP importer policies.
 */
#include "usbip_input_policy.h"

#include <cctype>
#include <cstddef>
#include <stdexcept>

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

    /// True for the Linux tool's per-exporter header, " - 192.168.50.35".
    ///
    /// A remote listing from the Linux tool is grouped BY EXPORTER, and each group opens with a
    /// line naming the machine whose rows follow. It is not a device and it is not the banner,
    /// and without a rule for it the whole reply is unreadable: the same document reads fine
    /// from `usbip-win2` and fails from `usbip`, so a Linux importer would attach nothing while
    /// the Windows host beside it worked — and "the exporter offers nothing" would report the
    /// identical wrong answer, because the header is still printed when there is nothing under
    /// it.
    ///
    /// Distinguished from the local list's device rows, which also begin with a dash: those
    /// continue with the word "busid". Anything else after the dash is an address rather than a
    /// device we failed to read, so the test is deliberately narrow — no whitespace, and only
    /// the characters a host name or an address can carry. A line that fails that is still
    /// handed back as unreadable, because a rule loose enough to swallow anything would turn a
    /// format change into "nothing is offered".
    bool is_exporter_header(const std::string_view line) {
      if (!starts_with(line, "- ") || starts_with(line, "- busid")) {
        return false;
      }
      const std::string_view rest = trim(line.substr(2));
      if (rest.empty() || rest.find_first_of(" \t") != std::string_view::npos) {
        return false;
      }
      return rest.find_first_not_of("0123456789abcdefABCDEF.:-_%[]") == std::string_view::npos;
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

      // The Linux tool's per-exporter header, which the Windows tool does not print at all.
      if (is_exporter_header(line)) {
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

  namespace {
    /**
     * @brief Read "Port NN:" from a row, in either tool's shape.
     *
     * Three answers, and the third is the one that matters: -1 means this is not a port row and
     * the caller should look elsewhere; -2 means it starts like our format and then is not one,
     * which has to STOP the parse. Skipping a malformed row would turn "we cannot read this"
     * into "this machine holds nothing", and the cost of that is a device never given back.
     *
     * The 255 ceiling is usbip's own: detach refuses a port outside [1,255]. Reading a larger
     * number is not a device we could release, so it is refused here rather than carried.
     */
    int parse_port_number(const std::string_view line) {
      constexpr std::string_view kPrefix = "Port ";
      if (!starts_with(line, kPrefix)) {
        return -1;
      }

      const auto colon = line.find(':');
      if (colon == std::string_view::npos || colon <= kPrefix.size()) {
        return -2;
      }

      const std::string_view digits = trim(line.substr(kPrefix.size(), colon - kPrefix.size()));
      if (digits.empty() || digits.size() > 3) {
        return -2;
      }

      int value = 0;
      for (const char c : digits) {
        if (!is_digit(c)) {
          return -2;
        }
        value = (value * 10) + (c - '0');
      }

      return value > 255 ? -2 : value;
    }

    /**
     * @brief Fill in the remote identity from a "-> usbip://HOST:SERVICE/BUSID" row.
     *
     * The busid is taken from the URL rather than from the row's leading column, because the two
     * tools disagree about what leads the row: usbip-win2 leaves it blank, the Linux tool prints
     * the busid there. The last path segment is the one place both agree.
     *
     * @return false when the row is not a URL we can read, or the busid fails validation — the
     *         busid arrives from another machine, so it is checked here exactly as any other
     *         value off the wire is.
     */
    bool parse_usbip_url(const std::string_view line, Attached &device) {
      constexpr std::string_view kScheme = "usbip://";
      const auto scheme = line.find(kScheme);
      if (scheme == std::string_view::npos) {
        return false;
      }

      const std::string_view rest = trim(line.substr(scheme + kScheme.size()));
      const auto slash = rest.find('/');
      if (slash == std::string_view::npos) {
        return false;
      }

      const std::string_view authority = rest.substr(0, slash);
      const std::string_view busid = trim(rest.substr(slash + 1));
      const auto colon = authority.rfind(':');
      if (colon == std::string_view::npos) {
        return false;
      }

      // Validate before storing: nothing leaves here for an argument vector unchecked.
      if (!is_valid_busid(busid)) {
        return false;
      }

      device.exporter = std::string(trim(authority.substr(0, colon)));
      device.service = std::string(trim(authority.substr(colon + 1)));
      device.busid = std::string(busid);
      return true;
    }

    /// The port-shaped twin of unreadable(), with the same rule about which line is the detail.
    AttachedList unreadable_attached(const std::string_view line, const std::string_view rest) {
      AttachedList result;
      result.outcome = PortOutcome::Unreadable;
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

  AttachedList parse_attached(const std::string_view output) {
    if (trim(output).empty()) {
      // Deliberately not NothingAttached. usbip-win2 prints NOTHING when it holds nothing and
      // exits 0, so zero bytes here is genuinely ambiguous and the caller's exit code is the
      // only thing that separates "holds nothing" from "could not reach the driver". Guessing
      // it here would be the same mistake parse_device_list refuses to make.
      AttachedList result;
      result.outcome = PortOutcome::NoOutput;
      return result;
    }

    AttachedList result;
    std::vector<Attached> devices;
    bool saw_any_line = false;
    bool have_device = false;
    // The tool's own diagnostics: seen, recorded, and skipped. Kept so that "it complained and
    // named no ports" can be told apart from "it holds nothing" — see the end of this function.
    std::string diagnostics;
    std::size_t diagnostic_count = 0;
    // The Linux tool's diagnostic prefix. It is the tool talking about itself, not about a port.
    constexpr std::string_view kToolDiagnostic = "libusbip: error:";
    // True once a "Port NN:" row has opened a device that still owes us its description row.
    // While this is set the next line cannot be a header, which is what stops a device whose
    // name merely starts with "Port " from being read as the start of a new one.
    bool wants_description = false;

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

      // Both tools' banner, both rows. The Linux tool prints it even when it holds nothing.
      if (line == "Imported USB devices" || is_header_underline(line)) {
        continue;
      }

      // The Linux tool's own diagnostics, printed alongside a perfectly valid port table.
      //
      // It emits these when a port is occupied and it cannot read that port's record file — and
      // on a modern kernel it cannot, every time, because the record directory it wants
      // (/var/run/vhci_hcd) no longer exists. That is the same reason the occupied port's row
      // reads "unknown host, remote port and remote busid".
      //
      // They must not stop the parse. A merged capture is a normal way to run this — a shell
      // pipeline, or a popen that did not separate the streams — and refusing a table that is
      // right there, over a line of the tool talking about itself, costs a device that is never
      // given back. Skipping is only safe because of the check at the end of this function:
      // complaining and naming no ports is not the same as holding nothing.
      if (starts_with(line, kToolDiagnostic)) {
        if (diagnostic_count < 3) {
          if (!diagnostics.empty()) {
            diagnostics += " | ";
          }
          diagnostics += std::string(line);
          ++diagnostic_count;
        }
        continue;
      }

      if (!wants_description) {
        const int port = parse_port_number(line);
        if (port >= 0) {
          Attached device;
          device.port = port;

          // "at <speed>" appears in both shapes. Kept verbatim; never branched on.
          constexpr std::string_view kAt = " at ";
          const auto at = line.rfind(kAt);
          if (at != std::string_view::npos) {
            device.speed = std::string(trim(line.substr(at + kAt.size())));
          }

          devices.push_back(std::move(device));
          have_device = true;
          wants_description = true;
          continue;
        }
        if (port == -2) {
          return unreadable_attached(line, output.substr(cursor));
        }
      }

      if (!have_device) {
        // Detail, or something we do not know, with no device to hang it off.
        return unreadable_attached(line, output.substr(cursor));
      }

      if (wants_description) {
        // The row after the header names the device, in both shapes. A "->" row here means the
        // header had no description: a shape we do not know, so refuse rather than absorb it
        // into the product name.
        if (line.find("->") != std::string_view::npos) {
          return unreadable_attached(line, output.substr(cursor));
        }
        devices.back().product = std::string(line);
        wants_description = false;
        continue;
      }

      // From here on every row belongs to the device above and must be one of its detail rows.
      if (line.find("->") == std::string_view::npos) {
        return unreadable_attached(line, output.substr(cursor));
      }

      // The Linux tool prints the remote busid in the row's LEADING column when it has no URL to
      // put it in — and on a modern kernel it never has one, because the record the URL would
      // come from is gone. That leaves the leading column as the only place the identity appears:
      //
      //     9-1 -> unknown host, remote port and remote busid
      //
      // Without it the held device has no identity, and a hold that cannot be shown to be the
      // device we wanted is one the planner hands back — so the importer would attach a device
      // and then detach it again on the next reconcile, forever. The column is only read when
      // what is in it passes the same validation any busid off the wire passes, and it never
      // overwrites a busid the URL already gave us.
      if (devices.back().busid.empty()) {
        const std::string_view lead = trim(line.substr(0, line.find("->")));
        if (is_valid_busid(lead)) {
          devices.back().busid = std::string(lead);
        }
      }

      if (line.find("usbip://") != std::string_view::npos) {
        if (!parse_usbip_url(line, devices.back())) {
          return unreadable_attached(line, output.substr(cursor));
        }
        continue;
      }

      constexpr std::string_view kSerial = "-> serial:";
      constexpr std::string_view kMode = "-> mode:";
      constexpr std::string_view kBusDev = "-> remote bus/dev";

      if (line.find("unknown host") != std::string_view::npos || starts_with(line, kBusDev)) {
        // The Linux tool could not read this port's record, or is telling us the remote bus and
        // device numbers. Neither carries identity we need: the port above is what releases it.
        continue;
      }
      if (starts_with(line, kSerial)) {
        // The tool prints "-> serial: " with a trailing space and nothing after it when the
        // attach carried no --serial, which is the normal case. The row is already trimmed on
        // the way in, so this trim is belt-and-braces rather than the thing that makes the
        // value empty — mutation testing showed that removing it changes nothing observable.
        // Kept because it costs nothing and the day the outer trim moves is the day it matters.
        devices.back().serial = std::string(trim(line.substr(kSerial.size())));
        continue;
      }
      if (starts_with(line, kMode)) {
        devices.back().mode = std::string(trim(line.substr(kMode.size())));
        continue;
      }

      return unreadable_attached(line, output.substr(cursor));
    }

    if (!saw_any_line) {
      result.outcome = PortOutcome::NoOutput;
      return result;
    }

    if (!devices.empty()) {
      result.outcome = PortOutcome::Devices;
      result.devices = std::move(devices);
      return result;
    }

    // The tool complained and named no ports at all. That is NOT "this machine holds nothing":
    // these lines are the tool saying it could not read a record, and answering "your hands are
    // empty" from them would leave a device attached here forever, with the machine it came from
    // missing the hardware and no row anywhere saying so. Refuse, and hand back what it said.
    if (diagnostic_count > 0) {
      result.outcome = PortOutcome::Unreadable;
      result.detail = diagnostics;
      return result;
    }

    // The banner was recognised and no port row followed. On Linux that is exactly what
    // "holding nothing" looks like, and it is reachable only because every line we could not
    // place returned early above.
    result.outcome = PortOutcome::NothingAttached;
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

  std::string describe(const PortOutcome outcome) {
    switch (outcome) {
      case PortOutcome::Devices:
        return "this machine is holding devices";
      case PortOutcome::NothingAttached:
        return "this machine is holding nothing";
      case PortOutcome::NoOutput:
        return "no output came back - the tool may not have reached the driver";
      case PortOutcome::Unreadable:
        return "output came back that this build could not read";
    }
    return "unknown";
  }
  std::string describe(const Action action) {
    switch (action) {
      case Action::Attach:
        return "attach";
      case Action::Detach:
        return "detach";
    }
    return "unknown";
  }

  std::size_t Plan::attach_count() const {
    std::size_t count = 0;
    for (const PlannedAction &action : actions) {
      if (action.action == Action::Attach) {
        ++count;
      }
    }
    return count;
  }

  std::size_t Plan::detach_count() const {
    std::size_t count = 0;
    for (const PlannedAction &action : actions) {
      if (action.action == Action::Detach) {
        ++count;
      }
    }
    return count;
  }

  namespace {
    bool contains(const std::vector<std::string> &values, const std::string_view needle) {
      for (const std::string &value : values) {
        if (value == needle) {
          return true;
        }
      }
      return false;
    }

    bool offers(const std::vector<Device> &offered, const std::string_view busid) {
      for (const Device &device : offered) {
        if (device.busid == busid) {
          return true;
        }
      }
      return false;
    }

    /// Whether this machine already holds that device. An entry with no readable remote identity
    /// never matches: it cannot be shown to be the device in question, and treating it as a match
    /// would leave a device held forever under a name nobody can check.
    bool holds(const std::vector<Attached> &attached, const std::string_view busid) {
      for (const Attached &held : attached) {
        if (!held.busid.empty() && held.busid == busid) {
          return true;
        }
      }
      return false;
    }

    /// The exporter's address becomes an argument to another program. It arrives from config
    /// rather than from the wire, so this is not the same threat as a busid - but a leading '-'
    /// would be read as an option and whitespace cannot survive an argument vector at all, so
    /// both are refused here rather than passed on and hoped about.
    bool is_usable_exporter(const std::string_view exporter) {
      if (exporter.empty() || exporter.front() == '-') {
        return false;
      }
      for (const char c : exporter) {
        const bool allowed = is_digit(c) || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
                             || c == '.' || c == ':' || c == '-' || c == '_';
        if (!allowed) {
          return false;
        }
      }
      return true;
    }
  }  // namespace

  Plan plan_reconcile(const std::vector<Device> &offered,
                      const std::vector<Attached> &attached,
                      const std::vector<std::string> &wanted) {
    Plan plan;

    // An empty want list means the user's answer is "whatever the exporter is offering", because
    // ArtMoon binds exactly what was toggled on and nothing else.
    const bool everything_wanted = wanted.empty();

    // Detaches first. A swap - the user now wants B instead of A - has to give up A's port
    // before B can be given one.
    for (const Attached &held : attached) {
      const bool still_wanted = everything_wanted || contains(wanted, held.busid);
      const bool still_offered = !held.busid.empty() && offers(offered, held.busid);

      if (still_wanted && still_offered) {
        continue;  // already right: held, offered and wanted
      }

      PlannedAction action;
      action.action = Action::Detach;
      action.busid = held.busid;
      action.port = held.port;
      if (held.busid.empty()) {
        action.reason = "port " + std::to_string(held.port)
                        + " is held with no readable remote identity, so it is given back";
      } else if (!still_wanted) {
        action.reason = held.busid + " is held but no longer wanted";
      } else {
        action.reason = held.busid + " is held but no longer offered by the exporter";
      }
      plan.actions.push_back(std::move(action));
    }

    // Then attaches, in the order the exporter listed them, so the plan is stable across runs.
    for (const Device &device : offered) {
      if (!everything_wanted && !contains(wanted, device.busid)) {
        continue;  // offered but not wanted: leave it exactly where it is
      }
      if (holds(attached, device.busid)) {
        continue;  // already held
      }

      PlannedAction action;
      action.action = Action::Attach;
      action.busid = device.busid;
      action.reason = device.busid + " is offered and wanted";
      plan.actions.push_back(std::move(action));
    }

    return plan;
  }

  std::vector<std::string> build_attach_argv(const std::string_view exporter,
                                             const std::string_view busid) {
    if (!is_valid_busid(busid)) {
      throw std::invalid_argument("refusing to attach a busid that is not a busid: '" +
                                  std::string(busid) + "'");
    }
    if (!is_usable_exporter(exporter)) {
      throw std::invalid_argument("refusing to attach from an unusable exporter address: '" +
                                  std::string(exporter) + "'");
    }
    // Element 0 is a placeholder. The platform layer replaces it with the client it resolved,
    // because on Windows the client is deliberately not on PATH.
    return {"usbip", "attach", "-r", std::string(exporter), "-b", std::string(busid)};
  }

  std::vector<std::string> build_detach_argv(const int port) {
    if (port < 0) {
      throw std::invalid_argument("refusing to detach a negative port");
    }
    // Port 0 is valid: the Linux tool numbers from 0, and reading 0 as "unset" would make the
    // first port on every Linux machine impossible to release.
    return {"usbip", "detach", "-p", std::to_string(port)};
  }

  std::string describe(const ClientState state) {
    switch (state) {
      case ClientState::Ready:
        return "the USB/IP client is ready";
      case ClientState::NotInstalled:
        return "the USB/IP client is not installed";
      case ClientState::DriverMissing:
        return "the USB/IP driver is missing";
      case ClientState::DriverNotLoaded:
        return "the USB/IP driver is present but not loaded";
      case ClientState::HelperMissing:
        return "the privileged helper is not set up";
    }
    return "the USB/IP client state could not be read";
  }

  bool names_the_client(const std::string_view display_name) {
    constexpr std::string_view needle = "usbip";
    if (display_name.size() < needle.size()) {
      return false;
    }

    for (std::size_t start = 0; start + needle.size() <= display_name.size(); ++start) {
      bool matched = true;
      for (std::size_t i = 0; i < needle.size(); ++i) {
        const auto ch = static_cast<unsigned char>(display_name[start + i]);
        if (std::tolower(ch) != static_cast<unsigned char>(needle[i])) {
          matched = false;
          break;
        }
      }
      if (!matched) {
        continue;
      }

      const auto after = start + needle.size();
      if (after == display_name.size()) {
        return true;
      }
      // The boundary is the whole rule. "USBip 0.9.8.1" ends the word here; "usbipd-win" carries
      // straight on into another product's name.
      if (std::isalnum(static_cast<unsigned char>(display_name[after])) == 0) {
        return true;
      }
    }
    return false;
  }

  ClientAvailability assess_client(const ClientProbe &probe) {
    ClientAvailability result;

    // Order matters. Each check assumes the ones above it passed, so the reason names the FIRST
    // thing missing rather than the last one we happened to look at — which is what makes it
    // something to act on instead of a list to work through.
    if (!probe.client_found) {
      // Not an error. The client ships with ArtLight's installer, so a machine without it is a
      // machine that has not run setup — the same shape as ArtMoon's install-the-service step.
      result.state = ClientState::NotInstalled;
      result.reason = "the USB/IP client is not set up on this PC";
      return result;
    }

    if (!probe.driver_present && !probe.driver_loadable) {
      // Measured on the z13: usbip-utils 2.0 installed and not one device importable, because
      // vhci_hcd was not loaded. The binary on PATH proves nothing, which is why the driver is
      // checked separately rather than inferred from the client being present.
      result.state = ClientState::DriverMissing;
      result.reason = "the USB/IP client is installed but its driver is not available on this PC";
      return result;
    }

    if (!probe.driver_present) {
      // Present-but-unloaded is deliberately its own state. It is a load, not a reinstall, and
      // reporting it as "missing" would send someone to reinstall a driver they already have.
      result.state = ClientState::DriverNotLoaded;
      result.reason = "the USB/IP driver is present but not loaded, so nothing can arrive yet";
      return result;
    }

    // The Linux half. On Windows privilege_required is false and this is skipped, because attach
    // there was proven to work from a limited token — the asymmetry is between the platforms and
    // must not be smoothed over by giving Windows a daemon it does not need.
    if (probe.privilege_required && !probe.helper_present) {
      result.state = ClientState::HelperMissing;
      result.reason = "the USB/IP client is ready, but the helper that can attach a device is not set up";
      return result;
    }

    result.ok = true;
    result.state = ClientState::Ready;
    result.reason.clear();
    return result;
  }
  namespace {
    /// Case-insensitive substring. Used ONLY to recognise failure lines that have actually been
    /// seen on real machines - never to interpret something a machine did not say.
    bool contains_ci(const std::string_view haystack, const std::string_view needle) {
      if (needle.empty() || haystack.size() < needle.size()) {
        return false;
      }
      for (std::size_t start = 0; start + needle.size() <= haystack.size(); ++start) {
        bool matched = true;
        for (std::size_t i = 0; i < needle.size(); ++i) {
          if (to_lower(haystack[start + i]) != to_lower(needle[i])) {
            matched = false;
            break;
          }
        }
        if (matched) {
          return true;
        }
      }
      return false;
    }

    /// The first line carrying `needle`, trimmed, kept as the tool wrote it. This is what goes in
    /// the log: a paraphrase of a tool's complaint is a new claim, and the raw line is the only
    /// thing that can be checked against the machine later.
    std::string line_containing(const std::string_view text, const std::string_view needle) {
      std::size_t start = 0;
      while (start <= text.size()) {
        const auto end = text.find('\n', start);
        const auto line = text.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
        if (contains_ci(line, needle)) {
          return std::string(trim(line));
        }
        if (end == std::string_view::npos) {
          break;
        }
        start = end + 1;
      }
      return {};
    }

    std::string first_meaningful_line(const std::string_view text) {
      std::size_t start = 0;
      while (start <= text.size()) {
        const auto end = text.find('\n', start);
        const auto line = text.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
        const auto trimmed = trim(line);
        if (!trimmed.empty()) {
          return std::string(trimmed);
        }
        if (end == std::string_view::npos) {
          break;
        }
        start = end + 1;
      }
      return {};
    }
  }  // namespace

  AttachResult classify_attach(const int exit_code, const std::string_view out, const std::string_view err) {
    AttachResult result;

    if (exit_code == 0) {
      result.outcome = AttachOutcome::Attached;
      return result;
    }

    // The exporter still counts the device as out. Documented on the exporter, and note the trap
    // recorded there: an EMPTY `Persisted:` table does not clear this - it is the vendor's veto,
    // not stale state, and retrying forever will not shake it loose.
    for (const std::string_view needle : {"already exported", "device busy"}) {
      const auto line = line_containing(err, needle);
      if (!line.empty()) {
        result.outcome = AttachOutcome::DeviceBusy;
        result.detail = line;
        return result;
      }
    }

    // The exporter does not have it to give. Measured on the real exporter, asking for a device it
    // was not sharing. Nothing moved, and the fix is on the exporter side - which is why this is
    // not filed with the generic failures.
    for (const std::string_view needle : {"not found by bus id", "device not found"}) {
      const auto line = line_containing(err, needle);
      if (!line.empty()) {
        result.outcome = AttachOutcome::NotOffered;
        result.detail = line;
        return result;
      }
    }

    // Privilege or policy said no. The Linux form was measured on the z13: an unprivileged attach
    // dies with this and exit 1, because /sys/devices/platform/vhci_hcd.0/attach is a root-only
    // write. On Linux this is also the shape a missing helper or a declined polkit prompt takes.
    for (const std::string_view needle :
         {"import device", "permission denied", "operation not permitted", "access denied"}) {
      const auto line = line_containing(err, needle);
      if (!line.empty()) {
        result.outcome = AttachOutcome::Refused;
        result.detail = line;
        return result;
      }
    }

    // It ran, it did not work, and this build will not pretend to know why. Keep what it said.
    result.outcome = AttachOutcome::Failed;
    result.detail = first_meaningful_line(err);
    if (result.detail.empty()) {
      result.detail = first_meaningful_line(out);
    }
    return result;
  }

  std::string describe(const AttachOutcome outcome) {
    switch (outcome) {
      case AttachOutcome::Attached:
        return "the device attached";
      case AttachOutcome::DeviceBusy:
        return "the exporter still has this device out - nothing moved";
      case AttachOutcome::NotOffered:
        return "the exporter is not offering that device - nothing moved";
      case AttachOutcome::Refused:
        return "the attach was refused - nothing moved";
      case AttachOutcome::Failed:
        return "the attach failed";
    }
    throw std::runtime_error("unhandled AttachOutcome");
  }

}  // namespace input::usbip
