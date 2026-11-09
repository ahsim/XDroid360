// XDroid360: ReXGlue's settings overlay (an ImGui menu) isn't ours: the launcher has the settings.
// OcultarAjustesEnMenu ("hide these settings in the menu") does nothing.
#pragma once

#include <string>
#include <vector>

namespace rex::ui {
inline void OcultarAjustesEnMenu(const std::vector<std::string>&) {}
}  // namespace rex::ui
