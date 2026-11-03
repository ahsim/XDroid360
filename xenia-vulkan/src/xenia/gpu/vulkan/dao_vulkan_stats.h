// DAO: per-frame counters of the Vulkan backend's memory traffic and render
// work, for choosing what to optimize on the phone's GPU (resolves, ownership
// transfers, texture loads, uploads into the shared memory buffer, readbacks,
// render passes, submissions, the CPU's time waiting for the GPU, and when the
// depth test of pixel-shaded draws runs: before the shader or after it).
// DAO_GPU_STATS=1 enables them; every 2 seconds the averages per frame go to
// the sink (the runtime's dao.log; Xenia's own log otherwise). No GPU
// timestamps: on Turnip they stall the GPU. All counting happens on the
// command processor thread.
#ifndef XENIA_GPU_VULKAN_DAO_VULKAN_STATS_H_
#define XENIA_GPU_VULKAN_DAO_VULKAN_STATS_H_

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <string>

#include "xenia/base/logging.h"

namespace xe::gpu::vulkan::dao_stats {

struct Counters {
  uint64_t frames = 0;
  uint64_t draws = 0;
  uint64_t passes = 0;
  uint64_t resolves = 0;
  uint64_t resolve_clears = 0;
  uint64_t resolve_pixels = 0;
  uint64_t transfers = 0;
  uint64_t texture_loads = 0;
  uint64_t texture_loads_gpu_written = 0;
  uint64_t texture_load_bytes = 0;
  uint64_t uploads = 0;
  uint64_t upload_bytes = 0;
  uint64_t readbacks = 0;
  uint64_t readback_bytes = 0;
  uint64_t submissions = 0;
  uint64_t cpu_wait_us = 0;
  // Draws with a pixel shader and the depth test on: early (EarlyFragmentTests)
  // or late, by the first reason that keeps the test after the shader.
  uint64_t z_early = 0;
  uint64_t z_late_shader = 0;  // The ucode kills, writes depth or memexports.
  uint64_t z_late_alpha = 0;   // Alpha test or alpha to coverage.
  uint64_t z_late_offset = 0;  // Polygon offset applied in the shader.
  uint64_t z_late_float24 = 0;  // 20e4 depth converted in the shader.
  uint64_t z_late_query = 0;   // Occlusion query counting in the shader.
  uint64_t z_late_write = 0;   // Late ones that also write depth.
};

inline bool Enabled() {
  static const bool enabled = [] {
    const char* env = std::getenv("DAO_GPU_STATS");
    return env && env[0] && env[0] != '0';
  }();
  return enabled;
}

// Since the last report.
inline Counters& Current() {
  static Counters counters;
  return counters;
}

using Sink = void (*)(const char* line);
inline Sink& ReportSink() {
  static Sink sink = nullptr;
  return sink;
}
// Where the reports go (the runtime sets dao.log's writer).
inline void SetSink(Sink sink) { ReportSink() = sink; }

#define DAO_STATS_ADD(field, value)                          \
  do {                                                       \
    if (::xe::gpu::vulkan::dao_stats::Enabled()) {           \
      ::xe::gpu::vulkan::dao_stats::Current().field += (value); \
    }                                                        \
  } while (false)

// Adds the CPU's time in its scope to cpu_wait_us (a wait for the GPU).
class WaitTimer {
 public:
  WaitTimer() {
    if (Enabled()) start_ = std::chrono::steady_clock::now();
  }
  ~WaitTimer() {
    if (Enabled()) {
      Current().cpu_wait_us += uint64_t(
          std::chrono::duration_cast<std::chrono::microseconds>(
              std::chrono::steady_clock::now() - start_)
              .count());
    }
  }

 private:
  std::chrono::steady_clock::time_point start_;
};

// At each guest frame swap, with the frame's draws issued to the host.
inline void OnSwap(uint32_t draws_issued) {
  if (!Enabled()) {
    return;
  }
  Counters& c = Current();
  ++c.frames;
  c.draws += draws_issued;
  static auto last = std::chrono::steady_clock::now();
  const auto now = std::chrono::steady_clock::now();
  const double seconds = std::chrono::duration<double>(now - last).count();
  if (seconds < 2.0) {
    return;
  }
  const double f = double(c.frames);
  const std::string line = fmt::format(
      "GPU stats: {} frames in {:.1f} s, per frame: draws {:.0f}, passes "
      "{:.0f}, resolves {:.1f} ({:.2f} MP, {:.1f} with clear), transfers "
      "{:.1f}, texture loads {:.1f} ({:.2f} MB, {:.1f} of GPU-written "
      "memory), uploads {:.1f} ({:.2f} MB), readbacks {:.2f} ({:.2f} MB), "
      "submissions {:.1f}, CPU waited for the GPU {:.2f} ms, depth-tested "
      "draws: early {:.0f}, late {:.0f} (shader {:.0f}, alpha {:.0f}, "
      "offset {:.0f}, float24 {:.0f}, query {:.0f}; {:.0f} writing depth)",
      c.frames, seconds, c.draws / f, c.passes / f, c.resolves / f,
      c.resolve_pixels / f / 1e6, c.resolve_clears / f, c.transfers / f,
      c.texture_loads / f, c.texture_load_bytes / f / 1048576.0,
      c.texture_loads_gpu_written / f, c.uploads / f,
      c.upload_bytes / f / 1048576.0, c.readbacks / f,
      c.readback_bytes / f / 1048576.0, c.submissions / f,
      c.cpu_wait_us / f / 1000.0, c.z_early / f,
      (c.z_late_shader + c.z_late_alpha + c.z_late_offset + c.z_late_float24 +
       c.z_late_query) / f,
      c.z_late_shader / f, c.z_late_alpha / f, c.z_late_offset / f,
      c.z_late_float24 / f, c.z_late_query / f, c.z_late_write / f);
  if (ReportSink()) {
    ReportSink()(line.c_str());
  } else {
    XELOGI("{}", line);
  }
  c = {};
  last = now;
}

}  // namespace xe::gpu::vulkan::dao_stats

#endif  // XENIA_GPU_VULKAN_DAO_VULKAN_STATS_H_
