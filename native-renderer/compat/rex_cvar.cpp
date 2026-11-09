// XDroid360: rex::cvar on our side (compat/rex/cvar.h).
#include <rex/cvar.h>

#include <algorithm>
#include <charconv>
#include <cstdlib>
#include <mutex>
#include <unordered_map>

namespace rex::string {

namespace {
template <typename T>
T ParseInteger(std::string_view value, T fallback) {
  T result{};
  const char* begin = value.data();
  const char* end = value.data() + value.size();
  int base = 10;
  if (value.size() > 2 && value[0] == '0' && (value[1] == 'x' || value[1] == 'X')) {
    begin += 2;
    base = 16;
  }
  const auto [ptr, error] = std::from_chars(begin, end, result, base);
  return error == std::errc() && ptr == end ? result : fallback;
}
}  // namespace

template <>
bool from_string<bool>(std::string_view value, bool fallback) {
  if (value == "true" || value == "1" || value == "yes" || value == "on") return true;
  if (value == "false" || value == "0" || value == "no" || value == "off") return false;
  return fallback;
}
template <>
int32_t from_string<int32_t>(std::string_view value, int32_t fallback) {
  return ParseInteger<int32_t>(value, fallback);
}
template <>
uint32_t from_string<uint32_t>(std::string_view value, uint32_t fallback) {
  return ParseInteger<uint32_t>(value, fallback);
}
template <>
int64_t from_string<int64_t>(std::string_view value, int64_t fallback) {
  return ParseInteger<int64_t>(value, fallback);
}
template <>
uint64_t from_string<uint64_t>(std::string_view value, uint64_t fallback) {
  return ParseInteger<uint64_t>(value, fallback);
}

}  // namespace rex::string

namespace rex::cvar {

namespace {
struct Registry {
  std::mutex mutex;
  std::vector<FlagEntry> entries;
  std::unordered_multimap<std::string, ChangeCallback> callbacks;
};
Registry& GetRegistry() {
  static Registry registry;
  return registry;
}
FlagEntry* Find(Registry& registry, std::string_view name) {
  for (FlagEntry& entry : registry.entries) {
    if (entry.name == name) return &entry;
  }
  return nullptr;
}
}  // namespace

bool RegisterFlag(FlagEntry entry) {
  Registry& registry = GetRegistry();
  std::lock_guard lock(registry.mutex);
  if (Find(registry, entry.name)) return false;
  registry.entries.push_back(std::move(entry));
  return true;
}

void UnregisterFlag(std::string_view name) {
  Registry& registry = GetRegistry();
  std::lock_guard lock(registry.mutex);
  std::erase_if(registry.entries, [&](const FlagEntry& entry) { return entry.name == name; });
}

bool SetFlagByName(std::string_view name, std::string_view value) {
  Registry& registry = GetRegistry();
  std::vector<ChangeCallback> to_call;
  std::string new_value;
  {
    std::lock_guard lock(registry.mutex);
    FlagEntry* entry = Find(registry, name);
    if (!entry || !entry->setter || !entry->setter(value)) return false;
    new_value = entry->getter ? entry->getter() : std::string(value);
    const auto [first, last] = registry.callbacks.equal_range(std::string(name));
    for (auto it = first; it != last; ++it) to_call.push_back(it->second);
  }
  for (const ChangeCallback& callback : to_call) callback(name, new_value);
  return true;
}

std::string GetFlagByName(std::string_view name) {
  Registry& registry = GetRegistry();
  std::lock_guard lock(registry.mutex);
  const FlagEntry* entry = Find(registry, name);
  return entry && entry->getter ? entry->getter() : std::string();
}

const FlagEntry* GetFlagInfo(std::string_view name) {
  Registry& registry = GetRegistry();
  std::lock_guard lock(registry.mutex);
  return Find(registry, name);
}

Source GetFlagSource(std::string_view name) {
  const FlagEntry* entry = GetFlagInfo(name);
  return entry && entry->getter && entry->getter() != entry->default_value ? Source::kRuntime
                                                                           : Source::kDefault;
}

bool ParseDouble(std::string_view value, double& out) {
  std::string text(value);
  char* end = nullptr;
  out = std::strtod(text.c_str(), &end);
  return end && end != text.c_str() && *end == '\0';
}

void RegisterChangeCallback(std::string_view name, ChangeCallback callback) {
  Registry& registry = GetRegistry();
  std::lock_guard lock(registry.mutex);
  registry.callbacks.emplace(std::string(name), std::move(callback));
}

namespace {
template <typename F>
void ChangeEntry(const std::string& name, F&& change) {
  if (name.empty()) return;
  Registry& registry = GetRegistry();
  std::lock_guard lock(registry.mutex);
  if (FlagEntry* entry = Find(registry, name)) change(*entry);
}
}  // namespace

FlagRegistrar&& FlagRegistrar::range(double min_value, double max_value) && {
  ChangeEntry(name_, [&](FlagEntry& entry) {
    entry.constraints.min = min_value;
    entry.constraints.max = max_value;
  });
  return std::move(*this);
}
FlagRegistrar&& FlagRegistrar::allowed(std::initializer_list<std::string> values) && {
  ChangeEntry(name_, [&](FlagEntry& entry) { entry.constraints.allowed_values = values; });
  return std::move(*this);
}
FlagRegistrar&& FlagRegistrar::lifecycle(Lifecycle lifecycle) && {
  ChangeEntry(name_, [&](FlagEntry& entry) { entry.lifecycle = lifecycle; });
  return std::move(*this);
}
FlagRegistrar&& FlagRegistrar::debug_only() && {
  ChangeEntry(name_, [&](FlagEntry& entry) { entry.is_debug_only = true; });
  return std::move(*this);
}

}  // namespace rex::cvar
