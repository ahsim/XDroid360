// XDroid360: the configuration variable API the native renderer was written against (ReXGlue's
// rex::cvar, BSD-3-Clause, rexglue-sdk include/rex/cvar.h), reimplemented small on our side: a
// registry by name with string get/set and change callbacks, and the REXCVAR_* macros with the
// same meaning (a function-local storage per variable, registered at static initialization).
// The game's config.toml [xenia] section sets them by name through SetFlagByName, like Xenia's.
#pragma once

#include <cstdint>
#include <functional>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace rex::string {
template <typename T>
T from_string(std::string_view value, T fallback);
template <>
bool from_string<bool>(std::string_view value, bool fallback);
template <>
int32_t from_string<int32_t>(std::string_view value, int32_t fallback);
template <>
uint32_t from_string<uint32_t>(std::string_view value, uint32_t fallback);
template <>
int64_t from_string<int64_t>(std::string_view value, int64_t fallback);
template <>
uint64_t from_string<uint64_t>(std::string_view value, uint64_t fallback);
}  // namespace rex::string

namespace rex::cvar {

enum class FlagType { Boolean, Int32, Int64, UInt32, UInt64, Double, String, Command };

enum class Lifecycle { kInitOnly, kHotReload, kRequiresRestart };

// Where a value came from: config.toml or code here (no command line), so kDefault or kRuntime.
enum class Source { kDefault, kConfig, kEnvironment, kCommandLine, kRuntime };
Source GetFlagSource(std::string_view name);

struct Constraints {
  std::optional<double> min;
  std::optional<double> max;
  std::vector<std::string> allowed_values;
};

struct FlagEntry {
  std::string name;
  FlagType type;
  std::string category;
  std::string description;
  std::function<bool(std::string_view)> setter;
  std::function<std::string()> getter;
  std::function<void(std::string_view args)> command_callback;
  Lifecycle lifecycle = Lifecycle::kHotReload;
  Constraints constraints;
  std::string default_value;
  bool is_debug_only = false;
};

bool RegisterFlag(FlagEntry entry);
void UnregisterFlag(std::string_view name);
// Sets a flag from its text form; false if there's no such flag or the value is rejected.
bool SetFlagByName(std::string_view name, std::string_view value);
// The flag's value as text, or "" if there's no such flag.
std::string GetFlagByName(std::string_view name);
// The registered entry, or nullptr (valid until the next registration).
const FlagEntry* GetFlagInfo(std::string_view name);
bool ParseDouble(std::string_view value, double& out);

using ChangeCallback =
    std::function<void(std::string_view name, std::string_view new_value)>;
void RegisterChangeCallback(std::string_view name, ChangeCallback callback);

// Registers at construction; the chained calls change the registered entry.
struct FlagRegistrar {
  std::string name_;
  explicit FlagRegistrar(FlagEntry entry) : name_(entry.name) {
    if (!RegisterFlag(std::move(entry))) {
      name_.clear();
    }
  }
  FlagRegistrar(FlagRegistrar&& other) noexcept : name_(std::move(other.name_)) {
    other.name_.clear();
  }
  FlagRegistrar&& range(double min_value, double max_value) &&;
  FlagRegistrar&& allowed(std::initializer_list<std::string> values) &&;
  FlagRegistrar&& lifecycle(Lifecycle lifecycle) &&;
  FlagRegistrar&& debug_only() &&;
};

}  // namespace rex::cvar

#define REXCVAR_DECLARE(type, name) type& FLAGS_##name##_storage_()
#define REXCVAR_GET(name) (FLAGS_##name##_storage_())
#define REXCVAR_SET(name, value) (FLAGS_##name##_storage_() = (value))

#define REXCVAR_DEFINE_TYPED_(ctype, flag_type, name, default_val, category, desc, to_text) \
  ctype& FLAGS_##name##_storage_() {                                                       \
    static ctype storage = (default_val);                                                  \
    return storage;                                                                        \
  }                                                                                        \
  static auto _cvar_reg_##name = ::rex::cvar::FlagRegistrar(                               \
      {#name, ::rex::cvar::FlagType::flag_type, category, desc,                            \
       [](std::string_view v) {                                                            \
         FLAGS_##name##_storage_() =                                                       \
             ::rex::string::from_string<ctype>(v, FLAGS_##name##_storage_());              \
         return true;                                                                      \
       },                                                                                  \
       []() { return to_text(FLAGS_##name##_storage_()); }, [](std::string_view) {},       \
       ::rex::cvar::Lifecycle::kHotReload, {}, to_text(ctype(default_val)), false})

#define REXCVAR_BOOL_TEXT_(v) std::string((v) ? "true" : "false")
#define REXCVAR_DEFINE_BOOL(name, default_val, category, desc) \
  REXCVAR_DEFINE_TYPED_(bool, Boolean, name, default_val, category, desc, REXCVAR_BOOL_TEXT_)
#define REXCVAR_DEFINE_INT32(name, default_val, category, desc) \
  REXCVAR_DEFINE_TYPED_(int32_t, Int32, name, default_val, category, desc, std::to_string)
#define REXCVAR_DEFINE_INT(name, default_val, category, desc) \
  REXCVAR_DEFINE_INT32(name, default_val, category, desc)
#define REXCVAR_DEFINE_INT64(name, default_val, category, desc) \
  REXCVAR_DEFINE_TYPED_(int64_t, Int64, name, default_val, category, desc, std::to_string)
#define REXCVAR_DEFINE_UINT32(name, default_val, category, desc) \
  REXCVAR_DEFINE_TYPED_(uint32_t, UInt32, name, default_val, category, desc, std::to_string)
#define REXCVAR_DEFINE_UINT(name, default_val, category, desc) \
  REXCVAR_DEFINE_UINT32(name, default_val, category, desc)
#define REXCVAR_DEFINE_UINT64(name, default_val, category, desc) \
  REXCVAR_DEFINE_TYPED_(uint64_t, UInt64, name, default_val, category, desc, std::to_string)

#define REXCVAR_DEFINE_DOUBLE(name, default_val, category, desc)                     \
  double& FLAGS_##name##_storage_() {                                                \
    static double storage = (default_val);                                           \
    return storage;                                                                  \
  }                                                                                  \
  static auto _cvar_reg_##name = ::rex::cvar::FlagRegistrar(                         \
      {#name, ::rex::cvar::FlagType::Double, category, desc,                         \
       [](std::string_view v) {                                                      \
         double value;                                                               \
         if (!::rex::cvar::ParseDouble(v, value)) return false;                      \
         FLAGS_##name##_storage_() = value;                                          \
         return true;                                                                \
       },                                                                            \
       []() { return std::to_string(FLAGS_##name##_storage_()); },                   \
       [](std::string_view) {}, ::rex::cvar::Lifecycle::kHotReload, {},              \
       std::to_string(double(default_val)), false})

#define REXCVAR_DEFINE_STRING(name, default_val, category, desc)                     \
  std::string& FLAGS_##name##_storage_() {                                           \
    static std::string storage = (default_val);                                      \
    return storage;                                                                  \
  }                                                                                  \
  static auto _cvar_reg_##name = ::rex::cvar::FlagRegistrar(                         \
      {#name, ::rex::cvar::FlagType::String, category, desc,                         \
       [](std::string_view v) {                                                      \
         FLAGS_##name##_storage_() = std::string(v);                                 \
         return true;                                                                \
       },                                                                            \
       []() { return FLAGS_##name##_storage_(); }, [](std::string_view) {},          \
       ::rex::cvar::Lifecycle::kHotReload, {}, std::string(default_val), false})
