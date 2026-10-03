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

  /// One device this machine currently holds.
  struct Attached {
    /// Remote busid, e.g. "9-1". Validated, so it is safe in an argument vector.
    ///
    /// Legitimately EMPTY on Linux: when the tool cannot read a port's record it prints the
    /// port and no remote identity at all. That is not a parse failure, and it does not stop
    /// us, because detach takes a port.
    std::string busid;
    /// Hub port number. usbip-win2 numbers from 1 and asserts non-zero; the Linux tool numbers
    /// from 0. So 0 is a valid value here and must not be treated as "unset".
    int port = 0;
    std::string exporter;  ///< Host from the "usbip://HOST:SERVICE/BUSID" row.
    std::string service;   ///< That row's TCP port, e.g. "3240".
    std::string speed;     ///< "High Speed(480Mbps)", verbatim. Empty when the row had none.
    std::string product;   ///< The description row, verbatim.
    std::string serial;    ///< Only present when the attach carried one. Usually empty.
    std::string mode;      ///< "zero-copy" or "low-latency". Empty when the row had none.
  };

  /**
   * @brief What a `port` call actually established.
   *
   * The same four states as ListOutcome, for the same reason, plus one asymmetry that is easy
   * to get wrong in the dangerous direction:
   *
   *   usbip-win2 prints its banner ONLY when it finds a device. So with nothing attached it
   *   prints ZERO BYTES and exits 0 — byte-identical to a call that never reached the driver.
   *   The Linux tool prints the banner unconditionally, so its empty case is distinguishable.
   *
   * That means on Windows this parse alone cannot tell "holds nothing" from "could not ask";
   * the exit code separates them. It is read as NoOutput here and reclassified by the caller,
   * exactly as parse_device_list is, because a machine wrongly reported as holding nothing is
   * a device that never gets given back.
   */
  enum class PortOutcome {
    Devices,          ///< Read successfully; this machine holds devices.
    NothingAttached,  ///< Read successfully; this machine holds none.
    NoOutput,         ///< Zero bytes. Exit 0 means NothingAttached; non-zero means we could not ask.
    Unreadable,       ///< Bytes came back and this build could not place them.
  };

  struct AttachedList {
    PortOutcome outcome = PortOutcome::NoOutput;
    std::vector<Attached> devices;
    /// For Unreadable: the tool's own error lines if it named any, else the row that defeated us.
    std::string detail;
  };

  /**
   * @brief Read what this machine currently holds, in either tool's shape.
   *
   * Two shapes, and they disagree in two places that matter:
   *
   *   Windows  "Port 01: device in use at High Speed(480Mbps)"     9-space description indent
   *            "           -> usbip://192.168.50.35:3240/9-1"       11-space detail indent
   *
   *   Linux    "Port 00: <Port in Use> at High Speed(480Mbps)"     7-space description indent
   *            "       9-1 -> usbip://192.168.50.35:3240/9-1"       the busid leads the row
   *
   * Both put the port number on the "Port NN:" row and the remote busid as the last path
   * segment of the "usbip://" URL, so this reads those two anchors rather than counting
   * columns — the layouts differ and the columns are not the contract.
   *
   * A device whose remote record could not be read prints no URL row at all (a real Linux
   * state, "unknown host, remote port and remote busid"). It is kept, with its port and an
   * empty busid, because the port is what releases it.
   *
   * @param output Everything the tool wrote to stdout, with stderr appended by the caller.
   * @return The outcome, the devices if any, and for Unreadable what defeated us.
   */
  AttachedList parse_attached(std::string_view output);

  /// @brief Render a port outcome as words. Never empty.
  std::string describe(PortOutcome outcome);
}  // namespace input::usbip
