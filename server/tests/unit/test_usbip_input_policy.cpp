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

// ── what this machine is currently holding ───────────────────────────────────────────────
//
// The port is what detach is addressed by, so this parse decides whether a device can ever be
// given back. The fixtures for these are derived from the printers' own format strings rather
// than captured from a machine — that provenance is stated at the fixtures, and the reason it
// is not a capture is that obtaining one means attaching a device, which moves it.

namespace {
  using input::usbip::AttachedList;
  using input::usbip::PortOutcome;

  AttachedList ports(const std::string_view text) {
    return input::usbip::parse_attached(text);
  }
}  // namespace

TEST(UsbipPortParse, ReadsTheWindowsShapeAndThePortItReports) {
  const AttachedList list = ports(usbip_fixtures::kPortWin2OneDevice);
  ASSERT_EQ(list.outcome, PortOutcome::Devices);
  ASSERT_EQ(list.devices.size(), 1u);
  EXPECT_EQ(list.devices[0].port, 1);
  EXPECT_EQ(list.devices[0].busid, "9-1");
  EXPECT_EQ(list.devices[0].exporter, "192.168.50.35");
  EXPECT_EQ(list.devices[0].service, "3240");
  EXPECT_EQ(list.devices[0].speed, "High Speed(480Mbps)");
  EXPECT_EQ(list.devices[0].mode, "zero-copy");
  EXPECT_NE(list.devices[0].product.find("Viper"), std::string::npos);

  // The row really does read "-> serial: " with nothing after the space when the attach carried
  // no --serial. Without trim this would come back as a serial of " ".
  EXPECT_TRUE(list.devices[0].serial.empty());
}

TEST(UsbipPortParse, ThePortIsTheKeyAndItIsAnInteger) {
  // Two devices is the shape a detach sweep has to walk, and the port — not the busid — is the
  // handle that gives each one back.
  const AttachedList list = ports(usbip_fixtures::kPortWin2TwoDevices);
  ASSERT_EQ(list.outcome, PortOutcome::Devices);
  ASSERT_EQ(list.devices.size(), 2u);
  EXPECT_EQ(list.devices[0].port, 1);
  EXPECT_EQ(list.devices[1].port, 2);
  EXPECT_EQ(list.devices[0].busid, "9-1");
  EXPECT_EQ(list.devices[1].busid, "9-2");
  EXPECT_EQ(list.devices[1].mode, "low-latency");
}

TEST(UsbipPortParse, WindowsHoldingNothingIsNoOutputAndTheExitCodeDecides) {
  // The one a confident parser gets wrong. usbip-win2 prints its banner only when it finds a
  // device, so "holding nothing" and "never reached the driver" are the same zero bytes. The
  // parse refuses to choose between them; exit 0 is what makes it NothingAttached.
  EXPECT_EQ(ports(usbip_fixtures::kPortWin2NothingAttached).outcome, PortOutcome::NoOutput);
  EXPECT_EQ(ports("").outcome, PortOutcome::NoOutput);
  EXPECT_EQ(ports("\n\n").outcome, PortOutcome::NoOutput);
}

TEST(UsbipPortParse, LinuxHoldingNothingSaysSoWithItsBanner) {
  // The mirror of the mistake above: the Linux tool prints the banner before it looks at any
  // port, so its empty case IS an answer and calling it NoOutput would be wrong the other way.
  const AttachedList list = ports(usbip_fixtures::kPortLinuxNothingAttached);
  EXPECT_EQ(list.outcome, PortOutcome::NothingAttached);
  EXPECT_EQ(list.devices.size(), 0u);
}

TEST(UsbipPortParse, ReadsTheLinuxShapeWhereTheBusidLeadsTheRow) {
  const AttachedList list = ports(usbip_fixtures::kPortLinuxOneDevice);
  ASSERT_EQ(list.outcome, PortOutcome::Devices);
  ASSERT_EQ(list.devices.size(), 1u);
  // Port 00 is a real Linux port number. Treating 0 as "unset" would strand the device.
  EXPECT_EQ(list.devices[0].port, 0);
  EXPECT_EQ(list.devices[0].busid, "9-1");
  EXPECT_EQ(list.devices[0].exporter, "192.168.50.35");
  EXPECT_EQ(list.devices[0].service, "3240");
}

TEST(UsbipPortParse, ADeviceWithNoRemoteIdentityStillKeepsItsPort) {
  // A real Linux state: the record could not be read, so there is no URL row and no busid. The
  // port is still true, and the port is what releases the device.
  const AttachedList list = ports(usbip_fixtures::kPortLinuxUnknownRemote);
  ASSERT_EQ(list.outcome, PortOutcome::Devices);
  ASSERT_EQ(list.devices.size(), 1u);
  EXPECT_EQ(list.devices[0].port, 1);
  EXPECT_TRUE(list.devices[0].busid.empty());
}

TEST(UsbipPortParse, TheRealLinuxDriverFailureIsUnreadableAndSaysWhatToDo) {
  // A live capture: usbip-utils with vhci_hcd not loaded. It must never read as "holding
  // nothing" — the machine was never successfully asked.
  const AttachedList list = ports(usbip_fixtures::kPortErrorLinux);
  EXPECT_EQ(list.outcome, PortOutcome::Unreadable);
  EXPECT_NE(list.detail.find("vhci_hcd"), std::string::npos) << list.detail;
}

TEST(UsbipPortParse, ACarriageReturnDoesNotBreakTheWindowsShape) {
  // usbip-win2 runs on Windows, where rows end CRLF. Nothing here may depend on the line ending.
  std::string crlf(usbip_fixtures::kPortWin2OneDevice);
  for (std::size_t at = crlf.find('\n'); at != std::string::npos; at = crlf.find('\n', at + 1)) {
    crlf.insert(at, 1, '\r');
    ++at;
  }
  const AttachedList list = ports(crlf);
  ASSERT_EQ(list.outcome, PortOutcome::Devices);
  ASSERT_EQ(list.devices.size(), 1u);
  EXPECT_EQ(list.devices[0].port, 1);
  EXPECT_EQ(list.devices[0].busid, "9-1");
  EXPECT_TRUE(list.devices[0].serial.empty());
}

TEST(UsbipPortParse, RefusesAPortRowThatIsNotANumber) {
  // Starts like our format and is not it. Skipping it would turn "cannot read this" into
  // "holding nothing", and the device would never be given back.
  EXPECT_EQ(ports("Imported USB devices\nPort xx: device in use\n").outcome,
            PortOutcome::Unreadable);
}

TEST(UsbipPortParse, RefusesAPortNumberThatDetachCouldNotAddress) {
  // 256 is outside usbip's own [1,255], so it is not a port we could ever detach with.
  const AttachedList list = ports("Imported USB devices\nPort 256: device in use at High Speed\n");
  EXPECT_EQ(list.outcome, PortOutcome::Unreadable);
}

TEST(UsbipPortParse, RefusesARowThatIsNeitherTheDescriptionNorADetail) {
  const AttachedList list = ports(
    "Imported USB devices\n"
    "Port 01: device in use at High Speed(480Mbps)\n"
    "         Some Device\n"
    "         some stray row\n");
  EXPECT_EQ(list.outcome, PortOutcome::Unreadable);
  EXPECT_EQ(list.detail, "some stray row");
}

TEST(UsbipPortParse, ADescriptionRowCannotBeADetailRow) {
  // A header with no description row is not a shape we know. Absorbing the detail row into the
  // product name would hide the fact that the layout moved under us.
  const AttachedList list = ports(
    "Imported USB devices\n"
    "Port 01: device in use at High Speed(480Mbps)\n"
    "           -> usbip://192.168.50.35:3240/9-1\n");
  EXPECT_EQ(list.outcome, PortOutcome::Unreadable);
}

TEST(UsbipPortParse, RefusesAUrlWhoseBusidIsNotABusid) {
  // The busid arrives from another machine, and this is the row where it enters — so this is
  // where it is checked. An option, a path, or anything with a space is never stored.
  for (const char *bad : {"a-1", "-a", "9-1 extra", "../etc/passwd", ""}) {
    const std::string text =
      std::string("Imported USB devices\nPort 01: device in use at High Speed(480Mbps)\n"
                  "         Some Device\n"
                  "           -> usbip://192.168.50.35:3240/") + bad + "\n";
    EXPECT_EQ(ports(text).outcome, PortOutcome::Unreadable) << bad;
  }
}

TEST(UsbipPortParse, ADetailRowBeforeAnyDeviceIsRefused) {
  EXPECT_EQ(ports("Imported USB devices\n           -> usbip://192.168.50.35:3240/9-1\n").outcome,
            PortOutcome::Unreadable);
}

TEST(UsbipPortParse, RefusesADetailRowThatIsNotOneOfTheKnownOnes) {
  // Found by mutation testing, which is the only reason it exists: an earlier version of this
  // suite refused a *stray* row and so never reached the refusal for a "->" row that is not one
  // of the detail rows we know. Removing that refusal changed nothing the tests could see.
  //
  // This is the row that says the tool grew a field, or moved the layout. Absorbing it silently
  // would be claiming to have read a document we do not understand.
  const AttachedList list = ports(
    "Imported USB devices\n"
    "Port 01: device in use at High Speed(480Mbps)\n"
    "         Some Device\n"
    "           -> something we have never seen before\n");
  EXPECT_EQ(list.outcome, PortOutcome::Unreadable);
  EXPECT_EQ(list.detail, "-> something we have never seen before");
}

TEST(UsbipPortParse, EveryPortOutcomeHasWords) {
  EXPECT_FALSE(input::usbip::describe(PortOutcome::Devices).empty());
  EXPECT_FALSE(input::usbip::describe(PortOutcome::NothingAttached).empty());
  EXPECT_FALSE(input::usbip::describe(PortOutcome::NoOutput).empty());
  EXPECT_FALSE(input::usbip::describe(PortOutcome::Unreadable).empty());
}
