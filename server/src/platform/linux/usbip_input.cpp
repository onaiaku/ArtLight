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

#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

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

  namespace {
    /// pkexec, by absolute path. It is the only supported way for an unprivileged process to hand
    /// this program privilege, and it is what the polkit action beside the helper binds to.
    constexpr auto kPkexec = "/usr/bin/pkexec";

    /**
     * Run a command and bring back BOTH streams separately.
     *
     * Separate, not merged, and the exporter half learned this the hard way: the reason a tool
     * refused arrives on stderr while the exit code says only "no". A shell pipeline that merged the
     * two once made a held device look like an unreadable one.
     *
     * Read sequentially to EOF rather than polled. The child blocks when a pipe fills; it does not
     * deadlock against us, because we do eventually read the other pipe once this one hits EOF. The
     * one case that would hang is a child that fills stderr and then waits for us to drain stdout
     * before writing anything more - which neither usbip nor pkexec does, and the caller runs this
     * off the request path so even a hang cannot stall a stream.
     */
    run_result_t run_process(const std::vector<std::string> &argv) {
      run_result_t result;
      if (argv.empty()) {
        result.code = -1;
        result.err = "no command to run";
        return result;
      }

      int out_pipe[2] = {-1, -1};
      int err_pipe[2] = {-1, -1};
      if (::pipe(out_pipe) != 0 || ::pipe(err_pipe) != 0) {
        result.code = -1;
        result.err = "could not create a pipe";
        return result;
      }

      const pid_t child = ::fork();
      if (child < 0) {
        result.code = -1;
        result.err = "could not fork";
        return result;
      }

      if (child == 0) {
        ::dup2(out_pipe[1], STDOUT_FILENO);
        ::dup2(err_pipe[1], STDERR_FILENO);
        ::close(out_pipe[0]);
        ::close(out_pipe[1]);
        ::close(err_pipe[0]);
        ::close(err_pipe[1]);

        std::vector<char *> cargv;
        cargv.reserve(argv.size() + 1);
        for (const auto &part : argv) {
          cargv.push_back(const_cast<char *>(part.c_str()));
        }
        cargv.push_back(nullptr);
        ::execv(cargv[0], cargv.data());

        std::cerr << "artlight: could not run " << argv[0] << ": " << std::strerror(errno) << "\n";
        ::_exit(127);
      }

      ::close(out_pipe[1]);
      ::close(err_pipe[1]);

      char buffer[4096];
      ssize_t read = 0;
      while ((read = ::read(out_pipe[0], buffer, sizeof(buffer))) > 0) {
        result.out.append(buffer, static_cast<std::size_t>(read));
      }
      while ((read = ::read(err_pipe[0], buffer, sizeof(buffer))) > 0) {
        result.err.append(buffer, static_cast<std::size_t>(read));
      }
      ::close(out_pipe[0]);
      ::close(err_pipe[0]);

      int status = 0;
      ::waitpid(child, &status, 0);
      result.code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
      return result;
    }

    /// Whether the privileged half is actually present. Both files, because a helper with no action
    /// cannot be invoked and an action with no helper authorises nothing - and a missing pair is a
    /// setup step, not a silent failure.
    bool helper_installed() {
      if (!is_executable(kHelperPath)) {
        return false;
      }
      std::error_code ec;
      return std::filesystem::exists(kPolicyPath, ec);
    }

    /// Every attach and detach on Linux goes through the helper, never directly. `attach` writes to
    /// /sys/devices/platform/vhci_hcd.0/attach, which is root-only, so a direct call would fail with
    /// `usbip: error: import device` - measured on the z13.
    run_result_t run_privileged(const std::vector<std::string> &args) {
      if (!helper_installed()) {
        run_result_t missing;
        missing.code = -1;
        missing.err = std::string("the USB/IP helper is not installed on this PC (") + kHelperPath +
                      " and " + kPolicyPath + ") - USB device sharing needs it";
        return missing;
      }
      std::vector<std::string> argv = {kPkexec, kHelperPath};
      argv.insert(argv.end(), args.begin(), args.end());
      return run_process(argv);
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

  run_result_t run_attach(const std::string_view exporter, const std::string_view busid) {
    try {
      // Build the argv so the POLICY validates the caller's input before anything is spawned, then
      // hand the validated pair to the helper. The helper validates again on its own side with the
      // same linked-in rules - it does not trust this process, which is the point of it.
      const auto argv = build_attach_argv(exporter, busid);
      return run_privileged({"attach", argv[3], argv[5]});
    } catch (const std::exception &error) {
      run_result_t refused;
      refused.code = -1;
      refused.err = error.what();
      return refused;
    }
  }

  run_result_t run_detach(const int port) {
    try {
      build_detach_argv(port);  // refuses a negative port before anything is spawned
      return run_privileged({"detach", std::to_string(port)});
    } catch (const std::exception &error) {
      run_result_t refused;
      refused.code = -1;
      refused.err = error.what();
      return refused;
    }
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
