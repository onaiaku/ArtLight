/*
 * artlight-input-service — the privileged half of "the importer never attaches by itself".
 *
 * WHY THIS EXISTS
 *
 * On Linux, taking a device from an exporter is not something an ordinary program can do.
 * `usbip attach` ends up writing to /sys/devices/platform/vhci_hcd.0/attach, which is mode 0600
 * owned by root, so an unprivileged call dies with `usbip: error: import device` and exit 1. That
 * was measured on the z13, not assumed. On Windows the same call succeeds from a UAC-filtered
 * limited token, and that asymmetry is real: Windows drives the client directly and needs no
 * service. Do not "fix" Windows by adding a daemon it provably does not need.
 *
 * So this is the Linux importer's equivalent of ArtMoon's exporter helper, reached the same way:
 *
 *     pkexec /usr/libexec/artlight-input-service attach <exporter> <busid>
 *     pkexec /usr/libexec/artlight-input-service detach <port>
 *     pkexec /usr/libexec/artlight-input-service detach-all
 *     pkexec /usr/libexec/artlight-input-service status
 *
 * authorised by org.artlight.input-service.policy, which grants the local ACTIVE session the right
 * to run exactly this program as root and nothing else. The password the user gives is the one
 * that placed this file and that policy — not one per device.
 *
 * WHAT IT REFUSES TO TRUST
 *
 * The caller is unprivileged and hands us two things: an exporter address and a busid. The address
 * is the more dangerous of the two, because it decides which machine this one connects to, so it
 * is validated by the SAME rule the app uses — is_usable_exporter() from the policy, linked in
 * rather than copied. A second copy living here would be a rule that drifts, and the drift would be
 * on the privileged side. This is the one place that must not happen.
 *
 * It keeps no state. It does not read the caller's list and act on it; it is told one action and
 * performs that action, and the kernel's own view is the only thing it consults.
 */

#include "src/usbip_input_policy.h"

#include <sys/wait.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

using input::usbip::is_usable_exporter;
using input::usbip::is_valid_busid;

namespace {

constexpr auto kSelf = "artlight-input-service";

// The path the client actually lives on, resolved absolutely. Never a bare name: this program runs
// as root, so what it executes must not be decided by anything it was handed.
constexpr const char *kUsbipCandidates[] = {"/usr/bin/usbip", "/usr/local/bin/usbip", "/bin/usbip"};

struct RunResult {
  int code = 0;
  std::string out;
  std::string err;
};

const char *resolveUsbip() {
  for (const auto *candidate : kUsbipCandidates) {
    if (::access(candidate, X_OK) == 0) {
      return candidate;
    }
  }
  return nullptr;
}

/**
 * Run a command and bring back BOTH streams separately.
 *
 * Separate, not merged, and that is not tidiness. The reason a tool refused arrives on stderr while
 * the exit code says only "no", and the caller classifies this result — a merged buffer turns a
 * diagnosable refusal into an anonymous one. The exporter half already learned this the hard way.
 */
RunResult runProcess(const std::vector<std::string> &argv) {
  RunResult result;

  int outPipe[2] = {-1, -1};
  int errPipe[2] = {-1, -1};
  if (::pipe(outPipe) != 0 || ::pipe(errPipe) != 0) {
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
    ::dup2(outPipe[1], STDOUT_FILENO);
    ::dup2(errPipe[1], STDERR_FILENO);
    ::close(outPipe[0]);
    ::close(outPipe[1]);
    ::close(errPipe[0]);
    ::close(errPipe[1]);

    std::vector<char *> cargv;
    cargv.reserve(argv.size() + 1);
    for (const auto &part : argv) {
      cargv.push_back(const_cast<char *>(part.c_str()));
    }
    cargv.push_back(nullptr);
    ::execv(cargv[0], cargv.data());

    // execv only returns on failure.
    std::cerr << kSelf << ": could not run " << argv[0] << ": " << std::strerror(errno) << "\n";
    ::_exit(127);
  }

  ::close(outPipe[1]);
  ::close(errPipe[1]);

  // Read both to EOF. If the child fills the stderr buffer while we are waiting on stdout, the
  // other pipe is still open and we will get to it - the child blocks, it does not deadlock against
  // us - but we must finish BOTH before reaping, or the buffered remainder is lost.
  auto drain = [](const int fd, std::string &into) {
    char buffer[4096];
    ssize_t read = 0;
    while ((read = ::read(fd, buffer, sizeof(buffer))) > 0) {
      into.append(buffer, static_cast<std::size_t>(read));
    }
  };
  drain(outPipe[0], result.out);
  drain(errPipe[0], result.err);

  ::close(outPipe[0]);
  ::close(errPipe[0]);

  int status = 0;
  ::waitpid(child, &status, 0);
  result.code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
  return result;
}

/// Print what the tool said, so the caller (and the log) keeps the tool's own words.
void echo(const RunResult &result) {
  if (!result.out.empty()) {
    std::cout << result.out;
    if (result.out.back() != '\n') {
      std::cout << "\n";
    }
  }
  if (!result.err.empty()) {
    std::cerr << result.err;
    if (result.err.back() != '\n') {
      std::cerr << "\n";
    }
  }
}

/// The vhci-hcd module is the gate on Linux, not the binary: a machine can have usbip installed and
/// still be unable to import anything because the module is not loaded. Loading it is privileged,
/// which is part of why this helper exists at all.
bool ensureImporterModule() {
  if (::access("/sys/module/vhci_hcd", F_OK) == 0) {
    return true;
  }
  const std::vector<std::string> modprobe = {"/usr/sbin/modprobe", "vhci_hcd"};
  const auto result = runProcess(modprobe);
  return result.code == 0 && ::access("/sys/module/vhci_hcd", F_OK) == 0;
}

bool parsePort(const std::string &text, int &port) {
  if (text.empty() || text.size() > 3) {
    return false;
  }
  for (const char c : text) {
    if (c < '0' || c > '9') {
      return false;
    }
  }
  port = std::stoi(text);
  // The Linux tool numbers ports FROM 0, unlike usbip-win2 which starts at 1 - so 0 must be
  // accepted here or the first port on every Linux machine could never be released.
  return port >= 0 && port <= 255;
}

}  // namespace

int main(int argc, char **argv) {
  // Fix what execv will find, before anything can run. This program runs as root and is started by
  // an unprivileged process; pkexec sanitises PATH, but that is a promise about pkexec, and this is
  // a fact about this file. It sits above the argument parsing so no path through the program can
  // precede it.
  ::setenv("PATH", "/usr/sbin:/usr/bin:/sbin:/bin", 1);

  const std::string verb = argc > 1 ? argv[1] : "";

  if (verb == "status") {
    const char *usbip = resolveUsbip();
    if (usbip == nullptr) {
      std::cerr << kSelf << ": no USB/IP client on this machine\n";
      return 2;
    }
    const auto result = runProcess({usbip, "port"});
    echo(result);
    return result.code == 0 ? 0 : 1;
  }

  if (verb == "attach") {
    if (argc != 4) {
      std::cerr << "usage: " << kSelf << " attach <exporter> <busid>\n";
      return 2;
    }
    const std::string exporter = argv[2];
    const std::string busid = argv[3];

    // Both of these are the CALLER'S input, and the caller is unprivileged. Validated with the
    // policy's own rules, linked in - the same functions the app uses to decide it is safe to ask.
    if (!is_usable_exporter(exporter)) {
      std::cerr << kSelf << ": refusing an unusable exporter address '" << exporter << "'\n";
      return 2;
    }
    if (!is_valid_busid(busid)) {
      std::cerr << kSelf << ": refusing '" << busid << "' - not a busid\n";
      return 2;
    }

    if (::geteuid() != 0) {
      std::cerr << kSelf << ": attaching needs administrator rights; this ran without them. "
                            "Nothing was changed.\n";
      return 3;
    }

    const char *usbip = resolveUsbip();
    if (usbip == nullptr) {
      std::cerr << kSelf << ": no USB/IP client on this machine\n";
      return 2;
    }

    if (!ensureImporterModule()) {
      std::cerr << kSelf << ": could not load the vhci-hcd module, so no device can arrive\n";
      return 1;
    }

    // argv, never a shell string. Nothing here can be re-split on the way to exec.
    const auto result = runProcess({usbip, "attach", "-r", exporter, "-b", busid});
    echo(result);
    return result.code == 0 ? 0 : 1;
  }

  if (verb == "detach" || verb == "detach-all") {
    if (verb == "detach" && argc != 3) {
      std::cerr << "usage: " << kSelf << " detach <port>\n";
      return 2;
    }

    int port = 0;
    if (verb == "detach") {
      if (!parsePort(argv[2], port)) {
        std::cerr << kSelf << ": refusing '" << argv[2] << "' - not a port\n";
        return 2;
      }
    }

    if (::geteuid() != 0) {
      std::cerr << kSelf << ": giving a device back needs administrator rights; this ran without "
                            "them. Nothing was changed.\n";
      return 3;
    }

    const char *usbip = resolveUsbip();
    if (usbip == nullptr) {
      std::cerr << kSelf << ": no USB/IP client on this machine\n";
      return 2;
    }

    // detach takes a PORT and nothing else, which is why the session has to remember busid -> port.
    // `detach-all` is the crash-recovery sweep: if ArtLight died mid-session the devices are still
    // attached here and the exporter has lost them until someone intervenes.
    const auto result = verb == "detach-all" ? runProcess({usbip, "detach", "-a"})
                                             : runProcess({usbip, "detach", "-p", std::to_string(port)});
    echo(result);
    return result.code == 0 ? 0 : 1;
  }

  std::cerr << "usage: " << kSelf << " attach <exporter> <busid>\n"
            << "       " << kSelf << " detach <port>\n"
            << "       " << kSelf << " detach-all\n"
            << "       " << kSelf << " status\n";
  return 2;
}
