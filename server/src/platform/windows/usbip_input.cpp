/**
 * @file src/platform/windows/usbip_input.cpp
 * @brief The Windows importer's edge: find the client, and find out whether anything could arrive.
 *
 * The one thing this file exists to get right is that **the client is not on PATH here**, and a
 * PATH lookup is therefore a lie. Measured on the reference machine: `usbip.exe` sits in
 * `C:\Program Files\USBip\`, the `usbip2_ude` driver is present and running, and the binary is
 * nowhere on PATH — so a PATH probe reports "not installed" on a machine that is fully working.
 * That false negative is what put nonsense into the first draft of this feature's plan, and this
 * file is written so it cannot come back.
 *
 * The other thing it exists to get right is what it does NOT do. On Windows, attach was proven to
 * work from a UAC-filtered limited token, so `privilege_required` stays false and no helper is
 * wanted. The asymmetry with Linux is real; do not "fix" it by adding a daemon Windows does not
 * need.
 */
#include "src/usbip_input.h"

#include <windows.h>

#include <filesystem>
#include <string>
#include <vector>

namespace input::usbip {
  namespace {
    namespace fs = std::filesystem;

    /// Where our installer puts the client. Checked first: it is the normal case, it costs one
    /// stat, and it is the location the bundling decision fixed.
    constexpr auto kDefaultClient = R"(C:\Program Files\USBip\usbip.exe)";

    /// The driver that makes a device actually arrive. `usbip2_filter` sits beside it and is not
    /// required here — demanding both would turn a working install into a reported failure, and a
    /// false negative on this path is worse than a missed extra check.
    constexpr auto kUdeServiceKey = L"SYSTEM\\CurrentControlSet\\Services\\usbip2_ude";
    constexpr auto kUninstallKey = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall";

    std::string to_utf8(const std::wstring &wide) {
      if (wide.empty()) {
        return {};
      }
      const int size = ::WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()),
                                             nullptr, 0, nullptr, nullptr);
      if (size <= 0) {
        return {};
      }
      std::string narrow(static_cast<std::size_t>(size), '\0');
      ::WideCharToMultiByte(CP_UTF8, 0, wide.c_str(), static_cast<int>(wide.size()),
                            narrow.data(), size, nullptr, nullptr);
      return narrow;
    }

    std::wstring read_string_value(HKEY key, const wchar_t *name) {
      DWORD type = 0;
      DWORD bytes = 0;
      if (::RegQueryValueExW(key, name, nullptr, &type, nullptr, &bytes) != ERROR_SUCCESS) {
        return {};
      }
      if (type != REG_SZ && type != REG_EXPAND_SZ) {
        return {};
      }
      std::wstring value(bytes / sizeof(wchar_t), L'\0');
      if (::RegQueryValueExW(key, name, nullptr, &type,
                             reinterpret_cast<BYTE *>(value.data()), &bytes) != ERROR_SUCCESS) {
        return {};
      }
      while (!value.empty() && value.back() == L'\0') {
        value.pop_back();
      }
      return value;
    }

    /// Whether a registry key exists. The same shape of test ArtMoon's exporter half makes: the
    /// driver's presence is the honest gate, because it is what decides whether a device can
    /// arrive at all.
    bool key_exists(HKEY root, const wchar_t *path) {
      HKEY key = nullptr;
      const auto status = ::RegOpenKeyExW(root, path, 0, KEY_READ | KEY_WOW64_64KEY, &key);
      if (key != nullptr) {
        ::RegCloseKey(key);
      }
      return status == ERROR_SUCCESS;
    }

    /// Ask the uninstall record where USBip went, for an install that is not in the default place.
    /// The record is the authority on "is this installed on this machine"; guessing at drive
    /// letters is not.
    std::string client_from_uninstall_record() {
      HKEY root = nullptr;
      if (::RegOpenKeyExW(HKEY_LOCAL_MACHINE, kUninstallKey, 0, KEY_READ | KEY_WOW64_64KEY, &root) !=
          ERROR_SUCCESS) {
        return {};
      }

      std::string found;
      for (DWORD index = 0; found.empty(); ++index) {
        wchar_t name[512] = {};
        DWORD name_length = static_cast<DWORD>(std::size(name));
        if (::RegEnumKeyExW(root, index, name, &name_length, nullptr, nullptr, nullptr, nullptr) !=
            ERROR_SUCCESS) {
          break;
        }

        HKEY entry = nullptr;
        if (::RegOpenKeyExW(root, name, 0, KEY_READ | KEY_WOW64_64KEY, &entry) != ERROR_SUCCESS) {
          continue;
        }

        // names_the_client, NOT a substring match. Measured on the reference machines: the
        // exporter's own tool is "usbipd-win" and the client is "USBip", and both live on these
        // boxes. A loose match reads the exporter as "the client is installed" on a machine with
        // no client at all.
        const auto display_name = read_string_value(entry, L"DisplayName");
        if (!display_name.empty() && names_the_client(to_utf8(display_name))) {
          const auto location = read_string_value(entry, L"InstallLocation");
          if (!location.empty()) {
            std::error_code ec;
            const fs::path candidate = fs::path(location) / "usbip.exe";
            if (fs::is_regular_file(candidate, ec)) {
              found = to_utf8(candidate.wstring());
            }
          }
        }
        ::RegCloseKey(entry);
      }

      ::RegCloseKey(root);
      return found;
    }
  }  // namespace

  std::string client_path() {
    std::error_code ec;
    if (fs::is_regular_file(fs::path(kDefaultClient), ec)) {
      return kDefaultClient;
    }

    if (const auto from_record = client_from_uninstall_record(); !from_record.empty()) {
      return from_record;
    }

    // Deliberately no PATH lookup. See the file comment: the binary is not on PATH on a fully
    // working install, so searching PATH here would report "not installed" for a machine that is
    // fine — and a false negative on this path is what this file exists to prevent.
    return {};
  }

  ClientProbe probe_client() {
    ClientProbe probe;

    probe.client_path = client_path();
    probe.client_found = !probe.client_path.empty();

    probe.driver_present = key_exists(HKEY_LOCAL_MACHINE, kUdeServiceKey);

    // Windows has no "present but not loaded" state for this driver: if the service exists the
    // driver loads with it. The field stays false rather than being faked into meaning something.
    probe.driver_loadable = false;

    // Proven while proving `bind`: a limited token attached successfully, so no elevation and no
    // helper. This is the asymmetry with Linux, and it is deliberate.
    probe.privilege_required = false;
    probe.helper_present = false;

    return probe;
  }
}  // namespace input::usbip
