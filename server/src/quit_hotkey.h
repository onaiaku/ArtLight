/**
 * @file src/quit_hotkey.h
 * @brief The one place the configured combination is handed to a platform.
 *
 * There are two triggers - a Win32 hotkey and a Linux evdev read - and they are genuinely
 * different pieces of code. What must NOT be different is the two decisions wrapped around them:
 * what the configured combination means, and when the host is allowed to be watching for it.
 * Those live here, once, so the platforms cannot drift into disagreeing about either.
 *
 * The failure this prevents is quiet and specific. If Windows gated on a live session and Linux
 * forgot to, Linux would sit reading a shared keyboard while idle, and nothing would look wrong
 * until somebody noticed their keystrokes being read for no reason. If they gated on different
 * counts, one of them would keep watching after a session ended. Neither shows up as a crash, a
 * failed build or a red test, which is exactly why it is settled here rather than twice.
 *
 * There is no start() or shutdown() to call. Each platform's trigger watches for its own lifetime
 * and asks should_watch() before it does anything, so there is no startup ordering to get wrong
 * and no place a caller can forget to wire up.
 */
#pragma once

#include <string>

namespace quit_hotkey {
  /**
   * @brief The configured combination changed, or was read for the first time.
   *
   * Parsed here rather than by the caller, because each platform needs the parsed form and
   * neither should be re-deriving it. A value that cannot be understood is reported and the
   * previous setting is kept: a typo in a config file must not stop the server, and must not
   * silently leave somebody with no way out of a stream whose keyboard has been taken.
   *
   * Safe to call at any time, from any thread.
   */
  void apply_config(const std::string &value);

  /**
   * @brief Whether the combination should be watched for right now.
   *
   * ONE answer, asked by both platforms. True only while a session is live: the host must not be
   * holding a system-wide combination, or reading a shared keyboard, while it has nothing to do.
   */
  bool should_watch();
}  // namespace quit_hotkey
