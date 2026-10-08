/**
 * @file src/platform/linux/quit_hotkey_device.h
 * @brief The Linux half that touches a device: finding the shared keyboard and reading it.
 *
 * This file exists separately from quit_hotkey_linux.h on purpose. That one is pure - numbers in,
 * yes or no out - and stays that way. Everything here needs a machine: it looks at sysfs, opens
 * device nodes and polls them. Keeping them apart means the part that is easy to get wrong for
 * boring reasons (paths, descriptors, lifetimes) cannot leak into the part that has to be exactly
 * right, and the exact part can still be tested with no hardware at all.
 *
 * The one decision worth being certain about is which devices are ours. A keyboard shared into
 * this machine over USB/IP arrives on the vhci_hcd bus, so its sysfs path contains a component
 * named vhci_hcd.N, and no keyboard built into the machine does. That is the same test the udev
 * rule in packaging/linux/70-artlight-input.rules applies, and it is written again here as a
 * function so that the two can be checked against each other - a rule that grants access and a
 * reader that reads must agree about which devices are meant, or the reader finds nothing and
 * the rule granted access to nobody.
 *
 * Measured on a real machine, and asserted in the tests:
 *   /devices/platform/vhci_hcd.0                      -> ours
 *   /devices/platform/vhci_hcd.0/usb5/5-1/.../event20 -> ours
 *   /devices/platform/PNP0C0E:00/input/input1         -> not ours
 *   /devices/pci0000:00/0000:00:1f.0/.../input/input0 -> not ours
 */
#pragma once

#include <atomic>
#include <cstddef>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "src/platform/linux/quit_hotkey_linux.h"

namespace quit_hotkey {
  /// A device that arrived over USB/IP and is worth reading.
  struct source_t {
    std::string path;     ///< The node to open, e.g. /dev/input/event20.
    std::string devpath;  ///< The sysfs path that decided it was ours. Kept for the log.
  };

  /**
   * @brief Is this sysfs path a device that arrived over USB/IP?
   *
   * True when any path component is named vhci_hcd followed by a dot and something - the shape
   * the kernel gives the virtual host controller. Deliberately strict: a component must start
   * with vhci_hcd., so a device merely containing those letters is not swept up.
   *
   * @param devpath A sysfs path, with or without a leading /sys.
   */
  bool is_imported_devpath(const std::string &devpath);

  /**
   * @brief Every keyboard this machine currently has from USB/IP, in a stable order.
   *
   * Empty is the normal state and is not an error: it means nothing is being shared in. Called
   * again after a session starts, because the device appears when it is attached and there is
   * nothing to watch before that.
   */
  std::vector<source_t> find_imported_keyboards();

  /**
   * @brief Reads the shared keyboards and reports the combo.
   *
   * Owns a thread that polls whatever it opened and feeds the matcher. Nothing here decides
   * whether a stream is live - that is the caller's business, and the caller starts and stops
   * this accordingly.
   */
  class watcher_t {
  public:
    /// Called on the watcher's thread when the combo is completed. Must not block.
    using fired_cb = std::function<void()>;

    explicit watcher_t(fired_cb on_fired);
    ~watcher_t();

    watcher_t(const watcher_t &) = delete;
    watcher_t &operator=(const watcher_t &) = delete;

    /**
     * @brief Open every imported keyboard that can produce this combo, and start reading.
     *
     * @return How many devices were opened. 0 means there was nothing to watch - no device is
     *         shared in, or none of them carries the combo's key - which is not a failure and
     *         is worth saying out loud rather than leaving a person wondering.
     */
    std::size_t start(const resolved_combo_t &combo);

    /// Close everything and forget modifier state. Safe to call when not running.
    void stop();

    /// Whether the reading thread is live.
    bool running() const;

    /**
     * @brief Read once from every open device.
     *
     * Public so a test can step it by hand instead of racing a thread. Does nothing when stopped.
     *
     * @param timeout_ms How long to wait for an event before giving up this pass.
     */
    void pump(int timeout_ms);

  private:
    struct device_t {
      int fd = -1;
      std::string path;
    };

    void thread_main();

    fired_cb on_fired_;
    matcher_t matcher_;
    std::vector<device_t> devices_;

    std::thread thread_;
    std::atomic<bool> stop_requested_{false};
    std::atomic<bool> running_{false};
    std::mutex mutex_;
  };
}  // namespace quit_hotkey
