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
}  // namespace usbip_fixtures
