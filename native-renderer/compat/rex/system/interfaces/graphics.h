// XDroid360: ReXGlue's graphics system interface (rex/system/interfaces/graphics.h), which the
// native renderer's system implements; the runtime drives it the way it drives Xenia's.
#pragma once

#include <cstdint>
#include <filesystem>

#include <rex/system/function_dispatcher.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xtypes.h>
#include <rex/ui/presenter.h>
#include <rex/ui/windowed_app_context.h>

namespace rex::system {

class IGraphicsSystem {
 public:
  virtual ~IGraphicsSystem() = default;
  virtual X_STATUS SetupPresentation(ui::WindowedAppContext* app_context) = 0;
  virtual X_STATUS SetupGuestGpu(runtime::FunctionDispatcher* function_dispatcher,
                                 KernelState* kernel_state) = 0;
  virtual bool has_presentation() const = 0;
  virtual ui::GraphicsProvider* provider() const { return nullptr; }
  virtual ui::Presenter* presenter() const { return nullptr; }
  virtual void SetInterruptCallback(uint32_t callback, uint32_t user_data) {}
  virtual void InitializeRingBuffer(uint32_t ptr, uint32_t size_log2) {}
  virtual void EnableReadPointerWriteBack(uint32_t ptr, uint32_t block_size_log2) {}
  virtual void InitializeShaderStorage(const std::filesystem::path& cache_root,
                                       uint32_t title_id, bool blocking) {}
  X_STATUS Setup(runtime::FunctionDispatcher* function_dispatcher, KernelState* kernel_state,
                 ui::WindowedAppContext* app_context, bool with_presentation) {
    if (with_presentation && !has_presentation()) {
      X_STATUS status = SetupPresentation(app_context);
      if (XFAILED(status)) {
        return status;
      }
    }
    return SetupGuestGpu(function_dispatcher, kernel_state);
  }
  virtual void Shutdown() = 0;
};

}  // namespace rex::system
