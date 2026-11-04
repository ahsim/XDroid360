// DAO: plain resolves copied straight from the render target (rather than
// from its dump to the EDRAM buffer), and the area they write for the
// textures of the resolved memory (dao_vulkan_texture_cache.cc): the render
// target cache's part, as the Direct3D 12 backend's dao_render_target_cache.cc.
// Native resolution only.
#include "xenia/gpu/vulkan/vulkan_render_target_cache.h"

#include <cstdint>

#include "xenia/base/cvar.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/gpu/draw_util.h"
#include "xenia/gpu/vulkan/vulkan_command_processor.h"
#include "xenia/gpu/xenos.h"
#include "xenia/ui/vulkan/vulkan_util.h"

DEFINE_bool(dao_resolve_to_textures, true,
            "Vulkan: write plain k_8_8_8_8 render target resolves straight into "
            "the textures of the resolved memory, rather than untiling them at "
            "their next use, and copy them from the render target rather than "
            "its EDRAM dump. Native resolution only.",
            "GPU");

namespace xe {
namespace gpu {
namespace vulkan {

namespace shaders {
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/xv_resolve_direct_cs.h"
}  // namespace shaders

void VulkanRenderTargetCache::DaoInitializeDirectResolve() {
  if (!cvars::dao_resolve_to_textures || IsDrawResolutionScaled() ||
      GetPath() != Path::kHostRenderTargets) {
    return;
  }
  const ui::vulkan::VulkanDevice* const vulkan_device =
      command_processor_.GetVulkanDevice();
  const ui::vulkan::VulkanDevice::Functions& dfn = vulkan_device->functions();
  const VkDevice device = vulkan_device->device();

  VkDescriptorSetLayout set_layouts[kResolveCopyDescriptorSetCount];
  // The source render target's transfer descriptor set in place of the EDRAM.
  set_layouts[kResolveCopyDescriptorSetEdram] =
      descriptor_set_layout_sampled_image_;
  set_layouts[kResolveCopyDescriptorSetDest] =
      command_processor_.GetSingleTransientDescriptorLayout(
          VulkanCommandProcessor::SingleTransientDescriptorLayout ::
              kStorageBufferCompute);
  VkPushConstantRange push_constant_range;
  push_constant_range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
  push_constant_range.offset = 0;
  push_constant_range.size = sizeof(draw_util::ResolveCopyShaderConstants);
  VkPipelineLayoutCreateInfo pipeline_layout_create_info;
  pipeline_layout_create_info.sType =
      VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipeline_layout_create_info.pNext = nullptr;
  pipeline_layout_create_info.flags = 0;
  pipeline_layout_create_info.setLayoutCount = kResolveCopyDescriptorSetCount;
  pipeline_layout_create_info.pSetLayouts = set_layouts;
  pipeline_layout_create_info.pushConstantRangeCount = 1;
  pipeline_layout_create_info.pPushConstantRanges = &push_constant_range;
  if (dfn.vkCreatePipelineLayout(device, &pipeline_layout_create_info, nullptr,
                                 &dao_resolve_direct_pipeline_layout_) !=
      VK_SUCCESS) {
    XELOGE(
        "VulkanRenderTargetCache: Failed to create the direct resolve copy "
        "pipeline layout");
    DaoShutdownDirectResolve();
    return;
  }
  dao_resolve_direct_pipeline_ = ui::vulkan::util::CreateComputePipeline(
      vulkan_device, dao_resolve_direct_pipeline_layout_,
      shaders::xv_resolve_direct_cs, sizeof(shaders::xv_resolve_direct_cs));
  if (dao_resolve_direct_pipeline_ == VK_NULL_HANDLE) {
    XELOGE(
        "VulkanRenderTargetCache: Failed to create the direct resolve copy "
        "pipeline");
    DaoShutdownDirectResolve();
    return;
  }
  XELOGI("VulkanRenderTargetCache: plain resolves are copied straight from "
         "render targets");
}

void VulkanRenderTargetCache::DaoShutdownDirectResolve() {
  const ui::vulkan::VulkanDevice* const vulkan_device =
      command_processor_.GetVulkanDevice();
  const ui::vulkan::VulkanDevice::Functions& dfn = vulkan_device->functions();
  const VkDevice device = vulkan_device->device();
  ui::vulkan::util::DestroyAndNullHandle(dfn.vkDestroyPipeline, device,
                                         dao_resolve_direct_pipeline_);
  ui::vulkan::util::DestroyAndNullHandle(dfn.vkDestroyPipelineLayout, device,
                                         dao_resolve_direct_pipeline_layout_);
}

VulkanRenderTargetCache::VulkanRenderTarget*
VulkanRenderTargetCache::DaoGetResolvedArea(
    const draw_util::ResolveInfo& resolve_info,
    draw_util::ResolveCopyShaderIndex copy_shader, bool copy_dest_scaled,
    uint32_t dump_base, uint32_t dump_row_length_used, uint32_t dump_rows,
    uint32_t dump_pitch, VulkanTextureCache::DaoResolvedArea& area_out) const {
  if (dao_resolve_direct_pipeline_ == VK_NULL_HANDLE || copy_dest_scaled ||
      GetPath() != Path::kHostRenderTargets ||
      copy_shader != draw_util::ResolveCopyShaderIndex::kFast32bpp1x2xMSAA ||
      resolve_info.IsCopyingDepth() || dump_rectangles_.size() != 1) {
    return nullptr;
  }
  const draw_util::ResolveEdramInfo edram_info = resolve_info.color_edram_info;
  const reg::RB_COPY_DEST_INFO dest_info = resolve_info.copy_dest_info;
  if (edram_info.msaa_samples != xenos::MsaaSamples::k1X ||
      xenos::ColorRenderTargetFormat(edram_info.format) !=
          xenos::ColorRenderTargetFormat::k_8_8_8_8 ||
      dest_info.copy_dest_array ||
      xenos::ColorFormat(dest_info.copy_dest_format) !=
          xenos::ColorFormat::k_8_8_8_8 ||
      uint32_t(dest_info.copy_dest_endian) > uint32_t(xenos::Endian::k16in32)) {
    return nullptr;
  }
  // The render target owns the whole area, without EDRAM wrapping.
  const ResolveCopyDumpRectangle& rectangle = dump_rectangles_.front();
  if (rectangle.row_first || rectangle.rows != dump_rows ||
      rectangle.row_first_start ||
      rectangle.row_last_end != dump_row_length_used ||
      dump_base + (dump_rows - 1) * dump_pitch + dump_row_length_used >
          xenos::kEdramTileCount) {
    return nullptr;
  }
  auto& render_target = *static_cast<VulkanRenderTarget*>(rectangle.render_target);
  const RenderTargetKey key = render_target.key();
  if (key.is_depth || key.msaa_samples != xenos::MsaaSamples::k1X ||
      key.GetColorFormat() != xenos::ColorRenderTargetFormat::k_8_8_8_8 ||
      key.next_period || key.pitch_tiles_at_32bpp != edram_info.pitch_tiles ||
      key.base_tiles > edram_info.base_tiles) {
    return nullptr;
  }
  // The area in the render target, laid out like the EDRAM from its base.
  const uint32_t pitch = edram_info.pitch_tiles;
  const uint32_t base_offset = edram_info.base_tiles - key.base_tiles;
  const uint32_t source_x =
      base_offset % pitch * xenos::kEdramTileWidthSamples +
      (resolve_info.coordinate_info.edram_offset_x_div_8 << 3);
  const uint32_t source_y =
      base_offset / pitch * xenos::kEdramTileHeightSamples +
      (resolve_info.coordinate_info.edram_offset_y_div_8 << 3);
  const uint32_t width = resolve_info.coordinate_info.width_div_8 << 3;
  const uint32_t height = resolve_info.height_div_8 << 3;
  if (source_x + width > pitch * xenos::kEdramTileWidthSamples ||
      source_x + width > key.GetWidth() ||
      source_y + height >
          GetRenderTargetHeight(key.pitch_tiles_at_32bpp, key.msaa_samples) ||
      resolve_info.dao_rect[0] < 0 || resolve_info.dao_rect[1] < 0 ||
      uint32_t(resolve_info.dao_rect[2] - resolve_info.dao_rect[0]) != width ||
      uint32_t(resolve_info.dao_rect[3] - resolve_info.dao_rect[1]) != height) {
    return nullptr;
  }
  area_out.source = render_target.GetDescriptorSetTransferSource();
  area_out.source_x = source_x;
  area_out.source_y = source_y;
  // A 32bpp destination's base is that of the texture (4 KB macro tiles), and
  // the area is at the rectangle's place in it.
  area_out.dest_base =
      register_file()[XE_GPU_REG_RB_COPY_DEST_BASE] & 0x1FFFFFFF;
  area_out.dest_pitch_div_32 =
      resolve_info.copy_dest_coordinate_info.pitch_aligned_div_32;
  area_out.dest_x = uint32_t(resolve_info.dao_rect[0]);
  area_out.dest_y = uint32_t(resolve_info.dao_rect[1]);
  area_out.width = width;
  area_out.height = height;
  area_out.swap_red_blue = dest_info.copy_dest_swap != 0;
  area_out.endian = xenos::Endian(uint32_t(dest_info.copy_dest_endian));
  return &render_target;
}

}  // namespace vulkan
}  // namespace gpu
}  // namespace xe
