// XDroid360: compat/rex/host.h and the ReXGlue system pieces built on it.
#include <rex/host.h>
#include <rex/kernel/xboxkrnl/video.h>
#include <rex/system/function_dispatcher.h>
#include <rex/system/xthread.h>

namespace rex::host {
namespace {
Services& Current() {
  static Services services;
  return services;
}
}  // namespace
void SetServices(Services services) { Current() = std::move(services); }
const Services& GetServices() { return Current(); }
}  // namespace rex::host

namespace rex::system {
namespace {
thread_local XThread t_thread;
}  // namespace

XThread* XThread::GetCurrentThread() { return &t_thread; }

void XHostThread::Create() {
  const auto& services = rex::host::GetServices();
  if (!services.start_guest_thread) {
    return;
  }
  join_ = services.start_guest_thread(name_, body_);
}
}  // namespace rex::system

namespace rex::runtime {
void FunctionDispatcher::ExecuteInterrupt(void* thread_state, uint32_t callback,
                                          const uint64_t* args, uint32_t arg_count) {
  const auto& services = rex::host::GetServices();
  if (!services.call_interrupt || arg_count < 2) {
    return;
  }
  auto* thread = static_cast<rex::system::XThread*>(thread_state);
  services.call_interrupt(callback, uint32_t(args[1]), uint32_t(args[0]),
                          thread ? thread->active_cpu() : 0);
}
}  // namespace rex::runtime

namespace rex::memory {
bool AddVirtualMappedRange(Memory*, uint32_t base, uint32_t mask, uint32_t size, void* context,
                           rex::host::MmioRead read, rex::host::MmioWrite write) {
  const auto& services = rex::host::GetServices();
  return services.map_mmio && services.map_mmio(base, mask, size, context, read, write);
}
}  // namespace rex::memory

namespace rex::kernel::xboxkrnl {
void VdQueryVideoMode(rex::system::X_VIDEO_MODE* mode) {
  const auto& services = rex::host::GetServices();
  const rex::host::VideoMode host =
      services.video_mode ? services.video_mode() : rex::host::VideoMode{};
  *mode = {};
  mode->display_width = host.width;
  mode->display_height = host.height;
  mode->is_widescreen = host.widescreen;
  mode->is_hi_def = host.hi_def;
  mode->refresh_rate = host.refresh_rate;
  mode->unknown_0x8a = 0x8A;
  mode->unknown_0x01 = 1;
}
}  // namespace rex::kernel::xboxkrnl
