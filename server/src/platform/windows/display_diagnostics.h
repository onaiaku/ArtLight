/**
 * @file src/platform/windows/display_diagnostics.h
 * @brief Whole-picture display diagnostics for when a topology will not apply.
 *
 * The display stack fails at several layers (the composer, libdisplaydevice, the
 * helper process, SetDisplayConfig itself). Each layer used to report only its own
 * line, so a failure took a build per question. This dumps everything the machine
 * knows about its displays into one JSON file on every failure: the requested
 * topology, every path Windows currently has, every output's full mode list, and a
 * breadcrumb naming the exact call that was in flight. One build, all the answers.
 */
#pragma once

#ifdef _WIN32

  #include <display_device/types.h>

  #include <filesystem>
  #include <map>
  #include <string>
  #include <string_view>
  #include <vector>

namespace display_diagnostics {
  /// Directory the diagnostics are written beside the log. Created on demand.
  std::filesystem::path output_directory();

  /// Overwrite a small "last operation" file. Cheap, flushed, safe from any thread.
  /// Written immediately before anything that can take the helper process down,
  /// so a dead pipe still leaves a note saying which call died.
  void breadcrumb(std::string_view text);

  /// Write a full JSON snapshot. Returns the file written (empty on failure).
  /// `requested_topology` and `requested_positions` may be null.
  std::filesystem::path snapshot(
    std::string_view reason,
    std::string_view label,
    const std::vector<std::vector<std::string>> *requested_topology,
    const std::map<std::string, display_device::Point> *requested_positions,
    std::string_view detail = {}
  );

  /// Same, but for callers holding an ActiveTopology-shaped request already
  /// flattened to device ids.
  std::filesystem::path snapshot(
    std::string_view reason,
    std::string_view label,
    const std::vector<std::string> &requested_devices,
    std::string_view detail = {}
  );
}  // namespace display_diagnostics

#endif
