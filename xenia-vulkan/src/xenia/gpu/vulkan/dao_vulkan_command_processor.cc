// DAO: this project's member functions of VulkanCommandProcessor, next to the
// upstream file (as gpu/d3d12/dao_command_processor.cc for Direct3D 12).
#include "xenia/gpu/vulkan/vulkan_command_processor.h"

#include <cstring>

#include "xenia/base/logging.h"
#include "xenia/gpu/vulkan/dao_vulkan_stats.h"
#include "xenia/base/cvar.h"

DECLARE_bool(dao_disable_bc_textures);
DECLARE_bool(dao_disable_geometry_shader);
DECLARE_bool(dao_disable_conditional_rendering);
DECLARE_bool(dao_disable_stencil_export);
#include "xenia/gpu/vulkan/vulkan_shared_memory.h"
#include "xenia/ui/vulkan/vulkan_util.h"

namespace xe {
namespace gpu {
namespace vulkan {

std::string VulkanCommandProcessor::DaoDescribeHostPaths() const {
  const ui::vulkan::VulkanDevice* const vulkan_device = GetVulkanDevice();
  const ui::vulkan::VulkanDevice::Properties& p = vulkan_device->properties();
  const ui::vulkan::VulkanInstance::Functions& ifn =
      vulkan_device->vulkan_instance()->functions();
  // As vulkan_texture_cache.cc decides per format: decompressed to RGBA8
  // unless sampled with linear filtering.
  std::string decompressed;
  static const std::pair<const char*, VkFormat> kBc[] = {
      {"BC1", VK_FORMAT_BC1_RGBA_UNORM_BLOCK},
      {"BC2", VK_FORMAT_BC2_UNORM_BLOCK},
      {"BC3", VK_FORMAT_BC3_UNORM_BLOCK},
      {"BC4", VK_FORMAT_BC4_UNORM_BLOCK},
      {"BC5", VK_FORMAT_BC5_UNORM_BLOCK}};
  for (const auto& bc : kBc) {
    VkFormatProperties format_properties;
    ifn.vkGetPhysicalDeviceFormatProperties(vulkan_device->physical_device(),
                                            bc.second, &format_properties);
    if (cvars::dao_disable_bc_textures) {
      format_properties.optimalTilingFeatures = 0;
    }
    constexpr VkFormatFeatureFlags kLinearFilterFeatures =
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    if ((format_properties.optimalTilingFeatures & kLinearFilterFeatures) !=
        kLinearFilterFeatures) {
      decompressed += decompressed.empty() ? bc.first : std::string(",") + bc.first;
    }
  }
  std::string switches;
  for (const auto& [on, name] : {
           std::pair{cvars::dao_disable_geometry_shader, "geometry shader"},
           std::pair{cvars::dao_disable_conditional_rendering,
                     "conditional rendering"},
           std::pair{cvars::dao_disable_stencil_export, "stencil export"},
           std::pair{cvars::dao_disable_bc_textures, "BC textures"}}) {
    if (on) {
      switches += switches.empty() ? name : std::string(", ") + name;
    }
  }
  return fmt::format(
      "{} (Vulkan {}.{}, driver id {}): render targets {}; BC textures {}; "
      "points/rectangles {}; memexport vertex {}, pixel {}; shared memory {}; "
      "resolution scale {}x{}; VIZ predicates {}; stencil export {}; "
      "testing switches off: {}",
      p.deviceName, VK_API_VERSION_MAJOR(p.apiVersion),
      VK_API_VERSION_MINOR(p.apiVersion), int(p.driverID),
      render_target_cache_->GetPath() ==
              RenderTargetCache::Path::kPixelShaderInterlock
          ? "fragment shader interlock"
          : "host (FBO)",
      decompressed.empty() ? "native" : "decompressed: " + decompressed,
      p.geometryShader ? "geometry shaders" : "expanded in the vertex shader",
      p.vertexPipelineStoresAndAtomics ? "yes" : "skipped",
      p.fragmentStoresAndAtomics ? "yes" : "skipped",
      p.sparseResidencyBuffer ? "sparse" : "allocated in full",
      texture_cache_->draw_resolution_scale_x(),
      texture_cache_->draw_resolution_scale_y(),
      p.conditionalRendering ? "on the GPU" : "CPU waits",
      vulkan_device->extensions().ext_EXT_shader_stencil_export
          ? "yes"
          : "no (per-bit)",
      switches.empty() ? std::string("none") : switches);
}

// The device itself, for a bug report from a phone nobody here has (dao.log): who
// made it, the driver and its version, and what Xenia relies on or falls back from.
std::string VulkanCommandProcessor::DaoDescribeDevice() const {
  const ui::vulkan::VulkanDevice* const vulkan_device = GetVulkanDevice();
  const ui::vulkan::VulkanDevice::Properties& p = vulkan_device->properties();
  const ui::vulkan::VulkanInstance::Functions& ifn =
      vulkan_device->vulkan_instance()->functions();
  const VkPhysicalDevice physical_device = vulkan_device->physical_device();
  std::string driver = "?";
  if (VK_API_VERSION_MINOR(p.apiVersion) >= 2 && ifn.vkGetPhysicalDeviceProperties2) {
    VkPhysicalDeviceDriverProperties driver_properties = {};
    driver_properties.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES;
    VkPhysicalDeviceProperties2 properties_2 = {};
    properties_2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    properties_2.pNext = &driver_properties;
    ifn.vkGetPhysicalDeviceProperties2(physical_device, &properties_2);
    driver = driver_properties.driverName;
    if (driver_properties.driverInfo[0]) {
      driver += std::string(" (") + driver_properties.driverInfo + ")";
    }
  }
  auto sampled = [&](VkFormat format) {
    VkFormatProperties format_properties;
    ifn.vkGetPhysicalDeviceFormatProperties(physical_device, format, &format_properties);
    return (format_properties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) != 0;
  };
  auto yes = [](bool b) { return b ? "yes" : "no"; };
  return fmt::format(
      "{}, vendor {:#06x} device {:#x}; driver {}, version {:#x}; Vulkan {}.{}.{}; "
      "independentBlend {}, geometryShader {}, fragmentStoresAndAtomics {}, "
      "vertexPipelineStoresAndAtomics {}, samplerAnisotropy {}, sparseResidencyBuffer {}, "
      "shaderClipDistance {}; textures BC1 {}, BC3 {}, BC5 {}, ETC2 {}, ASTC {}",
      p.deviceName, p.vendorID, p.deviceID, driver, p.driverVersion,
      VK_API_VERSION_MAJOR(p.apiVersion), VK_API_VERSION_MINOR(p.apiVersion),
      VK_API_VERSION_PATCH(p.apiVersion), yes(p.independentBlend), yes(p.geometryShader),
      yes(p.fragmentStoresAndAtomics), yes(p.vertexPipelineStoresAndAtomics),
      yes(p.samplerAnisotropy), yes(p.sparseResidencyBuffer), yes(p.shaderClipDistance),
      yes(sampled(VK_FORMAT_BC1_RGBA_UNORM_BLOCK)), yes(sampled(VK_FORMAT_BC3_UNORM_BLOCK)),
      yes(sampled(VK_FORMAT_BC5_UNORM_BLOCK)), yes(sampled(VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK)),
      yes(sampled(VK_FORMAT_ASTC_4x4_UNORM_BLOCK)));
}

bool VulkanCommandProcessor::DaoReadGpuWrittenMemory(
    uint32_t address, uint32_t length, uint32_t pixel_size_log2,
    std::vector<uint8_t>& out) {
  if (!length || !shared_memory_->IsRangeWrittenByGpu(address, length)) {
    return false;
  }
  // Resolves at the draw resolution scale are kept in the scaled buffer, not
  // read back here (no resolution scaling on this backend yet).
  if (texture_cache_->IsDrawResolutionScaled()) {
    return false;
  }
  if (!BeginSubmission(true)) {
    return false;
  }
  const VkBuffer readback_buffer = RequestReadbackBuffer(length);
  if (readback_buffer == VK_NULL_HANDLE) {
    return false;
  }
  DAO_STATS_ADD(readbacks, 1);  // DAO_GPU_STATS (dao_vulkan_stats.h).
  DAO_STATS_ADD(readback_bytes, length);
  const ui::vulkan::VulkanDevice* const vulkan_device = GetVulkanDevice();
  const ui::vulkan::VulkanDevice::Functions& dfn = vulkan_device->functions();
  const VkDevice device = vulkan_device->device();

  shared_memory_->Use(VulkanSharedMemory::Usage::kRead);
  VkBufferCopy copy_region;
  copy_region.srcOffset = address;
  copy_region.dstOffset = 0;
  copy_region.size = length;
  deferred_command_buffer_.CmdVkCopyBuffer(shared_memory_->buffer(),
                                           readback_buffer, 1, &copy_region);
  // The fence alone doesn't make the copy visible to the host.
  VkBufferMemoryBarrier buffer_memory_barrier;
  buffer_memory_barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
  buffer_memory_barrier.pNext = nullptr;
  buffer_memory_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
  buffer_memory_barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
  buffer_memory_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  buffer_memory_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  buffer_memory_barrier.buffer = readback_buffer;
  buffer_memory_barrier.offset = 0;
  buffer_memory_barrier.size = length;
  deferred_command_buffer_.CmdVkPipelineBarrier(
      VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0, nullptr,
      1, &buffer_memory_barrier, 0, nullptr);

  // Submits and waits for the GPU.
  if (!AwaitAllQueueOperationsCompletion()) {
    XELOGE("VulkanCommandProcessor: Failed to wait for the GPU memory read-back");
    return false;
  }
  void* mapping = nullptr;
  if (dfn.vkMapMemory(device, memexport_readback_buffer_memory_, 0, length, 0,
                      &mapping) != VK_SUCCESS ||
      !mapping) {
    XELOGE("VulkanCommandProcessor: Failed to map the GPU memory read-back buffer");
    return false;
  }
  out.resize(length);
  std::memcpy(out.data(), mapping, length);
  dfn.vkUnmapMemory(device, memexport_readback_buffer_memory_);
  return true;
}

}  // namespace vulkan
}  // namespace gpu
}  // namespace xe
