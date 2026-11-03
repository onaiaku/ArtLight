/**
 * @file src/platform/windows/display_diagnostics.cpp
 */
#ifdef _WIN32

  #include <winsock2.h>
  #include <windows.h>

  // standard
  #include <algorithm>
  #include <chrono>
  #include <cstdio>
  #include <ctime>
  #include <fstream>
  #include <mutex>
  #include <set>
  #include <sstream>

  // lib
  #include <nlohmann/json.hpp>

  // local
  #include "display_diagnostics.h"
  #include "src/logging.h"

  // Deliberately no platform/common.h or globals.h: those drag in video_colorspace.h and FFmpeg,
  // and this unit is also built into the display helper, which links neither.

namespace {
  using json = nlohmann::json;
  namespace fs = std::filesystem;

  std::mutex &diag_mutex() {
    static std::mutex m;
    return m;
  }

  std::string luid_to_string(const LUID &luid) {
    char buffer[40] {};
    std::snprintf(buffer, sizeof(buffer), "%08lx:%08lx",
                  static_cast<unsigned long>(luid.HighPart),
                  static_cast<unsigned long>(luid.LowPart));
    return buffer;
  }

  std::string wide_to_utf8(const wchar_t *value) {
    if (!value || !*value) {
      return {};
    }
    const int length = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
    if (length <= 1) {
      return {};
    }
    std::string buffer(static_cast<std::size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value, -1, buffer.data(), length, nullptr, nullptr);
    buffer.resize(static_cast<std::size_t>(length - 1));
    return buffer;
  }

  double rational_to_double(const DISPLAYCONFIG_RATIONAL &value) {
    if (value.Denominator == 0) {
      return 0.0;
    }
    return static_cast<double>(value.Numerator) / static_cast<double>(value.Denominator);
  }

  std::string now_iso() {
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm utc {};
    if (const auto *converted = std::gmtime(&time)) {
      utc = *converted;
    }
    char buffer[32] {};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%SZ", &utc);
    return buffer;
  }

  std::string now_stamp() {
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm local {};
    if (const auto *converted = std::localtime(&time)) {
      local = *converted;
    }
    char buffer[24] {};
    std::strftime(buffer, sizeof(buffer), "%Y%m%d-%H%M%S", &local);
    return buffer;
  }
  /// One path's mode information, whether Windows published it as a source or a
  /// target (virtual) mode.
  json mode_json(const std::vector<DISPLAYCONFIG_MODE_INFO> &modes, UINT32 index) {
    json out = json::object();
    if (index == DISPLAYCONFIG_PATH_MODE_IDX_INVALID || index >= modes.size()) {
      out["present"] = false;
      return out;
    }
    const auto &entry = modes[index];
    out["present"] = true;
    out["index"] = index;
    if (entry.infoType == DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE) {
      out["kind"] = "source";
      out["width"] = entry.sourceMode.width;
      out["height"] = entry.sourceMode.height;
      // A source mode carries no pixel rate - that lives on the target's signal info.
      out["pixel_format"] = static_cast<int>(entry.sourceMode.pixelFormat);
      out["position"] = {entry.sourceMode.position.x, entry.sourceMode.position.y};
    } else if (entry.infoType == DISPLAYCONFIG_MODE_INFO_TYPE_TARGET) {
      out["kind"] = "target";
      out["width"] = entry.targetMode.targetVideoSignalInfo.activeSize.cx;
      out["height"] = entry.targetMode.targetVideoSignalInfo.activeSize.cy;
      out["hz"] = rational_to_double(entry.targetMode.targetVideoSignalInfo.vSyncFreq);
      out["pixel_rate"] = entry.targetMode.targetVideoSignalInfo.pixelRate;
      out["total_size"] = {
        entry.targetMode.targetVideoSignalInfo.totalSize.cx,
        entry.targetMode.targetVideoSignalInfo.totalSize.cy,
      };
    } else {
      out["kind"] = "other";
    }
    return out;
  }

  json query_paths_json(std::string &error) {
    json result = json::object();
    UINT32 flags = QDC_ALL_PATHS | QDC_VIRTUAL_MODE_AWARE;
    UINT32 path_count = 0;
    UINT32 mode_count = 0;

    auto status = GetDisplayConfigBufferSizes(flags, &path_count, &mode_count);
    if (status != ERROR_SUCCESS) {
      // Older drivers reject the virtual-mode flag; fall back so the dump still
      // says something rather than nothing.
      flags = QDC_ALL_PATHS;
      status = GetDisplayConfigBufferSizes(flags, &path_count, &mode_count);
    }
    if (status != ERROR_SUCCESS) {
      error = "GetDisplayConfigBufferSizes failed: " + std::to_string(status);
      result["error"] = error;
      return result;
    }

    std::vector<DISPLAYCONFIG_PATH_INFO> paths(path_count);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(mode_count);
    status = QueryDisplayConfig(flags, &path_count, paths.data(), &mode_count, modes.data(), nullptr);
    if (status != ERROR_SUCCESS) {
      error = "QueryDisplayConfig failed: " + std::to_string(status);
      result["error"] = error;
      return result;
    }
    paths.resize(path_count);
    modes.resize(mode_count);

    result["flags"] = flags;
    result["mode_count"] = modes.size();
    json path_list = json::array();

    for (std::size_t i = 0; i < paths.size(); ++i) {
      const auto &path = paths[i];
      json entry = json::object();
      entry["index"] = i;
      entry["flags"] = path.flags;
      entry["active"] = (path.flags & DISPLAYCONFIG_PATH_ACTIVE) != 0;

      DISPLAYCONFIG_SOURCE_DEVICE_NAME source_name {};
      source_name.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
      source_name.header.size = sizeof(source_name);
      source_name.header.adapterId = path.sourceInfo.adapterId;
      source_name.header.id = path.sourceInfo.id;
      const bool have_source = DisplayConfigGetDeviceInfo(&source_name.header) == ERROR_SUCCESS;

      DISPLAYCONFIG_TARGET_DEVICE_NAME target_name {};
      target_name.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME;
      target_name.header.size = sizeof(target_name);
      target_name.header.adapterId = path.targetInfo.adapterId;
      target_name.header.id = path.targetInfo.id;
      const bool have_target = DisplayConfigGetDeviceInfo(&target_name.header) == ERROR_SUCCESS;

      entry["source"] = {
        {"luid", luid_to_string(path.sourceInfo.adapterId)},
        {"id", path.sourceInfo.id},
        {"gdi_name", have_source ? wide_to_utf8(source_name.viewGdiDeviceName) : std::string {}},
      };
      entry["target"] = {
        {"luid", luid_to_string(path.targetInfo.adapterId)},
        {"id", path.targetInfo.id},
        {"friendly_name", have_target ? wide_to_utf8(target_name.monitorFriendlyDeviceName) : std::string {}},
        {"device_path", have_target ? wide_to_utf8(target_name.monitorDevicePath) : std::string {}},
        {"output_technology", have_target ? static_cast<int>(target_name.outputTechnology) : -1},
        {"connected", path.targetInfo.targetAvailable != 0},
      };
      entry["source_mode"] = mode_json(modes, path.sourceInfo.modeInfoIdx);
      entry["target_mode"] = mode_json(modes, path.targetInfo.modeInfoIdx);
      path_list.push_back(std::move(entry));
    }

    result["paths"] = std::move(path_list);
    result["error"] = "";
    return result;
  }

  /// Every active GDI output with the full mode list Windows will offer it.
  /// This is what answers "does 2560x1440@144 actually exist on that screen".
  json outputs_json() {
    json outputs = json::array();
    for (DWORD index = 0;; ++index) {
      DISPLAY_DEVICEW adapter {};
      adapter.cb = sizeof(adapter);
      if (!EnumDisplayDevicesW(nullptr, index, &adapter, 0)) {
        break;
      }
      if ((adapter.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) == 0) {
        continue;
      }

      json entry = json::object();
      entry["gdi_name"] = wide_to_utf8(adapter.DeviceName);
      entry["description"] = wide_to_utf8(adapter.DeviceString);

      // Child monitor, for the friendly name and the hardware id.
      DISPLAY_DEVICEW child {};
      child.cb = sizeof(child);
      if (EnumDisplayDevicesW(adapter.DeviceName, 0, &child, 0)) {
        entry["monitor"] = wide_to_utf8(child.DeviceString);
        entry["monitor_id"] = wide_to_utf8(child.DeviceID);
      }

      DEVMODEW current {};
      current.dmSize = sizeof(current);
      if (EnumDisplaySettingsExW(adapter.DeviceName, ENUM_CURRENT_SETTINGS, &current, 0)) {
        entry["current"] = {
          {"width", current.dmPelsWidth},
          {"height", current.dmPelsHeight},
          {"hz", current.dmDisplayFrequency},
          {"orientation", current.dmDisplayOrientation},
        };
      } else {
        entry["current"] = nullptr;
      }

      std::set<std::string> seen;
      json mode_list = json::array();
      for (DWORD mode_index = 0; mode_list.size() < 200; ++mode_index) {
        DEVMODEW mode {};
        mode.dmSize = sizeof(mode);
        if (!EnumDisplaySettingsExW(adapter.DeviceName, mode_index, &mode, 0)) {
          break;
        }
        const auto label = std::to_string(mode.dmPelsWidth) + "x" + std::to_string(mode.dmPelsHeight) + "@" + std::to_string(mode.dmDisplayFrequency);
        if (!seen.insert(label).second) {
          continue;
        }
        mode_list.push_back(label);
      }
      entry["mode_count"] = seen.size();
      entry["modes"] = std::move(mode_list);
      outputs.push_back(std::move(entry));
    }
    return outputs;
  }
}  // namespace

namespace display_diagnostics {
  // Same rule the server uses (the executable's own config folder), written out here so this unit
  // does not depend on the platform layer to find it.
  std::filesystem::path appdata_directory() {
    WCHAR exe_path[MAX_PATH] {};
    const auto length = GetModuleFileNameW(nullptr, exe_path, static_cast<DWORD>(MAX_PATH));
    if (length == 0) {
      return {};
    }
    return std::filesystem::path {std::wstring {exe_path, length}}.remove_filename() / L"config";
  }

  std::filesystem::path output_directory() {
    std::error_code ec;
    auto directory = appdata_directory() / "logs";
    std::filesystem::create_directories(directory, ec);
    return directory;
  }

  void breadcrumb(std::string_view text) {
    std::lock_guard<std::mutex> lock {diag_mutex()};
    std::error_code ec;
    const auto path = output_directory() / "display-diag-breadcrumb.txt";
    std::ofstream out(path, std::ios::trunc);
    if (!out) {
      return;
    }
    out << now_iso() << "  " << std::string {text} << '\n';
    out.flush();
  }

  std::filesystem::path snapshot(
    std::string_view reason,
    std::string_view label,
    const std::vector<std::vector<std::string>> *requested_topology,
    const std::map<std::string, display_device::Point> *requested_positions,
    std::string_view detail
  ) {
    std::lock_guard<std::mutex> lock {diag_mutex()};

    std::string query_error;
    json document = json::object();
    document["written_at"] = now_iso();
    document["reason"] = std::string {reason};
    document["label"] = std::string {label};
    document["detail"] = std::string {detail};

    if (requested_topology) {
      document["requested_topology"] = *requested_topology;
    } else {
      document["requested_topology"] = nullptr;
    }

    if (requested_positions) {
      json positions = json::object();
      for (const auto &[device_id, point] : *requested_positions) {
        positions[device_id] = {point.m_x, point.m_y};
      }
      document["requested_positions"] = std::move(positions);
    } else {
      document["requested_positions"] = nullptr;
    }

    document["display_config"] = query_paths_json(query_error);
    document["outputs"] = outputs_json();

    std::error_code ec;
    const auto directory = output_directory();
    auto filename = std::string {"display-diag-"} + now_stamp() + "-" + std::string {reason} + ".json";
    std::replace(filename.begin(), filename.end(), ' ', '-');
    const auto path = directory / filename;

    {
      std::ofstream out(path, std::ios::trunc);
      if (out) {
        out << document.dump(2) << '\n';
        out.flush();
      }
    }
    {
      std::ofstream out(directory / "display-diag-latest.json", std::ios::trunc);
      if (out) {
        out << document.dump(2) << '\n';
        out.flush();
      }
    }

    BOOST_LOG(warning) << "Display diagnostics: " << reason << " (" << label << ") written to " << path.string()
                       << (query_error.empty() ? "" : (std::string {"; "} + query_error));
    return path;
  }

  std::filesystem::path snapshot(
    std::string_view reason,
    std::string_view label,
    const std::vector<std::string> &requested_devices,
    std::string_view detail
  ) {
    const std::vector<std::vector<std::string>> nested {requested_devices};
    return snapshot(reason, label, &nested, nullptr, detail);
  }
}  // namespace display_diagnostics

#endif
