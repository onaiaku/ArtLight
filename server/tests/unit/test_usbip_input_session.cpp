/**
 * @file tests/unit/test_usbip_input_session.cpp
 * @brief The return path: whatever a session takes, it gives back - including when the port table
 *        will not tell it the port at the time.
 *
 * What this exists to pin down is a defect that cost real hardware on 2026-10-06. A stream attached
 * a drive from the mini PC to the z13. `usbip attach` reported success. The `usbip port` read taken
 * straight afterwards came back showing nothing, so the session could not name the port - and a
 * port is the only thing `detach` accepts. Nothing ever looked again, so the drive stayed attached
 * to a machine whose stream had ended, and the machine it belongs to was missing it until a person
 * ran `usbip detach` by hand.
 *
 * The inputs below are the real bytes: ../support/usbip_captures.h holds the captures, and this file
 * adds nothing of its own except the TIMING. The parser, the port matching and the release logic are
 * the real ones.
 *
 * The platform edge - the six functions in usbip_input.h that touch a real machine - is stood in for
 * here. That is the whole reason this file can exist: attach/detach/port-table are questions about a
 * machine, and the decision about what to do with the answers lives in code that has no machine at
 * all. This is that claim, cashed.
 */
#include "../tests_common.h"

#include <deque>
#include <string>
#include <string_view>
#include <vector>

#include <src/usbip_input.h>
#include <src/usbip_input_session.h>

#include "../support/usbip_captures.h"

namespace {
  /// Successive `usbip port` outputs, one consumed per call. Once it runs out, the "after" text is
  /// returned forever. This is the timing under test and the only invention in the file.
  std::deque<std::string> g_port_outputs;
  std::string g_port_output_after;
  int g_port_read_code = 0;
  std::string g_port_read_err;
  /// What the attach itself says. Empty by default, which is the Windows shape and the shape a
  /// tool that names no port gives - the tests that need the port line set it explicitly.
  std::string g_attach_err;
  std::string g_offered;
  std::vector<int> g_detached;

  void reset(const std::string_view after) {
    g_port_outputs.clear();
    g_port_output_after = std::string {after};
    g_port_read_code = 0;
    g_port_read_err.clear();
    g_attach_err.clear();
    g_offered = std::string {usbip_fixtures::kListRemoteWin2};
    g_detached.clear();
    // The patience is TIME, and time is the thing these tests stage - so every test starts at
    // "the table gets exactly one look" and a test that needs it to answer LATER says so itself.
    // It must be set here rather than left at the production period: a case whose table never
    // answers would otherwise cost 20 seconds per test, and a timing suite that slow stops being
    // run - which is how the defect this file pins down survived four builds.
    input::usbip::set_port_table_patience(std::chrono::milliseconds {0});
  }

  input::usbip::session_request_t request_for_mouse() {
    input::usbip::session_request_t request;
    request.enabled = true;
    request.exporter = "192.168.50.32";
    // The busid the fixture's remote list actually offers, so the plan really does decide to attach.
    request.busids = "9-1";
    return request;
  }
}  // namespace

// ── the platform edge, stood in for ──────────────────────────────────────────────────────

namespace input::usbip {
  std::string client_path() {
    return "/usr/bin/usbip";
  }

  ClientProbe probe_client() {
    ClientProbe probe;
    probe.client_found = true;
    probe.client_path = client_path();
    probe.driver_present = true;
    probe.privilege_required = true;
    probe.helper_present = true;
    return probe;
  }

  run_result_t run_attach(std::string_view, std::string_view) {
    run_result_t result;  // exit 0: classify_attach reads that as Attached
    // The attach's own output, which is where the real tool names the port it used.
    result.err = g_attach_err;
    return result;
  }

  run_result_t run_list_remote(std::string_view) {
    run_result_t result;
    result.out = g_offered;
    return result;
  }

  run_result_t run_list_attached() {
    run_result_t result;
    result.code = g_port_read_code;
    result.err = g_port_read_err;
    if (!g_port_outputs.empty()) {
      result.out = g_port_outputs.front();
      g_port_outputs.pop_front();
      return result;
    }
    result.out = g_port_output_after;
    return result;
  }

  run_result_t run_detach(const int port) {
    g_detached.push_back(port);
    return run_result_t {};
  }
}  // namespace input::usbip

// ── the port table lags the attach ───────────────────────────────────────────────────────

TEST(UsbipSessionReturn, OwnsTheDeviceWhenThePortAppearsOnALaterLook) {
  reset(usbip_fixtures::kPortLinuxEmpty);
  // This test needs the wait to outlast an empty read: reset() leaves it at one look, the table
  // answers on the second.
  input::usbip::set_port_table_patience(std::chrono::milliseconds {1000});
  g_port_outputs.push_back(std::string {usbip_fixtures::kPortLinuxEmpty});   // the pre-attach read
  g_port_outputs.push_back(std::string {usbip_fixtures::kPortLinuxEmpty});   // look 1: too early
  g_port_outputs.push_back(std::string {usbip_fixtures::kPortLinuxAttached});  // look 2: there

  auto holder = input::usbip::session_holder_t::holding_nothing();
  holder->take_from(request_for_mouse());

  EXPECT_TRUE(holder->holding()) << holder->report();
  EXPECT_NE(holder->report().find("9-1 on port 0"), std::string::npos) << holder->report();

  holder->release_all();
  // Port 0 is a REAL port on Linux, not "unknown" - the fixture is the reason that distinction
  // exists, and this asserts it survives into the release.
  EXPECT_EQ(g_detached, (std::vector<int> {0})) << holder->release_failure();
}

// ── the port table never shows it, and the last look is what saves it ─────────────────────

TEST(UsbipSessionReturn, GivesTheDeviceBackOnTheWayOutWhenThePortWasNeverVisibleInSession) {
  // The 2026-10-06 shape exactly: every look during the session is empty, and by the time the
  // session ends the device is there. Nothing used to look again.
  reset(usbip_fixtures::kPortLinuxAttached);
  // reset() leaves the patience at a single look, so the take path reads the table exactly once:
  // one staged empty for the pre-attach read, one for that look. Everything after it reads the
  // "after" text, which is where the device finally is. That is the 2026-10-06 shape - nothing
  // in-session can name it, and the look on the way out is the one that saves the drive - with the
  // timing shortened from the production period so the case can be written down at all.
  g_port_outputs.push_back(std::string {usbip_fixtures::kPortLinuxEmpty});  // the pre-attach read
  g_port_outputs.push_back(std::string {usbip_fixtures::kPortLinuxEmpty});  // the one look the take gets

  auto holder = input::usbip::session_holder_t::holding_nothing();
  holder->take_from(request_for_mouse());

  EXPECT_FALSE(holder->holding());
  // The sentence has to promise the last look rather than shrug, because a person reading this log
  // is deciding whether to go and unplug something by hand.
  EXPECT_NE(holder->report().find("looked for again when the stream ends"), std::string::npos)
    << holder->report();

  holder->release_all();
  EXPECT_EQ(g_detached, (std::vector<int> {0})) << "the on-the-way-out look did not happen";
}

// ── the read itself fails: say so, and never call it an empty machine ─────────────────────

TEST(UsbipSessionReturn, SaysTheReadFailedRatherThanReportingAnEmptyMachine) {
  reset(usbip_fixtures::kPortLinuxEmpty);
  g_port_read_code = 1;
  g_port_read_err = "usbip: error: unable to reach the input service";

  auto holder = input::usbip::session_holder_t::holding_nothing();
  holder->take_from(request_for_mouse());

  // The tool's own words, not a bucket name. "Nothing is attached" and "I could not read what is
  // attached" are different answers and collapsing them is what hides a stranded device.
  EXPECT_NE(holder->report().find(g_port_read_err), std::string::npos) << holder->report();

  holder->release_all();
  // A device that could not be given back is RECORDED, and the record names which one.
  EXPECT_NE(holder->release_failure().find(g_port_read_err), std::string::npos)
    << holder->release_failure();
  EXPECT_NE(holder->release_failure().find("9-1"), std::string::npos) << holder->release_failure();
  // Nothing was invented: no port was guessed at and no blind detach was issued.
  EXPECT_TRUE(g_detached.empty());
}

// ── the attach names its own port: no table, no waiting, no stranded drive ───────────────

TEST(UsbipSessionReturn, TakesThePortFromTheAttachAndNeverWaitsOnATableThatStaysEmpty) {
  // The 2026-10-06 shape, second time. The attach SUCCEEDS and says which port it used; `usbip
  // port` then shows nothing for the whole session AND nothing at teardown either - on the real
  // machine the table only caught up fifteen minutes after the stream had ended. A session that
  // waits for the table to name the port cannot give the device back at all, so the only thing
  // that can save the drive is reading the port out of the attach itself.
  reset(usbip_fixtures::kPortLinuxEmpty);  // the table NEVER shows it: before, during, or after
  g_attach_err = usbip_fixtures::kAttachLinuxUsesPort8;

  auto holder = input::usbip::session_holder_t::holding_nothing();
  holder->take_from(request_for_mouse());

  // Held straight away, with the port the tool named - and no lookups were needed to get there.
  EXPECT_TRUE(holder->holding()) << holder->report();
  EXPECT_NE(holder->report().find("port 8"), std::string::npos) << holder->report();

  holder->release_all();
  EXPECT_EQ(g_detached, (std::vector<int> {8}))
    << "the attach's own port line is the only thing that can free this drive: " << holder->release_failure();
}

// ── and a tool that names no port still gets the table's help ────────────────────────────

TEST(UsbipSessionReturn, FallsBackToThePortTableWhenTheAttachNamesNoPort) {
  // Windows' usbip.exe 0.9.8.1 names no port, so this half has to keep working: an attach with
  // nothing useful on stderr must still end up owning the device through the table.
  reset(usbip_fixtures::kPortLinuxEmpty);
  input::usbip::set_port_table_patience(std::chrono::milliseconds {1000});
  g_port_outputs.push_back(std::string {usbip_fixtures::kPortLinuxEmpty});     // the pre-attach read
  g_port_outputs.push_back(std::string {usbip_fixtures::kPortLinuxEmpty});     // look 1: too early
  g_port_outputs.push_back(std::string {usbip_fixtures::kPortLinuxAttached});  // look 2: there
  // g_attach_err stays empty - the tool named nothing.

  auto holder = input::usbip::session_holder_t::holding_nothing();
  holder->take_from(request_for_mouse());

  EXPECT_TRUE(holder->holding()) << holder->report();
  holder->release_all();
  EXPECT_EQ(g_detached, (std::vector<int> {0})) << holder->release_failure();
}

// ── and the case that cost tonight: the table answers LATE, past the old budget ─────────────

TEST(UsbipSessionReturn, WaitsOutATableThatAnswersLongAfterTheOldBudgetWouldHaveGivenUp) {
  // Measured on the z13 on 2026-10-06, with the drive really moving: `usbip attach` exits 0 and
  // prints NOTHING on stdout or stderr (the "using port" line is in the binary and is not emitted),
  // while `usbip port` reports an EMPTY table at t+0 and only names the device at about t+3s.
  //
  // The budget this replaced was six looks 250ms apart - 1.25s - so it expired inside that window
  // on every single attach, and a drive with no port cannot be detached. Eight empty reads at
  // 400ms is 3.2s: past the old budget, comfortably inside the new ceiling.
  reset(usbip_fixtures::kPortLinuxAttached);
  input::usbip::set_port_table_patience(std::chrono::milliseconds {10000});
  g_port_outputs.push_back(std::string {usbip_fixtures::kPortLinuxEmpty});  // the pre-attach read
  for (int i = 0; i < 8; ++i) {
    g_port_outputs.push_back(std::string {usbip_fixtures::kPortLinuxEmpty});  // too early, over and over
  }
  // g_attach_err stays empty: this tool names no port, which is what the real one does.

  auto holder = input::usbip::session_holder_t::holding_nothing();
  holder->take_from(request_for_mouse());

  EXPECT_TRUE(holder->holding()) << holder->report();
  EXPECT_NE(holder->report().find("9-1 on port 0"), std::string::npos) << holder->report();

  holder->release_all();
  EXPECT_EQ(g_detached, (std::vector<int> {0})) << holder->release_failure();
}

// ── and it may never leave quietly ──────────────────────────────────────────────────────────

TEST(UsbipSessionReturn, NeverLeavesWithoutSayingWhenTheTableStillWillNotNameTheDevice) {
  // The failure that stranded a drive tonight, in the one form it must never take again: the table
  // reads FINE, never names the device, and the session ends anyway. The release path used to judge
  // "not in the table" to mean "not ours, and ordinary" and said nothing at all - so a drive sat
  // attached to a machine whose stream was gone and the only clue was one line, eight seconds after
  // the stream had started.
  reset(usbip_fixtures::kPortLinuxEmpty);  // readable every time, and empty every time

  auto holder = input::usbip::session_holder_t::holding_nothing();
  holder->take_from(request_for_mouse());
  EXPECT_FALSE(holder->holding()) << holder->report();

  holder->release_all();
  // Nothing was guessed at: a port nobody named is not a port to detach blindly.
  EXPECT_TRUE(g_detached.empty());
  // And it is SAID, naming the device, so the next person has something to act on.
  EXPECT_NE(holder->release_failure().find("9-1"), std::string::npos) << holder->release_failure();
  EXPECT_NE(holder->release_failure().find("not given back"), std::string::npos)
    << holder->release_failure();
}
