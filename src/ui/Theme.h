#pragma once

#include <imgui.h>

namespace Theme
{
ImVec4 Hex(unsigned rgb, float alpha = 1.0f);
void Apply();

// Colours used outside the style table.
inline constexpr unsigned kAccent = 0x4C6FC4;
inline constexpr unsigned kOk = 0x3FA46A;
inline constexpr unsigned kWarn = 0xD08A2E;
inline constexpr unsigned kError = 0xD0473F;
inline constexpr unsigned kBackground = 0xF4F7FC;

// Filled blue button with white text.
bool PrimaryButton(const char* label, bool small = false);
// Red text button for destructive actions.
bool DangerButton(const char* label);
}
