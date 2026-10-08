/**
 * @file src/quit_request_policy.cpp
 * @brief Definitions for the quit-combo policy.
 */
#include "quit_request_policy.h"

namespace quit_request {
  namespace {
    // The reasons live here rather than inline at the call site so the test can assert on the
    // same sentences the log will show. A reason that only exists where it is used is a reason
    // nobody can test, and this whole feature is one where "nothing happened" has to be
    // readable.
    constexpr auto kNothingShared =
      "nothing is being shared with this host, so the combo is handled on the client";
    constexpr auto kOneHolder =
      "the one client holding a shared device";
    constexpr auto kAmbiguous =
      "more than one client is holding shared devices, so there is no single stream to end";
  }  // namespace

  resolution_t resolve(const std::vector<std::string> &clients_holding_devices) {
    // Count CLIENTS, not entries. One client holding a keyboard and a mouse is one client, and
    // counting entries would turn the ordinary case into the ambiguous one - the failure this
    // whole decision exists to avoid.
    std::vector<std::string> distinct;
    for (const auto &uuid : clients_holding_devices) {
      if (uuid.empty()) {
        continue;
      }
      bool already_seen = false;
      for (const auto &existing : distinct) {
        if (existing == uuid) {
          already_seen = true;
          break;
        }
      }
      if (!already_seen) {
        distinct.emplace_back(uuid);
      }
    }

    if (distinct.empty()) {
      return {false, {}, kNothingShared};
    }
    if (distinct.size() > 1) {
      return {false, {}, kAmbiguous};
    }
    return {true, distinct.front(), kOneHolder};
  }
}  // namespace quit_request
