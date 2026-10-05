/**
 * @file src/quit_hotkey.cpp
 * @brief One answer to "what combination" and "may we watch for it", for both platforms.
 *
 * The two triggers are as different as they can be: Windows reserves the combination with the
 * OS and is told when it is pressed, Linux reads the shared keyboard itself and works it out
 * event by event. That difference is real and this file does not try to paper over it. What it
 * does is refuse to let the difference spread: the combination is parsed once here, and the
 * question of when the host is allowed to be watching is answered once here, so neither platform
 * gets its own opinion about either.
 *
 * should_watch() is deliberately the only gate, and deliberately asks for the session count with
 * no cleanup - session_count_no_cleanup() rather than session_count() - because a trigger making
 * a scheduling decision of its own must not enter session teardown in order to make it.
 *
 * On Linux the watcher is driven by a thread that polls that one answer and opens or closes the
 * shared keyboards accordingly, so the host holds nothing open while it has nothing to do. That
 * thread is started the first time a combination is applied rather than at some startup point a
 * caller has to remember, because a startup point a caller has to remember is a startup point
 * somebody eventually forgets.
 */
#include "src/quit_hotkey.h"

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>

#include <boost/log/trivial.hpp>

#include "src/quit_hotkey_parse.h"
#include "src/rtsp.h"

#ifdef _WIN32
  #include "src/platform/windows/hotkey_manager.h"
#else
  #include "src/platform/linux/quit_hotkey_device.h"
  #include "src/platform/linux/quit_hotkey_linux.h"
  #include "src/quit_request.h"
#endif

namespace quit_hotkey {
  bool should_watch() {
    // The single gate. Both triggers ask this and nothing else, so they cannot disagree about
    // when the host is allowed to be listening.
    return rtsp_stream::session_count_no_cleanup() > 0;
  }
}  // namespace quit_hotkey

#ifdef _WIN32

namespace quit_hotkey {
  namespace {
    // MOD_ALT, MOD_CONTROL, MOD_SHIFT and the VK_F1 base, from <winuser.h>. Repeated rather than
    // included, for the same reason the Linux file repeats its evdev codes: these are ABI, and
    // this file has no business pulling in windows.h to state four numbers.
    constexpr unsigned int kModAlt = 0x0001;
    constexpr unsigned int kModControl = 0x0002;
    constexpr unsigned int kModShift = 0x0004;
    constexpr int kVirtualKeyF1 = 0x70;

    int virtual_key_for(const parse_result_t &parsed) {
      if (parsed.function_key >= 1 && parsed.function_key <= 24) {
        return kVirtualKeyF1 + (parsed.function_key - 1);
      }
      // For a letter or a digit the virtual-key code IS the character's own value, so no table
      // is needed here - unlike evdev, where the letters are deliberately not in alphabetical
      // order and a table is the only honest way to state them.
      return static_cast<int>(parsed.key);
    }

    unsigned int modifiers_for(const parse_result_t &parsed) {
      unsigned int modifiers = 0;
      if (parsed.alt) {
        modifiers |= kModAlt;
      }
      if (parsed.ctrl) {
        modifiers |= kModControl;
      }
      if (parsed.shift) {
        modifiers |= kModShift;
      }
      return modifiers;
    }
  }  // namespace

  void apply_config(const std::string &value) {
    const auto parsed = parse(value);
    if (!parsed.ok) {
      BOOST_LOG(warning) << "input_quit_hotkey " << parsed.error << " - keeping the previous setting.";
      return;
    }

    const int vk = virtual_key_for(parsed);
    if (vk <= 0) {
      BOOST_LOG(warning) << "input_quit_hotkey names no key Windows has a code for - keeping the previous setting.";
      return;
    }

    // Handing this over while idle is correct rather than a mistake: the hotkey thread holds the
    // reservation only while should_watch() is true, so what is stored here is a wish, not a grab.
    platf::hotkey::update_quit_hotkey(vk, modifiers_for(parsed));
  }
}  // namespace quit_hotkey

#else

namespace quit_hotkey {
  namespace {
    /// How often the gate asks whether it should be watching, and how finely it slices that wait
    /// so a shutdown is prompt rather than up to half a second late.
    constexpr int kGatePollMs = 500;
    constexpr int kGateSliceMs = 50;

    std::mutex g_mutex;
    resolved_combo_t g_resolved{};
    bool g_have_combo = false;
    bool g_watching = false;
    std::jthread g_gate;

    /// The action, shared with the Windows trigger. Written once so neither platform can end
    /// something different from the other.
    void on_combo() {
      if (!quit_request::end_client_holding_shared_devices("quit hotkey")) {
        BOOST_LOG(info) << "Quit combo pressed, but no client could be resolved; no stream was ended.";
      }
    }

    watcher_t &watcher() {
      static watcher_t instance{&on_combo};
      return instance;
    }

    void gate_main(std::stop_token stop) {
      // Only said once per state, so a session with nothing shared in does not fill the log with
      // the same sentence twice a second.
      bool said_waiting = false;

      while (!stop.stop_requested()) {
        const bool live = should_watch();

        {
          std::lock_guard lock{g_mutex};

          if (live && !g_watching && g_have_combo) {
            const auto opened = watcher().start(g_resolved);
            g_watching = opened > 0;

            if (opened == 0) {
              if (!said_waiting) {
                BOOST_LOG(info) << "Quit combo: a session is live but no keyboard has been shared in yet; waiting for one.";
                said_waiting = true;
              }
            } else {
              said_waiting = false;
              BOOST_LOG(info) << "Quit combo: watching " << opened << " shared keyboard(s).";
            }
          } else if (!live && g_watching) {
            watcher().stop();
            g_watching = false;
            said_waiting = false;
            BOOST_LOG(info) << "Quit combo: no session is live; stopped reading the shared keyboard.";
          }
        }

        for (int waited = 0; waited < kGatePollMs && !stop.stop_requested(); waited += kGateSliceMs) {
          std::this_thread::sleep_for(std::chrono::milliseconds(kGateSliceMs));
        }
      }

      watcher().stop();
    }
  }  // namespace

  void apply_config(const std::string &value) {
    const auto parsed = parse(value);
    if (!parsed.ok) {
      BOOST_LOG(warning) << "input_quit_hotkey " << parsed.error << " - keeping the previous setting.";
      return;
    }

    const auto resolved = resolve(to_combo(parsed));
    if (!resolved) {
      BOOST_LOG(warning) << "input_quit_hotkey names a key this build has no code for - keeping the previous setting.";
      return;
    }

    {
      std::lock_guard lock{g_mutex};
      g_resolved = *resolved;
      g_have_combo = true;
    }

    // Started on the first combination rather than at a named startup point, so there is no
    // ordering to get right and nothing to forget. A later change to the setting takes effect
    // from the next gate pass, which is at most half a second and only matters mid-session.
    if (!g_gate.joinable()) {
      g_gate = std::jthread{gate_main};
    }
  }
}  // namespace quit_hotkey

#endif
