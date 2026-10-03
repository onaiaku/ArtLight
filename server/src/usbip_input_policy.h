/**
 * @file src/usbip_input_policy.h
 * @brief Portable policy for the USB/IP importer: busid validation, exporter output parsing,
 *        and the attach/detach plan.
 *
 * Nothing in this header touches the operating system, spawns a process, or knows which
 * platform it is running on. That is deliberate. The parse is the part that fails quietly —
 * a device that never arrives, behind a log line that says it did — so it is the part that
 * has to be testable without a driver, a network, or a second machine.
 *
 * Terminology is not cosmetic here; see ArtMoon/docs/usb-ip-input-passthrough.md. The machine
 * a device is plugged into is the EXPORTER, the machine it arrives on is the IMPORTER. Never
 * write "host" or "client" bare: usbip names its roles after the device side and streaming
 * names them after the video, and on this pair of products the two are inverted.
 */
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace input::usbip {
  /**
   * @brief Whether a value is a busid of the shape usbip emits, e.g. "3-10" or "9-3".
   *
   * Two properties matter beyond tidiness. The first character must be a digit, so a value
   * can never be read as an option by usbip ("-p", "--help"). And nothing outside [0-9-] may
   * appear, so a busid arriving from the exporter can never carry whitespace, a path, a
   * newline, or a shell metacharacter into an argument vector.
   *
   * The digits are deliberately not range-checked and leading zeros are not rejected:
   * refusing a device the exporter really is offering is a worse failure than handing a
   * malformed busid to usbip, which will simply report no such device.
   *
   * @param busid Candidate, as received from the exporter.
   * @return true for exactly one run of digits, a single '-', and another run of digits.
   */
  bool is_valid_busid(std::string_view busid);

  /// One device an exporter is offering.
  struct Device {
    std::string busid;    ///< "9-1". Guaranteed to have passed is_valid_busid.
    std::string vendor;   ///< "Razer USA, Ltd". Empty when the row carried no vendor.
    std::string product;  ///< "unknown product" is a normal value here, not a parse failure.
    std::string vid_pid;  ///< "1532:007b", lowercased. Empty when the row carried none.
    std::vector<std::string> interfaces;  ///< Per-interface detail rows, verbatim, colon stripped.
  };

  /**
   * @brief What a list call actually established.
   *
   * Four states rather than "devices or not", because two of them get collapsed by accident
   * and the collapse is silent. "The exporter is offering nothing" is a fact about the
   * exporter. "We could not read the reply" is a fact about us. A user told the first when the
   * second is true will go and check the wrong machine.
   */
  enum class ListOutcome {
    Devices,        ///< Read successfully; one or more devices are offered.
    NothingOffered, ///< Read successfully; the exporter is genuinely offering nothing.
    NoOutput,       ///< Nothing came back at all. The caller's own timeout decides what that means.
    Unreadable,     ///< Bytes came back and this build could not place them.
  };

  struct DeviceList {
    ListOutcome outcome = ListOutcome::NoOutput;
    std::vector<Device> devices;
    /// For Unreadable: the first line that could not be placed, or the tool's own error line.
    /// Empty for every other outcome.
    std::string detail;
  };

  /**
   * @brief Read one device list, in either tool's shape.
   *
   * Two shapes have to parse, and they are genuinely different:
   *
   *   Linux   " - busid 3-10 (8087:0033)" then an indented "Intel Corp. : AX211 Bluetooth"
   *   Windows "    9-1    : Razer USA, Ltd : unknown product (1532:028d)" then indented ":" rows
   *
   * The busid decides which: on the Windows row it sits before the separator, on the Linux one
   * after the word "busid". Continuation rows carrying only interface detail are attached to
   * the device above them and are never mistaken for new devices.
   *
   * An empty document is NoOutput rather than NothingOffered, on purpose. This function cannot
   * see the exit code or the clock, and from output alone "the exporter is asleep" and "the
   * exporter is offering nothing" are indistinguishable — the asleep case produces the same
   * zero bytes as the clean-empty case, only after hanging indefinitely. The caller combines
   * this with its own timeout and exit status to choose. See
   * tests/fixtures/captured-2026-10-03-network-probe.md.
   *
   * @param output Everything the tool wrote to stdout, with stderr appended by the caller.
   * @return The outcome, the devices if any, and for Unreadable the line that defeated us.
   */
  DeviceList parse_device_list(std::string_view output);

  /// @brief Render an outcome as words, for a log line or a status row. Never empty.
  std::string describe(ListOutcome outcome);
}  // namespace input::usbip
