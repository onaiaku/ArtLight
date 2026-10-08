/**
 * @file src/quit_request.h
 * @brief End the stream the quit combo was pressed on, and nobody else's.
 *
 * The quit combo ends a stream and is normally read by the CLIENT. Sharing a keyboard over
 * USB/IP takes that keyboard away from the client's machine, so the client cannot see the combo
 * any more and this end has to catch it instead.
 *
 * This is the whole action, in one call, so no trigger has to know how a session is found or
 * which of two clients is the right one. The decision lives in quit_request_policy.h; this file
 * is the part that touches the running server.
 */
#pragma once

#include <string>

namespace quit_request {
  /**
   * @brief End the stream of the one client that is holding a shared device.
   *
   * Ends THAT client's sessions and leaves every other client watching the same stream alone,
   * which is the entire requirement: two clients can watch one stream, and only the one the
   * combo was pressed on should leave.
   *
   * Never throws, and safe to call with nothing streaming.
   *
   * @param reason Short phrase naming what asked for this, for the log - e.g. "quit hotkey".
   * @return True if a client's sessions were stopped. False when nothing was shared, when more
   *         than one client was holding devices, or when the disconnect found nothing to stop.
   */
  bool end_client_holding_shared_devices(const std::string &reason);
}  // namespace quit_request
