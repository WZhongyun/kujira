#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/FileUtil.h"

struct GLFWwindow;

// Thin platform layer. Everything OS specific lives behind these calls.
namespace Platform
{
// Per-user config directory, e.g. %APPDATA%\Kujira or ~/.config/kujira.
fs::path ConfigDir();

// User home directory.
fs::path HomeDir();

// Returns false if another instance already holds the lock.
bool AcquireSingleInstance();

// Hide the pet window from the taskbar / Alt-Tab (tool window on Windows).
void MakeToolWindow(GLFWwindow* window);

// Private memory of this process in bytes (0 if unknown).
uint64_t ProcessMemoryBytes();

// Launch at login.
bool AutostartSupported();
bool IsAutostartEnabled();
bool SetAutostart(bool enable);

// Candidate CJK font files for the settings window, best first.
// The int is the face index inside a .ttc collection.
std::vector<std::pair<fs::path, int>> CjkFontCandidates();

// Open a folder in the system file manager.
void OpenFolder(const fs::path& folder);

// Environment variable as UTF-8 ("" if missing).
std::string GetEnv(const char* name);
}
