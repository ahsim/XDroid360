// XDroid360: ReXGlue's guest memory (rex/system/xmemory.h) as our xe::Memory, the same class.
#pragma once

#include "xenia/base/byte_order.h"
#include "xenia/base/memory.h"
#include "xenia/memory.h"

namespace rex::memory {
using xe::Memory;
using xe::load_and_swap;
using xe::store_and_swap;
}  // namespace rex::memory
