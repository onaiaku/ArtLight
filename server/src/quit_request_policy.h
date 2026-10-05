/**
 * @file src/quit_request_policy.h
 * @brief Which client's stream the quit combo should end, decided on its own.
 *
 * The quit combo ends a stream, and it is normally read by the CLIENT: the keypress happens on
 * the machine the person is sitting at and the client ends its own stream. Sharing a keyboard
 * over USB/IP takes that keyboard away from the client's machine, so the client can no longer
 * see the combo at all - it arrives here instead, as a real keyboard. So this end has to catch
 * it, and end the right stream.
 *
 * Catching it is the easy half. Knowing WHOSE stream to end is the half that can be got wrong,
 * and the wrong answer is somebody else's stream vanishing: two clients can watch one stream,
 * and only the one the combo was pressed on should leave. So the decision lives here, with no
 * server, no session and no platform underneath it - the part that has to be right is the part
 * that can be tested without two machines and a shared keyboard.
 *
 * The rule, in full:
 *
 *   - No live client is holding a shared device -> resolve nothing. If nothing is shared then
 *     the combo never reached this end, so the client is still handling it locally, and acting
 *     here would end a stream nobody asked to end.
 *   - Exactly one client is holding a shared device -> that client. A person is sitting at one
 *     machine, and the keyboard they pressed it on is one that was taken from that machine.
 *   - More than one -> resolve nothing, and say so. This cannot arise while one person is
 *     sitting at one machine, because a keyboard is taken from the machine it is plugged into
 *     and given back when the stream ends. If it ever does arise, ending the wrong stream is
 *     worse than ending none, and the log line names the clients that were holding devices so
 *     the case is recognisable rather than mysterious.
 *
 * The single-client case is unambiguous rather than lucky: only one machine's keyboard is
 * shared at a time, which is why one entry means one answer.
 */
#pragma once

#include <string>
#include <vector>

namespace quit_request {
  /**
   * @brief What the decision came to, and why.
   *
   * The reason is not decoration. A combo that does nothing looks identical whether the rule
   * declined to act or the feature is broken, and that is the state that costs an afternoon to
   * diagnose. So every outcome carries a sentence, including the one that acted - because "it
   * worked, and here is why" is what makes the other case readable.
   */
  struct resolution_t {
    bool resolved = false;    ///< True only when client_uuid names exactly one client to end.
    std::string client_uuid;  ///< The client whose stream should end. Empty unless resolved.
    std::string reason;       ///< In words, never empty. Belongs in the log either way.
  };

  /**
   * @brief Decide which client's stream the quit combo should end.
   *
   * @param clients_holding_devices The client uuid of every live session currently holding at
   *        least one imported USB device. Order is not significant; empty entries are ignored,
   *        and one client named more than once is still one client.
   * @return A resolution. `resolved` is true only when exactly one client is named.
   */
  resolution_t resolve(const std::vector<std::string> &clients_holding_devices);
}  // namespace quit_request
