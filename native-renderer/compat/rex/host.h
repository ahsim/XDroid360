// XDroid360: what the native renderer's ReXGlue-shaped code needs from our runtime, as
// callbacks the runtime sets before it creates the renderer (android/runtime). Everything under
// compat/rex/system goes through these.
#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace rex::host {

struct VideoMode {
  uint32_t width = 1280;
  uint32_t height = 720;
  float refresh_rate = 60.0f;
  bool widescreen = true;
  bool hi_def = true;
};

using MmioRead = uint32_t (*)(void* ppc_context, void* context, uint32_t address);
using MmioWrite = void (*)(void* ppc_context, void* context, uint32_t address, uint32_t value);

struct Services {
  // Runs `body` on a new host thread attached as a guest thread (so it can call into guest code,
  // for the interrupt callback), named `name`; returns a function that waits for it to end.
  std::function<std::function<void()>(const std::string& name, std::function<int()> body)>
      start_guest_thread;
  // Calls the guest's graphics interrupt callback with (source, user data) on the calling thread,
  // which must be a guest-attached one, on CPU `cpu`.
  std::function<void(uint32_t callback, uint32_t user_data, uint32_t source, uint32_t cpu)>
      call_interrupt;
  // Routes the GPU's MMIO range (writes from recompiled stores, reads where the runtime has a
  // hook) to the renderer's handlers.
  std::function<bool(uint32_t base, uint32_t mask, uint32_t size, void* context, MmioRead read,
                     MmioWrite write)>
      map_mmio;
  std::function<VideoMode()> video_mode;
};

void SetServices(Services services);
const Services& GetServices();

}  // namespace rex::host
