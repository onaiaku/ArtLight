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

#include <chrono>
#include <filesystem>
#include <string>
#include <thread>
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
        // exporter's own tool is "usbipd-win" and the client is "USBip", and both are installed on
        // these boxes. A substring match puts the exporter forward as a candidate and leaves the
        // right answer resting on whether that folder happens to contain usbip.exe.
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

  namespace {
    /// Windows' own command-line quoting (the rules `CommandLineToArgvW` inverts). An argument is
    /// wrapped in quotes only when it needs to be, and a backslash run before a quote has to be
    /// doubled or it escapes the quote instead of ending it.
    std::wstring quote_arg(const std::wstring &arg) {
      if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) {
        return arg;
      }

      std::wstring quoted = L"\"";
      std::size_t backslashes = 0;
      for (const wchar_t ch : arg) {
        if (ch == L'\\') {
          ++backslashes;
          continue;
        }
        if (ch == L'"') {
          quoted.append(backslashes * 2 + 1, L'\\');
          quoted.push_back(L'"');
          backslashes = 0;
          continue;
        }
        quoted.append(backslashes, L'\\');
        backslashes = 0;
        quoted.push_back(ch);
      }
      quoted.append(backslashes * 2, L'\\');
      quoted.push_back(L'"');
      return quoted;
    }

    std::wstring to_wide(const std::string &narrow) {
      if (narrow.empty()) {
        return {};
      }
      const int size = ::MultiByteToWideChar(CP_UTF8, 0, narrow.c_str(),
                                             static_cast<int>(narrow.size()), nullptr, 0);
      if (size <= 0) {
        return {};
      }
      std::wstring wide(static_cast<std::size_t>(size), L'\0');
      ::MultiByteToWideChar(CP_UTF8, 0, narrow.c_str(), static_cast<int>(narrow.size()),
                            wide.data(), size);
      return wide;
    }

    /// Drain everything currently readable from a pipe, without blocking.
    void drain(HANDLE pipe, std::string &into) {
      for (;;) {
        DWORD available = 0;
        if (::PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr) == FALSE ||
            available == 0) {
          return;
        }
        char buffer[4096];
        DWORD read = 0;
        const DWORD wanted = available < sizeof(buffer) ? available : static_cast<DWORD>(sizeof(buffer));
        if (::ReadFile(pipe, buffer, wanted, &read, nullptr) == FALSE || read == 0) {
          return;
        }
        into.append(buffer, read);
      }
    }

    /// Run the client with the given argv (element 0 replaced by the resolved path) and bring back
    /// both streams SEPARATELY.
    ///
    /// Two pipes rather than one merged one: the reason an attach was refused arrives on stderr
    /// while the exit code says only "no", and merging the two is exactly what made a held device
    /// look unreadable on the exporter half.
    ///
    /// Both pipes are polled while the process runs, so a child that fills either buffer cannot
    /// deadlock against us - the classic way a pipe read hangs forever.
    run_result_t run_client(const std::vector<std::string> &argv) {
      run_result_t result;

      if (argv.empty()) {
        result.code = -1;
        result.err = "no command to run";
        return result;
      }

      HANDLE out_read = nullptr, out_write = nullptr;
      HANDLE err_read = nullptr, err_write = nullptr;
      SECURITY_ATTRIBUTES inheritable{};
      inheritable.nLength = sizeof(inheritable);
      inheritable.bInheritHandle = TRUE;

      if (::CreatePipe(&out_read, &out_write, &inheritable, 0) == FALSE ||
          ::CreatePipe(&err_read, &err_write, &inheritable, 0) == FALSE) {
        result.code = -1;
        result.err = "could not create a pipe: " + std::to_string(::GetLastError());
        return result;
      }
      // Our read ends must NOT be inherited, or the child holds them open and the pipe never
      // reports end-of-file.
      ::SetHandleInformation(out_read, HANDLE_FLAG_INHERIT, 0);
      ::SetHandleInformation(err_read, HANDLE_FLAG_INHERIT, 0);

      std::wstring command;
      for (const auto &part : argv) {
        if (!command.empty()) {
          command.push_back(L' ');
        }
        command += quote_arg(to_wide(part));
      }

      STARTUPINFOW startup{};
      startup.cb = sizeof(startup);
      startup.dwFlags = STARTF_USESTDHANDLES;
      startup.hStdOutput = out_write;
      startup.hStdError = err_write;
      startup.hStdInput = ::GetStdHandle(STD_INPUT_HANDLE);

      PROCESS_INFORMATION process{};
      std::wstring mutable_command = command;
      const auto started = ::CreateProcessW(nullptr, mutable_command.data(), nullptr, nullptr, TRUE,
                                            CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process);

      // The child owns the write ends now. Closing ours is what lets the read ends ever see EOF.
      ::CloseHandle(out_write);
      ::CloseHandle(err_write);

      if (started == FALSE) {
        result.code = -1;
        result.err = "could not start the client: " + std::to_string(::GetLastError());
        ::CloseHandle(out_read);
        ::CloseHandle(err_read);
        return result;
      }

      constexpr auto kTimeout = std::chrono::seconds(20);
      const auto deadline = std::chrono::steady_clock::now() + kTimeout;
      bool exited = false;
      bool timed_out = false;

      for (;;) {
        drain(out_read, result.out);
        drain(err_read, result.err);

        if (::WaitForSingleObject(process.hProcess, 50) == WAIT_OBJECT_0) {
          exited = true;
          // One last read: the child can write between our last drain and its exit.
          drain(out_read, result.out);
          drain(err_read, result.err);
          break;
        }
        if (std::chrono::steady_clock::now() > deadline) {
          timed_out = true;
          ::TerminateProcess(process.hProcess, 1);
          break;
        }
      }

      DWORD exit_code = 1;
      ::GetExitCodeProcess(process.hProcess, &exit_code);
      ::CloseHandle(out_read);
      ::CloseHandle(err_read);
      ::CloseHandle(process.hThread);
      ::CloseHandle(process.hProcess);

      if (timed_out) {
        result.code = -1;
        result.err += "\nthe client did not finish within 20 seconds and was stopped";
        return result;
      }
      result.code = exited ? static_cast<int>(exit_code) : -1;
      return result;
    }

    /// The client and the argv, refused unless there is genuinely one to run. Element 0 of the
    /// policy's argv is the bare name `usbip`; on this platform that would need PATH, which is
    /// exactly what does not work here, so it is replaced with the path we resolved.
    std::vector<std::string> argv_with_client(std::vector<std::string> argv) {
      const auto client = client_path();
      if (client.empty()) {
        throw std::runtime_error("no USB/IP client on this machine to run");
      }
      argv.front() = client;
      return argv;
    }
  }  // namespace

  run_result_t run_attach(const std::string_view exporter, const std::string_view busid) {
    try {
      return run_client(argv_with_client(build_attach_argv(exporter, busid)));
    } catch (const std::exception &error) {
      run_result_t refused;
      refused.code = -1;
      refused.err = error.what();
      return refused;
    }
  }

  run_result_t run_detach(const int port) {
    try {
      return run_client(argv_with_client(build_detach_argv(port)));
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
