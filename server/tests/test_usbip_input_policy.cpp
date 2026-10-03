/**
 * @file tests/test_usbip_input_policy.cpp
 * @brief Tests for the portable USB/IP importer policies.
 */
#include <gtest/gtest.h>

#include "usbip_input_policy.h"

namespace {
  // These are not invented examples. Every busid below was read off real hardware on
  // 2026-10-03 — the z13 (Linux exporter), the mini PC (Windows exporter) and the gaming PC
  // (Windows importer) — so this list is what must keep working.
  constexpr const char *kBusidsSeenOnRealHardware[] = {
    "3-10",  // Intel AX211 Bluetooth, z13
    "3-6",   // ASUSTek, z13
    "3-7",   // Elan Microelectronics, z13
    "3-8",   // IMC Networks, z13
    "3-9",   // ASUSTek, z13
    "9-1",   // Razer Viper Ultimate, mini PC
    "9-2",   // Razer BlackWidow V4 Pro, mini PC
    "9-3",   // Trust USB microphone, mini PC
    "10-2",  // Razer Kiyo Pro, mini PC
    "1-5",   // Intel Wireless Bluetooth, mini PC
    "5-3",   // Razer Mouse Dock, mini PC
  };
}  // namespace

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
