/**
 * @file src/usbip_input.h
 * @brief The importer's per-platform edge. Everything that DECIDES anything lives in
 *        usbip_input_policy.h; this header is only the part that has to look at a real machine.
 *
 * The split is deliberate and it is the whole reason this feature can be trusted. The platform
 * layer answers questions ("where is the client", "is the driver loaded") and nothing else. The
 * policy layer turns those answers into a verdict and a sentence, and it runs in CI on a machine
 * that has no client, no driver and no second PC. A decision that lives in a platform file is a
 * decision that can only be tested on the one platform it was written for, which is how a Windows
 * result came to be written down as a universal one in this plan's first draft.
 *
 * Terminology: the machine a device is plugged into is the EXPORTER, the machine it arrives on is
 * the IMPORTER. This file is the importer's. Never write "host" or "client" bare — see
 * ArtMoon/docs/usb-ip-input-passthrough.md.
 */
#pragma once

#include <string>

#include "src/usbip_input_policy.h"

namespace input::usbip {
  /**
   * @brief Where this platform's USB/IP client lives, or empty if it is not installed.
   *
   * The two platforms answer this differently and the difference is not cosmetic. On Windows the
   * client is deliberately NOT on PATH — measured on the reference machine, a PATH lookup reports
   * "not installed" for a fully working install — so Windows checks the location our installer
   * uses and then the uninstall record. On Linux it is an ordinary system binary and is resolved
   * the ordinary way.
   *
   * @return An absolute path, or empty. Never a bare name that would need PATH to mean anything.
   */
  std::string client_path();

  /**
   * @brief Gather the facts about this machine that decide whether a device could arrive.
   *
   * Runs nothing privileged and changes nothing. A field this platform cannot answer is left
   * false rather than guessed at.
   */
  ClientProbe probe_client();

  /**
   * @brief The verdict: is the client usable here, and if not, exactly what is missing.
   *
   * Portable, and defined here because it is the same judgement on both platforms — only the
   * facts differ. Callers use this, not probe_client(), so the decision cannot be made twice in
   * two different ways.
   */
  inline ClientAvailability client_available() {
    return assess_client(probe_client());
  }

  /**
   * @brief What a run of the client produced.
   *
   * stdout and stderr are kept APART, not merged, and that is deliberate on both platforms. The
   * reason a tool refused is on stderr while the exit code only says "no", so a merged buffer turns
   * a diagnosable refusal into an anonymous one. This mirrors the exporter half, where a shell
   * pipeline that merged the two made a held device look like an unreadable one.
   */
  struct run_result_t {
    int code = 0;
    std::string out;
    std::string err;
  };

  /**
   * @brief Take one device from the exporter. Blocking; the caller decides where it runs.
   *
   * Windows drives the client directly as the logged-on user - proven: a UAC-filtered limited token
   * attached successfully. Linux CANNOT, because `/sys/devices/platform/vhci_hcd.0/attach` is a
   * root-only write, so there this goes through the privileged helper and the polkit action beside
   * it. The asymmetry is real and is not to be smoothed over by giving Windows a helper it
   * provably does not need.
   */
  run_result_t run_attach(std::string_view exporter, std::string_view busid);

  /**
   * @brief Ask the exporter what it is offering.
   *
   * Read-only and unprivileged on both platforms: it is a query, not an action, so it needs neither
   * the helper nor elevation. Runs the client with the two streams kept apart.
   */
  run_result_t run_list_remote(std::string_view exporter);

  /**
   * @brief Ask this machine what it is currently holding.
   *
   * This is the ONLY way to learn a held device's port, and the port is the only thing `detach`
   * accepts - so a session that attaches and does not read this back cannot give the device up.
   * Read-only and unprivileged on both platforms, proven: on Linux `usbip port` printed a full
   * port table to an unprivileged user on the reference box.
   */
  run_result_t run_list_attached();

  /**
   * @brief Give one device back. Takes a PORT, not a busid - `detach` accepts nothing else, which
   *        is why the caller has to remember busid -> port for the life of the session.
   */
  run_result_t run_detach(int port);

  /**
   * @brief Attach, and say what actually happened. The judgement is the policy's, not this file's,
   *        so the same output is read the same way wherever it came from.
   */
  inline AttachResult attach(std::string_view exporter, std::string_view busid) {
    const auto result = run_attach(exporter, busid);
    return classify_attach(result.code, result.out, result.err);
  }
}  // namespace input::usbip
