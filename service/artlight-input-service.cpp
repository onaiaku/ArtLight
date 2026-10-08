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
 * HOW ROOT RUNS IT — AND WHY IT IS NO LONGER pkexec
 *
 * This used to be reached as `pkexec /usr/libexec/vibeshine/artlight-input-service attach ...`,
 * authorised by org.artlight.input-service.policy. That cannot work, and it was measured rather
 * than reasoned about. The caller is the ArtLight host, which is a privileged machine host: it
 * starts by calling sanitize_startup_capabilities(), which ends in
 *
 *     prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0)
 *
 * NoNewPrivileges is inherited across fork and exec and cannot be unset. The kernel therefore
 * refuses to honour pkexec's setuid bit for the host or anything it spawns, pkexec finds
 * geteuid() != 0, and it dies with `pkexec must be setuid root`. Measured on the z13 on
 * 2026-10-06: /proc/<host>/status read NoNewPrivs: 1 while pkexec on disk was 4755 and fine.
 *
 * The polkit action and rule that used to authorise that call were correct about *authorisation*
 * and blind to *capability*. Their own comment describes the ArtMoon shape - a helper called from
 * a GUI in the user's active session - and ArtLight is not that shape and never was: its caller is
 * a hardened systemd system unit. No amount of polkit fixing makes a setuid transition happen
 * inside a process that has permanently refused new privileges.
 *
 * So the privilege comes from outside the caller, which is the only place it can come from:
 * systemd owns a socket at /run/artlight/usbip-helper.sock, mode 0660 root:artlight, and starts
 * one copy of this program as root per accepted connection. The caller is the only account that
 * can reach the socket, and it cannot use it to run anything but this program:
 *
 *     systemd socket -> artlight-input-service --stdin
 *     request  (one packet): <verb> [arg [arg]]
 *     response (one packet): <code> <outlen> <errlen>\n<out bytes><err bytes>
 *
 * Both directions of that encoding live in src/usbip_helper_protocol.h, which the caller includes
 * too. A wire format written at both ends is a wire format that drifts, and this drift would land
 * on the half that runs as root.
 *
 * The verbs are unchanged, and so is every rule about what this program refuses to trust.
 *
 * WHAT IT REFUSES TO TRUST
 *
 * The caller is unprivileged and hands us two things: an exporter address and a busid. The address
 * is the more dangerous of the two, because it decides which machine this one connects to, so it
 * is validated by the SAME rule the app uses — is_usable_exporter() from the policy, linked in
 * rather than copied. A second copy living here would be a rule that drifts, and the drift would
 * be on the privileged side. This is the one place that must not happen.
 *
 * It keeps no state. It does not read the caller's list and act on it; it is told one action and
 * performs that action, and the kernel's own view is the only thing it consults.
 */

#include "src/usbip_helper_protocol.h"
#include "src/usbip_input_policy.h"

#include <sys/wait.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

using input::usbip::is_usable_exporter;
using input::usbip::is_valid_busid;
// A namespace alias, not just two using-declarations: the verbs below also name
// helper_protocol::Request and helper_protocol::kMaxRequest, and bringing in only the functions
// leaves those two unqualified names undeclared. The first build of this file said so.
namespace helper_protocol = input::usbip::helper_protocol;
using helper_protocol::decode_request;
using helper_protocol::encode_reply;

namespace {

constexpr auto kSelf = "artlight-input-service";

struct RunResult {
  int code = 0;
  std::string out;
  std::string err;
};

/// The path the client actually lives on, resolved absolutely. Never a bare name: this program runs
/// as root, so what it executes must not be decided by anything it was handed.
constexpr const char *kUsbipCandidates[] = {"/usr/bin/usbip", "/usr/local/bin/usbip", "/bin/usbip"};

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

/// Print what the tool said, so the caller (and the log) keeps the tool's own words. The streams
/// are passed in rather than written to directly, because in socket mode they are captured and
/// framed instead of going to the terminal.
void echo(const RunResult &result, std::ostream &out, std::ostream &err) {
  if (!result.out.empty()) {
    out << result.out;
    if (result.out.back() != '\n') {
      out << "\n";
    }
  }
  if (!result.err.empty()) {
    err << result.err;
    if (result.err.back() != '\n') {
      err << "\n";
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

/**
 * One verb, against the given streams. This is the whole program, and both entry points below run
 * it: argv when an administrator runs it by hand while diagnosing, and one request packet when
 * systemd is running it for the host.
 */
int runVerb(const std::string &verb, const std::vector<std::string> &args,
            std::ostream &out, std::ostream &err) {
  if (verb == "status") {
    const char *usbip = resolveUsbip();
    if (usbip == nullptr) {
      err << kSelf << ": no USB/IP client on this machine\n";
      return 2;
    }
    const auto result = runProcess({usbip, "port"});
    echo(result, out, err);
    return result.code == 0 ? 0 : 1;
  }

  if (verb == "attach") {
    if (args.size() != 2) {
      err << "usage: " << kSelf << " attach <exporter> <busid>\n";
      return 2;
    }
    const std::string &exporter = args[0];
    const std::string &busid = args[1];

    // Both of these are the CALLER'S input, and the caller is unprivileged. Validated with the
    // policy's own rules, linked in - the same functions the app uses to decide it is safe to ask.
    if (!is_usable_exporter(exporter)) {
      err << kSelf << ": refusing an unusable exporter address '" << exporter << "'\n";
      return 2;
    }
    if (!is_valid_busid(busid)) {
      err << kSelf << ": refusing '" << busid << "' - not a busid\n";
      return 2;
    }

    if (::geteuid() != 0) {
      err << kSelf << ": attaching needs administrator rights; this ran without them. "
                     "Nothing was changed.\n";
      return 3;
    }

    const char *usbip = resolveUsbip();
    if (usbip == nullptr) {
      err << kSelf << ": no USB/IP client on this machine\n";
      return 2;
    }

    if (!ensureImporterModule()) {
      err << kSelf << ": could not load the vhci-hcd module, so no device can arrive\n";
      return 1;
    }

    // argv, never a shell string. Nothing here can be re-split on the way to exec.
    const auto result = runProcess({usbip, "attach", "-r", exporter, "-b", busid});
    echo(result, out, err);
    return result.code == 0 ? 0 : 1;
  }

  if (verb == "detach" || verb == "detach-all") {
    if (verb == "detach" && args.size() != 1) {
      err << "usage: " << kSelf << " detach <port>\n";
      return 2;
    }

    int port = 0;
    if (verb == "detach" && !parsePort(args[0], port)) {
      err << kSelf << ": refusing '" << args[0] << "' - not a port\n";
      return 2;
    }

    if (::geteuid() != 0) {
      err << kSelf << ": giving a device back needs administrator rights; this ran without "
                     "them. Nothing was changed.\n";
      return 3;
    }

    const char *usbip = resolveUsbip();
    if (usbip == nullptr) {
      err << kSelf << ": no USB/IP client on this machine\n";
      return 2;
    }

    // detach takes a PORT and nothing else, which is why the session has to remember busid -> port.
    // `detach-all` is the crash-recovery sweep: if ArtLight died mid-session the devices are still
    // attached here and the exporter has lost them until someone intervenes.
    const auto result = verb == "detach-all" ? runProcess({usbip, "detach", "-a"})
                                             : runProcess({usbip, "detach", "-p", std::to_string(port)});
    echo(result, out, err);
    return result.code == 0 ? 0 : 1;
  }

  err << "usage: " << kSelf << " attach <exporter> <busid>\n"
      << "       " << kSelf << " detach <port>\n"
      << "       " << kSelf << " detach-all\n"
      << "       " << kSelf << " status\n";
  return 2;
}

/**
 * The socket entry point. systemd hands us ONE accepted connection on stdin, and the request is ONE
 * packet: `<verb> [arg [arg]]`. The reply is ONE packet, framed so the caller can hand stdout and
 * stderr back to the host as the two separate streams it classifies on. Both encodings come from
 * src/usbip_helper_protocol.h, shared with the caller, so the two ends cannot disagree.
 */
int runFromSocket() {
  std::vector<char> raw(helper_protocol::kMaxRequest);
  const ssize_t got = ::read(STDIN_FILENO, raw.data(), raw.size());
  if (got <= 0) {
    std::cerr << kSelf << ": empty request, nothing done\n";
    return 2;
  }

  helper_protocol::Request request;
  if (!decode_request(std::string_view {raw.data(), static_cast<std::size_t>(got)}, request)) {
    std::cerr << kSelf << ": empty request, nothing done\n";
    return 2;
  }

  std::ostringstream captured_out;
  std::ostringstream captured_err;
  const int code = runVerb(request.verb, request.args, captured_out, captured_err);

  const std::string packet = encode_reply(code, captured_out.str(), captured_err.str());

  // One write, one packet. A SOCK_SEQPACKET write is atomic, so a short write here would mean the
  // packet was too big for the buffer - reported rather than silently short-delivered.
  const ssize_t sent = ::write(STDOUT_FILENO, packet.data(), packet.size());
  if (sent != static_cast<ssize_t>(packet.size())) {
    std::cerr << kSelf << ": could not deliver the reply packet (" << sent << " of "
              << packet.size() << " bytes): " << std::strerror(errno) << "\n";
    return 1;
  }
  return 0;
}

}  // namespace

int main(int argc, char **argv) {
  // Fix what execv will find, before anything can run. This program runs as root and is started on
  // an unprivileged caller's request; systemd already gives it a clean PATH, but that is a promise
  // about systemd, and this is a fact about this file. It sits above the argument parsing so no
  // path through the program can precede it.
  ::setenv("PATH", "/usr/sbin:/usr/bin:/sbin:/bin", 1);

  const std::string verb = argc > 1 ? argv[1] : "";

  // systemd's entry point. One connection, one verb, and the result framed back over the socket.
  if (verb == "--stdin") {
    return runFromSocket();
  }

  // An administrator's entry point, kept deliberately: when this path misbehaves, the first useful
  // thing anyone does is run the verb by hand as root and read what it says. That must stay
  // possible without a socket in the way.
  const std::vector<std::string> args {argv + 2, argv + argc};
  return runVerb(verb, args, std::cout, std::cerr);
}
