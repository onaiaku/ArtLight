/**
 * @file tests/support/usbip_captures.h
 * @brief The exact bytes the usbip tools emitted, captured from real machines on 2026-10-03.
 *
 * These are not hand-written samples. They were de-quoted from the probe captures in
 * tests/fixtures/ — the quoting the probe added was stripped and nothing else was touched — so
 * a parser test can be checked against what the tools really printed rather than against what
 * somebody remembered them printing. The remote list in particular exists because the first
 * draft of this work assumed a Linux-shaped row for a Windows exporter and was wrong.
 *
 * Kept as compiled-in strings, not files, so the tests are hermetic: no working-directory
 * dependency, no path to get wrong in CI, and no way for a fixture to go missing quietly.
 *
 * Provenance:
 *   kListRemoteWin2  usbip.exe list -r 192.168.50.35  (USBip 0.9.8.1, gaming PC -> mini PC)
 *                    tests/fixtures/captured-importer-win2.txt, section "REMOTE LIST"
 *   kListLocalLinux  usbip list -l                    (usbip-utils 2.0, z13)
 *                    tests/fixtures/captured-2026-10-03.txt, section B
 *   kPortErrorLinux  usbip port                       (usbip-utils 2.0, z13, no vhci_hcd)
 *                    tests/fixtures/captured-2026-10-03.txt, section C
 *   kListRemoteLinux usbip list -r 192.168.50.35      (usbip-utils 2.0, z13 -> mini PC)
 *                    captured 2026-10-03 evening, one device shared
 *   kPortLinuxEmpty  usbip port                       (usbip-utils 2.0, z13, vhci_hcd, nothing held)
 */
#pragma once

#include <string_view>

namespace usbip_fixtures {
  /// An exporter that IS offering devices, as a Windows importer sees them.
  inline constexpr std::string_view kListRemoteWin2 = R"USBIPFIXTURE(Exportable USB devices
======================
    9-1    : Razer USA, Ltd : RC30-0305 Gaming Mouse Dongle [Viper Ultimate (Wireless)] (1532:007b)
           : USB\VID_1532&PID_007B\7&3A2774D3&0&1
           : (Defined at Interface level) (00/00/00)
           :  0 - Human Interface Device/Boot Interface Subclass/Mouse (03/01/02)
           :  1 - Human Interface Device/No Subclass/Keyboard (03/00/01)
           :  2 - Human Interface Device/No Subclass/Keyboard (03/00/01)

    9-2    : Razer USA, Ltd : unknown product (1532:028d)
           : USB\VID_1532&PID_028D\7&3A2774D3&0&2
           : (Defined at Interface level) (00/00/00)
           :  0 - Human Interface Device/Boot Interface Subclass/Keyboard (03/01/01)
           :  1 - Human Interface Device/No Subclass/Keyboard (03/00/01)
           :  2 - Human Interface Device/Boot Interface Subclass/Mouse (03/01/02)
           :  3 - Human Interface Device/No Subclass/None (03/00/00)
           :  4 - Human Interface Device/No Subclass/Keyboard (03/00/01)
)USBIPFIXTURE";

  /// The exporter's own local list, in the Linux tool's shape — deliberately different from
  /// the Windows remote list above, because both have to parse.
  inline constexpr std::string_view kListLocalLinux = R"USBIPFIXTURE( - busid 3-10 (8087:0033)
   Intel Corp. : AX211 Bluetooth (8087:0033)

 - busid 3-6 (0b05:1a30)
   ASUSTek Computer, Inc. : unknown product (0b05:1a30)

 - busid 3-7 (04f3:0c6e)
   Elan Microelectronics Corp. : unknown product (04f3:0c6e)

 - busid 3-8 (13d3:5492)
   IMC Networks : unknown product (13d3:5492)

 - busid 3-9 (0b05:18c6)
   ASUSTek Computer, Inc. : unknown product (0b05:18c6)

)USBIPFIXTURE";

  /// A tool that failed. There are no devices in this output, and reporting it as "nothing is
  /// being offered" would be a lie with a straight face.
  inline constexpr std::string_view kPortErrorLinux = R"USBIPFIXTURE(libusbip: error: udev_device_new_from_subsystem_sysname failed
usbip: error: open vhci_driver (is vhci_hcd loaded?)
usbip: error: list imported devices
)USBIPFIXTURE";

  // ── the "port" rows: what this machine is currently holding ─────────────────────────────
  //
  // Windows port fixtures below are REAL CAPTURES taken from the gaming PC on 2026-10-03 with
  // usbip-win2 0.9.8.1 (verified on the box: `usbip.exe --version`) against the mini PC as
  // exporter, by attaching the Razer Viper Ultimate and reading `usbip port`. Byte-for-byte, CRLF
  // included — see captured-2026-10-03-port-win2-*.txt.
  //
  // The live capture corrected two things a format-derived version of these had wrong, and both
  // are the sort of thing only real bytes catch:
  //   * the description row ends with the device's VID:PID, " (1532:007b)", which the derived
  //     version did not have at all;
  //   * the speed was "Full Speed(12Mbps)", not the "High Speed(480Mbps)" that had been assumed
  //     for a wireless mouse dongle.
  //
  // The Linux port rows are still DERIVED, from linux tools/usb/usbip src/usbip_port.c and
  // libsrc/vhci_driver.c, because the z13 was unreachable when the capture was taken. That is the
  // emitter's own code, not a memory of its output — but it is not a machine's bytes, and the
  // Linux fixtures are a debt against a real capture rather than a substitute for one.
  //
  // Written as explicit lines rather than one raw literal on purpose: the leading whitespace
  // IS the format under test (9 spaces before the Windows description row, 11 before its
  // detail rows, 7 on Linux, and the Linux busid right-aligned in 10), and a raw literal hides
  // exactly the thing a reader needs to count.

  // What the CLI itself does, measured on the gaming PC, because the executor has to tell these
  // apart and none of them was known before:
  //   usbip port      holding nothing     0 bytes, exit 0        <- ambiguous by itself
  //   usbip port      holding a device    the table, exit 0
  //   usbip attach    success             "successfully attached to port N", exit 0
  //   usbip attach -t success             just "N", exit 0
  //   usbip attach    cannot connect      "error: A connection attempt failed ...", exit 1,
  //                                       after ~22 SECONDS
  //   usbip detach    success             "port N is successfully detached", exit 0
  //   usbip detach    nothing there       "error: The device is not connected.", exit 1
  // That 22s is the number the attach timeout has to clear: a 5s timeout would abort a call that
  // was going to fail on its own schedule anyway. `--once` did NOT shorten it (21s with, 22s
  // without) — it prevents repeated attempts, it does not make the connect fail faster.

  /// usbip-win2, holding nothing. REAL: 0 bytes, exit 0, and it is byte-identical to a call that
  /// never reached the driver — the exit code is the only thing that separates them.
  inline constexpr std::string_view kPortWin2NothingAttached = "";

  /// usbip-win2, one device attached. REAL capture. Note "serial:" with nothing after it but a
  /// space: an attach that carried no --serial prints the key and stops, which is the normal case.
  inline const std::string_view kPortWin2OneDevice =
    "Imported USB devices\r\n"
    "====================\r\n"
    "Port 01: device in use at Full Speed(12Mbps)\r\n"
    "         Razer USA, Ltd : RC30-0305 Gaming Mouse Dongle [Viper Ultimate (Wireless)] (1532:007b)\r\n"
    "           -> usbip://192.168.50.35:3240/9-1\r\n"
    "           -> remote bus/dev: 009/001\r\n"
    "           -> serial: \r\n"
    "           -> mode: zero-copy\r\n";

  /// usbip-win2, two devices — the map a detach sweep has to walk, and the reason the port is an
  /// integer rather than the busid. NOT a capture: only one device was attached, so the second
  /// record is built to the shape the real one above proved, VID:PID suffix included.
  inline const std::string_view kPortWin2TwoDevices =
    "Imported USB devices\r\n"
    "====================\r\n"
    "Port 01: device in use at Full Speed(12Mbps)\r\n"
    "         Razer USA, Ltd : RC30-0305 Gaming Mouse Dongle [Viper Ultimate (Wireless)] (1532:007b)\r\n"
    "           -> usbip://192.168.50.35:3240/9-1\r\n"
    "           -> remote bus/dev: 009/001\r\n"
    "           -> serial: \r\n"
    "           -> mode: zero-copy\r\n"
    "Port 02: device in use at Full Speed(12Mbps)\r\n"
    "         Razer USA, Ltd : unknown product (1532:028d)\r\n"
    "           -> usbip://192.168.50.35:3240/9-2\r\n"
    "           -> remote bus/dev: 009/002\r\n"
    "           -> serial: \r\n"
    "           -> mode: low-latency\r\n";

  /// The Linux tool, holding nothing. It prints the banner UNCONDITIONALLY — before it looks at
  /// any port — so unlike the Windows case this empty state is distinguishable from silence.
  inline const std::string_view kPortLinuxNothingAttached =
    "Imported USB devices\n"
    "====================\n";

  /// The Linux tool, one device. Two differences from Windows in one sample: the status is
  /// bracketed, and the busid LEADS the URL row (right-aligned in 10) instead of being absent.
  /// The port is 00, which is valid here and must never be read as "unset".
  inline const std::string_view kPortLinuxOneDevice =
    "Imported USB devices\n"
    "====================\n"
    "Port 00: <Port in Use> at High Speed(480Mbps)\n"
    "       Razer USA, Ltd : RC30-0305 Gaming Mouse Dongle [Viper Ultimate (Wireless)]\n"
    "       9-1 -> usbip://192.168.50.35:3240/9-1\n"
    "           -> remote bus/dev 001/002\n";

  /// The Linux tool when it cannot read a port's record. There is no URL row at all, so there
  /// is no remote identity — but the port is real and the port is what releases the device.
  inline const std::string_view kPortLinuxUnknownRemote =
    "Imported USB devices\n"
    "====================\n"
    "Port 01: <Port in Use> at Full Speed(12Mbps)\n"
    "       unknown product\n"
    "           -> unknown host, remote port and remote busid\n";

  /// The Linux tool's REMOTE listing, from a Linux importer reaching a live exporter.
  ///
  /// A different code path from `usbip-win2`'s remote list, and it shows: the Linux tool groups
  /// its output BY EXPORTER and opens each group with a line naming the machine, " - <address>".
  /// That line is the whole reason this fixture exists — the parser pinned against the Windows
  /// bytes had no rule for a dash that was not followed by "busid", so it rejected the entire
  /// reply and a Linux importer could attach nothing at all. The device rows differ too: the
  /// busid is followed immediately by the separator rather than being padded out to a column,
  /// and the interface rows are spaced ("Human Interface Device / Boot Interface Subclass / ...")
  /// where the Windows tool runs the words together.
  ///
  /// Captured 2026-10-03 from the z13 (usbip-utils 2.0) against the mini PC, after the exporter
  /// had one device genuinely shared.
  inline constexpr std::string_view kListRemoteLinux = R"USBIPFIXTURE(Exportable USB devices
======================
 - 192.168.50.35
        9-1: Razer USA, Ltd : RC30-0305 Gaming Mouse Dongle [Viper Ultimate (Wireless)] (1532:007b)
           : USB\VID_1532&PID_007B\7&3A2774D3&0&1
           : (Defined at Interface level) (00/00/00)
           :  0 - Human Interface Device / Boot Interface Subclass / Mouse (03/01/02)
           :  1 - Human Interface Device / No Subclass / Keyboard (03/00/01)
           :  2 - Human Interface Device / No Subclass / Keyboard (03/00/01)

)USBIPFIXTURE";

  /// The same call against an exporter that is offering NOTHING. The per-exporter header is
  /// still printed, which is what makes it worth its own fixture: without a rule for it this
  /// reads as "we could not understand the reply" instead of "there is nothing to take", and
  /// the user is sent to check a machine that answered perfectly.
  inline constexpr std::string_view kListRemoteLinuxNothing = R"USBIPFIXTURE(Exportable USB devices
======================
 - 192.168.50.35
)USBIPFIXTURE";

  /// The Linux tool's port table with nothing attached. NOT derived — captured from the z13
  /// with `vhci_hcd` loaded and no device held. Two header rows and then nothing, so the
  /// "this machine holds nothing" answer has to be reached from a document that is not empty.
  inline constexpr std::string_view kPortLinuxEmpty = R"USBIPFIXTURE(Imported USB devices
====================
)USBIPFIXTURE";
}  // namespace usbip_fixtures
