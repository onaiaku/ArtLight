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
  // THE PROVENANCE IS DIFFERENT HERE AND IT MATTERS. The three above are live captures from
  // machines. These are not. They are derived from the format string of the program that
  // prints them, at the exact version in use — which is real evidence (the emitter's own code,
  // not a memory of its output) but it is NOT a machine's bytes, and none of these may be
  // called a capture:
  //
  //   Windows  vadimgrn/usbip-win2 tag v.0.9.8.1, userspace/usbip/port.cpp + strings.cpp
  //            Confirmed to be the importer's installed version: usbip.exe --version = 0.9.8.1
  //   Linux    linux, tools/usb/usbip, src/usbip_port.c + libsrc/vhci_driver.c
  //
  // Confirming these live means attaching a device, and attaching MOVES it off the machine it
  // is plugged into. That is far too much to spend on confirming a format string, so it was
  // not spent, and the gap is written down here rather than quietly closed with a guess.
  //
  // Written as explicit lines rather than one raw literal on purpose: the leading whitespace
  // IS the format under test (9 spaces before the Windows description row, 11 before its
  // detail rows, 7 on Linux, and the Linux busid right-aligned in 10), and a raw literal hides
  // exactly the thing a reader needs to count.

  /// usbip-win2, holding nothing. LIVE: `usbip.exe port` on the gaming PC, 2026-10-03, USBip
  /// 0.9.8.1, nothing attached. Zero bytes and exit 0 — and that pair is the whole point. The
  /// banner is printed only when a device is found, so this is byte-identical to a call that
  /// never reached the driver. Only the exit code tells them apart.
  inline constexpr std::string_view kPortWin2NothingAttached = "";

  /// usbip-win2, one device attached. Note "serial:" with nothing after it but a space: an
  /// attach that carried no --serial prints the key and stops, which is the normal case.
  inline const std::string_view kPortWin2OneDevice =
    "Imported USB devices\n"
    "====================\n"
    "Port 01: device in use at High Speed(480Mbps)\n"
    "         Razer USA, Ltd : RC30-0305 Gaming Mouse Dongle [Viper Ultimate (Wireless)]\n"
    "           -> usbip://192.168.50.35:3240/9-1\n"
    "           -> remote bus/dev: 001/002\n"
    "           -> serial: \n"
    "           -> mode: zero-copy\n";

  /// usbip-win2, two devices — the map a detach sweep has to walk, and the reason the port is
  /// an integer rather than the busid.
  inline const std::string_view kPortWin2TwoDevices =
    "Imported USB devices\n"
    "====================\n"
    "Port 01: device in use at High Speed(480Mbps)\n"
    "         Razer USA, Ltd : RC30-0305 Gaming Mouse Dongle [Viper Ultimate (Wireless)]\n"
    "           -> usbip://192.168.50.35:3240/9-1\n"
    "           -> remote bus/dev: 001/002\n"
    "           -> serial: \n"
    "           -> mode: zero-copy\n"
    "Port 02: device in use at Full Speed(12Mbps)\n"
    "         Razer USA, Ltd : unknown product\n"
    "           -> usbip://192.168.50.35:3240/9-2\n"
    "           -> remote bus/dev: 001/003\n"
    "           -> serial: \n"
    "           -> mode: low-latency\n";

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
}  // namespace usbip_fixtures
