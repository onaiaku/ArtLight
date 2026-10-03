/**
 * @file src/usbip_input_session.cpp
 * @brief The devices one stream is holding. Implementation.
 *
 * The one rule this file exists to keep: whatever it takes, it gives back. See the header for why
 * that is a destructor rather than a call.
 */
#include "src/usbip_input_session.h"

#include <algorithm>
#include <sstream>
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

    /**
     * Learn which port each of our busids landed on.
     *
     * `usbip attach` does not hand back a port - the port is assigned by the driver and the only
     * way to read it is to ask what this machine now holds. So after attaching we re-read the port
     * table and match entries to the busids we asked for. A busid that does not appear is NOT
     * recorded: we did not get a port for it, so we cannot claim to hold it, and inventing one
     * would mean detaching whatever ends up on that number later.
     */
    std::vector<held_device_t> match_ports(const std::vector<std::string> &attached_busids) {
      const auto listed = run_list_attached();
      const auto parsed = parse_attached(listed.out);

      std::vector<held_device_t> held;
      for (const auto &want : attached_busids) {
        const auto found = std::find_if(parsed.devices.begin(), parsed.devices.end(),
                                        [&want](const Attached &device) { return device.busid == want; });
        if (found != parsed.devices.end()) {
          held.push_back(held_device_t {found->busid, found->port});
        }
      }
      return held;
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

  std::shared_ptr<session_holder_t> session_holder_t::attach(const session_request_t &request) {
    // A shared_ptr to a private constructor: this is the only way to make one, so nobody can
    // construct a holder that skipped the attaching and still believes it owns nothing.
    auto holder = std::shared_ptr<session_holder_t>(new session_holder_t {});

    if (!request.enabled) {
      // The default. Said out loud rather than returning silently, because "sharing is off" and
      // "sharing is on and found nothing" look identical from the outside otherwise.
      holder->m_Report = "USB device sharing is switched off for this stream";
      return holder;
    }

    if (request.exporter.empty()) {
      holder->m_Report =
        "no exporter address to attach from - the stream's source address was not known and no "
        "override is configured";
      return holder;
    }

    // The exporter address is the most dangerous input in this feature - it decides which machine
    // this one connects to, and it arrives from a stream's source address rather than from a
    // person. Validate it FIRST, before the machine readiness check and before anything else, so
    // that a bad address is refused on its own merits rather than incidentally by whatever check
    // happens to come first. The policy validates it again when it builds the argv; this is the
    // gate that keeps it out of the feature entirely.
    if (!is_usable_exporter(request.exporter)) {
      holder->m_Report = "refusing an unusable exporter address: '" + request.exporter + "'";
      return holder;
    }

    // Ask whether this machine could take a device before asking for one. A refusal here is the
    // whole point of the probe: it is the sentence that tells a person what to install, and the
    // alternative is an attach that fails with a tool error they cannot act on.
    const auto availability = client_available();
    if (!availability.ok) {
      holder->m_Report = std::string {"this PC cannot take USB devices yet: "} + describe(availability.state);
      return holder;
    }

    // What is the exporter offering?
    const auto offered_run = run_list_remote(request.exporter);
    if (offered_run.code != 0) {
      // Both streams are reported, because on this path the reason is on stderr and the exit code
      // only says "no". A reader with only the code cannot tell a wrong address from a busy port.
      holder->m_Report = "could not ask " + request.exporter + " what it is offering (client exit " +
                         std::to_string(offered_run.code) + "): " +
                         (offered_run.err.empty() ? std::string {"no detail"} : offered_run.err);
      return holder;
    }

    const auto offered = parse_device_list(offered_run.out);
    if (offered.outcome != ListOutcome::Devices) {
      holder->m_Report = "the exporter has nothing to offer: " + describe(offered.outcome);
      return holder;
    }

    // Decide. plan_reconcile is the policy's, not this file's, so the decision is the same one the
    // tests exercise and the same one the exporter's half makes.
    const auto wanted = parse_allowlist(request.busids);
    std::vector<std::string> wanted_list {wanted.begin(), wanted.end()};

    const auto held_now = parse_attached(run_list_attached().out).devices;
    const auto plan = plan_reconcile(offered.devices, held_now, wanted_list);

    std::vector<std::string> took;
    std::vector<std::string> refused;
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
      } else {
        // "NOTHING MOVED" cases are collected as well as failures, because a device left behind is
        // the thing the user will ring about, and the sentence that says why is the difference
        // between a two-minute answer and an afternoon.
        refused.push_back(action.busid + " (" + describe(result.outcome) + ")");
      }
    }

    holder->m_Held = match_ports(took);

    // Built once, from what actually happened, so the log line cannot claim more than the holder
    // can give back. This is the audit trail for the promise.
    std::ostringstream report;
    if (holder->m_Held.empty()) {
      report << "no devices were taken";
      if (!took.empty()) {
        // We were told it attached but could not find a port for it. That is the one genuinely
        // alarming state in this file: a device may be held that we have no way to release.
        report << ", but " << took.size()
               << " device(s) reported attached and no port could be found for them - this machine "
                  "may be holding "
               << join_busids(took) << " with no way to give it back";
      }
    } else {
      report << "holding " << holder->m_Held.size() << " device(s) from " << request.exporter << ": ";
      bool first = true;
      for (const auto &device : holder->m_Held) {
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

    holder->m_Report = report.str();
    return holder;
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

  session_holder_t::~session_holder_t() {
    // The promise. Every end path reaches here, which is the entire reason the devices are owned
    // by an object instead of released by a call.
    for (const auto &device : m_Held) {
      release(device);
    }
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
