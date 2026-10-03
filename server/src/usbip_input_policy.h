/**
 * @file src/usbip_input_policy.h
 * @brief Portable policy for the USB/IP importer: busid validation, exporter output parsing,
 *        and the attach/detach plan.
 *
 * Nothing in this header touches the operating system, spawns a process, or knows which
 * platform it is running on. That is deliberate. The parse is the part that fails quietly —
 * a device that never arrives, behind a log line that says it did — so it is the part that
 * has to be testable without a driver, a network, or a second machine.
 *
 * Terminology is not cosmetic here; see ArtMoon/docs/usb-ip-input-passthrough.md. The machine
 * a device is plugged into is the EXPORTER, the machine it arrives on is the IMPORTER. Never
 * write "host" or "client" bare: usbip names its roles after the device side and streaming
 * names them after the video, and on this pair of products the two are inverted.
 */
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace input::usbip {
  /**
   * @brief Whether a value is a busid of the shape usbip emits, e.g. "3-10" or "9-3".
   *
   * Two properties matter beyond tidiness. The first character must be a digit, so a value
   * can never be read as an option by usbip ("-p", "--help"). And nothing outside [0-9-] may
   * appear, so a busid arriving from the exporter can never carry whitespace, a path, a
   * newline, or a shell metacharacter into an argument vector.
   *
   * The digits are deliberately not range-checked and leading zeros are not rejected:
   * refusing a device the exporter really is offering is a worse failure than handing a
   * malformed busid to usbip, which will simply report no such device.
   *
   * @param busid Candidate, as received from the exporter.
   * @return true for exactly one run of digits, a single '-', and another run of digits.
   */
  bool is_valid_busid(std::string_view busid);

  /// One device an exporter is offering.
  struct Device {
    std::string busid;    ///< "9-1". Guaranteed to have passed is_valid_busid.
    std::string vendor;   ///< "Razer USA, Ltd". Empty when the row carried no vendor.
    std::string product;  ///< "unknown product" is a normal value here, not a parse failure.
    std::string vid_pid;  ///< "1532:007b", lowercased. Empty when the row carried none.
    std::vector<std::string> interfaces;  ///< Per-interface detail rows, verbatim, colon stripped.
  };

  /**
   * @brief What a list call actually established.
   *
   * Four states rather than "devices or not", because two of them get collapsed by accident
   * and the collapse is silent. "The exporter is offering nothing" is a fact about the
   * exporter. "We could not read the reply" is a fact about us. A user told the first when the
   * second is true will go and check the wrong machine.
   */
  enum class ListOutcome {
    Devices,        ///< Read successfully; one or more devices are offered.
    NothingOffered, ///< Read successfully; the exporter is genuinely offering nothing.
    NoOutput,       ///< Nothing came back at all. The caller's own timeout decides what that means.
    Unreadable,     ///< Bytes came back and this build could not place them.
  };

  struct DeviceList {
    ListOutcome outcome = ListOutcome::NoOutput;
    std::vector<Device> devices;
    /// For Unreadable: the first line that could not be placed, or the tool's own error line.
    /// Empty for every other outcome.
    std::string detail;
  };

  /**
   * @brief Read one device list, in either tool's shape.
   *
   * Two shapes have to parse, and they are genuinely different:
   *
   *   Linux   " - busid 3-10 (8087:0033)" then an indented "Intel Corp. : AX211 Bluetooth"
   *   Windows "    9-1    : Razer USA, Ltd : unknown product (1532:028d)" then indented ":" rows
   *
   * The busid decides which: on the Windows row it sits before the separator, on the Linux one
   * after the word "busid". Continuation rows carrying only interface detail are attached to
   * the device above them and are never mistaken for new devices.
   *
   * An empty document is NoOutput rather than NothingOffered, on purpose. This function cannot
   * see the exit code or the clock, and from output alone "the exporter is asleep" and "the
   * exporter is offering nothing" are indistinguishable — the asleep case produces the same
   * zero bytes as the clean-empty case, only after hanging indefinitely. The caller combines
   * this with its own timeout and exit status to choose. See
   * tests/fixtures/captured-2026-10-03-network-probe.md.
   *
   * @param output Everything the tool wrote to stdout, with stderr appended by the caller.
   * @return The outcome, the devices if any, and for Unreadable the line that defeated us.
   */
  DeviceList parse_device_list(std::string_view output);

  /// @brief Render an outcome as words, for a log line or a status row. Never empty.
  std::string describe(ListOutcome outcome);

  /// One device this machine currently holds.
  struct Attached {
    /// Remote busid, e.g. "9-1". Validated, so it is safe in an argument vector.
    ///
    /// Legitimately EMPTY on Linux: when the tool cannot read a port's record it prints the
    /// port and no remote identity at all. That is not a parse failure, and it does not stop
    /// us, because detach takes a port.
    std::string busid;
    /// Hub port number. usbip-win2 numbers from 1 and asserts non-zero; the Linux tool numbers
    /// from 0. So 0 is a valid value here and must not be treated as "unset".
    int port = 0;
    std::string exporter;  ///< Host from the "usbip://HOST:SERVICE/BUSID" row.
    std::string service;   ///< That row's TCP port, e.g. "3240".
    std::string speed;     ///< "High Speed(480Mbps)", verbatim. Empty when the row had none.
    std::string product;   ///< The description row, verbatim.
    std::string serial;    ///< Only present when the attach carried one. Usually empty.
    std::string mode;      ///< "zero-copy" or "low-latency". Empty when the row had none.
  };

  /**
   * @brief What a `port` call actually established.
   *
   * The same four states as ListOutcome, for the same reason, plus one asymmetry that is easy
   * to get wrong in the dangerous direction:
   *
   *   usbip-win2 prints its banner ONLY when it finds a device. So with nothing attached it
   *   prints ZERO BYTES and exits 0 — byte-identical to a call that never reached the driver.
   *   The Linux tool prints the banner unconditionally, so its empty case is distinguishable.
   *
   * That means on Windows this parse alone cannot tell "holds nothing" from "could not ask";
   * the exit code separates them. It is read as NoOutput here and reclassified by the caller,
   * exactly as parse_device_list is, because a machine wrongly reported as holding nothing is
   * a device that never gets given back.
   */
  enum class PortOutcome {
    Devices,          ///< Read successfully; this machine holds devices.
    NothingAttached,  ///< Read successfully; this machine holds none.
    NoOutput,         ///< Zero bytes. Exit 0 means NothingAttached; non-zero means we could not ask.
    Unreadable,       ///< Bytes came back and this build could not place them.
  };

  struct AttachedList {
    PortOutcome outcome = PortOutcome::NoOutput;
    std::vector<Attached> devices;
    /// For Unreadable: the tool's own error lines if it named any, else the row that defeated us.
    std::string detail;
  };

  /**
   * @brief Read what this machine currently holds, in either tool's shape.
   *
   * Two shapes, and they disagree in two places that matter:
   *
   *   Windows  "Port 01: device in use at High Speed(480Mbps)"     9-space description indent
   *            "           -> usbip://192.168.50.35:3240/9-1"       11-space detail indent
   *
   *   Linux    "Port 00: <Port in Use> at High Speed(480Mbps)"     7-space description indent
   *            "       9-1 -> usbip://192.168.50.35:3240/9-1"       the busid leads the row
   *
   * Both put the port number on the "Port NN:" row and the remote busid as the last path
   * segment of the "usbip://" URL, so this reads those two anchors rather than counting
   * columns — the layouts differ and the columns are not the contract.
   *
   * A device whose remote record could not be read prints no URL row at all (a real Linux
   * state, "unknown host, remote port and remote busid"). It is kept, with its port and an
   * empty busid, because the port is what releases it.
   *
   * @param output Everything the tool wrote to stdout, with stderr appended by the caller.
   * @return The outcome, the devices if any, and for Unreadable what defeated us.
   */
  AttachedList parse_attached(std::string_view output);

  /// @brief Render a port outcome as words. Never empty.
  std::string describe(PortOutcome outcome);

  /// What the importer should do about one device.
  enum class Action {
    Attach,  ///< Fetch it: offered, wanted, and this machine does not hold it.
    Detach,  ///< Give it back: this machine holds it and it is no longer wanted or offered.
  };

  /// @brief Render an action as words, for a log line. Never empty.
  std::string describe(Action action);

  /**
   * @brief One thing to do, and why.
   *
   * The reason is carried because this plan is what a log line is written from, and "detached
   * 9-1" without the reason is the kind of entry that makes a later reader guess.
   */
  struct PlannedAction {
    Action action = Action::Attach;
    /// For Attach: the device to fetch, validated. For Detach: the device being released, and
    /// legitimately EMPTY when the tool could not read the port's remote identity.
    std::string busid;
    /// For Detach: the hub port to release, which is the ONLY thing usbip detach accepts.
    /// -1 for Attach, which is addressed by busid.
    int port = -1;
    /// Why this action is in the plan, in words. Never empty.
    std::string reason;
  };

  struct Plan {
    /// Detaches first, then attaches, each in the order its input arrived.
    std::vector<PlannedAction> actions;

    bool empty() const { return actions.empty(); }
    std::size_t attach_count() const;
    std::size_t detach_count() const;
  };

  /**
   * @brief Decide what to do, given what is offered, what is held, and what is wanted.
   *
   * Four states, and each is a real one on a real pair of machines:
   *
   *   offered, wanted, not held        -> attach
   *   held, offered, wanted            -> leave alone (already right)
   *   held, not wanted                 -> detach (the user unticked it)
   *   held, no longer offered          -> detach (the exporter unbound it, or this is a stale
   *                                       hold from a session that did not end cleanly)
   *   offered, not wanted              -> leave alone. ArtMoon binds only what is ticked, so in
   *                                       practice everything offered is wanted; this branch
   *                                       exists for the case where they disagree.
   *
   * **`wanted` empty means every offered device is wanted.** That is not a convenience: ArtMoon
   * binds exactly what the user toggled on, so "what the exporter is offering" IS the user's
   * answer. The parameter exists so a caller that knows better can say so, not so every caller
   * has to repeat the list back.
   *
   * **A held device whose busid is empty is detached.** Linux prints no remote identity for a
   * port whose record it cannot read, so such an entry cannot be matched against `wanted` at
   * all. Giving it back is the safe direction: the device returns to the machine it is plugged
   * into, and it can always be attached again. The alternative - leaving it held because we
   * cannot name it - is a device that never goes home.
   *
   * **Detach is by PORT, never by busid.** `usbip attach` assigns a vhci port and
   * `usbip detach -p <port>` is the only way back, so every Detach action carries the port it
   * was read with. Attach is by busid, because the port does not exist until it succeeds.
   *
   * Deterministic and idempotent: the same three inputs always produce the same plan, in the
   * same order, so running it twice cannot double-attach anything. Detaches are ordered before
   * attaches so that a swap (the user wants B instead of A) frees A's port before B needs one.
   *
   * @param offered What the exporter says it is offering, in the order it was read.
   * @param attached What this machine currently holds, in the order it was read.
   * @param wanted The busids the user wants. Empty means "everything offered".
   * @return The actions to take, and the reason for each.
   */
  Plan plan_reconcile(const std::vector<Device> &offered,
                      const std::vector<Attached> &attached,
                      const std::vector<std::string> &wanted);

  /**
   * @brief Build the argument vector to fetch one device, or throw refusing to.
   *
   * The shape is identical on both tools - `attach -r <exporter> -b <busid>` - so this is
   * portable, and only element 0 differs: it is written here as the bare name `usbip`, and the
   * platform layer REPLACES it with the path it resolved, because on Windows the client is
   * deliberately not on PATH.
   *
   * Always a vector, never a shell string, so nothing can be re-split on the way to exec.
   *
   * @throws std::invalid_argument if the busid is not a valid busid, or the exporter is empty,
   *         carries whitespace, or begins with '-' and would be read as an option.
   */
  std::vector<std::string> build_attach_argv(std::string_view exporter, std::string_view busid);

  /**
   * @brief Build the argument vector to give one device back, or throw refusing to.
   *
   * @throws std::invalid_argument if the port is negative. Note that **port 0 is valid**: the
   *         Linux tool numbers ports from 0, and treating 0 as "unset" would make the first
   *         port on every Linux machine impossible to release.
   */
  std::vector<std::string> build_detach_argv(int port);

  /**
   * @brief What this machine looks like, as facts. Gathered by the platform layer, judged here.
   *
   * Splitting the gathering from the judging is what makes this testable: the decision that
   * actually matters — "is this machine ready, and if not, exactly what is missing" — runs in CI
   * on a machine that has no client and no driver. A field the platform cannot answer is left
   * false rather than faked, which is why `privilege_required` exists instead of every platform
   * pretending its privileged path is ready.
   */
  struct ClientProbe {
    bool client_found = false;      ///< The CLI binary exists where it is supposed to be.
    std::string client_path;        ///< Where it was found, for the log. Empty when not found.
    bool driver_present = false;    ///< usbip2_ude present (Windows) / vhci_hcd loaded (Linux).
    bool driver_loadable = false;   ///< Linux only: the module exists but is not loaded yet.
    bool privilege_required = false;///< Linux only: attach writes a root-only sysfs node.
    bool helper_present = false;    ///< Linux only: the helper binary AND its polkit policy.
  };

  /**
   * @brief Why the client is or is not usable.
   *
   * Five states, because four of them are a different thing to do about it, and collapsing any
   * pair of them sends someone to fix the wrong machine. In particular a module that is present
   * and merely unloaded is NOT the same as a driver that is missing: one is a load, the other is
   * a reinstall.
   */
  enum class ClientState {
    Ready,           ///< Everything this platform needs is in place.
    NotInstalled,    ///< No client at all. A setup step, not an error.
    DriverMissing,   ///< The client is here, its driver is not and cannot be loaded.
    DriverNotLoaded, ///< The driver exists but is not loaded. Fixable without reinstalling anything.
    HelperMissing,   ///< Client and driver are fine; the privileged path attach needs is not set up.
  };

  /// The verdict, and a sentence a human can act on rather than a code to look up.
  struct ClientAvailability {
    bool ok = false;
    ClientState state = ClientState::NotInstalled;
    std::string reason;
  };

  /**
   * @brief Whether an uninstall record's DisplayName names the USB/IP CLIENT.
   *
   * The Windows exporter and the Windows importer install two DIFFERENT products whose names both
   * begin "usbip": the importer's client is `USBip`, the exporter's tool is `usbipd-win`. Both are
   * installed on the reference machines.
   *
   * A plain substring match puts `usbipd-win` forward as a candidate. Measured on the reference
   * exporter, that candidate is then rejected downstream - the install directory is required to
   * contain `usbip.exe` - so the loose match does NOT currently produce a wrong answer. The reason
   * to name the product instead is that the loose match makes the right answer depend on what
   * happens to be in a folder rather than on what the product is called, and that holds only until
   * usbipd-win ships a file with that name.
   *
   * The rule is on the NAME: a case-insensitive `usbip` whose next character is not a letter or a
   * digit. "USBip 0.9.8.1" is the client. "usbipd-win" is not.
   */
  bool names_the_client(std::string_view display_name);

  ClientAvailability assess_client(const ClientProbe &probe);
  std::string describe(ClientState state);
}  // namespace input::usbip
