/**
 * @file src/platform/windows/hotkey_manager.cpp
 * @brief Global hotkey registration for Windows.
 */
#ifdef _WIN32
  #include "hotkey_manager.h"

  #include "src/logging.h"
  #include "src/platform/windows/misc.h"
  #include "src/platform/windows/virtual_display_cleanup.h"
  #include "src/quit_request.h"
  #include "src/rtsp.h"

  #include <atomic>
  #include <ios>
  #include <mutex>
  #include <thread>
  #include <winsock2.h>
  #include <Windows.h>

using namespace std::literals;

namespace {
  constexpr UINT kMsgUpdateHotkey = WM_APP + 1;
  constexpr UINT kMsgShutdown = WM_APP + 2;
  constexpr UINT kMsgUpdateQuitHotkey = WM_APP + 3;
  constexpr int kRestoreHotkeyId = 1;
  constexpr int kQuitHotkeyId = 2;

  // The quit hotkey is held only while something is streaming, so this thread asks the server
  // once a second whether that is still true. A timer keeps the answer inside this thread
  // rather than requiring session start and stop to know that hotkeys exist.
  constexpr UINT_PTR kSessionWatchTimerId = 1;
  constexpr UINT kSessionWatchIntervalMs = 1000;

  std::mutex &hotkey_mutex() {
    static std::mutex m;
    return m;
  }

  DWORD g_hotkey_thread_id = 0;
  bool g_hotkey_thread_started = false;

  bool g_hotkey_registered = false;
  int g_current_hotkey_vk = 0;
  UINT g_current_hotkey_modifiers = 0;

  // What the config asked for. For the restore hotkey wanted and current are always the same
  // thing; for the quit hotkey "wanted" is the configured combo while "current" is what is
  // actually reserved right now - which is nothing whenever no session is running.
  int g_wanted_hotkey_vk = 0;
  UINT g_wanted_hotkey_modifiers = 0;

  bool g_quit_hotkey_registered = false;
  int g_current_quit_vk = 0;
  UINT g_current_quit_modifiers = 0;
  int g_wanted_quit_vk = 0;
  UINT g_wanted_quit_modifiers = 0;

  std::atomic<bool> g_warned_system {false};

  void register_restore_hotkey_locked(int vk_code, UINT modifiers) {
    if (g_hotkey_registered) {
      UnregisterHotKey(nullptr, kRestoreHotkeyId);
      g_hotkey_registered = false;
    }

    g_current_hotkey_vk = vk_code;
    g_current_hotkey_modifiers = modifiers;
    if (vk_code <= 0) {
      return;
    }

  #ifdef MOD_NOREPEAT
    if (modifiers != 0) {
      modifiers |= MOD_NOREPEAT;
    }
  #endif
    if (!RegisterHotKey(nullptr, kRestoreHotkeyId, modifiers, static_cast<UINT>(vk_code))) {
      BOOST_LOG(warning) << "Failed to register restore hotkey (VK "sv << vk_code
                         << "): "sv << GetLastError();
      return;
    }

    g_hotkey_registered = true;
    BOOST_LOG(info) << "Registered restore hotkey (VK "sv << vk_code << ", modifiers 0x"
                    << std::hex << modifiers << std::dec << ").";
  }

  void register_quit_hotkey_locked(int vk_code, UINT modifiers) {
    if (g_quit_hotkey_registered) {
      UnregisterHotKey(nullptr, kQuitHotkeyId);
      g_quit_hotkey_registered = false;
    }

    g_current_quit_vk = vk_code;
    g_current_quit_modifiers = modifiers;
    if (vk_code <= 0) {
      return;
    }

  #ifdef MOD_NOREPEAT
    if (modifiers != 0) {
      modifiers |= MOD_NOREPEAT;
    }
  #endif
    if (!RegisterHotKey(nullptr, kQuitHotkeyId, modifiers, static_cast<UINT>(vk_code))) {
      BOOST_LOG(warning) << "Failed to register quit hotkey (VK "sv << vk_code
                         << "): "sv << GetLastError();
      return;
    }

    g_quit_hotkey_registered = true;
    BOOST_LOG(info) << "Registered quit hotkey (VK "sv << vk_code << ", modifiers 0x"
                    << std::hex << modifiers << std::dec << ").";
  }

  void update_quit_registration_locked() {
    // A running session is what makes the combo worth holding, and a configured combo is the
    // only reason to hold it at all. With nothing to quit the reservation is given back: being
    // the quiet owner of a system-wide combo is not a thing to be while idle.
    //
    // session_count_no_cleanup() rather than session_count(), because this thread is making a
    // scheduling decision of its own and must not enter session teardown in order to make it.
    const bool hold = g_wanted_quit_vk > 0 && rtsp_stream::session_count_no_cleanup() > 0;
    const int vk_code = hold ? g_wanted_quit_vk : 0;

    if (vk_code == g_current_quit_vk && g_wanted_quit_modifiers == g_current_quit_modifiers) {
      return;
    }

    register_quit_hotkey_locked(vk_code, g_wanted_quit_modifiers);
  }

  void trigger_restore() {
    BOOST_LOG(info) << "Restore hotkey triggered; reverting display configuration.";
    const auto cleanup = platf::virtual_display_cleanup::terminate_all("restore_hotkey");
    if (!cleanup.virtual_displays_removed) {
      BOOST_LOG(warning) << "Restore hotkey cleanup: one or more virtual displays could not be removed; session recovery remains disengaged.";
    }
  }

  void trigger_quit() {
    // The keyboard this was pressed on is not on its own machine any more - it is here, which
    // is the whole reason the host has to catch this combo at all. Which stream ends is the
    // entire question, and the answer is deliberately not written here: quit_request resolves
    // the client holding shared devices and ends that one client, never the session, so a
    // second person watching the same stream keeps watching it.
    if (!quit_request::end_client_holding_shared_devices("quit hotkey")) {
      BOOST_LOG(info) << "Quit hotkey pressed, but no client could be resolved; no stream was ended.";
    }
  }

  void hotkey_thread_main(HANDLE ready_event) {
    MSG msg;
    PeekMessage(&msg, nullptr, 0, 0, PM_NOREMOVE);

    {
      // The wanted values are read here rather than handed in, because they are already kept
      // under this same lock. Passing copies in would create a second source of truth for the
      // one thing that must not have two.
      std::lock_guard<std::mutex> lock(hotkey_mutex());
      g_hotkey_thread_id = GetCurrentThreadId();
      g_hotkey_thread_started = true;
      register_restore_hotkey_locked(g_wanted_hotkey_vk, g_wanted_hotkey_modifiers);
      update_quit_registration_locked();
    }

    SetEvent(ready_event);

    SetTimer(nullptr, kSessionWatchTimerId, kSessionWatchIntervalMs, nullptr);

    while (GetMessage(&msg, nullptr, 0, 0) > 0) {
      if (msg.message == WM_HOTKEY && msg.wParam == kRestoreHotkeyId) {
        trigger_restore();
        continue;
      }

      if (msg.message == WM_HOTKEY && msg.wParam == kQuitHotkeyId) {
        trigger_quit();
        continue;
      }

      if (msg.message == kMsgUpdateHotkey) {
        const int new_vk = static_cast<int>(msg.wParam);
        const UINT new_modifiers = static_cast<UINT>(msg.lParam);
        std::lock_guard<std::mutex> lock(hotkey_mutex());
        if (new_vk != g_current_hotkey_vk || new_modifiers != g_current_hotkey_modifiers) {
          register_restore_hotkey_locked(new_vk, new_modifiers);
        }
        continue;
      }

      if (msg.message == kMsgUpdateQuitHotkey) {
        std::lock_guard<std::mutex> lock(hotkey_mutex());
        g_wanted_quit_vk = static_cast<int>(msg.wParam);
        g_wanted_quit_modifiers = static_cast<UINT>(msg.lParam);
        update_quit_registration_locked();
        continue;
      }

      if (msg.message == WM_TIMER && msg.wParam == kSessionWatchTimerId) {
        std::lock_guard<std::mutex> lock(hotkey_mutex());
        update_quit_registration_locked();
        continue;
      }

      if (msg.message == kMsgShutdown) {
        break;
      }
    }

    KillTimer(nullptr, kSessionWatchTimerId);

    std::lock_guard<std::mutex> lock(hotkey_mutex());
    if (g_hotkey_registered) {
      UnregisterHotKey(nullptr, kRestoreHotkeyId);
      g_hotkey_registered = false;
    }
    if (g_quit_hotkey_registered) {
      UnregisterHotKey(nullptr, kQuitHotkeyId);
      g_quit_hotkey_registered = false;
    }
  }

  // Starts the hotkey thread when it is not running yet and at least one hotkey is wanted. The
  // caller holds hotkey_mutex(); the ready event comes back to be waited on after that lock is
  // dropped, because the thread takes this same mutex on its way up.
  bool start_hotkey_thread_locked(HANDLE &ready_event) {
    ready_event = nullptr;

    if (g_hotkey_thread_started) {
      return false;
    }

    if (g_wanted_hotkey_vk <= 0 && g_wanted_quit_vk <= 0) {
      return false;
    }

    ready_event = CreateEvent(nullptr, TRUE, FALSE, nullptr);
    if (!ready_event) {
      BOOST_LOG(warning) << "Failed to create hotkey event: "sv << GetLastError();
      return false;
    }

    const HANDLE started_event = ready_event;
    std::thread([started_event]() {
      hotkey_thread_main(started_event);
    }).detach();

    return true;
  }

  void update_hotkey(int vk_code, unsigned int modifiers, UINT message, const char *label) {
    if (vk_code < 0) {
      vk_code = 0;
    }

    if (platf::is_running_as_system() && !g_warned_system.exchange(true)) {
      BOOST_LOG(warning) << "Hotkey registration may fail while running as SYSTEM "
                            "(no interactive session).";
    }

    std::unique_lock<std::mutex> lock(hotkey_mutex());
    if (message == kMsgUpdateHotkey) {
      g_wanted_hotkey_vk = vk_code;
      g_wanted_hotkey_modifiers = modifiers;
    } else {
      g_wanted_quit_vk = vk_code;
      g_wanted_quit_modifiers = modifiers;
    }

    if (!g_hotkey_thread_started) {
      HANDLE ready_event = nullptr;
      if (!start_hotkey_thread_locked(ready_event)) {
        return;
      }

      lock.unlock();
      WaitForSingleObject(ready_event, 5000);
      CloseHandle(ready_event);
      return;
    }

    const DWORD thread_id = g_hotkey_thread_id;
    lock.unlock();

    if (thread_id == 0) {
      BOOST_LOG(warning) << label << " hotkey thread not ready; update skipped.";
      return;
    }

    if (!PostThreadMessage(thread_id, message, static_cast<WPARAM>(vk_code), static_cast<LPARAM>(modifiers))) {
      BOOST_LOG(warning) << "Failed to post " << label << " hotkey update: "sv << GetLastError();
    }
  }
}  // namespace

namespace platf::hotkey {
  void update_restore_hotkey(int vk_code, unsigned int modifiers) {
    update_hotkey(vk_code, modifiers, kMsgUpdateHotkey, "restore");
  }

  void update_quit_hotkey(int vk_code, unsigned int modifiers) {
    update_hotkey(vk_code, modifiers, kMsgUpdateQuitHotkey, "quit");
  }
}  // namespace platf::hotkey
#endif
