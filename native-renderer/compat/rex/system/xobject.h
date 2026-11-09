// XDroid360: ReXGlue's object_ref (rex/system/xobject.h) for the renderer's two host threads; a
// shared owning pointer is enough here.
#pragma once

#include <memory>

namespace rex::system {
template <typename T>
class object_ref {
 public:
  object_ref() = default;
  explicit object_ref(T* object) : object_(object) {}
  T* operator->() const { return object_.get(); }
  T* get() const { return object_.get(); }
  explicit operator bool() const { return bool(object_); }
  void reset() { object_.reset(); }

 private:
  std::shared_ptr<T> object_;
};
}  // namespace rex::system
