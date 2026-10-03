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
}  // namespace input::usbip
