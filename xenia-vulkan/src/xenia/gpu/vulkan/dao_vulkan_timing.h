// DAO: where the GPU's time goes, for a bug report from a phone nobody here has
// (DAO_GPU_STATS=1 together with DAO_GPU_TIMING=1). Timestamps bracket every
// submission's command buffer, and the work done outside the guest's render
// passes in groups: texture loads (untiling, BC decompression), resolves and
// render target ownership transfers. Results are read back only once their
// submission has completed, never waited for. The per-frame GPU time (median,
// 95th percentile) and the groups' averages join DAO_GPU_STATS' report.
// Timestamps between draws of one pass mean little on a tiled GPU, so draws
// are the frame's time minus the groups'. Off by default: timestamp queries
// stalled Turnip.
//
// Also the frame-time samples DAO_GPU_STATS takes on the command processor
// thread (ThreadCpuTimeUs) and the work scopes that tag what a render pass was
// ended for (WorkScope).
#ifndef XENIA_GPU_VULKAN_DAO_VULKAN_TIMING_H_
#define XENIA_GPU_VULKAN_DAO_VULKAN_TIMING_H_

#include <cstdint>
#include <string>

#include "xenia/gpu/vulkan/dao_vulkan_stats.h"
#include "xenia/gpu/vulkan/deferred_command_buffer.h"
#include "xenia/ui/vulkan/vulkan_api.h"

namespace xe::ui::vulkan {
class VulkanDevice;
}  // namespace xe::ui::vulkan

namespace xe::gpu::vulkan::dao_timing {

// Requested (DAO_GPU_STATS and DAO_GPU_TIMING) - whether the device can do it
// is decided on the first submission.
bool Requested();

// The command processor thread. A submission opens: picks its query slot,
// reading back the slots of completed submissions first.
void OnBeginSubmission(const ui::vulkan::VulkanDevice* device,
                       uint64_t submission, uint64_t completed_submission,
                       uint64_t frame);
// The submission's real command buffer, around the deferred commands.
void OnCommandBufferBegin(const ui::vulkan::VulkanDevice* device,
                          VkCommandBuffer command_buffer);
void OnCommandBufferEnd(const ui::vulkan::VulkanDevice* device,
                        VkCommandBuffer command_buffer);
void Shutdown(const ui::vulkan::VulkanDevice* device);

// The groups, as written into the submission's deferred command buffer.
uint32_t GroupBegin(DeferredCommandBuffer& command_buffer,
                    dao_stats::Activity group);
void GroupEnd(DeferredCommandBuffer& command_buffer, uint32_t token);

// The timing part of DAO_GPU_STATS' report (empty if not requested or not
// available); takes the frames whose submissions have all been read back.
std::string TakeSummary();

// The calling thread's CPU time, in microseconds.
uint64_t ThreadCpuTimeUs();

// A piece of work outside the guest's draws: tags the render passes ended for
// it (DAO_GPU_STATS) and times it on the GPU (DAO_GPU_TIMING).
class WorkScope {
 public:
  WorkScope(DeferredCommandBuffer& command_buffer, dao_stats::Activity activity)
      : command_buffer_(command_buffer),
        previous_activity_(dao_stats::CurrentActivity()) {
    dao_stats::CurrentActivity() = activity;
    if (Requested()) {
      token_ = GroupBegin(command_buffer, activity);
    }
  }
  ~WorkScope() {
    if (token_ != UINT32_MAX) {
      GroupEnd(command_buffer_, token_);
    }
    dao_stats::CurrentActivity() = previous_activity_;
  }
  WorkScope(const WorkScope&) = delete;
  WorkScope& operator=(const WorkScope&) = delete;

 private:
  DeferredCommandBuffer& command_buffer_;
  dao_stats::Activity previous_activity_;
  uint32_t token_ = UINT32_MAX;
};

}  // namespace xe::gpu::vulkan::dao_timing

#endif  // XENIA_GPU_VULKAN_DAO_VULKAN_TIMING_H_
