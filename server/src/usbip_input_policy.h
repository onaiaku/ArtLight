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

#include <string_view>

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
}
