/**
 * @file src/platform/linux/maintenance_cli.h
 * @brief Native-package maintenance commands, dispatched before host startup.
 */
#pragma once

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string_view>
#include <vector>

#include <unistd.h>

namespace platf::linux_cli {
  inline constexpr const char *help =
    "Linux maintenance (native packages):\n"
    "  artlight paths                    Show settings and program locations\n"
    "  artlight status                   Show machine service status\n"
    "  sudo artlight logs                 Show recent service logs\n"
    "  sudo artlight configure USER       Select the desktop owner and migrate settings\n"
    "  sudo artlight migrate              Prepare existing settings after an upgrade\n"
    "  sudo artlight authorize-commands   Approve commands in the application list\n"
    "  sudo artlight driver install       Install/update the virtual-display driver\n"
    "  sudo artlight driver status        Show virtual-display driver status\n"
    "  sudo artlight reset                Erase settings and pairings; ends active streams\n";

  // A missing optional means this is a normal host invocation. An empty vector
  // denotes a recognized command with invalid arguments; it must never fall
  // through to configuration parsing or start another host.
  inline std::optional<std::vector<const char *>> command(int argc, char **argv) {
    if (argc < 2) {
      return std::nullopt;
    }
    const std::string_view name {argv[1]};
    constexpr auto machine = "/usr/libexec/vibeshine/artlight-machine-host";
    if (name == "configure") {
      if (argc == 3 && argv[2][0] != '\0' && argv[2][0] != '-') {
        return std::vector<const char *> {machine, "configure", argv[2]};
      }
    } else if (name == "migrate" || name == "authorize-commands" || name == "reset") {
      if (argc == 2) {
        return std::vector<const char *> {machine, name == "migrate" ? "configure-auto" : argv[1]};
      }
    } else if (name == "driver") {
      if (argc == 3 && (std::string_view {argv[2]} == "install" || std::string_view {argv[2]} == "status")) {
        return std::vector<const char *> {"/usr/libexec/vibeshine/vibeshine-drm-install", argv[2]};
      }
    } else if (name == "status") {
      if (argc == 2) {
        return std::vector<const char *> {"/usr/bin/systemctl", "--no-pager", "--full", "status",
                                        "artlight-session-controller.service", "artlight-session-exec.socket", "artlight.service"};
      }
    } else if (name == "logs") {
      if (argc == 2) {
        return std::vector<const char *> {"/usr/bin/journalctl", "--no-pager", "-n", "200",
                                        "-u", "artlight-session-controller.service", "-u", "artlight-session-exec@.service",
                                        "-u", "artlight.service"};
      }
    } else if (name != "paths" && name != "maintenance-help") {
      return std::nullopt;
    }
    return std::vector<const char *> {};
  }

  inline std::optional<int> dispatch(int argc, char **argv) {
    auto arguments = command(argc, argv);
    if (!arguments) {
      return std::nullopt;
    }
    if (argc == 2 && std::string_view {argv[1]} == "paths") {
      std::puts("Native Linux package locations:\n"
                "  Settings, credentials, pairings, apps: /var/lib/artlight\n"
                "  Configuration file: /var/lib/artlight/artlight.conf\n"
                "  Administrator policy: /etc/artlight\n"
                "  Temporary session data: /run/artlight\n"
                "  Programs: /usr/bin/artlight, /usr/libexec/vibeshine\n"
                "  Assets: /usr/share/artlight\n"
                "  Logs: sudo artlight logs\n"
                "  Legacy user settings (imported once): ~/.config/artlight");
      return 0;
    }
    if (argc == 2 && std::string_view {argv[1]} == "maintenance-help") {
      std::fputs(help, stdout);
      return 0;
    }
    if (arguments->empty()) {
      std::fputs(help, stderr);
      return 2;
    }
    arguments->push_back(nullptr);
    // Fixed executables and separate arguments: no shell, PATH search, or
    // privilege escalation. Administrative helpers enforce their own UID check.
    execv(arguments->front(), const_cast<char *const *>(arguments->data()));
    std::fprintf(stderr, "ArtLight: cannot execute %s: %s\n", arguments->front(), std::strerror(errno));
    return 1;
  }
}  // namespace platf::linux_cli
