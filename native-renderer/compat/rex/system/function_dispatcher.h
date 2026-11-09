// XDroid360: ReXGlue's FunctionDispatcher as the renderer uses it: the guest memory, and running
// the guest's graphics interrupt callback (through the runtime).
#pragma once

#include <cstdint>

#include <rex/host.h>
#include <rex/system/xmemory.h>
#include <rex/system/xthread.h>

namespace rex::runtime {

class FunctionDispatcher {
 public:
  explicit FunctionDispatcher(rex::memory::Memory* memory) : memory_(memory) {}
  rex::memory::Memory* memory() const { return memory_; }
  // args: {source, user data}, as the renderer passes them.
  void ExecuteInterrupt(void* thread_state, uint32_t callback, const uint64_t* args,
                        uint32_t arg_count);

 private:
  rex::memory::Memory* memory_;
};

}  // namespace rex::runtime

namespace rex::memory {
// ReXGlue's Memory::AddVirtualMappedRange: here the runtime routes the range (rex::host).
bool AddVirtualMappedRange(Memory* memory, uint32_t base, uint32_t mask, uint32_t size,
                           void* context, rex::host::MmioRead read, rex::host::MmioWrite write);
}  // namespace rex::memory
