// XDroid360: ReXGlue's thread helpers (rex/thread.h) onto Xenia's threading.
#pragma once

#include <chrono>
#include <string>
#include <thread>

#include "xenia/base/threading.h"

namespace rex::thread {
inline void set_current_thread_name(const std::string& name) { xe::threading::set_name(name); }
template <typename Rep, typename Period>
inline void Sleep(std::chrono::duration<Rep, Period> duration) {
  std::this_thread::sleep_for(duration);
}
}  // namespace rex::thread
