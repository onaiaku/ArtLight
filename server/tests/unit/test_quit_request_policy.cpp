/**
 * @file tests/unit/test_quit_request_policy.cpp
 * @brief Test src/quit_request_policy.h
 *
 * The case this exists for: two clients watching one stream, the combo pressed on one of them.
 * The other one keeps watching. Everything below is that requirement, split into the situations
 * it can actually arrive in.
 */

#include "../tests_common.h"
#include "src/quit_request_policy.h"

#include <initializer_list>
#include <string>
#include <vector>

namespace {
  using quit_request::resolve;

  std::vector<std::string> holders(std::initializer_list<const char *> uuids) {
    std::vector<std::string> out;
    for (const auto *uuid : uuids) {
      out.emplace_back(uuid);
    }
    return out;
  }
}  // namespace

// Nothing shared means the combo never reached this end, so it must not end anything: the
// client is still handling it locally.
TEST(QuitRequestPolicy, NothingSharedResolvesNothing) {
  const auto decision = resolve({});
  EXPECT_FALSE(decision.resolved);
  EXPECT_TRUE(decision.client_uuid.empty());
  EXPECT_FALSE(decision.reason.empty());
}

// The ordinary case: one machine's keyboard is shared, so one client is holding it.
TEST(QuitRequestPolicy, OneHolderResolvesToThatClient) {
  const auto decision = resolve(holders({"mini-pc-uuid"}));
  EXPECT_TRUE(decision.resolved);
  EXPECT_EQ(decision.client_uuid, "mini-pc-uuid");
  EXPECT_FALSE(decision.reason.empty());
}

// The requirement itself, written as a test: the client that did not press it is never named.
TEST(QuitRequestPolicy, TheWatchingClientIsNeverNamed) {
  const auto decision = resolve(holders({"mini-pc-uuid"}));
  EXPECT_NE(decision.client_uuid, "z13-uuid");
}

// A keyboard and a mouse from one machine is one client, not an ambiguity.
TEST(QuitRequestPolicy, OneClientHoldingSeveralDevicesIsStillOneClient) {
  const auto decision = resolve(holders({"mini-pc-uuid", "mini-pc-uuid", "mini-pc-uuid"}));
  EXPECT_TRUE(decision.resolved);
  EXPECT_EQ(decision.client_uuid, "mini-pc-uuid");
}

// Cannot arise with one person at one machine. Ending the wrong stream is worse than ending
// none, so this declines and says why rather than picking one.
TEST(QuitRequestPolicy, TwoHoldersResolveNothing) {
  const auto decision = resolve(holders({"mini-pc-uuid", "z13-uuid"}));
  EXPECT_FALSE(decision.resolved);
  EXPECT_TRUE(decision.client_uuid.empty());
  EXPECT_FALSE(decision.reason.empty());
}

// An empty entry is not a client.
TEST(QuitRequestPolicy, EmptyEntriesAreIgnored) {
  const auto decision = resolve(holders({"", "mini-pc-uuid", ""}));
  EXPECT_TRUE(decision.resolved);
  EXPECT_EQ(decision.client_uuid, "mini-pc-uuid");
}

// Order must not decide it: the same single client at either end of the list resolves the same.
TEST(QuitRequestPolicy, OrderDoesNotDecideIt) {
  const auto first = resolve(holders({"", "mini-pc-uuid"}));
  const auto second = resolve(holders({"mini-pc-uuid", ""}));
  EXPECT_TRUE(first.resolved);
  EXPECT_TRUE(second.resolved);
  EXPECT_EQ(first.client_uuid, second.client_uuid);
}
