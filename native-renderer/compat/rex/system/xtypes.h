// XDroid360: the X_STATUS values the native renderer returns (rex/system/xtypes.h).
#pragma once

#include <cstdint>

namespace rex {
using X_STATUS = uint32_t;
}  // namespace rex
#ifndef X_STATUS_SUCCESS
#define X_STATUS_SUCCESS ((rex::X_STATUS)0x00000000L)
#endif
#ifndef X_STATUS_UNSUCCESSFUL
#define X_STATUS_UNSUCCESSFUL ((rex::X_STATUS)0xC0000001L)
#endif
#ifndef XFAILED
#define XFAILED(s) (((rex::X_STATUS)(s)) & 0x80000000)
#endif
