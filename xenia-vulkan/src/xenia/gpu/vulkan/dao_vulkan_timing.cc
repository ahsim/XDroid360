// DAO: DAO_GPU_TIMING (see dao_vulkan_timing.h).
#include "xenia/gpu/vulkan/dao_vulkan_timing.h"

#include <array>
#include <cstdlib>
#include <map>
#include <vector>

#include "xenia/base/logging.h"
#include "xenia/base/platform.h"
#include "xenia/ui/vulkan/vulkan_device.h"
#include "xenia/ui/vulkan/vulkan_instance.h"

#if XE_PLATFORM_WIN32
#include "xenia/base/platform_win.h"
#else
#include <time.h>
#endif

namespace xe::gpu::vulkan::dao_timing {

namespace {

// Submissions in flight that can be timed at once (one slot each); a
// submission whose slot is still in flight isn't timed.
constexpr uint32_t kSlots = 8;
// Per submission: its begin and end, then a begin/end pair per group.
// (SoD: about 120 groups a frame - resolves, transfers, texture loads.)
constexpr uint32_t kQueriesPerSlot = 1024;
constexpr uint32_t kGroupPairsPerSlot = (kQueriesPerSlot - 2) / 2;
constexpr uint32_t kGroupCount = 4;  // dao_stats::Activity.

struct Slot {
  uint64_t submission = 0;
  uint64_t frame = 0;
  uint32_t used = 0;
  // Begun (queries reset in its command buffer) and recorded to the end.
  bool begun = false;
  bool recorded = false;
  // Waiting for the read-back.
  bool pending = false;
  std::array<uint8_t, kGroupPairsPerSlot> pair_group = {};
};

struct State {
  bool initialized = false;
  bool available = false;
  VkQueryPool pool = VK_NULL_HANDLE;
  double period_ns = 1.0;
  uint64_t valid_mask = ~uint64_t(0);
  std::array<Slot, kSlots> slots;
  Slot* current = nullptr;
  // GPU time by guest frame, until the frame's submissions are all read back.
  std::map<uint64_t, uint64_t> frame_ns;
  uint64_t newest_read_frame = 0;
  uint64_t group_ns[kGroupCount] = {};
  uint64_t group_overflows = 0;
  uint64_t untimed_submissions = 0;
};

State& GetState() {
  static State state;
  return state;
}

uint32_t SlotBase(const State& state, const Slot& slot) {
  return uint32_t(&slot - state.slots.data()) * kQueriesPerSlot;
}

void Initialize(const ui::vulkan::VulkanDevice* device) {
  State& state = GetState();
  state.initialized = true;
  const ui::vulkan::VulkanInstance::Functions& ifn =
      device->vulkan_instance()->functions();
  VkPhysicalDeviceProperties properties;
  ifn.vkGetPhysicalDeviceProperties(device->physical_device(), &properties);
  uint32_t family_count = 0;
  ifn.vkGetPhysicalDeviceQueueFamilyProperties(device->physical_device(),
                                               &family_count, nullptr);
  std::vector<VkQueueFamilyProperties> families(family_count);
  ifn.vkGetPhysicalDeviceQueueFamilyProperties(device->physical_device(),
                                               &family_count, families.data());
  const uint32_t family = device->queue_family_graphics_compute();
  const uint32_t valid_bits =
      family < family_count ? families[family].timestampValidBits : 0;
  if (!valid_bits || properties.limits.timestampPeriod <= 0.0f) {
    XELOGW(
        "DAO_GPU_TIMING: unavailable on this device (timestamp valid bits {}, "
        "period {} ns, compute and graphics {})",
        valid_bits, properties.limits.timestampPeriod,
        properties.limits.timestampComputeAndGraphics);
    return;
  }
  VkQueryPoolCreateInfo create_info = {VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
  create_info.queryType = VK_QUERY_TYPE_TIMESTAMP;
  create_info.queryCount = kSlots * kQueriesPerSlot;
  const ui::vulkan::VulkanDevice::Functions& dfn = device->functions();
  if (dfn.vkCreateQueryPool(device->device(), &create_info, nullptr,
                            &state.pool) != VK_SUCCESS) {
    XELOGW("DAO_GPU_TIMING: failed to create the timestamp query pool");
    state.pool = VK_NULL_HANDLE;
    return;
  }
  state.period_ns = properties.limits.timestampPeriod;
  state.valid_mask =
      valid_bits >= 64 ? ~uint64_t(0) : (uint64_t(1) << valid_bits) - 1;
  state.available = true;
  XELOGI("DAO_GPU_TIMING: on ({} valid bits, {} ns per tick)", valid_bits,
         state.period_ns);
}

// Reads back the slots of the submissions that have completed.
void ReadBack(const ui::vulkan::VulkanDevice* device,
              uint64_t completed_submission) {
  State& state = GetState();
  const ui::vulkan::VulkanDevice::Functions& dfn = device->functions();
  uint64_t results[kQueriesPerSlot];
  for (Slot& slot : state.slots) {
    if (!slot.pending || slot.submission > completed_submission) {
      continue;
    }
    slot.pending = false;
    if (dfn.vkGetQueryPoolResults(
            device->device(), state.pool, SlotBase(state, slot), slot.used,
            sizeof(uint64_t) * slot.used, results, sizeof(uint64_t),
            VK_QUERY_RESULT_64_BIT) != VK_SUCCESS) {
      continue;
    }
    auto span_ns = [&state, &results](uint32_t first) {
      uint64_t ticks = (results[first + 1] - results[first]) & state.valid_mask;
      return uint64_t(double(ticks) * state.period_ns);
    };
    state.frame_ns[slot.frame] += span_ns(0);
    state.newest_read_frame = std::max(state.newest_read_frame, slot.frame);
    for (uint32_t i = 2; i + 1 < slot.used; i += 2) {
      state.group_ns[slot.pair_group[(i - 2) / 2] % kGroupCount] +=
          span_ns(i);
    }
  }
}

}  // namespace

bool Requested() {
  static const bool requested = [] {
    if (!dao_stats::Enabled()) {
      return false;
    }
    const char* env = std::getenv("DAO_GPU_TIMING");
    return env && env[0] && env[0] != '0';
  }();
  return requested;
}

void OnBeginSubmission(const ui::vulkan::VulkanDevice* device,
                       uint64_t submission, uint64_t completed_submission,
                       uint64_t frame) {
  if (!Requested()) {
    return;
  }
  State& state = GetState();
  if (!state.initialized) {
    Initialize(device);
  }
  if (!state.available) {
    return;
  }
  ReadBack(device, completed_submission);
  Slot& slot = state.slots[submission % kSlots];
  if (slot.pending) {
    // Its previous submission is still in flight: don't wait for it.
    ++state.untimed_submissions;
    state.current = nullptr;
    return;
  }
  slot.submission = submission;
  slot.frame = frame;
  slot.used = 2;
  slot.begun = false;
  slot.recorded = false;
  state.current = &slot;
}

void OnCommandBufferBegin(const ui::vulkan::VulkanDevice* device,
                          VkCommandBuffer command_buffer) {
  State& state = GetState();
  if (!state.current) {
    return;
  }
  const ui::vulkan::VulkanDevice::Functions& dfn = device->functions();
  const uint32_t base = SlotBase(state, *state.current);
  // In the command buffer: the stock Mali driver is Vulkan 1.1, without host
  // query reset.
  dfn.vkCmdResetQueryPool(command_buffer, state.pool, base, kQueriesPerSlot);
  dfn.vkCmdWriteTimestamp(command_buffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                          state.pool, base);
  state.current->begun = true;
}

void OnCommandBufferEnd(const ui::vulkan::VulkanDevice* device,
                        VkCommandBuffer command_buffer) {
  State& state = GetState();
  Slot* slot = state.current;
  state.current = nullptr;
  if (!slot || !slot->begun) {
    return;
  }
  const ui::vulkan::VulkanDevice::Functions& dfn = device->functions();
  dfn.vkCmdWriteTimestamp(command_buffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                          state.pool, SlotBase(state, *slot) + 1);
  slot->recorded = true;
  slot->pending = true;
}

void Shutdown(const ui::vulkan::VulkanDevice* device) {
  State& state = GetState();
  if (state.pool != VK_NULL_HANDLE) {
    device->functions().vkDestroyQueryPool(device->device(), state.pool,
                                           nullptr);
  }
  state = State();
}

uint32_t GroupBegin(DeferredCommandBuffer& command_buffer,
                    dao_stats::Activity group) {
  State& state = GetState();
  Slot* slot = state.current;
  if (!slot) {
    return UINT32_MAX;
  }
  if (slot->used + 2 > kQueriesPerSlot) {
    ++state.group_overflows;
    return UINT32_MAX;
  }
  const uint32_t index = slot->used;
  slot->used += 2;
  slot->pair_group[(index - 2) / 2] = uint8_t(group);
  command_buffer.CmdVkWriteTimestamp(VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                                     state.pool, SlotBase(state, *slot) + index);
  return index;
}

void GroupEnd(DeferredCommandBuffer& command_buffer, uint32_t token) {
  State& state = GetState();
  Slot* slot = state.current;
  if (!slot || token == UINT32_MAX) {
    return;
  }
  command_buffer.CmdVkWriteTimestamp(VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
                                     state.pool,
                                     SlotBase(state, *slot) + token + 1);
}

std::string TakeSummary() {
  if (!Requested()) {
    return std::string();
  }
  State& state = GetState();
  if (!state.available) {
    return state.initialized ? "; GPU timing unavailable" : std::string();
  }
  // Frames older than the newest one read back have all their submissions in.
  std::vector<float> frame_ms;
  for (auto it = state.frame_ns.begin();
       it != state.frame_ns.end() && it->first < state.newest_read_frame;) {
    frame_ms.push_back(float(it->second) / 1e6f);
    it = state.frame_ns.erase(it);
  }
  const auto [p50, p95] = dao_stats::MedianAndP95(frame_ms);
  const double frames = std::max(size_t(1), frame_ms.size());
  std::string summary = fmt::format(
      "; GPU time per frame (p50/p95 ms) {:.1f}/{:.1f} over {} frames, of it "
      "on average: texture loads {:.2f}, resolves {:.2f}, transfers {:.2f} ms",
      p50, p95, frame_ms.size(),
      state.group_ns[uint32_t(dao_stats::Activity::kTextureLoad)] / frames /
          1e6,
      state.group_ns[uint32_t(dao_stats::Activity::kResolve)] / frames / 1e6,
      state.group_ns[uint32_t(dao_stats::Activity::kTransfer)] / frames / 1e6);
  if (state.untimed_submissions || state.group_overflows) {
    summary += fmt::format(" ({} submissions untimed, {} groups over the limit)",
                           state.untimed_submissions, state.group_overflows);
  }
  for (uint64_t& ns : state.group_ns) {
    ns = 0;
  }
  state.untimed_submissions = 0;
  state.group_overflows = 0;
  return summary;
}

uint64_t ThreadCpuTimeUs() {
#if XE_PLATFORM_WIN32
  FILETIME creation, exit, kernel, user;
  if (!GetThreadTimes(GetCurrentThread(), &creation, &exit, &kernel, &user)) {
    return 0;
  }
  auto to_us = [](const FILETIME& t) {
    return ((uint64_t(t.dwHighDateTime) << 32) | t.dwLowDateTime) / 10;
  };
  return to_us(kernel) + to_us(user);
#else
  timespec ts;
  if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts)) {
    return 0;
  }
  return uint64_t(ts.tv_sec) * 1000000 + uint64_t(ts.tv_nsec) / 1000;
#endif
}

}  // namespace xe::gpu::vulkan::dao_timing
