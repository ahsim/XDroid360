// XDroid360: ReXGlue's threads as the renderer uses them (rex/system/xthread.h). Its vblank and
// ring threads (XHostThread) start through the runtime as guest-attached host threads, and the
// interrupt path asks for the current thread only to dispatch on it.
#pragma once

#include <cstdint>
#include <functional>
#include <string>

#include <rex/host.h>
#include <rex/system/kernel_state.h>

namespace rex::system {

class XThread {
 public:
  // Non-null on any thread (one per thread, made on first use).
  static XThread* GetCurrentThread();
  void SetActiveCpu(uint8_t cpu) { active_cpu_ = cpu; }
  uint8_t active_cpu() const { return active_cpu_; }
  void* thread_state() { return this; }

 private:
  uint8_t active_cpu_ = 0;
};

class XHostThread {
 public:
  XHostThread(KernelState*, uint32_t stack_size, uint32_t flags, std::function<int()> body)
      : body_(std::move(body)) {
    (void)stack_size;
    (void)flags;
  }
  void set_name(std::string name) { name_ = std::move(name); }
  void Create();
  // Waits for the thread to end (ReXGlue's arguments, unused: an unbounded wait).
  void Wait(uint32_t, uint32_t, uint32_t, void*) {
    if (join_) {
      join_();
      join_ = nullptr;
    }
  }

 private:
  std::string name_;
  std::function<int()> body_;
  std::function<void()> join_;
};

}  // namespace rex::system
