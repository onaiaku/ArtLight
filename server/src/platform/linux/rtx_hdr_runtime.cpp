#include "rtx_hdr_runtime.h"

#include "src/config.h"
#include "src/process.h"

#include <algorithm>
#include <atomic>

namespace platf::rtx_hdr {
  namespace {
    std::atomic<std::shared_ptr<const frame_settings_t>> current {std::make_shared<const frame_settings_t>()};
    std::atomic<std::uint32_t> generation {0};
  }  // namespace

  void notify_live_settings_changed() {
    auto next = std::make_shared<frame_settings_t>();
    const auto &values = config::video.rtx_hdr;
    next->enabled = values.enabled && config::runtime_config_override_enabled("rtx_hdr");
    next->sdr_brightness = std::clamp(values.sdr_brightness, 0, 100);
    next->values = {std::clamp(values.contrast + 100, 0, 200), std::clamp(values.saturation + 100, 0, 200), std::clamp(values.middle_gray, 10, 100), std::clamp(values.peak_brightness, 400, 2000)};
    const auto previous = current.load();
    if (previous->generation && previous->enabled == next->enabled && previous->values == next->values && previous->sdr_brightness == next->sdr_brightness) {
      return;
    }
    next->generation = generation.fetch_add(1) + 1;
    current.store(std::move(next));
  }

  std::shared_ptr<const frame_settings_t> frame_settings() {
    return current.load();
  }

  live_output_metadata_state_t live_output_metadata_state() {
    const auto settings = current.load();
    return {settings->generation, settings->values.peak};
  }

  foreground_t::foreground_t():
      worker([this](std::stop_token stop) {
        poll(stop);
      }) {}

  foreground_t::~foreground_t() {
    worker.request_stop();
    wake.notify_all();
  }

  bool foreground_t::matches() const {
    // Upstream asks its machine service which window owns the desktop, so the conversion can
    // follow one application. This fork has no such service yet, so a live stream with RTX HDR
    // switched on is the whole condition: same picture, without the per-app rule. When that
    // service arrives, only this poll changes - the callers do not.
    return matched.load();
  }

  void foreground_t::poll(std::stop_token stop) {
    while (!stop.stop_requested()) {
      const int id = proc::proc.current_app_id();
      app_id.store(id);
      matched.store(id > 0);
      std::unique_lock lock {mutex};
      wake.wait_for(lock, stop, std::chrono::milliseconds(500), [] {
        return false;
      });
    }
    matched.store(false);
  }
}  // namespace platf::rtx_hdr
