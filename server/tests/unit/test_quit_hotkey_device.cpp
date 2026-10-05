/**
 * @file tests/unit/test_quit_hotkey_device.cpp
 * @brief Which devices are ours - asserted against paths taken from a real machine.
 *
 * Every path in this file was read off a real Arch box with udevadm and readlink, not invented.
 * That matters more than it looks: the whole feature rests on one claim, that a keyboard shared
 * over USB/IP can be told apart from the ones built into the machine, and a test written against
 * guessed paths would prove only that the guess was self-consistent.
 *
 * The three cases marked PROBED were also checked on that machine with udevadm test against the
 * udev rule itself. The rule grants access to a device and this function decides whether to read
 * it; if the two ever disagree the reader finds nothing while the rule grants access to nobody,
 * which fails quietly. These cases are the agreement, written down.
 */
#include <gtest/gtest.h>

#include "src/platform/linux/quit_hotkey_device.h"

using quit_hotkey::is_imported_devpath;

// --- Ours ------------------------------------------------------------------------------------

TEST(QuitHotkeyDevice, TheVirtualHostControllerItselfIsOurs) {
  // PROBED. This is the platform device the kernel creates for USB/IP, and the string the udev
  // rule matches on.
  EXPECT_TRUE(is_imported_devpath("/devices/platform/vhci_hcd.0"));
}

TEST(QuitHotkeyDevice, AKeyboardAttachedOverUsbipIsOurs) {
  // The shape a real attached keyboard takes: enumerated below vhci_hcd.0, so every input node
  // it produces sits underneath it.
  EXPECT_TRUE(is_imported_devpath(
      "/devices/platform/vhci_hcd.0/usb5/5-1/5-1:1.0/0003:046D:C52B.0001/input/input20/event20"));
}

TEST(QuitHotkeyDevice, ALeadingSysIsAcceptedEitherWay) {
  // callers pass it both ways depending on where the path came from, and it is not a difference
  // worth failing over.
  EXPECT_TRUE(is_imported_devpath("/sys/devices/platform/vhci_hcd.0"));
  EXPECT_TRUE(is_imported_devpath("/devices/platform/vhci_hcd.0"));
}

TEST(QuitHotkeyDevice, AHigherControllerIndexIsOurs) {
  // The kernel numbers them, and which number a device lands on is not fixed.
  EXPECT_TRUE(is_imported_devpath("/devices/platform/vhci_hcd.7/usb9/9-2/9-2:1.0/input/input3/event3"));
}

// --- Not ours --------------------------------------------------------------------------------

TEST(QuitHotkeyDevice, TheMachinesOwnKeyboardIsNotOurs) {
  // PROBED, all three. These are the actual built-in devices on the machine this was written
  // against - the laptop's own keyboard and two platform devices. Reading any of them would mean
  // watching keys the person is typing for reasons that have nothing to do with a stream.
  EXPECT_FALSE(is_imported_devpath("/devices/pci0000:00/0000:00:1f.0/PNP0C09:01/PNP0C0D:01/input/input0"));
  EXPECT_FALSE(is_imported_devpath("/devices/platform/PNP0C0E:00/input/input1"));
  EXPECT_FALSE(is_imported_devpath("/devices/platform/PNP0C0C:00/input/input2"));
}

TEST(QuitHotkeyDevice, AnOrdinaryUsbKeyboardIsNotOurs) {
  // Plugged in, not shared in. It sits under the PCI controller, not the virtual one.
  EXPECT_FALSE(is_imported_devpath(
      "/devices/pci0000:00/0000:00:14.0/usb1/1-2/1-2:1.0/0003:046D:C31C.0002/input/input9/event9"));
}

// --- The edges that would quietly widen the match --------------------------------------------

TEST(QuitHotkeyDevice, TheNameMustHaveTheKernelsNumberOnIt) {
  // "vhci_hcd" on its own is not what the kernel creates. Requiring the dot keeps the match to
  // the real thing rather than anything that happens to share the stem.
  EXPECT_FALSE(is_imported_devpath("/devices/platform/vhci_hcd"));
}

TEST(QuitHotkeyDevice, AComponentThatMerelyContainsTheNameIsNotOurs) {
  // A component must START with the name. Without this, a directory called "not-vhci_hcd.0" or
  // "my-vhci_hcd.1" would be swept up, and the match would quietly grow to include things
  // nobody meant.
  EXPECT_FALSE(is_imported_devpath("/devices/platform/xvhci_hcd.0"));
  EXPECT_FALSE(is_imported_devpath("/devices/platform/my-vhci_hcd.1/input/input4/event4"));
  EXPECT_FALSE(is_imported_devpath("/devices/platform/notvhci_hcd.2/input/input5/event5"));
}

TEST(QuitHotkeyDevice, TheMatchIsDeliberatelyNoStricterThanTheRule) {
  // The reader must not be stricter than the udev rule that grants it access. If it were, the
  // rule would hand it a device and it would then refuse to open it - the feature would do
  // nothing, and there would be nothing in the log to say why. The rule is */vhci_hcd.[0-9]*,
  // so this accepts a name beginning the same way:
  EXPECT_TRUE(is_imported_devpath("/devices/platform/something/vhci_hcd.0-backup/input/input6/event6"));
  // That name does not exist and nothing creates it: the kernel creates vhci_hcd.N and nothing
  // else. So the cost of being no stricter than the rule is a hypothetical directory, and the
  // cost of being stricter is a real keyboard that is never read. The asymmetry is the reason,
  // and this test exists to stop somebody "tidying" it into the stricter form later.
}

TEST(QuitHotkeyDevice, ANumberIsRequiredAfterTheDot) {
  // The kernel numbers these. A component with the right stem but no number is not one of them,
  // and the digit is what keeps the match to the real thing rather than the stem alone.
  EXPECT_FALSE(is_imported_devpath("/devices/platform/vhci_hcd.x/input/input7/event7"));
  EXPECT_FALSE(is_imported_devpath("/devices/platform/vhci_hcd./input/input8/event8"));
  EXPECT_FALSE(is_imported_devpath("/devices/platform/vhci_hcd.-1/input/input9/event9"));
}

TEST(QuitHotkeyDevice, AnEmptyPathIsNotOurs) {
  // devpath_for returns empty when a node cannot be resolved. That must never be read as a match,
  // or an unresolvable device would be opened as if it were ours.
  EXPECT_FALSE(is_imported_devpath(""));
  EXPECT_FALSE(is_imported_devpath("/"));
  EXPECT_FALSE(is_imported_devpath("/devices"));
}

TEST(QuitHotkeyDevice, ATrailingSlashDoesNotHideAMatch) {
  // Guarding against the splitting loop treating the empty last component as the end.
  EXPECT_TRUE(is_imported_devpath("/devices/platform/vhci_hcd.0/"));
  EXPECT_FALSE(is_imported_devpath("/devices/platform/PNP0C0E:00/"));
}
