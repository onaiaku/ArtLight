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

#include <set>
#include <stdexcept>
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
  EXPECT_EQ(list.devices[0].speed, "Full Speed(12Mbps)");
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

// ── the reconcile plan ───────────────────────────────────────────────────────────────────

namespace {
  using input::usbip::Action;
  using input::usbip::Attached;
  using input::usbip::Device;
  using input::usbip::Plan;

  /// A device the exporter is offering. Real busids, read off the mini PC and the z13.
  Device offer(const char *busid) {
    Device device;
    device.busid = busid;
    return device;
  }

  /// A device this machine is holding, at the port usbip gave it.
  Attached holds(const char *busid, const int port) {
    Attached attached;
    attached.busid = busid;
    attached.port = port;
    return attached;
  }

  Plan reconcile(const std::vector<Device> &offered,
                 const std::vector<Attached> &attached,
                 const std::vector<std::string> &wanted) {
    return input::usbip::plan_reconcile(offered, attached, wanted);
  }
}  // namespace

TEST(UsbipPlan, AttachesWhatIsOfferedAndNotYetHeld) {
  const Plan plan = reconcile({offer("9-1"), offer("9-2")}, {}, {});
  EXPECT_EQ(plan.attach_count(), 2u);
  EXPECT_EQ(plan.detach_count(), 0u);
  EXPECT_EQ(plan.actions[0].action, Action::Attach);
  EXPECT_EQ(plan.actions[0].busid, "9-1");
  EXPECT_EQ(plan.actions[1].busid, "9-2");
}

TEST(UsbipPlan, LeavesAnAlreadyHeldDeviceExactlyAlone) {
  // Held, offered and wanted is the steady state. A plan that re-attaches here would fight
  // itself on every session start.
  const Plan plan = reconcile({offer("9-1")}, {holds("9-1", 1)}, {});
  EXPECT_TRUE(plan.empty());
}

TEST(UsbipPlan, NothingAnywhereIsAnEmptyPlan) {
  EXPECT_TRUE(reconcile({}, {}, {}).empty());
}

TEST(UsbipPlan, DetachesWhatIsHeldButNoLongerOffered) {
  // The exporter unbound it, or this is a hold left over from a session that did not end
  // cleanly. Either way the device belongs back on its own machine.
  const Plan plan = reconcile({}, {holds("9-1", 1)}, {});
  EXPECT_EQ(plan.detach_count(), 1u);
  EXPECT_EQ(plan.actions[0].action, Action::Detach);
  EXPECT_EQ(plan.actions[0].busid, "9-1");
}

TEST(UsbipPlan, DetachesWhatIsNoLongerWanted) {
  // The user unticked it in ArtMoon. In practice ArtMoon also unbinds it, so this arrives as
  // "no longer offered" - but a caller that passes a want list gets the right answer anyway.
  const Plan plan = reconcile({offer("9-1"), offer("9-2")}, {holds("9-2", 2)}, {"9-1"});
  EXPECT_EQ(plan.detach_count(), 1u);
  EXPECT_EQ(plan.actions[0].busid, "9-2");
  EXPECT_EQ(plan.attach_count(), 1u);
  EXPECT_EQ(plan.actions[1].busid, "9-1");
}

TEST(UsbipPlan, LeavesAnOfferedButUnwantedDeviceWhereItIs) {
  const Plan plan = reconcile({offer("9-1")}, {}, {"9-2"});
  EXPECT_TRUE(plan.empty());
}

TEST(UsbipPlan, AnEmptyWantListMeansEverythingOfferedIsWanted) {
  // ArtMoon binds exactly what the user toggled on, so "what the exporter offers" IS the
  // user's answer. This is the ordinary path, not a special case.
  const Plan plan = reconcile({offer("9-1"), offer("9-2"), offer("9-3")}, {}, {});
  EXPECT_EQ(plan.attach_count(), 3u);
}

TEST(UsbipPlan, DetachesBeforeItAttachesSoASwapCanReuseThePort) {
  // The user now wants 9-2 instead of 9-1. Attaching first would ask for a port while the one
  // being given up is still held.
  const Plan plan = reconcile({offer("9-2")}, {holds("9-1", 1)}, {"9-2"});
  ASSERT_EQ(plan.actions.size(), 2u);
  EXPECT_EQ(plan.actions[0].action, Action::Detach);
  EXPECT_EQ(plan.actions[0].busid, "9-1");
  EXPECT_EQ(plan.actions[1].action, Action::Attach);
  EXPECT_EQ(plan.actions[1].busid, "9-2");
}

TEST(UsbipPlan, ADetachCarriesThePortAndNotOnlyTheBusid) {
  // detach takes a port. Carrying the busid is for the log line; carrying the port is the
  // thing that actually gives the device back.
  const Plan plan = reconcile({}, {holds("9-1", 3)}, {});
  ASSERT_EQ(plan.actions.size(), 1u);
  EXPECT_EQ(plan.actions[0].port, 3);
}

TEST(UsbipPlan, DetachesAHeldDeviceWithNoReadableRemoteIdentity) {
  // A real Linux state: the tool prints the port and no remote identity at all. Such an entry
  // cannot be matched against anything, and leaving it held because we cannot name it is a
  // device that never goes home.
  const Plan plan = reconcile({}, {holds("", 0)}, {});
  ASSERT_EQ(plan.actions.size(), 1u);
  EXPECT_EQ(plan.actions[0].action, Action::Detach);
  EXPECT_EQ(plan.actions[0].port, 0);
  EXPECT_TRUE(plan.actions[0].busid.empty());
}

TEST(UsbipPlan, AHeldDeviceWithNoIdentityIsNotMistakenForTheDeviceWeWant) {
  // The dangerous direction of the case above: an unnamed hold must never satisfy "we already
  // have 9-1", or 9-1 is never fetched. The hold is given back AND 9-1 is still fetched, in
  // that order - so this asserts on the action, not on its position.
  const Plan plan = reconcile({offer("9-1")}, {holds("", 0)}, {});
  EXPECT_EQ(plan.attach_count(), 1u);
  EXPECT_EQ(plan.detach_count(), 1u);

  bool fetched_nine_one = false;
  for (const auto &action : plan.actions) {
    if (action.action == Action::Attach && action.busid == "9-1") {
      fetched_nine_one = true;
    }
  }
  EXPECT_TRUE(fetched_nine_one);
}

TEST(UsbipPlan, IsIdempotentSoARerunCannotDoubleAttach) {
  const std::vector<Device> offered = {offer("9-1"), offer("9-2")};
  const std::vector<Attached> attached = {holds("9-2", 1)};
  const Plan first = reconcile(offered, attached, {});
  const Plan second = reconcile(offered, attached, {});
  ASSERT_EQ(first.actions.size(), second.actions.size());
  for (std::size_t i = 0; i < first.actions.size(); ++i) {
    EXPECT_EQ(first.actions[i].action, second.actions[i].action);
    EXPECT_EQ(first.actions[i].busid, second.actions[i].busid);
    EXPECT_EQ(first.actions[i].port, second.actions[i].port);
  }
}

TEST(UsbipPlan, EveryPlannedActionSaysWhyItIsThere) {
  // This plan is what the log is written from. "detached 9-1" with no reason is the entry that
  // makes a later reader guess.
  const Plan plan = reconcile({offer("9-1")}, {holds("9-2", 1)}, {"9-1"});
  ASSERT_FALSE(plan.empty());
  for (const auto &action : plan.actions) {
    EXPECT_FALSE(action.reason.empty());
  }
}

TEST(UsbipPlan, EveryActionHasWords) {
  EXPECT_FALSE(input::usbip::describe(Action::Attach).empty());
  EXPECT_FALSE(input::usbip::describe(Action::Detach).empty());
}

// ── the argument vectors ─────────────────────────────────────────────────────────────────

TEST(UsbipArgv, AttachNamesTheExporterAndTheDevice) {
  const std::vector<std::string> argv =
    input::usbip::build_attach_argv("192.168.50.35", "9-1");
  ASSERT_EQ(argv.size(), 6u);
  EXPECT_EQ(argv[0], "usbip");  // a placeholder; the platform layer replaces it
  EXPECT_EQ(argv[1], "attach");
  EXPECT_EQ(argv[2], "-r");
  EXPECT_EQ(argv[3], "192.168.50.35");
  EXPECT_EQ(argv[4], "-b");
  EXPECT_EQ(argv[5], "9-1");
}

TEST(UsbipArgv, AttachRefusesABusidThatIsNotABusid) {
  // The busid list comes off the wire, so a value that is not a busid must never reach an
  // argument vector - it is refused while building it, not after.
  for (const char *bad : {"", "-p", "9-1; rm -rf /", "9 1", "../9-1", "9-1\n", "--help"}) {
    EXPECT_THROW(input::usbip::build_attach_argv("192.168.50.35", bad), std::invalid_argument)
      << "'" << bad << "'";
  }
}

TEST(UsbipArgv, AttachRefusesAnExporterThatWouldBeReadAsAnOption) {
  EXPECT_THROW(input::usbip::build_attach_argv("-r", "9-1"), std::invalid_argument);
  EXPECT_THROW(input::usbip::build_attach_argv("--help", "9-1"), std::invalid_argument);
}

TEST(UsbipArgv, AttachRefusesAnEmptyOrWhitespaceExporter) {
  EXPECT_THROW(input::usbip::build_attach_argv("", "9-1"), std::invalid_argument);
  EXPECT_THROW(input::usbip::build_attach_argv("  ", "9-1"), std::invalid_argument);
  EXPECT_THROW(input::usbip::build_attach_argv("192.168.50.35; whoami", "9-1"),
               std::invalid_argument);
}

TEST(UsbipArgv, AttachAcceptsTheExporterShapesThisPairActuallyUses) {
  // A LAN address, a Tailscale address, and an IPv6 literal - the three shapes that appear on
  // these machines.
  EXPECT_NO_THROW(input::usbip::build_attach_argv("192.168.50.35", "9-1"));
  EXPECT_NO_THROW(input::usbip::build_attach_argv("10.6.0.3", "3-10"));
  EXPECT_NO_THROW(input::usbip::build_attach_argv("fe80::1", "3-10"));
  EXPECT_NO_THROW(input::usbip::build_attach_argv("niks-minipc", "9-1"));
}

TEST(UsbipArgv, DetachTakesAPortAndPortZeroIsValid) {
  // The Linux tool numbers ports from 0. Reading 0 as "unset" would make the first port on
  // every Linux machine impossible to release.
  const std::vector<std::string> argv = input::usbip::build_detach_argv(0);
  ASSERT_EQ(argv.size(), 4u);
  EXPECT_EQ(argv[0], "usbip");
  EXPECT_EQ(argv[1], "detach");
  EXPECT_EQ(argv[2], "-p");
  EXPECT_EQ(argv[3], "0");
}

TEST(UsbipArgv, DetachRefusesANegativePort) {
  EXPECT_THROW(input::usbip::build_detach_argv(-1), std::invalid_argument);
}

TEST(UsbipArgv, DetachCarriesThePortItWasGiven) {
  EXPECT_EQ(input::usbip::build_detach_argv(1)[3], "1");
  EXPECT_EQ(input::usbip::build_detach_argv(12)[3], "12");
}

// ── the Linux tool's REMOTE listing ──────────────────────────────────────────────────────
//
// A remote listing from the Linux tool is grouped BY EXPORTER and opens each group with a line
// naming the machine, " - <address>". The parser was pinned against `usbip-win2`'s bytes, which
// print no such line, so it had no rule for a dash that was not followed by "busid" and refused
// the whole reply. Two consequences, one of them worse than the other: a Linux importer could
// attach nothing at all while the Windows host beside it worked, and an exporter genuinely
// offering nothing reported "we could not understand this" instead of "there is nothing here".

TEST(UsbipParseRemote, TheLinuxToolsPerExporterHeaderIsNotADevice) {
  const auto out = parse(usbip_fixtures::kListRemoteLinux);
  ASSERT_EQ(out.outcome, ListOutcome::Devices) << "detail: " << out.detail;
  ASSERT_EQ(out.devices.size(), 1u);
  EXPECT_EQ(out.devices[0].busid, "9-1");
  EXPECT_EQ(out.devices[0].vendor, "Razer USA, Ltd");
  EXPECT_EQ(out.devices[0].vid_pid, "1532:007b");
  EXPECT_EQ(out.devices[0].product,
            "RC30-0305 Gaming Mouse Dongle [Viper Ultimate (Wireless)]");
}

TEST(UsbipParseRemote, ALinuxExporterOfferingNothingIsNotUnreadable) {
  const auto out = parse(usbip_fixtures::kListRemoteLinuxNothing);
  EXPECT_EQ(out.outcome, ListOutcome::NothingOffered) << "detail: " << out.detail;
  EXPECT_TRUE(out.devices.empty());
}

// The header rule has to stay narrow in both directions. Too loose and a format change is
// reported as "nothing is offered" — a device that never gets given back. So a dash line that
// is not an address must still stop the parse, and the local list's own dash rows must still
// be devices.
TEST(UsbipParseRemote, ADashLineThatIsNotAnAddressIsStillUnreadable) {
  EXPECT_EQ(parse(" - this is not a device\n").outcome, ListOutcome::Unreadable);
  EXPECT_EQ(parse(" - 192.168.50.35 and then some prose\n").outcome, ListOutcome::Unreadable);
}

TEST(UsbipParseRemote, AHeaderAloneIsAnExporterWithNothingToOffer) {
  EXPECT_EQ(parse(" - 192.168.50.35\n").outcome, ListOutcome::NothingOffered);
}

TEST(UsbipParseRemote, TheLocalListsDashRowsAreStillDevices) {
  const auto out = parse(usbip_fixtures::kListLocalLinux);
  ASSERT_EQ(out.outcome, ListOutcome::Devices) << "detail: " << out.detail;
  EXPECT_EQ(out.devices.size(), 5u);
}

// ── a Linux importer that is HOLDING a device ────────────────────────────────────────────
//
// Captured for real: the mini PC's mouse was attached to the z13, then detached straight after.
// Two DERIVED assumptions died in that capture, and both would have cost a device — the second
// one permanently, because a port read that fails is a device that never gets given back.

TEST(UsbipParseAttached, TheLinuxToolReadsAPortWithNoUrlRowAtAll) {
  const auto out = input::usbip::parse_attached(usbip_fixtures::kPortLinuxAttached);
  ASSERT_EQ(out.outcome, input::usbip::PortOutcome::Devices) << "detail: " << out.detail;
  ASSERT_EQ(out.devices.size(), 1u);
  EXPECT_EQ(out.devices[0].port, 0) << "port 0 is valid on Linux and must not read as unset";
  EXPECT_EQ(out.devices[0].busid, "9-1")
      << "on this kernel the busid is in the row's leading column and nowhere else";
  EXPECT_EQ(out.devices[0].exporter, "") << "there is no URL row to carry one";
}

TEST(UsbipParseAttached, TheToolsOwnDiagnosticsDoNotCostUsTheTable) {
  // The same call with stderr merged in, which is what a shell pipeline or an unseparated popen
  // produces. Refusing a table that is right there means never releasing the device.
  const auto out = input::usbip::parse_attached(usbip_fixtures::kPortLinuxAttachedMerged);
  ASSERT_EQ(out.outcome, input::usbip::PortOutcome::Devices) << "detail: " << out.detail;
  ASSERT_EQ(out.devices.size(), 1u);
  EXPECT_EQ(out.devices[0].port, 0);
  EXPECT_EQ(out.devices[0].busid, "9-1");
}

// The other direction, and the one that must never be got wrong. The tool complaining is not the
// machine being empty: answering "your hands are empty" here leaves a device attached to this
// machine forever, with the machine it came from missing the hardware and nothing saying so.
TEST(UsbipParseAttached, DiagnosticsWithNoPortsAreNotAnEmptyHand) {
  const std::string only_noise =
      "Imported USB devices\n====================\n"
      "libusbip: error: fopen\nlibusbip: error: read_record\n";
  const auto out = input::usbip::parse_attached(only_noise);
  EXPECT_EQ(out.outcome, input::usbip::PortOutcome::Unreadable);
  EXPECT_FALSE(out.detail.empty()) << "and it says what the tool said";
}

// A hold whose identity we cannot read is still a hold. It must be reported, not dropped: the
// planner needs to see it to decide what to do, and silently forgetting a device is how one gets
// left behind on a machine nobody is looking at.
TEST(UsbipParseAttached, AHoldWithNoReadableIdentityIsStillReported) {
  const std::string anonymous_hold =
      "Imported USB devices\n====================\n"
      "Port 03: <Port in Use> at High Speed(480Mbps)\n"
      "       unknown product\n"
      "           -> unknown host, remote port and remote busid\n";
  const auto out = input::usbip::parse_attached(anonymous_hold);
  ASSERT_EQ(out.outcome, input::usbip::PortOutcome::Devices) << "detail: " << out.detail;
  ASSERT_EQ(out.devices.size(), 1u);
  EXPECT_EQ(out.devices[0].port, 3);
  EXPECT_EQ(out.devices[0].busid, "") << "empty is honest; invented would not be";
}

// ── is the client stack actually here? ───────────────────────────────────────────────────
//
// Five states, and the whole value is that four of them are a DIFFERENT thing to do about it.
// The two that get collapsed by accident are DriverMissing and DriverNotLoaded: one is a
// reinstall, the other is a load. Sending someone to reinstall a driver they already have is
// the failure this set exists to prevent.

TEST(UsbipClientProbe, NoClientIsASetupStepNotAnError) {
  const auto out = input::usbip::assess_client({});
  EXPECT_FALSE(out.ok);
  EXPECT_EQ(out.state, input::usbip::ClientState::NotInstalled);
  EXPECT_FALSE(out.reason.empty()) << "a state with no reason is a code the user has to look up";
}

TEST(UsbipClientProbe, AClientWithNoDriverAndNoModuleIsMissingItsDriver) {
  input::usbip::ClientProbe probe;
  probe.client_found = true;
  probe.client_path = "/usr/bin/usbip";
  const auto out = input::usbip::assess_client(probe);
  EXPECT_EQ(out.state, input::usbip::ClientState::DriverMissing);
}

TEST(UsbipClientProbe, AModuleThatExistsButIsNotLoadedIsItsOwnState) {
  // The z13 measured exactly this: usbip-utils 2.0 installed, vhci_hcd not loaded, and not one
  // device importable. "Missing" here would be a lie that costs someone a reinstall.
  input::usbip::ClientProbe probe;
  probe.client_found = true;
  probe.driver_loadable = true;
  const auto out = input::usbip::assess_client(probe);
  EXPECT_FALSE(out.ok);
  EXPECT_EQ(out.state, input::usbip::ClientState::DriverNotLoaded);
  EXPECT_NE(out.reason, input::usbip::assess_client([] {
    input::usbip::ClientProbe p;
    p.client_found = true;
    return p;
  }()).reason) << "unloaded and missing must not read as the same thing";
}

TEST(UsbipClientProbe, WindowsNeedsNoHelperAndMustNotBeAskedForOne) {
  // Attach on Windows was proven from a UAC-filtered limited token. privilege_required stays
  // false there, and this is the test that fails if someone "fixes" the symmetry by demanding a
  // daemon Windows does not need.
  input::usbip::ClientProbe probe;
  probe.client_found = true;
  probe.client_path = R"(C:\Program Files\USBip\usbip.exe)";
  probe.driver_present = true;
  probe.privilege_required = false;
  probe.helper_present = false;
  const auto out = input::usbip::assess_client(probe);
  EXPECT_TRUE(out.ok);
  EXPECT_EQ(out.state, input::usbip::ClientState::Ready);
  EXPECT_TRUE(out.reason.empty());
}

TEST(UsbipClientProbe, LinuxWithoutTheHelperIsNotReadyEvenWithEverythingElse) {
  // The Linux half, and the finding that corrected the plan: attach writes a root-only sysfs
  // node, so a Linux importer needs a privileged helper the Windows one does not.
  input::usbip::ClientProbe probe;
  probe.client_found = true;
  probe.driver_present = true;
  probe.privilege_required = true;
  probe.helper_present = false;
  const auto out = input::usbip::assess_client(probe);
  EXPECT_FALSE(out.ok);
  EXPECT_EQ(out.state, input::usbip::ClientState::HelperMissing);
}

TEST(UsbipClientProbe, LinuxWithTheHelperIsReady) {
  input::usbip::ClientProbe probe;
  probe.client_found = true;
  probe.driver_present = true;
  probe.privilege_required = true;
  probe.helper_present = true;
  const auto out = input::usbip::assess_client(probe);
  EXPECT_TRUE(out.ok);
  EXPECT_EQ(out.state, input::usbip::ClientState::Ready);
}

TEST(UsbipClientProbe, EveryStateHasItsOwnSentence) {
  // If two states ever read the same to a human, the split was pointless and this catches it.
  const std::vector<input::usbip::ClientState> all{
      input::usbip::ClientState::Ready,
      input::usbip::ClientState::NotInstalled,
      input::usbip::ClientState::DriverMissing,
      input::usbip::ClientState::DriverNotLoaded,
      input::usbip::ClientState::HelperMissing};
  std::set<std::string> seen;
  for (const auto state : all) {
    const auto text = input::usbip::describe(state);
    EXPECT_FALSE(text.empty());
    EXPECT_TRUE(seen.insert(text).second) << "two states describe themselves identically: " << text;
  }
  EXPECT_EQ(seen.size(), all.size());
}

// ── which product is this? ───────────────────────────────────────────────────────────────
//
// Both DisplayNames below were read off real machines, and they are the reason this rule exists:
// the importer's client is "USBip", the exporter's tool is "usbipd-win". They are different
// products and they live on the same boxes.

TEST(UsbipUninstallRecord, TheClientsOwnDisplayNameIsTheClient) {
  // niks-gaming, read live.
  EXPECT_TRUE(input::usbip::names_the_client("USBip 0.9.8.1"));
  EXPECT_TRUE(input::usbip::names_the_client("USBip"));
  EXPECT_TRUE(input::usbip::names_the_client("usbip 0.9.8.1")) << "case must not decide this";
}

TEST(UsbipUninstallRecord, TheExportersToolIsNotTheClient) {
  // niks-minipc, read live. A loose substring match calls this "the client is installed" on a
  // machine that has no client at all, and it was only saved downstream by a file check that
  // happened to fail. The name has to carry the decision, not the folder contents.
  EXPECT_FALSE(input::usbip::names_the_client("usbipd-win"));
  EXPECT_FALSE(input::usbip::names_the_client("USBipd-win"));
  EXPECT_FALSE(input::usbip::names_the_client("usbipd"));
}

TEST(UsbipUninstallRecord, TheBoundaryIsWhatSeparatesThem) {
  // "usbip" followed by anything that is not a letter or digit is the client; followed by a letter
  // or digit it is some other product that merely starts the same way.
  EXPECT_TRUE(input::usbip::names_the_client("usbip-utils"));
  EXPECT_TRUE(input::usbip::names_the_client("USBip (x64)"));
  EXPECT_FALSE(input::usbip::names_the_client("usbipclient"));
  EXPECT_FALSE(input::usbip::names_the_client("usbip2"));
}

TEST(UsbipUninstallRecord, NothingAtAllIsNotTheClient) {
  EXPECT_FALSE(input::usbip::names_the_client(""));
  EXPECT_FALSE(input::usbip::names_the_client("USB/IP client"));
  EXPECT_FALSE(input::usbip::names_the_client("Razer Synapse"));
}

// ── what an attach attempt actually did ──────────────────────────────────────────────────
//
// The point of this classifier is that "nothing moved" and "it failed" are different sentences to
// the person reading them. Every string below was seen on a real machine.

TEST(UsbipAttachOutcome, ASuccessfulAttachSaysSo) {
  const auto r = input::usbip::classify_attach(0, "", "");
  EXPECT_EQ(r.outcome, input::usbip::AttachOutcome::Attached);
  EXPECT_TRUE(r.detail.empty()) << "nothing went wrong, so there is nothing to quote";
}

TEST(UsbipAttachOutcome, TheExportersVetoIsItsOwnState) {
  // The exporter refused because it still counts the device as out. An empty `Persisted:` table
  // does NOT clear this, so it must not be filed with ordinary failures - retrying will not help,
  // and the person needs to know the device did not move.
  const auto r = input::usbip::classify_attach(1, "", "usbip: error: Device busy (already exported)\n");
  EXPECT_EQ(r.outcome, input::usbip::AttachOutcome::DeviceBusy);
  EXPECT_NE(r.outcome, input::usbip::AttachOutcome::Failed) << "this is the whole reason the enum exists";
  EXPECT_EQ(r.detail, "usbip: error: Device busy (already exported)") << "kept verbatim, not paraphrased";
}

TEST(UsbipAttachOutcome, TheLinuxPrivilegeRefusalIsMeasuredNotGuessed) {
  // Verbatim from the z13: an unprivileged attach, because /sys/.../attach is a root-only write.
  const auto r = input::usbip::classify_attach(1, "", "usbip: error: import device\n");
  EXPECT_EQ(r.outcome, input::usbip::AttachOutcome::Refused);
  EXPECT_EQ(r.detail, "usbip: error: import device");
}

TEST(UsbipAttachOutcome, AnUnnameableFailureKeepsItsOwnWords) {
  // A build that guesses here is worse than one that admits it cannot name the reason: the raw
  // line is the only thing checkable against the machine afterwards.
  const auto r = input::usbip::classify_attach(1, "", "usbip: error: something nobody has seen yet\n");
  EXPECT_EQ(r.outcome, input::usbip::AttachOutcome::Failed);
  EXPECT_EQ(r.detail, "usbip: error: something nobody has seen yet");
}

TEST(UsbipAttachOutcome, SilenceOnStderrStillFindsTheReasonOnStdout) {
  const auto r = input::usbip::classify_attach(2, "usbip: error: cannot open\n", "");
  EXPECT_EQ(r.outcome, input::usbip::AttachOutcome::Failed);
  EXPECT_EQ(r.detail, "usbip: error: cannot open") << "the reason is not always on stderr";
}

TEST(UsbipAttachOutcome, BusyAndFailedAreNotTheSameWordInTheLog) {
  const auto busy = input::usbip::classify_attach(1, "", "Device busy (already exported)\n");
  const auto failed = input::usbip::classify_attach(1, "", "usbip: error: unknown\n");
  EXPECT_NE(input::usbip::describe(busy.outcome), input::usbip::describe(failed.outcome));
  EXPECT_NE(input::usbip::describe(busy.outcome), input::usbip::describe(input::usbip::AttachOutcome::Attached));
}
