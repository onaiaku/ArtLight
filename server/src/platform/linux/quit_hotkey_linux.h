/**
 * @file src/platform/linux/quit_hotkey_linux.h
 * @brief The quit combo, recognised from a stream of raw key events.
 *
 * On Windows the OS reserves the combo for us: RegisterHotKey is told the combination once and
 * the OS reports when it is pressed. Linux has no equivalent that can be pointed at one device,
 * so this end reads the key events itself and has to decide, event by event, whether the person
 * just pressed the combo.
 *
 * That decision is deliberately kept apart from everything that makes it hard: opening a device,
 * finding the right one, and knowing when a stream is live are all somebody else's problem. What
 * is left here is the part worth being certain about - given a key going down and the modifiers
 * held at that moment, was it the combo? - and it can be answered with nothing but numbers, no
 * hardware and no privileges. Which is the point: this is the half that can be exhaustively
 * tested, so it is the half that should hold no surprises.
 *
 * The key codes below are the values from <linux/input-event-codes.h>. They are repeated rather
 * than included so that the matcher can be built and tested without a kernel header on the
 * include path. They are ABI: the kernel has not changed them and will not.
 */
#pragma once

#include <optional>
#include <string>

#include "src/quit_hotkey_parse.h"

namespace quit_hotkey {
  /// The evdev codes this matcher knows about. Source of truth: <linux/input-event-codes.h>.
  namespace key {
    constexpr unsigned int o = 24;
    constexpr unsigned int left_ctrl = 29;
    constexpr unsigned int right_ctrl = 97;
    constexpr unsigned int left_alt = 56;
    constexpr unsigned int right_alt = 100;
    constexpr unsigned int left_shift = 42;
    constexpr unsigned int right_shift = 54;
  }  // namespace key

  /// The third field of an input_event: what the key just did.
  enum class event_value : int {
    release = 0,
    press = 1,
    repeat = 2,
  };

  /**
   * @brief A combination resolved into something a device can be asked about.
   *
   * The shared combo_t is how a person writes it; this is the same combination as evdev codes.
   * The two are kept apart because the evdev codes are a Linux detail and a config file must
   * never contain them: `resolve()` is the only place the conversation happens.
   */
  struct resolved_combo_t {
    unsigned int key = key::o;  ///< The evdev code that ends the stream.
    bool ctrl = true;           ///< Whether either Ctrl must be held.
    bool alt = true;            ///< Whether either Alt must be held.
    bool shift = true;          ///< Whether either Shift must be held.
  };

  /**
   * @brief Turn a configuration value's combination into evdev codes.
   *
   * @return The resolved combination, or std::nullopt if the key has no evdev code here. The
   *         caller reports that and keeps the old setting, rather than silently watching for
   *         a key nobody can press.
   */
  std::optional<resolved_combo_t> resolve(const combo_t &combo);

  /**
   * @brief Tracks modifier state and reports the moment the combo is completed.
   *
   * Stateful by necessity: the combo is only knowable from the events that came before the key
   * that completes it. Feed every event from the device in order; feed returns true exactly
   * once per completed combo.
   */
  class matcher_t {
  public:
    explicit matcher_t(resolved_combo_t combo = {});

    /**
     * @brief Offer one key event.
     *
     * @param code  The evdev key code.
     * @param value The event value: 0 release, 1 press, 2 repeat. An unrecognised value is
     *              treated as a release, which fails safe - the matcher forgets a modifier
     *              rather than believing in one that was let go.
     * @return True if this event is the key going down with every required modifier held.
     */
    bool feed(unsigned int code, int value);

    /// Forget every modifier. For use when a session ends: the keyboard is going back to the
    /// machine it came from, and it will not be sending this end any releases.
    void reset();

    /// What is currently held, in words. For the log line that tells a person why the combo
    /// did nothing they pressed it and expected it to work.
    std::string state() const;

  private:
    resolved_combo_t combo_;
    bool ctrl_ = false;
    bool alt_ = false;
    bool shift_ = false;
  };
}  // namespace quit_hotkey
