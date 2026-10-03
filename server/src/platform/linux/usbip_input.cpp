/**
 * @file src/platform/linux/usbip_input.cpp
 * @brief The Linux importer's edge: find the client, and find out whether anything could arrive.
 *
 * Two things about Linux that a Windows reading of this feature gets wrong, both measured on the
 * z13 and both encoded here:
 *
 * 1. **A client on PATH proves nothing.** The z13 had usbip-utils 2.0 installed and could not
 *    import a single device, because `vhci_hcd` was not loaded. `usbip port` says so in as many
 *    words: `open vhci_driver (is vhci_hcd loaded?)`. The module is the gate, not the binary, so
 *    the two are checked separately and reported as different states.
 *
 * 2. **Attach needs root here, and does not on Windows.** `/sys/devices/platform/vhci_hcd.0/attach`
 *    is mode 0600 owned by root, so an unprivileged `usbip attach` fails with
 *    `usbip: error: import device`. A Linux importer therefore needs the same privileged-helper
 *    shape the Linux exporter already has. That is a real asymmetry between the platforms and it
 *    must not be smoothed over by giving Windows a daemon it provably does not need.
 */
#include "src/usbip_input.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

namespace input::usbip {
  namespace {
    namespace fs = std::filesystem;

    /// The privileged helper attach needs on this platform, and the polkit action that authorises
    /// it. Both must be present: a helper with no action cannot be invoked, and an action with no
    /// helper authorises nothing. Named to match the exporter's existing pair so the two halves of
    /// this feature are recognisably the same shape.
    constexpr auto kHelperPath = "/usr/libexec/artlight-input-service";
    constexpr auto kPolicyPath = "/usr/share/polkit-1/actions/org.artlight.input-service.policy";

    /// The client's usual homes. Checked before PATH because a package that installs elsewhere
    /// still installs to one of these, and an absolute path cannot be shadowed.
    constexpr const char *kClientCandidates[] = {
      "/usr/bin/usbip",
      "/usr/local/bin/usbip",
      "/bin/usbip",
    };

    bool is_executable(const fs::path &path) {
      return ::access(path.c_str(), X_OK) == 0;
    }

    /// The running kernel's release, read rather than shelled out to. Empty when unreadable, which
    /// is reported as "cannot tell" rather than guessed at.
    std::string kernel_release() {
      std::ifstream release("/proc/sys/kernel/osrelease");
      std::string value;
      std::getline(release, value);
      return value;
    }

    /// Whether the module exists on disk, ready to be loaded. This is what separates "the driver is
    /// missing" from "the driver is present and simply not loaded" — one is a reinstall, the other
    /// is a load, and reporting them as the same thing costs someone real time.
    ///
    /// The compressed suffixes are not decoration: on this machine the file is `vhci-hcd.ko.zst`.
    /// Note also that the FILE is `vhci-hcd` and the MODULE is `vhci_hcd`; the sysfs directory
    /// below uses the underscored name, this glob uses the hyphenated one.
    bool module_file_present() {
      const auto release = kernel_release();
      if (release.empty()) {
        return false;
      }

      const fs::path directory = fs::path("/lib/modules") / release / "kernel/drivers/usb/usbip";
      std::error_code ec;
      if (!fs::is_directory(directory, ec)) {
        return false;
      }

      for (const auto &entry : fs::directory_iterator(directory, ec)) {
        const auto name = entry.path().filename().string();
        if (name.rfind("vhci-hcd.ko", 0) == 0) {
          return true;
        }
      }
      return false;
    }
  }  // namespace

  std::string client_path() {
    for (const auto *candidate : kClientCandidates) {
      if (is_executable(candidate)) {
        return candidate;
      }
    }

    // Then PATH, which is legitimate on Linux: it is an ordinary system binary and the user's own
    // environment is the authority on where they put it.
    const auto *path = std::getenv("PATH");
    if (path == nullptr) {
      return {};
    }

    std::string search{path};
    std::size_t start = 0;
    while (start <= search.size()) {
      const auto end = search.find(':', start);
      const auto directory = search.substr(start, end == std::string::npos ? std::string::npos : end - start);
      if (!directory.empty()) {
        const fs::path candidate = fs::path(directory) / "usbip";
        if (is_executable(candidate)) {
          return candidate.string();
        }
      }
      if (end == std::string::npos) {
        break;
      }
      start = end + 1;
    }
    return {};
  }

  ClientProbe probe_client() {
    ClientProbe probe;

    probe.client_path = client_path();
    probe.client_found = !probe.client_path.empty();

    // The module, not the binary. `/sys/module/vhci_hcd` exists exactly when the kernel has it
    // loaded, and that is the thing that decides whether a device can arrive at all.
    std::error_code ec;
    probe.driver_present = fs::is_directory("/sys/module/vhci_hcd", ec);

    // Only worth asking when it is not already loaded — and asking matters, because a machine in
    // this state is one `modprobe` away from working, not one reinstall away.
    if (!probe.driver_present) {
      probe.driver_loadable = module_file_present();
    }

    probe.privilege_required = true;
    probe.helper_present = is_executable(kHelperPath) && fs::exists(kPolicyPath, ec);

    return probe;
  }
}  // namespace input::usbip
