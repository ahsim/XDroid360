// DAO: resolves writing their areas straight into the textures of the
// resolved memory, as the Direct3D 12 backend's dao_texture_cache.cc does:
// VulkanTextureCache's part (the render target cache's is
// VulkanRenderTargetCache::DaoGetResolvedArea).
#include "xenia/gpu/vulkan/vulkan_texture_cache.h"

#include <algorithm>
#include <utility>
#include <vector>

#include "xenia/base/clock.h"
#include "xenia/base/cvar.h"
#include "xenia/base/logging.h"
#include "xenia/base/math.h"
#include "xenia/gpu/vulkan/deferred_command_buffer.h"
#include "xenia/gpu/vulkan/vulkan_command_processor.h"
#include "xenia/ui/vulkan/vulkan_util.h"

DECLARE_bool(dao_resolve_to_textures);

namespace xe {
namespace gpu {
namespace vulkan {

namespace shaders {
#include "xenia/gpu/shaders/bytecode/vulkan_spirv/xv_resolve_texture_cs.h"
}  // namespace shaders

namespace {
// The shader's push constants.
struct ResolveTextureConstants {
  uint32_t source_offset[2];
  uint32_t dest_offset[2];
  uint32_t size[2];
  uint32_t channels;
};

// Byte positions in a 32-bit word after an endian swap.
void SwapBytes32(uint32_t bytes[4], xenos::Endian endian) {
  switch (endian) {
    case xenos::Endian::k8in16:
      std::swap(bytes[0], bytes[1]);
      std::swap(bytes[2], bytes[3]);
      break;
    case xenos::Endian::k8in32:
      std::swap(bytes[0], bytes[3]);
      std::swap(bytes[1], bytes[2]);
      break;
    case xenos::Endian::k16in32:
      std::swap(bytes[0], bytes[2]);
      std::swap(bytes[1], bytes[3]);
      break;
    default:
      break;
  }
}
}  // namespace

void VulkanTextureCache::DaoInitializeResolveTextures() {
  if (!cvars::dao_resolve_to_textures || IsDrawResolutionScaled()) {
    return;
  }
  const ui::vulkan::VulkanDevice* const vulkan_device =
      command_processor_.GetVulkanDevice();
  const ui::vulkan::VulkanInstance::Functions& ifn =
      vulkan_device->vulkan_instance()->functions();
  const ui::vulkan::VulkanDevice::Functions& dfn = vulkan_device->functions();
  const VkDevice device = vulkan_device->device();

  // The host format of k_8_8_8_8 must be what the shader writes.
  if (host_formats_[uint32_t(xenos::TextureFormat::k_8_8_8_8)]
          .format_unsigned.format != VK_FORMAT_R8G8B8A8_UNORM) {
    XELOGW(
        "VulkanTextureCache: k_8_8_8_8 isn't R8G8B8A8_UNORM, resolves aren't "
        "written into textures");
    return;
  }
  VkFormatProperties format_properties;
  ifn.vkGetPhysicalDeviceFormatProperties(vulkan_device->physical_device(),
                                          VK_FORMAT_R8G8B8A8_UNORM,
                                          &format_properties);
  if (!(format_properties.optimalTilingFeatures &
        VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT)) {
    XELOGW(
        "VulkanTextureCache: R8G8B8A8_UNORM isn't a storage image format, "
        "resolves aren't written into textures");
    return;
  }

  // The source: as the render target cache's descriptor_set_layout_sampled_image_
  // (compatible with its transfer source sets).
  VkDescriptorSetLayoutBinding binding;
  binding.binding = 0;
  binding.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
  binding.descriptorCount = 1;
  binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT;
  binding.pImmutableSamplers = nullptr;
  VkDescriptorSetLayoutCreateInfo layout_create_info;
  layout_create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  layout_create_info.pNext = nullptr;
  layout_create_info.flags = 0;
  layout_create_info.bindingCount = 1;
  layout_create_info.pBindings = &binding;
  if (dfn.vkCreateDescriptorSetLayout(device, &layout_create_info, nullptr,
                                      &dao_resolve_source_layout_) !=
      VK_SUCCESS) {
    XELOGE("VulkanTextureCache: Failed to create the resolve source layout");
    DaoShutdownResolveTextures();
    return;
  }
  binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
  binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
  if (dfn.vkCreateDescriptorSetLayout(device, &layout_create_info, nullptr,
                                      &dao_resolve_dest_layout_) !=
      VK_SUCCESS) {
    XELOGE(
        "VulkanTextureCache: Failed to create the resolve destination layout");
    DaoShutdownResolveTextures();
    return;
  }
  VkDescriptorPoolSize pool_size;
  pool_size.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
  pool_size.descriptorCount = 1;
  dao_resolve_dest_pool_ =
      std::make_unique<ui::vulkan::SingleLayoutDescriptorSetPool>(
          vulkan_device, 64, 1, &pool_size, dao_resolve_dest_layout_);

  VkDescriptorSetLayout set_layouts[] = {dao_resolve_source_layout_,
                                         dao_resolve_dest_layout_};
  VkPushConstantRange push_constant_range;
  push_constant_range.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
  push_constant_range.offset = 0;
  push_constant_range.size = sizeof(ResolveTextureConstants);
  VkPipelineLayoutCreateInfo pipeline_layout_create_info;
  pipeline_layout_create_info.sType =
      VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipeline_layout_create_info.pNext = nullptr;
  pipeline_layout_create_info.flags = 0;
  pipeline_layout_create_info.setLayoutCount = uint32_t(xe::countof(set_layouts));
  pipeline_layout_create_info.pSetLayouts = set_layouts;
  pipeline_layout_create_info.pushConstantRangeCount = 1;
  pipeline_layout_create_info.pPushConstantRanges = &push_constant_range;
  if (dfn.vkCreatePipelineLayout(device, &pipeline_layout_create_info, nullptr,
                                 &dao_resolve_pipeline_layout_) != VK_SUCCESS) {
    XELOGE(
        "VulkanTextureCache: Failed to create the resolve into textures "
        "pipeline layout");
    DaoShutdownResolveTextures();
    return;
  }
  dao_resolve_pipeline_ = ui::vulkan::util::CreateComputePipeline(
      vulkan_device, dao_resolve_pipeline_layout_,
      shaders::xv_resolve_texture_cs, sizeof(shaders::xv_resolve_texture_cs));
  if (dao_resolve_pipeline_ == VK_NULL_HANDLE) {
    XELOGE(
        "VulkanTextureCache: Failed to create the resolve into textures "
        "pipeline");
    DaoShutdownResolveTextures();
    return;
  }
  XELOGI("VulkanTextureCache: resolves are written straight into textures");
}

void VulkanTextureCache::DaoShutdownResolveTextures() {
  const ui::vulkan::VulkanDevice* const vulkan_device =
      command_processor_.GetVulkanDevice();
  const ui::vulkan::VulkanDevice::Functions& dfn = vulkan_device->functions();
  const VkDevice device = vulkan_device->device();
  ui::vulkan::util::DestroyAndNullHandle(dfn.vkDestroyPipeline, device,
                                         dao_resolve_pipeline_);
  ui::vulkan::util::DestroyAndNullHandle(dfn.vkDestroyPipelineLayout, device,
                                         dao_resolve_pipeline_layout_);
  dao_resolve_dest_pool_.reset();
  ui::vulkan::util::DestroyAndNullHandle(dfn.vkDestroyDescriptorSetLayout,
                                         device, dao_resolve_dest_layout_);
  ui::vulkan::util::DestroyAndNullHandle(dfn.vkDestroyDescriptorSetLayout,
                                         device, dao_resolve_source_layout_);
}

bool VulkanTextureCache::DaoIsResolveWritable(const TextureKey& key) const {
  return dao_resolve_pipeline_ != VK_NULL_HANDLE &&
         key.dimension == xenos::DataDimension::k2DOrStacked && key.tiled &&
         !key.depth_or_array_size_minus_1 && !key.mip_max_level &&
         key.format == xenos::TextureFormat::k_8_8_8_8 &&
         !key.signed_separate && !key.scaled_resolve &&
         (!key.packed_mips || std::min(key.GetWidth(), key.GetHeight()) > 16);
}

VkDescriptorSet VulkanTextureCache::VulkanTexture::DaoGetResolveDescriptorSet(
    VkFormat format) {
  auto& vulkan_texture_cache =
      static_cast<VulkanTextureCache&>(texture_cache());
  if (dao_resolve_descriptor_index_ != SIZE_MAX) {
    return vulkan_texture_cache.dao_resolve_dest_pool_->Get(
        dao_resolve_descriptor_index_);
  }
  const ui::vulkan::VulkanDevice* const vulkan_device =
      vulkan_texture_cache.command_processor_.GetVulkanDevice();
  const ui::vulkan::VulkanDevice::Functions& dfn = vulkan_device->functions();
  const VkDevice device = vulkan_device->device();
  if (dao_resolve_view_ == VK_NULL_HANDLE) {
    VkImageViewCreateInfo view_create_info;
    view_create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view_create_info.pNext = nullptr;
    view_create_info.flags = 0;
    view_create_info.image = image_;
    view_create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_create_info.format = format;
    view_create_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
    view_create_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
    view_create_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
    view_create_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
    view_create_info.subresourceRange =
        ui::vulkan::util::InitializeSubresourceRange();
    if (dfn.vkCreateImageView(device, &view_create_info, nullptr,
                              &dao_resolve_view_) != VK_SUCCESS) {
      dao_resolve_view_ = VK_NULL_HANDLE;
      return VK_NULL_HANDLE;
    }
  }
  size_t index = vulkan_texture_cache.dao_resolve_dest_pool_->Allocate();
  if (index == SIZE_MAX) {
    return VK_NULL_HANDLE;
  }
  VkDescriptorSet descriptor_set =
      vulkan_texture_cache.dao_resolve_dest_pool_->Get(index);
  VkDescriptorImageInfo image_info;
  image_info.sampler = VK_NULL_HANDLE;
  image_info.imageView = dao_resolve_view_;
  image_info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
  VkWriteDescriptorSet write;
  write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  write.pNext = nullptr;
  write.dstSet = descriptor_set;
  write.dstBinding = 0;
  write.dstArrayElement = 0;
  write.descriptorCount = 1;
  write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
  write.pImageInfo = &image_info;
  write.pBufferInfo = nullptr;
  write.pTexelBufferView = nullptr;
  dfn.vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
  dao_resolve_descriptor_index_ = index;
  return descriptor_set;
}

bool VulkanTextureCache::DaoPrepareResolvedTextures(
    const DaoResolvedArea& area) {
  std::vector<Texture*>& textures = resolve_invalidated_textures_;
  const uint32_t pitch = area.dest_pitch_div_32 << 5;
  if (dao_resolve_pipeline_ == VK_NULL_HANDLE || area.dest_base & 0xFFF ||
      area.dest_x + area.width > pitch) {
    textures.clear();
    return false;
  }
  // The same texels the untiling would load, into a texture created as a
  // storage image. Textures not used lately (other views of the memory) are
  // left outdated rather than kept alive and written by every resolve: lately
  // is in time rather than submissions, of which a frame may have several.
  constexpr uint64_t kUsedLatelyMilliseconds = 250;
  const uint64_t now = xe::Clock::QueryHostUptimeMillis();
  auto described = [&](const Texture* texture) {
    const TextureKey& key = texture->key();
    // Created before a resolve first wrote its memory, or of another format:
    // untiled after every resolve while it stays cached.
    return DaoIsResolveWritable(key) &&
           static_cast<const VulkanTexture*>(texture)->dao_resolve_writable() &&
           key.base_page == ((area.dest_base >> 12) & 0x1FFFF) &&
           key.pitch == area.dest_pitch_div_32 &&
           area.dest_x < key.GetWidth() && area.dest_y < key.GetHeight() &&
           texture->last_usage_time() + kUsedLatelyMilliseconds >= now;
  };
  textures.erase(std::remove_if(textures.begin(), textures.end(),
                                [&](const Texture* texture) {
                                  return !described(texture);
                                }),
                 textures.end());
  return !textures.empty();
}

void VulkanTextureCache::DaoWriteResolvedTextures(const DaoResolvedArea& area) {
  DeferredCommandBuffer& command_buffer =
      command_processor_.deferred_command_buffer();
  // The resolve writes the render target's RGBA bytes (red / blue swapped if
  // requested) with its endian swap, the untiling reads them with the
  // texture's.
  uint32_t written[4] = {0, 1, 2, 3};
  if (area.swap_red_blue) {
    std::swap(written[0], written[2]);
  }
  SwapBytes32(written, area.endian);
  ResolveTextureConstants constants;
  constants.source_offset[0] = area.source_x;
  constants.source_offset[1] = area.source_y;
  constants.dest_offset[0] = area.dest_x;
  constants.dest_offset[1] = area.dest_y;
  const VkFormat format =
      host_formats_[uint32_t(xenos::TextureFormat::k_8_8_8_8)]
          .format_unsigned.format;
  bool pipeline_bound = false;
  for (Texture* texture : resolve_invalidated_textures_) {
    auto& vulkan_texture = *static_cast<VulkanTexture*>(texture);
    const TextureKey& key = texture->key();
    VkDescriptorSet dest = vulkan_texture.DaoGetResolveDescriptorSet(format);
    if (dest == VK_NULL_HANDLE) {
      // Loaded from the resolved memory instead.
      break;
    }
    uint32_t loaded[4] = {written[0], written[1], written[2], written[3]};
    SwapBytes32(loaded, key.endianness);
    constants.channels =
        loaded[0] | loaded[1] << 2 | loaded[2] << 4 | loaded[3] << 6;
    constants.size[0] = std::min(area.width, key.GetWidth() - area.dest_x);
    constants.size[1] = std::min(area.height, key.GetHeight() - area.dest_y);

    VulkanTexture::Usage old_usage =
        vulkan_texture.SetUsage(VulkanTexture::Usage::kDaoResolveWrite);
    VkPipelineStageFlags src_stage_mask, dst_stage_mask;
    VkAccessFlags src_access_mask, dst_access_mask;
    VkImageLayout old_layout, new_layout;
    GetTextureUsageMasks(old_usage, src_stage_mask, src_access_mask,
                         old_layout);
    GetTextureUsageMasks(VulkanTexture::Usage::kDaoResolveWrite,
                         dst_stage_mask, dst_access_mask, new_layout);
    // Also a write after a write of an earlier resolve, so not skipped when
    // the usage is the same.
    command_processor_.PushImageMemoryBarrier(
        vulkan_texture.image(), ui::vulkan::util::InitializeSubresourceRange(),
        src_stage_mask, dst_stage_mask, src_access_mask, dst_access_mask,
        old_layout, new_layout, VK_QUEUE_FAMILY_IGNORED,
        VK_QUEUE_FAMILY_IGNORED, false);

    if (!pipeline_bound) {
      pipeline_bound = true;
      command_processor_.BindExternalComputePipeline(dao_resolve_pipeline_);
      command_buffer.CmdVkBindDescriptorSets(
          VK_PIPELINE_BIND_POINT_COMPUTE, dao_resolve_pipeline_layout_, 0, 1,
          &area.source, 0, nullptr);
    }
    command_buffer.CmdVkBindDescriptorSets(VK_PIPELINE_BIND_POINT_COMPUTE,
                                           dao_resolve_pipeline_layout_, 1, 1,
                                           &dest, 0, nullptr);
    command_buffer.CmdVkPushConstants(dao_resolve_pipeline_layout_,
                                      VK_SHADER_STAGE_COMPUTE_BIT, 0,
                                      sizeof(constants), &constants);
    command_processor_.SubmitBarriers(true);
    command_buffer.CmdVkDispatch((constants.size[0] + 7) / 8,
                                 (constants.size[1] + 7) / 8, 1);
    texture->MarkAsUsed();
    MakeUpToDateAndWatch(*texture);
  }
  resolve_invalidated_textures_.clear();
}

}  // namespace vulkan
}  // namespace gpu
}  // namespace xe
