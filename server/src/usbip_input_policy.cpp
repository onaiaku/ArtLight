/**
 * @file src/usbip_input_policy.cpp
 * @brief Definitions for the portable USB/IP importer policies.
 */
#include "usbip_input_policy.h"

#include <cstddef>

namespace input::usbip {
  namespace {
    // A real busid is a bus number and a port number, both small. The cap exists so that a
    // pathological value from the network is refused before it reaches an argument vector.
    constexpr std::size_t kMaxBusidLength = 16;

    constexpr bool is_digit(const char c) {
      return c >= '0' && c <= '9';
    }
  }  // namespace

  bool is_valid_busid(const std::string_view busid) {
    if (busid.empty() || busid.size() > kMaxBusidLength) {
      return false;
    }

    // The first character must be a digit. This is what makes a leading '-' impossible, and
    // a leading '-' is what would turn a busid into an option usbip acts on.
    if (!is_digit(busid.front())) {
      return false;
    }

    std::size_t dashes = 0;
    for (const char c : busid) {
      if (c == '-') {
        ++dashes;
        continue;
      }
      if (!is_digit(c)) {
        return false;
      }
    }

    if (dashes != 1) {
      return false;
    }

    // The single separator has to sit between the two runs, not at the end. A trailing '-'
    // passes the digit test above and would otherwise be accepted.
    const auto separator = busid.find('-');
    return separator + 1 < busid.size();
  }
}  // namespace input::usbip
