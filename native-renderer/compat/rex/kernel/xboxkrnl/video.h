// XDroid360: VdQueryVideoMode for the renderer, from the runtime's video mode (rex::host).
#pragma once

#include <rex/system/xvideo.h>

namespace rex::kernel::xboxkrnl {
void VdQueryVideoMode(rex::system::X_VIDEO_MODE* mode);
}  // namespace rex::kernel::xboxkrnl
