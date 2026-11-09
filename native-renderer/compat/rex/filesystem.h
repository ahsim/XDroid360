// XDroid360: what the native renderer took from ReXGlue's filesystem helpers. The "executable
// folder" it keeps files next to is the game instance's user data folder here (the runtime sets it).
#pragma once

#include <filesystem>

namespace rex::filesystem {
std::filesystem::path GetExecutableFolder();
std::filesystem::path GetExecutablePath();
void SetNativeRendererDataFolder(const std::filesystem::path& folder);
}  // namespace rex::filesystem
