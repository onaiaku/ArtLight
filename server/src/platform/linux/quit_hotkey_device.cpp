/**
 * @file src/platform/linux/quit_hotkey_device.cpp
 * @brief Finding the shared keyboard, and reading it.
 *
 * The split in this file is the same one as everywhere else in this feature: the decision is
 * separated from the plumbing so the decision can be tested. is_imported_devpath is the decision
 * and it is pure - a string in, a bool out - which is why the tests can assert it against the
 * exact sysfs paths taken from a real machine, including the ones it must say no to. Everything
 * below that needs a descriptor is the plumbing.
 *
 * Two things are handled carefully because getting them wrong is silent rather than loud:
 *
 * - A shared keyboard appears when it is attached, not before, so start() looks again rather
 *   than caching a list from startup. Nothing to watch is the normal state, not an error.
 * - The reading thread and stop() must not race over descriptors. stop() asks the thread to
 *   finish and joins it before closing anything, so a descriptor is never closed underneath a
 *   read in progress. The mutex is not held across the join, which is what keeps that from
 *   deadlocking.
 */
#include "src/platform/linux/quit_hotkey_device.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <system_error>

#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace quit_hotkey {
  namespace {
    constexpr const char *kDevInput = "/dev/input";
    constexpr const char *kSysInput = "/sys/class/input";
    constexpr const char *kSysPrefix = "/sys";

    /// The kernel's name for the virtual host controller, e.g. "vhci_hcd.0".
    constexpr const char *kVhciPrefix = "vhci_hcd.";

    /// How long a single pass waits for something to happen before looking at whether it should stop.
    constexpr int kPumpTimeoutMs = 200;

    /// Does this device actually carry the key we are waiting for?
    ///
    /// A shared mouse is also on vhci_hcd and is not worth reading. Asking the device is cheaper
    /// and more honest than guessing from its name: if it cannot produce this key, there is
    /// nothing to wait for on it.
    bool carries_key(int fd, unsigned int code) {
      unsigned long bits[(KEY_MAX / (8 * sizeof(unsigned long))) + 1] = {};
      if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(bits)), bits) < 0) {
        return false;
      }
      const auto index = code / (8 * sizeof(unsigned long));
      const auto bit = code % (8 * sizeof(unsigned long));
      return (bits[index] >> bit) & 1UL;
    }

    /// The sysfs path of a node under /dev/input, without the /sys prefix, or empty if it cannot
    /// be resolved. Symlinks are followed: /sys/class/input/eventN points at the real device.
    std::string devpath_for(const std::string &event_path) {
      std::error_code ec;
      const auto real = std::filesystem::canonical(event_path, ec);
      if (ec) {
        return {};
      }
      auto text = real.string();
      if (text.rfind(kSysPrefix, 0) == 0) {
        text.erase(0, std::strlen(kSysPrefix));
      }
      return text;
    }
  }  // namespace

  bool is_imported_devpath(const std::string &devpath) {
    // Split on '/', and require a component that IS the virtual host controller: it must be named
    // vhci_hcd, a dot, and then a number, which is the only shape the kernel creates. The digit is
    // required rather than "anything after the dot" so that a component merely beginning the same
    // way - vhci_hcd.0-backup, say - is not swept up. The udev rule carries the identical test
    // (*/vhci_hcd.[0-9]*) and the two must stay in step: the rule grants access to a device and
    // this decides whether to read it, so a reader stricter than the rule is a feature that
    // silently does nothing.
    std::size_t start = 0;
    while (start <= devpath.size()) {
      const auto end = devpath.find('/', start);
      const auto component = devpath.substr(start, end == std::string::npos ? std::string::npos : end - start);

      const auto prefix_length = std::strlen(kVhciPrefix);
      if (component.size() > prefix_length && component.compare(0, prefix_length, kVhciPrefix) == 0 &&
          std::isdigit(static_cast<unsigned char>(component[prefix_length])) != 0) {
        return true;
      }

      if (end == std::string::npos) {
        break;
      }
      start = end + 1;
    }
    return false;
  }

  std::vector<source_t> find_imported_keyboards() {
    std::vector<source_t> found;

    std::error_code ec;
    std::filesystem::directory_iterator entries{kSysInput, ec};
    if (ec) {
      return found;
    }

    for (const auto &entry : entries) {
      const auto name = entry.path().filename().string();
      if (name.rfind("event", 0) != 0) {
        continue;
      }

      const auto devpath = devpath_for(entry.path().string());
      if (devpath.empty() || !is_imported_devpath(devpath)) {
        continue;
      }

      found.push_back(source_t{.path = std::string(kDevInput) + "/" + name, .devpath = devpath});
    }

    // A stable order, so a log line reads the same way twice.
    std::sort(found.begin(), found.end(), [](const source_t &a, const source_t &b) {
      return a.path < b.path;
    });
    return found;
  }

  watcher_t::watcher_t(fired_cb on_fired):
      on_fired_(std::move(on_fired)) {
  }

  watcher_t::~watcher_t() {
    stop();
  }

  bool watcher_t::running() const {
    return running_.load();
  }

  std::size_t watcher_t::start(const resolved_combo_t &combo) {
    stop();

    std::lock_guard lock{mutex_};

    matcher_ = matcher_t{combo};

    for (const auto &source : find_imported_keyboards()) {
      const int fd = ::open(source.path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
      if (fd < 0) {
        // Not fatal: the udev rule may not have applied yet, or the node may have gone. The
        // caller reports how many it got, which is more useful than a failure with no count.
        continue;
      }

      if (!carries_key(fd, combo.key)) {
        ::close(fd);
        continue;
      }

      devices_.push_back(device_t{.fd = fd, .path = source.path});
    }

    if (devices_.empty()) {
      return 0;
    }

    stop_requested_.store(false);
    running_.store(true);
    thread_ = std::thread{&watcher_t::thread_main, this};
    return devices_.size();
  }

  void watcher_t::stop() {
    if (!running_.load() && devices_.empty()) {
      return;
    }

    stop_requested_.store(true);
    if (thread_.joinable()) {
      // Joined BEFORE the descriptors are closed, so a read is never holding a descriptor that
      // has just been closed underneath it. Deliberately not holding the mutex here.
      thread_.join();
    }

    std::lock_guard lock{mutex_};
    for (auto &device : devices_) {
      if (device.fd >= 0) {
        ::close(device.fd);
        device.fd = -1;
      }
    }
    devices_.clear();
    matcher_.reset();
    running_.store(false);
  }

  void watcher_t::pump(int timeout_ms) {
    std::lock_guard lock{mutex_};

    if (devices_.empty()) {
      return;
    }

    std::vector<pollfd> fds;
    fds.reserve(devices_.size());
    for (const auto &device : devices_) {
      fds.push_back(pollfd{.fd = device.fd, .events = POLLIN, .revents = 0});
    }

    const int ready = ::poll(fds.data(), fds.size(), timeout_ms);
    if (ready <= 0) {
      return;
    }

    input_event events[64];
    for (std::size_t i = 0; i < devices_.size(); ++i) {
      if ((fds[i].revents & POLLIN) == 0) {
        continue;
      }

      const auto bytes = ::read(devices_[i].fd, events, sizeof(events));
      if (bytes <= 0) {
        continue;
      }

      const auto count = static_cast<std::size_t>(bytes) / sizeof(input_event);
      for (std::size_t n = 0; n < count; ++n) {
        // Only key events matter. Everything else a device sends - synchronisation, timestamps,
        // a mouse moving - is offered to the matcher as a non-match rather than filtered by
        // guesswork, so the matcher stays the only place that decides.
        if (events[n].type != EV_KEY) {
          continue;
        }

        if (matcher_.feed(static_cast<unsigned int>(events[n].code), events[n].value)) {
          if (on_fired_) {
            on_fired_();
          }
        }
      }
    }
  }

  void watcher_t::thread_main() {
    while (!stop_requested_.load()) {
      pump(kPumpTimeoutMs);
    }
  }
}  // namespace quit_hotkey
