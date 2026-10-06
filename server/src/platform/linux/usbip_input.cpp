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
#include "src/usbip_helper_protocol.h"

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

namespace input::usbip {
  namespace {
    namespace fs = std::filesystem;

    /// The privileged helper systemd runs as root on request, and the socket it accepts on. Both
    /// must be present: a helper with no socket can never be reached, and a socket with no helper
    /// has nothing behind it. Named to match the exporter's existing pair so the two halves of this
    /// feature are recognisably the same shape.
    constexpr auto kHelperPath = "/usr/libexec/vibeshine/artlight-input-service";
    constexpr auto kHelperSocket = "/run/artlight/usbip-helper.sock";

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
    /**
     * The socket systemd owns for the privileged half, and the only way this process can reach it.
     *
     * It used to be pkexec, by absolute path, and that could never have worked here. This process
     * is the privileged machine host: it calls sanitize_startup_capabilities() before configuration
     * or logging is parsed, and that ends in prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0).
     * NoNewPrivileges is inherited across fork and exec and cannot be unset, so the kernel refuses
     * to honour pkexec's setuid bit for this process or for anything it spawns. pkexec then finds
     * geteuid() != 0 and dies with `pkexec must be setuid root`.
     *
     * Measured on the z13 on 2026-10-06, not reasoned about: /proc/<host>/status read NoNewPrivs: 1
     * while /usr/bin/pkexec on disk was 4755 root and ran perfectly from an ordinary shell. The
     * polkit action beside the helper was right about authorisation and blind to capability, and no
     * amount of fixing the authorisation makes a setuid transition happen inside a process that has
     * permanently refused new privileges.
     *
     * The privilege therefore comes from outside this process, which is the only place it can come
     * from now: systemd listens on the socket below (mode 0660 root:artlight) and starts exactly one
     * copy of the helper as root per accepted connection. The account this server runs as is the
     * only account that can reach it.
     */
    auto helper_socket_path() {
      return kHelperSocket;
    }

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

    /// Whether the privileged half is actually installed. Both parts, because a helper with no
    /// socket can never be reached and a socket with no helper has nothing behind it - and a
    /// missing pair is a setup step, not a silent failure.
    ///
    /// This answers "is it installed". It deliberately does NOT answer "will a call work", because
    /// the only honest way to answer that is to make one; see the live probe in probe_client().
    bool helper_installed() {
      if (!is_executable(kHelperPath)) {
        return false;
      }
      return ::access(kHelperSocket, F_OK) == 0;
    }

    /// Whether the privileged half is installed AND actually answering. The socket being present is
    /// not the same as the service behind it being up, and the caller that conflates the two asks
    /// an exporter for a device it then cannot take - which on this platform is not a no-op, because
    /// the exporter has bound the device by then. One connect() costs nothing and answers the real
    /// question. socket activation starts the helper for the connection; it reads EOF, finds no
    /// request, and exits, so the cost of a probe is one short-lived unit and nothing else.
    bool helper_reachable() {
      if (!helper_installed()) {
        return false;
      }
      const std::string socket_path = helper_socket_path();
      if (socket_path.size() >= sizeof(sockaddr_un::sun_path)) {
        return false;
      }
      const int connection = ::socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
      if (connection < 0) {
        return false;
      }
      sockaddr_un address {};
      address.sun_family = AF_UNIX;
      std::memcpy(address.sun_path, socket_path.c_str(), socket_path.size() + 1);
      const bool reachable =
        ::connect(connection, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) == 0;
      ::close(connection);
      return reachable;
    }

    // The reply's framing lives in ONE place, shared with the helper that produces it:
    // src/usbip_helper_protocol.h. A wire format defined at both ends is a wire format that drifts,
    // and this particular drift would land on the half that runs as root.

    /// Every attach and detach on Linux goes through the helper, never directly. `attach` writes to
    /// /sys/devices/platform/vhci_hcd.0/attach, which is root-only, so a direct call would fail with
    /// `usbip: error: import device` - measured on the z13.
    ///
    /// One request packet out, one reply packet back. The socket is SOCK_SEQPACKET, matching the
    /// socket unit's ListenSequentialPacket, so both directions are all-or-nothing and neither side
    /// has to guess where a message ended.
    run_result_t run_privileged(const std::vector<std::string> &args) {
      run_result_t result;

      if (!helper_installed()) {
        result.code = -1;
        result.err = std::string("the USB/IP helper is not installed on this PC (") + kHelperPath +
                     " and " + kHelperSocket + ") - USB device sharing needs it";
        return result;
      }

      const std::string socket_path = helper_socket_path();
      if (socket_path.size() >= sizeof(sockaddr_un::sun_path)) {
        result.code = -1;
        result.err = "the USB/IP helper socket path is longer than a UNIX socket can carry";
        return result;
      }

      const int connection = ::socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
      if (connection < 0) {
        result.code = -1;
        result.err = std::string("could not open a UNIX socket: ") + std::strerror(errno);
        return result;
      }

      sockaddr_un address {};
      address.sun_family = AF_UNIX;
      std::memcpy(address.sun_path, socket_path.c_str(), socket_path.size() + 1);

      if (::connect(connection, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) != 0) {
        // The service not being up is its own sentence. It is the difference between "install the
        // package" and "the package is installed and its service is not running", and those are
        // different phone calls.
        result.code = -1;
        result.err = std::string("the USB/IP helper service is not running on this PC (") +
                     socket_path + ": " + std::strerror(errno) +
                     ") - the artlight-input-service socket is not up";
        ::close(connection);
        return result;
      }

      // One request, one packet. The encoding is shared with the helper - see
      // src/usbip_helper_protocol.h - so the two ends cannot disagree about what it looks like.
      if (args.empty()) {
        result.code = -1;
        result.err = "no verb to ask the USB/IP helper for";
        ::close(connection);
        return result;
      }
      const std::string request = helper_protocol::encode_request(
        args.front(), std::vector<std::string> {args.begin() + 1, args.end()});

      const ssize_t sent = ::write(connection, request.data(), request.size());
      if (sent != static_cast<ssize_t>(request.size())) {
        result.code = -1;
        result.err = std::string("could not send the request to the USB/IP helper: ") +
                     std::strerror(errno);
        ::close(connection);
        return result;
      }

      // Sized from the protocol's own cap, not from a number copied in here: the reply carries both
      // captured streams inside one packet, and a SEQPACKET read that does not fit discards the
      // remainder - which would be a reason we would never get to read.
      std::vector<char> packet(helper_protocol::kMaxReplyPacket);
      const ssize_t received = ::read(connection, packet.data(), packet.size());
      ::close(connection);

      if (received <= 0) {
        result.code = -1;
        result.err = "the USB/IP helper closed the connection without answering";
        return result;
      }

      if (!helper_protocol::decode_reply(packet.data(), static_cast<std::size_t>(received),
                                         result.code, result.out, result.err)) {
        result.code = -1;
        result.err = "the USB/IP helper sent a reply this process could not read";
      }
      return result;
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

  run_result_t run_list_remote(const std::string_view exporter) {
    try {
      // Element 0 of the policy's argv is the bare name `usbip`, and that is a placeholder, not a
      // program. It has to be replaced here: this runs inside the host's service, whose PATH need
      // not carry the client's directory, and run_process execs rather than searches. Left in
      // place it fails as `could not run usbip: No such file or directory` with exit 127 on a
      // machine where usbip is installed, executable, and runs fine as this very user - measured
      // on the z13, where it had failed on every attempt. Resolved the way `run_list_attached`
      // below already does it, so the two callers agree rather than drifting apart.
      auto argv = build_list_remote_argv(exporter);  // the policy validates the address first
      const auto path = client_path();
      if (path.empty()) {
        run_result_t missing;
        missing.code = -1;
        missing.err = "the USB/IP client is not installed on this PC";
        return missing;
      }
      argv.front() = path;
      return run_process(argv);
    } catch (const std::exception &error) {
      run_result_t refused;
      refused.code = -1;
      refused.err = error.what();
      return refused;
    }
  }

  run_result_t run_list_attached() {
    // ASK THE HELPER FIRST, and the reason for that is measured rather than theoretical.
    //
    // This used to run the client here, unprivileged, on a belief written into this file as fact:
    // that "`usbip port` printed a complete table to an ordinary user on the reference box". That
    // belief is FALSE on any machine where the host does not run as root - and on Linux the host
    // runs as `artlight`:
    //
    //     $ ls -ld /var/run/vhci_hcd/            drwx------ root root      (0700)
    //     $ sudo -u artlight usbip port
    //     libusbip: error: fopen
    //     libusbip: error: read_record
    //     Port 08: <Port in Use> at Super Speed(5000Mbps)
    //            6-1 -> unknown host, remote port and remote busid   <- never names the device
    //
    // The remote busid lives in /var/run/vhci_hcd/port<N>, which only root may read. So the
    // unprivileged lookup can see WHICH PORTS are in use and never WHICH DEVICE is on them - and a
    // device that cannot be named cannot be given back. Measured on the z13, 2026-10-06: a stream
    // took a drive from the exporter, ended, and left it attached here, because the give-back looked
    // 42 times over 20s for a name this process could never read. The drive stayed off the machine
    // that owns it until it was detached by hand.
    //
    // The helper already answers this as root - the `status` verb runs `usbip port` on the far side
    // of the privilege line, reading the very records that are unreadable here. It is read-only, it
    // takes no caller input, and it is reached over the systemd socket rather than pkexec, so there
    // is no prompt to raise. That prompt was the whole of the old objection to doing this, and it
    // died with pkexec.
    //
    // If the helper is absent or silent, this falls back to running the client here. That is the
    // blind lookup above, so it is a worse answer - but it is the answer this platform gave before,
    // and it keeps a machine without the helper behaving exactly as it did.
    const auto via_helper = run_privileged({"status"});
    if (via_helper.code == 0 && !via_helper.out.empty()) {
      return via_helper;
    }

    const auto path = client_path();
    if (path.empty()) {
      run_result_t missing;
      missing.code = -1;
      missing.err = "the USB/IP client is not installed on this PC";
      return missing;
    }
    return run_process({path, "port"});
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
    // Installed AND answering. The difference matters more here than anywhere else in this file:
    // this machine cannot take a device without root, and by the time an attach is attempted the
    // exporter has ALREADY bound the device - so a helper that is present but dead does not merely
    // fail to take it, it takes the device off the exporting machine and leaves it on neither.
    //
    // Measured on the z13 on 2026-10-06: the exporter had bound 5-1 and 5-2, the attach died inside
    // pkexec, and the keyboard and mouse were gone from both machines until they were unbound by
    // hand. The probe is asked before anything is requested, which is the only moment this can be
    // prevented from, so it has to answer the real question rather than the installed one.
    probe.helper_present = helper_reachable();

    return probe;
  }
}  // namespace input::usbip
