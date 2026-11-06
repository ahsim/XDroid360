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

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

#include "xenia/base/logging.h"

namespace xe::gpu::vulkan::dao_timing {
// dao_vulkan_timing.cc.
uint64_t ThreadCpuTimeUs();
std::string TakeSummary();
}  // namespace xe::gpu::vulkan::dao_timing

namespace xe::gpu::vulkan::dao_stats {

// What the backend is doing outside the guest's draws (set by
// dao_timing::WorkScope), so a render pass ended for a barrier says whose.
enum class Activity : uint32_t { kNone, kTextureLoad, kResolve, kTransfer };
inline Activity& CurrentActivity() {
  static Activity activity = Activity::kNone;
  return activity;
}

// Why a render pass ended, as the caller of EndRenderPass says
// (PendingPassEnd), split for barriers by the current activity.
enum class PassEnd : uint32_t {
  kOther,
  kRenderTargets,  // The next draw uses other render targets.
  kBarrier,        // A pipeline barrier (by Activity below).
  kQuery,          // Occlusion query segments.
  kVizPredicate,   // Copies of VIZ query predicates.
  kSubmission,     // The submission ends.
};
enum PassEndIndex : uint32_t {
  kPassEndRenderTargets,
  kPassEndBarrierTextureLoad,
  kPassEndBarrierResolve,
  kPassEndBarrierTransfer,
  kPassEndBarrierOther,
  kPassEndQuery,
  kPassEndVizPredicate,
  kPassEndSubmission,
  kPassEndOther,
  kPassEndCount,
};
inline PassEnd& PendingPassEnd() {
  static PassEnd reason = PassEnd::kOther;
  return reason;
}

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
  // GPU-written texture loads by the texture's (base) xenos::TextureFormat.
  uint64_t texture_loads_gpu_written_by_format[64] = {};
  // Render passes ended, by PassEndIndex.
  uint64_t pass_ends[kPassEndCount] = {};
  // Texture loads: the texture's first, or again after the CPU or the GPU
  // wrote its memory; and the ones BC-decompressed on the GPU (guest bytes).
  uint64_t texture_loads_first = 0;
  uint64_t texture_loads_again_cpu = 0;
  uint64_t texture_loads_again_gpu = 0;
  uint64_t texture_loads_bc_decompressed = 0;
  uint64_t texture_load_bc_decompressed_bytes = 0;
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

// A render pass ends, for the reason PendingPassEnd holds (reset after).
inline void OnPassEnd() {
  PassEnd reason = PendingPassEnd();
  PendingPassEnd() = PassEnd::kOther;
  if (!Enabled()) {
    return;
  }
  uint32_t index = kPassEndOther;
  switch (reason) {
    case PassEnd::kRenderTargets:
      index = kPassEndRenderTargets;
      break;
    case PassEnd::kBarrier:
      switch (CurrentActivity()) {
        case Activity::kTextureLoad:
          index = kPassEndBarrierTextureLoad;
          break;
        case Activity::kResolve:
          index = kPassEndBarrierResolve;
          break;
        case Activity::kTransfer:
          index = kPassEndBarrierTransfer;
          break;
        default:
          index = kPassEndBarrierOther;
          break;
      }
      break;
    case PassEnd::kQuery:
      index = kPassEndQuery;
      break;
    case PassEnd::kVizPredicate:
      index = kPassEndVizPredicate;
      break;
    case PassEnd::kSubmission:
      index = kPassEndSubmission;
      break;
    default:
      break;
  }
  ++Current().pass_ends[index];
}

// Graphics pipelines created, and the CPU time vkCreateGraphicsPipelines took
// (any thread: the asynchronous creation threads too).
inline std::atomic<uint64_t>& PipelinesCreated() {
  static std::atomic<uint64_t> count{0};
  return count;
}
inline std::atomic<uint64_t>& PipelineCreationUs() {
  static std::atomic<uint64_t> us{0};
  return us;
}
inline void OnPipelineCreated(uint64_t us) {
  if (!Enabled()) {
    return;
  }
  PipelinesCreated().fetch_add(1, std::memory_order_relaxed);
  PipelineCreationUs().fetch_add(us, std::memory_order_relaxed);
}

// The median and the 95th percentile of the samples (sorts them).
inline std::pair<double, double> MedianAndP95(std::vector<float>& samples) {
  if (samples.empty()) {
    return {0.0, 0.0};
  }
  std::sort(samples.begin(), samples.end());
  return {samples[samples.size() / 2],
          samples[std::min(samples.size() - 1, samples.size() * 95 / 100)]};
}

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
  // Per frame (the command processor thread, which this runs on): the time
  // since the previous swap, the thread's CPU time, and its waits for the GPU.
  static auto last_swap = now;
  static uint64_t last_cpu_us = dao_timing::ThreadCpuTimeUs();
  static uint64_t last_wait_us = 0;
  static std::vector<float> interval_ms, cpu_ms, wait_ms;
  const uint64_t cpu_us = dao_timing::ThreadCpuTimeUs();
  interval_ms.push_back(
      std::chrono::duration<float, std::milli>(now - last_swap).count());
  cpu_ms.push_back(float(cpu_us - last_cpu_us) / 1000.0f);
  wait_ms.push_back(float(c.cpu_wait_us - last_wait_us) / 1000.0f);
  last_swap = now;
  last_cpu_us = cpu_us;
  last_wait_us = c.cpu_wait_us;
  const double seconds = std::chrono::duration<double>(now - last).count();
  if (seconds < 2.0) {
    return;
  }
  const double f = double(c.frames);
  std::string by_format;
  for (uint32_t i = 0; i < 64; ++i) {
    if (c.texture_loads_gpu_written_by_format[i]) {
      by_format += fmt::format(" {}:{:.1f}", i,
                               c.texture_loads_gpu_written_by_format[i] / f);
    }
  }
  const std::string line = fmt::format(
      "GPU stats: {} frames in {:.1f} s, per frame: draws {:.0f}, passes "
      "{:.0f}, resolves {:.1f} ({:.2f} MP, {:.1f} with clear), transfers "
      "{:.1f}, texture loads {:.1f} ({:.2f} MB, {:.1f} of GPU-written "
      "memory), uploads {:.1f} ({:.2f} MB), readbacks {:.2f} ({:.2f} MB), "
      "submissions {:.1f}, CPU waited for the GPU {:.2f} ms, depth-tested "
      "draws: early {:.0f}, late {:.0f} (shader {:.0f}, alpha {:.0f}, "
      "offset {:.0f}, float24 {:.0f}, query {:.0f}; {:.0f} writing depth), "
      "GPU-written loads by texture format:{}",
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
      c.z_late_float24 / f, c.z_late_query / f, c.z_late_write / f,
      by_format.empty() ? std::string(" none") : by_format);
  const auto [interval_p50, interval_p95] = MedianAndP95(interval_ms);
  const auto [cpu_p50, cpu_p95] = MedianAndP95(cpu_ms);
  const auto [wait_p50, wait_p95] = MedianAndP95(wait_ms);
  interval_ms.clear();
  cpu_ms.clear();
  wait_ms.clear();
  const uint64_t pipelines = PipelinesCreated().exchange(0);
  const uint64_t pipeline_us = PipelineCreationUs().exchange(0);
  const uint64_t* e = c.pass_ends;
  const std::string frame_line = fmt::format(
      "GPU stats, frame (p50/p95 ms): interval {:.1f}/{:.1f}, GPU commands "
      "thread CPU {:.1f}/{:.1f}, its waits for the GPU {:.1f}/{:.1f}; pass "
      "ends per frame: RT change {:.1f}, barrier for texture load {:.1f} / "
      "resolve {:.1f} / transfer {:.1f} / other {:.1f}, query {:.1f}, VIZ "
      "{:.1f}, submission {:.1f}, other {:.1f}; texture loads per frame: "
      "first {:.1f}, again after CPU write {:.1f}, after GPU write {:.1f}, "
      "BC decompressed {:.1f} ({:.2f} MB); new pipelines {} ({:.0f} ms CPU){}",
      interval_p50, interval_p95, cpu_p50, cpu_p95, wait_p50, wait_p95,
      e[kPassEndRenderTargets] / f, e[kPassEndBarrierTextureLoad] / f,
      e[kPassEndBarrierResolve] / f, e[kPassEndBarrierTransfer] / f,
      e[kPassEndBarrierOther] / f, e[kPassEndQuery] / f,
      e[kPassEndVizPredicate] / f, e[kPassEndSubmission] / f,
      e[kPassEndOther] / f, c.texture_loads_first / f,
      c.texture_loads_again_cpu / f, c.texture_loads_again_gpu / f,
      c.texture_loads_bc_decompressed / f,
      c.texture_load_bc_decompressed_bytes / f / 1048576.0, pipelines,
      pipeline_us / 1000.0, dao_timing::TakeSummary());
  if (ReportSink()) {
    ReportSink()(line.c_str());
    ReportSink()(frame_line.c_str());
  } else {
    XELOGI("{}", line);
    XELOGI("{}", frame_line);
  }
  c = {};
  last = now;
}

}  // namespace xe::gpu::vulkan::dao_stats

#endif  // XENIA_GPU_VULKAN_DAO_VULKAN_STATS_H_
