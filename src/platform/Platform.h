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
// Another process holds that lock: the pet is running or still starting up.
bool IsPetRunning();

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

// Read-only memory mapping of a file. Mapped pages are backed by the file itself,
// so they are shared with other processes and not counted as our private memory.
struct MappedFile
{
    void* data = nullptr;
    size_t size = 0;
    void* handle = nullptr;
};
MappedFile MapFile(const fs::path& path);
void UnmapFile(MappedFile& file);

// Keep the pet above the taskbar and normal windows, re-asserted whenever the
// foreground window changes. It yields to other topmost windows (screenshot
// overlays, mini players) and can hide while a fullscreen app is in front.
// No-op where unsupported.
void SetStayOnTop(GLFWwindow* window, bool keepOnTop, bool hideForFullscreen);
// Re-check the foreground window (catches an app leaving fullscreen); call about once a second.
void UpdateStayOnTop();

// Wait for window events or until `timeout` seconds pass, then process events.
// Unlike glfwWaitEventsTimeout, this wakes on time on Windows, whose default
// timer only ticks every ~15.6 ms and makes frame pacing uneven.
void WaitEvents(double timeout);

// Open a folder in the system file manager.
void OpenFolder(const fs::path& folder);

// Environment variable as UTF-8 ("" if missing).
std::string GetEnv(const char* name);

// Starts `exe` (with one optional argument) as an independent process: no
// inherited handles or stdio, not tied to our lifetime. Returns false if it
// could not be started.
bool LaunchDetached(const fs::path& exe, const std::string& arg = {});
}
