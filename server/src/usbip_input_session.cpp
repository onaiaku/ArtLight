/**
 * @file src/usbip_input_session.cpp
 * @brief The devices one stream is holding. Implementation.
 *
 * The one rule this file exists to keep: whatever it takes, it gives back. See the header for why
 * that is a destructor rather than a call.
 */
#include "src/usbip_input_session.h"

#include <algorithm>
#include <chrono>
#include <sstream>
#include <thread>
#include <unordered_set>
#include <utility>

namespace input::usbip {
  namespace {
    /**
     * Split the optional busid allowlist.
     *
     * Tolerant of the ways a person actually types a list into a config file - spaces after the
     * commas, a trailing comma, both. A malformed entry is DROPPED rather than passed on: the
     * policy validates busids again before anything is spawned, but a bad entry that reaches it
     * throws, and throwing out of "which devices did the user ask for" would report a config typo
     * as a failure to attach. Dropping it means the typo costs one device, not the feature.
     */
    std::unordered_set<std::string> parse_allowlist(const std::string &raw) {
      std::unordered_set<std::string> wanted;
      std::stringstream stream {raw};
      std::string piece;
      while (std::getline(stream, piece, ',')) {
        const auto first = piece.find_first_not_of(" \t\r\n");
        if (first == std::string::npos) {
          continue;
        }
        const auto last = piece.find_last_not_of(" \t\r\n");
        const auto trimmed = piece.substr(first, last - first + 1);
        if (is_valid_busid(trimmed)) {
          wanted.insert(trimmed);
        }
      }
      return wanted;
    }

    /// How hard we look for a port before believing there is not one.
    ///
    /// `usbip attach` succeeding does NOT mean the port exists yet: the driver assigns it, and a
    /// read taken in the same breath as the tool that moved the device comes back before the port
    /// is recorded. Measured on the z13 on 2026-10-06: the attach printed NOTHING on either stream
    /// (the `using port` format string is in the binary and is not emitted on success), and
    /// `usbip port` reported an EMPTY table at t+0 and only named the device at t+3s.
    ///
    /// The budget this replaced - six looks 250ms apart, 1.25s in total - sat entirely INSIDE that
    /// window, so it expired before the table could answer, on every attach, and the device went
    /// unported. A device nobody can name cannot be detached, which is the one failure this file
    /// exists to prevent.
    ///
    /// So wait the lag out instead of guessing at it: a ceiling, not a doorman. The loop returns the
    /// moment the table names the device (about 3s on the machine that measured it), and only spends
    /// the ceiling when the answer is genuinely not coming. This runs off the request path, after
    /// the stream's answer has gone out, so the patience is not paid for by the client.
    constexpr std::chrono::milliseconds kPortLookupCeiling {20000};
    constexpr std::chrono::milliseconds kPortLookupPause {400};

    /**
     * What a port lookup found - and, when it found nothing, WHICH nothing.
     *
     * "This machine holds nothing" and "I could not read what this machine holds" are different
     * answers and must never collapse into one. The first is a fact; the second is an admission
     * with a device possibly sitting attached behind it. The policy layer keeps them apart on
     * purpose - see the PortOutcome note about refusing to answer "your hands are empty" - and this
     * carries that distinction out to the caller that has to act on it.
     */
    struct port_lookup_t {
      std::vector<held_device_t> held;
      /// Non-empty when the read itself failed, in the tool's own words. Never composed by us.
      std::string unreadable;
      /// How many times the table was actually read. Carried out to the report so the sentence says
      /// how hard we looked rather than quoting a constant that no longer bounds the search.
      int looks = 0;
    };

    /**
     * Learn which port each of our busids landed on.
     *
     * `usbip attach` does not hand back a port - the port is assigned by the driver and the only
     * way to read it is to ask what this machine now holds. So after attaching we re-read the port
     * table and match entries to the busids we asked for. A busid that does not appear is NOT
     * recorded as held: we did not get a port for it, and inventing one would mean detaching
     * whatever ends up on that number later. It IS recorded as unported, because a device we
     * attached is ours to give back whether or not we can name its port yet.
     */
    port_lookup_t match_ports(const std::vector<std::string> &attached_busids) {
      port_lookup_t lookup;
      if (attached_busids.empty()) {
        return lookup;
      }

      const auto ceiling = std::chrono::steady_clock::now() + port_table_patience();
      while (true) {
        ++lookup.looks;
        const auto listed = run_list_attached();

        if (listed.code != 0) {
          // The read failed. That is an admission, not an empty table - and the tool's own words
          // are the detail, not a bucket name.
          lookup.unreadable = listed.err.empty()
                                ? ("the port table could not be read (client exit " +
                                   std::to_string(listed.code) + ")")
                                : listed.err;
          // A tool that will not run is not a table that has not answered yet. Waiting cannot fix it,
          // so a failing read ends the wait here instead of spending the whole period on it.
          break;
        } else {
          const auto parsed = parse_attached(listed.out);
          if (parsed.outcome == PortOutcome::Devices ||
              parsed.outcome == PortOutcome::NothingAttached) {
            lookup.unreadable.clear();
            lookup.held.clear();
            for (const auto &want : attached_busids) {
              const auto found =
                std::find_if(parsed.devices.begin(), parsed.devices.end(),
                             [&want](const Attached &device) { return device.busid == want; });
              if (found != parsed.devices.end()) {
                lookup.held.push_back(held_device_t {found->busid, found->port});
              }
            }
            if (lookup.held.size() == attached_busids.size()) {
              return lookup;
            }
          } else {
            // The tool refused, or said something we could not place. Carry its sentence.
            lookup.unreadable = describe(parsed.outcome);
            if (!parsed.detail.empty()) {
              lookup.unreadable += ": ";
              lookup.unreadable += parsed.detail;
            }
          }
        }

        // The ceiling is checked AFTER the read, so the first look is always immediate and the last
        // one always counts - a table that answers on the very edge of the window is still read.
        if (std::chrono::steady_clock::now() >= ceiling) {
          break;
        }
        std::this_thread::sleep_for(kPortLookupPause);
      }

      return lookup;
    }

    std::string join_busids(const std::vector<std::string> &busids) {
      std::string joined;
      for (const auto &busid : busids) {
        if (!joined.empty()) {
          joined += ", ";
        }
        joined += busid;
      }
      return joined;
    }
  }  // namespace

  namespace {
    /// See port_table_patience(). One value for every holder, so a test can shorten the wait without
    /// having to find every object that might look.
    std::chrono::milliseconds g_port_table_patience {kPortLookupCeiling};
  }  // namespace

  std::chrono::milliseconds port_table_patience() {
    return g_port_table_patience;
  }

  void set_port_table_patience(const std::chrono::milliseconds patience) {
    g_port_table_patience = patience;
  }

  std::shared_ptr<session_holder_t> session_holder_t::holding_nothing() {
    // A shared_ptr to a private constructor: this is the only way to make one, so nobody can
    // construct a holder by a path that skipped what this file exists to do.
    return std::shared_ptr<session_holder_t>(new session_holder_t {});
  }

  std::shared_ptr<session_holder_t> session_holder_t::attach(const session_request_t &request) {
    // The one-shot form: make the holder and fill it before handing it back.
    //
    // A caller that needs the stream to ANSWER before the devices move - which is the streaming
    // path, because a client that is kept waiting on a USB probe gives up on the whole handshake -
    // uses holding_nothing() and take_from() separately, so the session owns its hold across the
    // whole of the slow part. That is why the two are no longer one function: there is nothing
    // wrong with this form, it just cannot publish a holder in the middle of itself.
    auto holder = holding_nothing();
    holder->take_from(request);
    return holder;
  }

  void session_holder_t::take_from(const session_request_t &request) {
    if (m_Released) {
      // The session let go of this holder before anything was asked of it. There is nothing to
      // give back - and nothing may be taken either: release_all() will not run a second time, so
      // a device put on this holder now would never be given back at all.
      m_Report = "the stream ended before any device was asked for";
      return;
    }

    if (!request.enabled) {
      // The default. Said out loud rather than returning silently, because "sharing is off" and
      // "sharing is on and found nothing" look identical from the outside otherwise.
      m_Report = "USB device sharing is switched off for this stream";
      return;
    }

    if (request.exporter.empty()) {
      m_Report =
        "no exporter address to attach from - the stream's source address was not known and no "
        "override is configured";
      return;
    }

    // The exporter address is the most dangerous input in this feature - it decides which machine
    // this one connects to, and it arrives from a stream's source address rather than from a
    // person. Validate it FIRST, before the machine readiness check and before anything else, so
    // that a bad address is refused on its own merits rather than incidentally by whatever check
    // happens to come first. The policy validates it again when it builds the argv; this is the
    // gate that keeps it out of the feature entirely.
    if (!is_usable_exporter(request.exporter)) {
      m_Report = "refusing an unusable exporter address: '" + request.exporter + "'";
      return;
    }

    // Ask whether this machine could take a device before asking for one. A refusal here is the
    // whole point of the probe: it is the sentence that tells a person what to install, and the
    // alternative is an attach that fails with a tool error they cannot act on.
    const auto availability = client_available();
    if (!availability.ok) {
      m_Report = std::string {"this PC cannot take USB devices yet: "} + describe(availability.state);
      return;
    }

    // What is the exporter offering?
    const auto offered_run = run_list_remote(request.exporter);
    if (offered_run.code != 0) {
      // Both streams are reported, because on this path the reason is on stderr and the exit code
      // only says "no". A reader with only the code cannot tell a wrong address from a busy port.
      m_Report = "could not ask " + request.exporter + " what it is offering (client exit " +
                         std::to_string(offered_run.code) + "): " +
                         (offered_run.err.empty() ? std::string {"no detail"} : offered_run.err);
      return;
    }

    const auto offered = parse_device_list(offered_run.out);
    if (offered.outcome != ListOutcome::Devices) {
      m_Report = "the exporter has nothing to offer: " + describe(offered.outcome);
      return;
    }

    // Decide. plan_reconcile is the policy's, not this file's, so the decision is the same one the
    // tests exercise and the same one the exporter's half makes.
    const auto wanted = parse_allowlist(request.busids);
    std::vector<std::string> wanted_list {wanted.begin(), wanted.end()};

    const auto held_now = parse_attached(run_list_attached().out).devices;
    const auto plan = plan_reconcile(offered.devices, held_now, wanted_list);

    std::vector<std::string> took;
    std::vector<std::string> refused;
    // Devices the attach itself named a port for. Authoritative and immediate - see the comment
    // on the capture below - so these never have to wait on the port table at all.
    std::vector<held_device_t> named;
    for (const auto &action : plan.actions) {
      if (action.action != Action::Attach) {
        // This holder does not release what it did not take. A device found already attached was
        // taken by someone else - often a previous run that died - and giving it back from here
        // would be detaching another session's device. Leftovers are the startup sweep's job,
        // where there is provably no session at all.
        continue;
      }

      // Qualified: the free function that runs the client and classifies the outcome, NOT the
      // static member of the same name that we are inside. Unqualified, this would call itself.
      const auto result = input::usbip::attach(request.exporter, action.busid);
      if (result.outcome == AttachOutcome::Attached) {
        took.push_back(action.busid);

        // The tool named its port. Take it, rather than making the port table prove it - the table
        // is exactly what failed on 2026-10-06: a successful attach printed
        // `usbip: info: using port 8 (vhci_hcd.0)` while `usbip port` showed NOTHING at every look
        // for the whole session, and only listed the device minutes after the session had ended.
        // So the session ended holding a device it could not name - and a device that cannot be
        // named cannot be detached.
        if (result.port >= 0) {
          named.push_back(held_device_t {action.busid, result.port});
        }
      } else {
        // "NOTHING MOVED" cases are collected as well as failures, because a device left behind is
        // the thing the user will ring about, and the sentence that says why is the difference
        // between a two-minute answer and an afternoon.
        //
        // So keep the sentence. The classification alone says which bucket it landed in and
        // throws away the reason the tool actually gave - and a bucket name is not an answer.
        // The failure that cost a night proves it: a polkit refusal classified as Failed printed
        // as "the attach failed" while the real reason, already captured in detail, was dropped
        // here. detail is a line written by the tool, never composed by us.
        std::string why = describe(result.outcome);
        if (!result.detail.empty()) {
          why += ": ";
          why += result.detail;
        }
        refused.push_back(action.busid + " (" + why + ")");
      }
    }

    // What the tool named is authoritative and immediate, so the table is asked ONLY about the
    // devices the tool did NOT name. On a machine whose attach reports its port this costs no
    // lookups and no waiting at all - and a device the tool named can never end up unnamed.
    std::vector<std::string> unnamed;
    for (const auto &busid : took) {
      const auto known = std::find_if(named.begin(), named.end(),
                                      [&busid](const held_device_t &device) { return device.busid == busid; });
      if (known == named.end()) {
        unnamed.push_back(busid);
      }
    }

    const auto lookup = match_ports(unnamed);
    m_Held = named;
    m_Held.insert(m_Held.end(), lookup.held.begin(), lookup.held.end());

    // Anything we attached and could not find a port for is STILL OURS TO GIVE BACK. Recorded here
    // so the release path looks again, when the table has settled - a device we moved must not
    // become a device nobody owns just because one read came back empty.
    m_Unported.clear();
    for (const auto &busid : took) {
      const auto held = std::find_if(m_Held.begin(), m_Held.end(),
                                     [&busid](const held_device_t &device) { return device.busid == busid; });
      if (held == m_Held.end()) {
        m_Unported.push_back(busid);
      }
    }

    // Built once, from what actually happened, so the log line cannot claim more than the holder
    // can give back. This is the audit trail for the promise.
    std::ostringstream report;
    if (m_Held.empty()) {
      report << "no devices were taken";
      if (!took.empty()) {
        // We were told it attached but could not find a port for it. That is the one genuinely
        // alarming state in this file: a device may be held that we have no way to release - and
        // saying WHICH nothing we found is the difference between reading the tool's own sentence
        // and reading our shrug.
        report << ", but " << took.size() << " device(s) reported attached and ";
        if (lookup.unreadable.empty()) {
          report << "neither the attach nor the port table named one after " << lookup.looks
                 << " look(s), so no port could be found for ";
        } else {
          report << "the port table could not be read (" << lookup.unreadable
                 << "), so no port could be found for ";
        }
        report << join_busids(took)
               << " - this machine may be holding them, and they will be looked for again when the "
                  "stream ends";
      }
    } else {
      report << "holding " << m_Held.size() << " device(s) from " << request.exporter << ": ";
      bool first = true;
      for (const auto &device : m_Held) {
        if (!first) {
          report << ", ";
        }
        first = false;
        report << device.busid << " on port " << device.port;
      }
    }

    if (!refused.empty()) {
      report << "; not taken: " << join_busids(refused);
    }

    m_Report = report.str();

    if (m_Released) {
      // The session ended WHILE the devices were being moved - they are slow to move, and a stream
      // can end in the middle of it. That release has already been recorded, and it released a
      // holder that was still empty, because these were not on it yet. The guard in release_all()
      // would make the destructor's call a no-op, so give them back HERE, while there is still
      // something that can. Without this the devices stay on a machine whose stream is gone, which
      // is the exact state this whole file exists to prevent.
      m_Report += "; the stream ended while they were being attached, so they were given straight back";
      give_back_everything();
      m_Held.clear();  // so holding() tells the truth: nothing is held any more
    }
  }

  bool session_holder_t::release(const held_device_t &device) {
    const auto result = run_detach(device.port);
    if (result.code == 0) {
      return true;
    }

    // A failed release is RECORDED, not retried and not thrown. Retrying would block a teardown
    // path on a device that is not coming back; throwing would end the process instead of
    // releasing the rest of them. The port table is the authority, and if the device is still held
    // the next startup sweep finds it - which is exactly what the sweep is for.
    //
    // This module deliberately has no logging dependency, so the failure is carried out in words
    // for the CALLER to log. That is not tidiness: it is why this file compiles and is tested
    // without the full server toolchain, which is the only reason it can be tested at all.
    if (!m_ReleaseFailure.empty()) {
      m_ReleaseFailure += "; ";
    }
    m_ReleaseFailure += "port " + std::to_string(device.port) +
                        (device.busid.empty() ? std::string {} : (" (" + device.busid + ")")) +
                        " was not given back (client exit " + std::to_string(result.code) + "): " +
                        (result.err.empty() ? std::string {"no detail"} : result.err);
    return false;
  }

  void session_holder_t::give_back_everything() {
    for (const auto &device : m_Held) {
      release(device);
    }

    // The last look, and it is a PATIENT one.
    //
    // A device we attached but could not find a port for at the time is read again here, on the way
    // out. This used to be a single read with no waiting, on the reasoning that a teardown must never
    // be slow and that "by now the thing that was too early has long since happened".
    //
    // That reasoning is wrong for the case it was written for. Measured on 2026-10-06: attach at
    // 20:18:22, stream gone at 20:18:30 - eight seconds - and the port table did not name the device
    // inside that window. The single read came back empty, and an empty read is indistinguishable
    // from "this machine holds nothing", so the device was judged not to be ours after all and a
    // drive sat attached until a person detached it by hand. Eight seconds is not a long stream; it
    // is one that was ended the moment somebody saw something was wrong.
    //
    // So wait the table out the same way the take path does, and NEVER leave without a sentence. The
    // cost is bounded, and it is only paid when a device is genuinely unported - which the take
    // path's own ceiling now makes rare. The alternative is somebody's drive going missing with
    // nothing in the log that says so.
    if (m_Unported.empty()) {
      return;
    }

    std::vector<std::string> still_held = m_Unported;
    m_Unported.clear();

    std::string last_reason;
    int looks = 0;
    const auto ceiling = std::chrono::steady_clock::now() + port_table_patience();

    while (true) {
      ++looks;
      const auto listed = run_list_attached();

      if (listed.code != 0) {
        last_reason = listed.err.empty() ? std::string {"no detail"} : listed.err;
        // Same reasoning as the take path: a tool that will not run is not a table that is slow.
        break;
      } else {
        const auto parsed = parse_attached(listed.out);
        if (parsed.outcome == PortOutcome::Devices ||
            parsed.outcome == PortOutcome::NothingAttached) {
          last_reason.clear();
          std::vector<std::string> not_found;
          for (const auto &busid : still_held) {
            const auto found = std::find_if(parsed.devices.begin(), parsed.devices.end(),
                                            [&busid](const Attached &device) { return device.busid == busid; });
            if (found == parsed.devices.end()) {
              not_found.push_back(busid);
              continue;
            }
            release(held_device_t {found->busid, found->port});
          }
          still_held = not_found;
          if (still_held.empty()) {
            return;
          }
        } else {
          last_reason = describe(parsed.outcome);
          if (!parsed.detail.empty()) {
            last_reason += ": ";
            last_reason += parsed.detail;
          }
        }
      }

      if (std::chrono::steady_clock::now() >= ceiling) {
        break;
      }
      std::this_thread::sleep_for(kPortLookupPause);
    }

    // Still ours, and the table would not name it. Say so, with WHICH nothing we found: a device away
    // from the machine it belongs to is the one state a person has to be told about, and it must not
    // be lost to a read that came back empty.
    for (const auto &busid : still_held) {
      record_give_back_failure(
        busid,
        (last_reason.empty() ? std::string {"the port table never named it"} : last_reason) +
          " (after " + std::to_string(looks) + " look(s) over " +
          std::to_string(port_table_patience().count() / 1000) + "s)",
        0);
    }
  }

  void session_holder_t::record_give_back_failure(const std::string &busid, const std::string &reason,
                                                  const int code) {
    if (!m_ReleaseFailure.empty()) {
      m_ReleaseFailure += "; ";
    }
    m_ReleaseFailure += "the device on " + busid + " was not given back" +
                        (code == 0 ? std::string {}
                                   : (" (client exit " + std::to_string(code) + ")")) +
                        ": " + reason;
  }

  void session_holder_t::release_all() {
    if (m_Released) {
      return;
    }
    m_Released = true;

    give_back_everything();
  }

  session_holder_t::~session_holder_t() {
    // The promise. Every end path reaches here, which is the entire reason the devices are owned
    // by an object instead of released by a call.
    release_all();
  }

  std::string session_holder_t::report() const {
    return m_Report.empty() ? "no devices were taken" : m_Report;
  }

  std::string sweep_leftovers() {
    const auto availability = client_available();
    if (!availability.ok) {
      // Not a failure. A machine that cannot attach also cannot be holding anything.
      return std::string {"not swept: "} + describe(availability.state);
    }

    const auto listed = run_list_attached();
    const auto parsed = parse_attached(listed.out);

    if (parsed.outcome == PortOutcome::NothingAttached) {
      return "nothing was left behind by a previous run";
    }
    if (parsed.outcome != PortOutcome::Devices) {
      return std::string {"could not read what this machine holds: "} + describe(parsed.outcome);
    }

    // Everything the kernel says is attached is a leftover here, because this runs at startup
    // before any session exists. There is no session that could legitimately own one yet.
    std::vector<std::string> released;
    std::vector<std::string> stuck;
    for (const auto &device : parsed.devices) {
      const auto result = run_detach(device.port);
      const auto name = device.busid.empty() ? ("port " + std::to_string(device.port)) : device.busid;
      if (result.code == 0) {
        released.push_back(name);
      } else {
        stuck.push_back(name);
      }
    }

    std::ostringstream report;
    if (released.empty() && stuck.empty()) {
      return "nothing was left behind by a previous run";
    }
    if (!released.empty()) {
      report << "gave back " << released.size()
             << " device(s) a previous run had left attached ("
             << "a stream that died before it could release them"
             << "): " << join_busids(released);
    }
    if (!stuck.empty()) {
      if (!released.empty()) {
        report << "; ";
      }
      report << "could NOT give back " << join_busids(stuck)
             << " - the machine they are plugged into is still missing them";
    }
    return report.str();
  }
}  // namespace input::usbip
