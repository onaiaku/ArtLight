#include "../../../../src/platform/linux/maintenance_cli.h"

#include <cstdlib>
#include <initializer_list>
#include <string>

static void check(std::initializer_list<const char *> input,
                  std::initializer_list<const char *> expected, bool recognized = true) {
  std::vector<char *> argv;
  for (auto value : input) {
    argv.push_back(const_cast<char *>(value));
  }
  const auto result = platf::linux_cli::command(static_cast<int>(argv.size()), argv.data());
  if (result.has_value() != recognized || (result && result->size() != expected.size())) {
    std::abort();
  }
  if (result) {
    auto actual = result->begin();
    for (auto value : expected) {
      if (std::string {*actual++} != value) {
        std::abort();
      }
    }
  }
}

int main() {
  constexpr auto helper = "/usr/libexec/vibeshine/artlight-machine-host";
  check({"artlight", "configure", "alice"}, {helper, "configure", "alice"});
  // Arguments remain literal and cannot become shell commands or helper options.
  check({"artlight", "configure", "alice; touch /tmp/unwanted"}, {helper, "configure", "alice; touch /tmp/unwanted"});
  check({"artlight", "configure", "--help"}, {});
  check({"artlight", "configure"}, {});
  check({"artlight", "configure", "alice", "bob"}, {});
  check({"artlight", "migrate"}, {helper, "configure-auto"});
  check({"artlight", "reset"}, {helper, "reset"});
  check({"artlight", "reset", "anything"}, {});
  check({"artlight", "authorize-commands"}, {helper, "authorize-commands"});
  check({"artlight", "driver", "status"}, {"/usr/libexec/vibeshine/vibeshine-drm-install", "status"});
  check({"artlight", "driver", "install"}, {"/usr/libexec/vibeshine/vibeshine-drm-install", "install"});
  check({"artlight", "driver", "arbitrary-operation"}, {});
  check({"artlight", "status"}, {"/usr/bin/systemctl", "--no-pager", "--full", "status",
                                  "artlight-session-controller.service", "artlight-session-exec.socket", "artlight.service"});
  check({"artlight", "logs"}, {"/usr/bin/journalctl", "--no-pager", "-n", "200",
                                "-u", "artlight-session-controller.service", "-u", "artlight-session-exec@.service", "-u", "artlight.service"});
  check({"artlight"}, {}, false);
  check({"artlight", "/var/lib/artlight/artlight.conf"}, {}, false);
  check({"artlight", "--version"}, {}, false);
  check({"artlight", "encoder=nvenc"}, {}, false);

  char name[] = "artlight";
  char paths[] = "paths";
  char extra[] = "unexpected";
  char *argv[] = {name, paths, extra};
  if (platf::linux_cli::dispatch(2, argv) != 0 || platf::linux_cli::dispatch(3, argv) != 2) {
    return 1;
  }
  std::puts("PASS: Linux maintenance dispatch");
}
