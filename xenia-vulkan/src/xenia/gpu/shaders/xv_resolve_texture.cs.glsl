#version 460
// DAO: writes a resolved area of a host render target into a texture of the
// resolved memory, the same texels the resolve and the texture's untiling would
// give (see VulkanTextureCache::DaoWriteResolvedTextures; the Direct3D 12
// version is dao_resolve_texture.cs.hlsl).
#extension GL_EXT_samplerless_texture_functions : require

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

// The render target's transfer source descriptor set.
layout(set = 0, binding = 0) uniform texture2D xv_resolve_source;
layout(set = 1, binding = 0, rgba8) uniform writeonly image2D xv_resolve_dest;

layout(push_constant) uniform XvResolveTextureConstants {
  uvec2 source_offset;
  uvec2 dest_offset;
  uvec2 size;
  // The source channel of each destination channel, 2 bits each (red / blue
  // swap and byte swaps of the resolve and the texture).
  uint channels;
} xv_resolve_constants;

void main() {
  uvec2 id = gl_GlobalInvocationID.xy;
  if (any(greaterThanEqual(id, xv_resolve_constants.size))) {
    return;
  }
  vec4 source = texelFetch(
      xv_resolve_source, ivec2(xv_resolve_constants.source_offset + id), 0);
  uint channels = xv_resolve_constants.channels;
  vec4 dest = vec4(source[channels & 3u], source[(channels >> 2u) & 3u],
                   source[(channels >> 4u) & 3u], source[(channels >> 6u) & 3u]);
  imageStore(xv_resolve_dest, ivec2(xv_resolve_constants.dest_offset + id),
             dest);
}
