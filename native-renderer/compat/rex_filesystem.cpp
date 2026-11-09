// XDroid360: compat/rex/filesystem.h.
#include <rex/filesystem.h>

namespace rex::filesystem {
namespace {
std::filesystem::path& DataFolder() {
  static std::filesystem::path folder;
  return folder;
}
}  // namespace
void SetNativeRendererDataFolder(const std::filesystem::path& folder) { DataFolder() = folder; }
std::filesystem::path GetExecutableFolder() { return DataFolder(); }
std::filesystem::path GetExecutablePath() { return DataFolder() / "game"; }
}  // namespace rex::filesystem
