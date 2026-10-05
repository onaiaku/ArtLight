/**
 * @file src/quit_request.cpp
 * @brief The quit combo's action: find the one client holding a shared device, end that one.
 */
#include "src/quit_request.h"

#include "src/logging.h"
#include "src/quit_request_policy.h"
#include "src/rtsp.h"

#include <boost/log/trivial.hpp>

#include <string>
#include <vector>

using namespace std::literals;

namespace quit_request {
  bool end_client_holding_shared_devices(const std::string &reason) {
    // The server hands these back as a list and the policy takes a vector. Neither choice should
    // reach the other, so the conversion happens here, once, at the boundary. Built explicitly
    // rather than braced: `{first, last}` reads as two iterators in a list, not as a range.
    const auto holders = rtsp_stream::clients_holding_usbip_devices();
    const std::vector<std::string> clients(holders.begin(), holders.end());

    const auto decision = resolve(clients);

    if (!decision.resolved) {
      // Declining is a normal outcome, not a failure, and it must not read like one. Without the
      // reason, "nothing happened" and "this is broken" are the same line in a log, and that is
      // the state that costs an afternoon to diagnose.
      BOOST_LOG(info) << "Quit combo ("sv << reason << "): "sv << decision.reason;
      return false;
    }

    // One client, named by uuid. disconnect_client_sessions stops the sessions whose client this
    // is and leaves the others on the same stream running - which is the difference between the
    // person who pressed the combo going back to their own screen and both of them being kicked.
    const bool ended = rtsp_stream::disconnect_client_sessions(decision.client_uuid);
    BOOST_LOG(info) << "Quit combo ("sv << reason << "): ending "sv << decision.client_uuid
                    << " - "sv << decision.reason;
    return ended;
  }
}  // namespace quit_request
