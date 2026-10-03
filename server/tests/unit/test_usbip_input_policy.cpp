/**
 * @file tests/unit/test_usbip_input_policy.cpp
 * @brief Tests for the portable USB/IP importer policies.
 *
 * The inputs are the bytes the tools actually emitted, kept in ../support/usbip_captures.h and
 * derived from the captures in tests/fixtures/. A parser for another program's output is only
 * as good as its fixtures: the first draft of this work assumed a Linux-shaped row for a
 * Windows exporter, and the capture is what corrected it.
 */
#include "../tests_common.h"

#include <string>
#include <string_view>
#include <vector>

#include <src/usbip_input_policy.h>

#include "../support/usbip_captures.h"

namespace {
  using input::usbip::DeviceList;
  using input::usbip::ListOutcome;

  // Not invented examples. Every busid below was read off real hardware on 2026-10-03 — the
  // z13 (Linux exporter), the mini PC (Windows exporter) and the gaming PC (Windows importer).
  constexpr const char *kBusidsSeenOnRealHardware[] = {
    "3-10", "3-6", "3-7", "3-8", "3-9",        // z13
    "9-1", "9-2", "9-3", "10-2", "1-5", "5-3", // mini PC
  };

  DeviceList parse(const std::string_view text) {
    return input::usbip::parse_device_list(text);
  }
}  // namespace

// ── the busid rule ───────────────────────────────────────────────────────────────────────

TEST(UsbipBusid, AcceptsEveryBusidSeenOnRealHardware) {
  for (const char *busid : kBusidsSeenOnRealHardware) {
    EXPECT_TRUE(input::usbip::is_valid_busid(busid)) << busid;
  }
}

TEST(UsbipBusid, AcceptsTheSmallestPossibleShape) {
  EXPECT_TRUE(input::usbip::is_valid_busid("1-1"));
  EXPECT_TRUE(input::usbip::is_valid_busid("0-0"));
}

TEST(UsbipBusid, RejectsAnEmptyOrOversizedValue) {
  EXPECT_FALSE(input::usbip::is_valid_busid(""));
  EXPECT_FALSE(input::usbip::is_valid_busid("12345678901234567"));
}

// The security property. A busid beginning with '-' is an option to usbip, not a device.
TEST(UsbipBusid, RejectsAnythingThatCouldBeReadAsAnOption) {
  EXPECT_FALSE(input::usbip::is_valid_busid("-p"));
  EXPECT_FALSE(input::usbip::is_valid_busid("-a"));
  EXPECT_FALSE(input::usbip::is_valid_busid("--help"));
  EXPECT_FALSE(input::usbip::is_valid_busid("--version"));
  EXPECT_FALSE(input::usbip::is_valid_busid("-10"));
  EXPECT_FALSE(input::usbip::is_valid_busid("-"));
}

TEST(UsbipBusid, RejectsAMalformedSeparator) {
  EXPECT_FALSE(input::usbip::is_valid_busid("3-"));
  EXPECT_FALSE(input::usbip::is_valid_busid("3--10"));
  EXPECT_FALSE(input::usbip::is_valid_busid("3-1-0"));
  EXPECT_FALSE(input::usbip::is_valid_busid("3"));
  EXPECT_FALSE(input::usbip::is_valid_busid("310"));
}

// Everything here would be dangerous downstream, and none of it is a busid.
TEST(UsbipBusid, RejectsInjectionAndGarbage) {
  EXPECT_FALSE(input::usbip::is_valid_busid("3-10; rm -rf /"));
  EXPECT_FALSE(input::usbip::is_valid_busid("3-10|sh"));
  EXPECT_FALSE(input::usbip::is_valid_busid("3-10`id`"));
  EXPECT_FALSE(input::usbip::is_valid_busid("3-10$(id)"));
  EXPECT_FALSE(input::usbip::is_valid_busid("../../../etc/passwd"));
  EXPECT_FALSE(input::usbip::is_valid_busid("3-10 extra"));
  EXPECT_FALSE(input::usbip::is_valid_busid(" 3-10"));
  EXPECT_FALSE(input::usbip::is_valid_busid("3-10 "));
  EXPECT_FALSE(input::usbip::is_valid_busid("3\t10"));
  EXPECT_FALSE(input::usbip::is_valid_busid("3-10\n"));
  EXPECT_FALSE(input::usbip::is_valid_busid("3-10\x01"));
  EXPECT_FALSE(input::usbip::is_valid_busid("0x3-10"));
  EXPECT_FALSE(input::usbip::is_valid_busid("3-1a"));
  EXPECT_FALSE(input::usbip::is_valid_busid("a-1"));
  EXPECT_FALSE(input::usbip::is_valid_busid("3-1.0"));
  EXPECT_FALSE(input::usbip::is_valid_busid("3-10,3-6"));
}

// ── the Windows remote list, as captured ─────────────────────────────────────────────────

TEST(UsbipListParse, ReadsTheCapturedWindowsRemoteList) {
  const DeviceList list = parse(usbip_fixtures::kListRemoteWin2);

  EXPECT_EQ(list.outcome, ListOutcome::Devices);
  if (list.devices.size() != 2) {
    EXPECT_EQ(list.devices.size(), 2u);  // fail here rather than index out of range below
    return;
  }

  EXPECT_EQ(list.devices[0].busid, "9-1");
  EXPECT_EQ(list.devices[0].vendor, "Razer USA, Ltd");
  EXPECT_EQ(list.devices[0].vid_pid, "1532:007b");
  EXPECT_EQ(list.devices[0].product, "RC30-0305 Gaming Mouse Dongle [Viper Ultimate (Wireless)]");

  EXPECT_EQ(list.devices[1].busid, "9-2");
  EXPECT_EQ(list.devices[1].vendor, "Razer USA, Ltd");
  EXPECT_EQ(list.devices[1].vid_pid, "1532:028d");
  // The tool prints "unknown product" when it has no name for the device. That is a value,
  // not a parse failure, and treating it as one would hide a device that really is offered.
  EXPECT_EQ(list.devices[1].product, "unknown product");
}

TEST(UsbipListParse, ContinuationRowsNeverBecomeDevices) {
  // The trap in this format: every interface row also starts with whitespace and a colon.
  const DeviceList list = parse(usbip_fixtures::kListRemoteWin2);
  EXPECT_EQ(list.devices.size(), 2u);
  if (list.devices.size() != 2) {
    return;
  }
  // Counted from the capture itself, not from what the layout looked like at a glance:
  // 9-1 carries 5 detail rows and 9-2 carries 7.
  EXPECT_EQ(list.devices[0].interfaces.size(), 5u);
  EXPECT_EQ(list.devices[1].interfaces.size(), 7u);
  EXPECT_EQ(list.devices[0].interfaces[0], "USB\\VID_1532&PID_007B\\7&3A2774D3&0&1");
}

TEST(UsbipListParse, TheInterfaceCountIsNotBorrowedFromTheWrongDevice) {
  // Answering with the right number of devices but the wrong rows attached is the kind of
  // wrong that looks fine on a screenshot.
  const DeviceList list = parse(usbip_fixtures::kListRemoteWin2);
  if (list.devices.size() != 2) {
    EXPECT_EQ(list.devices.size(), 2u);
    return;
  }
  EXPECT_TRUE(list.devices[0].interfaces[0].find("VID_1532&PID_007B") != std::string::npos)
      << list.devices[0].interfaces[0];
  EXPECT_TRUE(list.devices[1].interfaces[0].find("VID_1532&PID_028D") != std::string::npos)
      << list.devices[1].interfaces[0];
}

TEST(UsbipListParse, ParsesTheSameWayWithWindowsLineEndings) {
  // Not the exotic case. This parser runs on Windows, where CRLF is what a captured stream
  // actually contains, and a stray '\r' left on the end of every row would corrupt every name.
  std::string crlf;
  const std::string source(usbip_fixtures::kListRemoteWin2);
  for (const char c : source) {
    if (c == '\n') {
      crlf += '\r';
    }
    crlf += c;
  }

  const DeviceList list = parse(crlf);
  EXPECT_EQ(list.outcome, ListOutcome::Devices);
  EXPECT_EQ(list.devices.size(), 2u);
  if (list.devices.size() != 2) {
    return;
  }
  EXPECT_EQ(list.devices[1].busid, "9-2");
  EXPECT_EQ(list.devices[1].product, "unknown product");
  EXPECT_EQ(list.devices[0].vid_pid, "1532:007b");
}

// ── the Linux local list, as captured ────────────────────────────────────────────────────

TEST(UsbipListParse, ReadsTheCapturedLinuxLocalList) {
  const DeviceList list = parse(usbip_fixtures::kListLocalLinux);

  EXPECT_EQ(list.outcome, ListOutcome::Devices);
  EXPECT_EQ(list.devices.size(), 5u);
  if (list.devices.size() != 5) {
    return;
  }

  EXPECT_EQ(list.devices[0].busid, "3-10");
  EXPECT_EQ(list.devices[0].vendor, "Intel Corp.");
  EXPECT_EQ(list.devices[0].product, "AX211 Bluetooth");
  EXPECT_EQ(list.devices[0].vid_pid, "8087:0033");
  EXPECT_EQ(list.devices[4].busid, "3-9");
  EXPECT_EQ(list.devices[4].vendor, "ASUSTek Computer, Inc.");
}

TEST(UsbipListParse, DoesNotMistakeALinuxDescriptionRowForAnInterface) {
  // In this shape the vendor and product arrive on the row AFTER the busid, indented, with a
  // colon in it — structurally identical to a Windows interface row. Reading it as one would
  // silently strip the name off every device the exporter offers.
  const DeviceList list = parse(usbip_fixtures::kListLocalLinux);
  if (list.devices.size() != 5) {
    EXPECT_EQ(list.devices.size(), 5u);
    return;
  }
  EXPECT_EQ(list.devices[0].interfaces.size(), 0u);
  EXPECT_EQ(list.devices[1].vendor, "ASUSTek Computer, Inc.");
  EXPECT_EQ(list.devices[1].product, "unknown product");
}

// ── the states that must never be confused with each other ───────────────────────────────

TEST(UsbipListParse, AToolErrorIsNotAnEmptyList) {
  // THE test in this file. This capture contains no devices. Reporting it as "the exporter is
  // offering nothing" would send someone to go and check a machine that is fine, when the
  // fault is on the machine they are sitting at.
  const DeviceList list = parse(usbip_fixtures::kPortErrorLinux);

  EXPECT_EQ(list.outcome, ListOutcome::Unreadable);
  EXPECT_EQ(list.devices.size(), 0u);
  // And the line worth showing is the second one, not the preamble.
  EXPECT_TRUE(list.detail.find("vhci_hcd") != std::string::npos) << list.detail;
}

TEST(UsbipListParse, NoOutputIsNotNothingOfferedEither) {
  // "The exporter is asleep" produces the same zero bytes as a clean empty answer, only after
  // hanging forever — the timeout and the exit code are what tell them apart, and neither is
  // visible from here. So this must not claim to know.
  EXPECT_EQ(parse("").outcome, ListOutcome::NoOutput);
  EXPECT_EQ(parse("").devices.size(), 0u);
  EXPECT_EQ(parse("   \n\n  \n").outcome, ListOutcome::NoOutput);
}

TEST(UsbipListParse, AHeaderWithNoRowsIsNothingOffered) {
  // The one case where an empty result IS a fact about the exporter: the tool told us it
  // understood the question.
  EXPECT_EQ(parse("Exportable USB devices\n======================\n").outcome,
            ListOutcome::NothingOffered);
}

TEST(UsbipListParse, ABareUnderlineStillCountsAsTheBanner) {
  // The banner is two rows and only one of them carries the words. Recognising the second is
  // what keeps "nothing offered" from depending on a single line of someone else's formatting.
  EXPECT_EQ(parse("======================\n").outcome, ListOutcome::NothingOffered);
}

TEST(UsbipListParse, AnUnrecognisedShapeIsUnreadableRatherThanEmpty) {
  const DeviceList list = parse("something went sideways\n");
  EXPECT_EQ(list.outcome, ListOutcome::Unreadable);
  EXPECT_EQ(list.detail, "something went sideways");
}

TEST(UsbipListParse, ARowWhoseBusidIsNotABusidIsRefused) {
  // A row that looks like a device but whose first token is an option is not a device.
  const DeviceList list = parse("   -a    : Razer USA, Ltd : unknown product (1532:028d)\n");
  EXPECT_EQ(list.outcome, ListOutcome::Unreadable);
  EXPECT_EQ(list.devices.size(), 0u);
}

TEST(UsbipListParse, ADetailRowBeforeAnyDeviceIsRefused) {
  // Hanging detail with nothing to hang off is a shape we do not know, not a device list.
  const DeviceList list = parse("           : USB\\VID_1532&PID_007B\\7&3A2774D3&0&1\n");
  EXPECT_EQ(list.outcome, ListOutcome::Unreadable);
  EXPECT_EQ(list.devices.size(), 0u);
}

TEST(UsbipListParse, EveryOutcomeHasWords) {
  EXPECT_FALSE(input::usbip::describe(ListOutcome::Devices).empty());
  EXPECT_FALSE(input::usbip::describe(ListOutcome::NothingOffered).empty());
  EXPECT_FALSE(input::usbip::describe(ListOutcome::NoOutput).empty());
  EXPECT_FALSE(input::usbip::describe(ListOutcome::Unreadable).empty());
}
