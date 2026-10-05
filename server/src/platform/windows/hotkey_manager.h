/**
 * @file src/platform/windows/hotkey_manager.h
 * @brief Global hotkey registration for Windows.
 */
#pragma once

#ifdef _WIN32
namespace platf::hotkey {
  // Update the restore hotkey (virtual-key code + modifier flags). Use 0 to disable.
  void update_restore_hotkey(int vk_code, unsigned int modifiers);

  // Update the quit hotkey (virtual-key code + modifier flags). Use 0 to disable.
  //
  // Unlike the restore hotkey this one is not held all the time: it is reserved only while a
  // session is live, so that with nothing streaming the host is not sitting on a combo
  // system-wide for no reason.
  void update_quit_hotkey(int vk_code, unsigned int modifiers);
}  // namespace platf::hotkey
#endif
