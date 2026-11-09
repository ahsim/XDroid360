// XDroid360: the video mode the renderer asks for (rex/system/xvideo.h), host-endian here.
#pragma once

#include <cstdint>

namespace rex::system {
struct X_VIDEO_MODE {
  uint32_t display_width;
  uint32_t display_height;
  uint32_t is_interlaced;
  uint32_t is_widescreen;
  uint32_t is_hi_def;
  float refresh_rate;
  uint32_t video_standard;
  uint32_t unknown_0x8a;
  uint32_t unknown_0x01;
  uint32_t reserved[3];
};
}  // namespace rex::system
