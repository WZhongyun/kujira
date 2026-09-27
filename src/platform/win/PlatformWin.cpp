#include "platform/Platform.h"

#include <algorithm>
#include <cwchar>
#include <iterator>

#include <windows.h>
#include <psapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <timeapi.h>

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

namespace
{
const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const wchar_t* kRunValue = L"Kujira";

std::wstring Widen(const std::string& s)
{
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

std::string Narrow(const std::wstring& w)
{
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

// Stay-on-top state (one pet window per process).
HWND g_pet = nullptr;
bool g_keepOnTop = false;
bool g_hideForFullscreen = false;
bool g_hiddenForFullscreen = false;
HWINEVENTHOOK g_foregroundHook = nullptr;

bool HasClass(HWND hwnd, const wchar_t* name)
{
    wchar_t cls[64] = {};
    GetClassNameW(hwnd, cls, static_cast<int>(std::size(cls)));
    return wcscmp(cls, name) == 0;
}

bool IsTaskbar(HWND hwnd)
{
    return HasClass(hwnd, L"Shell_TrayWnd") || HasClass(hwnd, L"Shell_SecondaryTrayWnd");
}

bool IsDesktop(HWND hwnd)
{
    return HasClass(hwnd, L"Progman") || HasClass(hwnd, L"WorkerW");
}

bool IsFullscreen(HWND hwnd)
{
    if (IsDesktop(hwnd) || IsTaskbar(hwnd) || !IsWindowVisible(hwnd) || IsIconic(hwnd)) return false;
    RECT rc{};
    if (!GetWindowRect(hwnd, &rc)) return false;
    MONITORINFO mi{};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &mi)) return false;
    return rc.left <= mi.rcMonitor.left && rc.top <= mi.rcMonitor.top &&
           rc.right >= mi.rcMonitor.right && rc.bottom >= mi.rcMonitor.bottom;
}

void RaisePet()
{
    SetWindowPos(g_pet, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER);
}

void CheckForeground()
{
    if (!g_pet) return;
    HWND fg = GetForegroundWindow();
    if (!fg) return;
    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    if (pid == GetCurrentProcessId()) return;  // our own settings window

    const bool fgTopmost = (GetWindowLongW(fg, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
    const bool taskbar = IsTaskbar(fg);

    // Hide behind a fullscreen app (video, game). Topmost overlays such as the
    // screenshot selection layer also cover the screen, so they don't count.
    const bool hide = g_hideForFullscreen && !fgTopmost && IsFullscreen(fg);
    if (hide != g_hiddenForFullscreen)
    {
        g_hiddenForFullscreen = hide;
        ShowWindow(g_pet, hide ? SW_HIDE : SW_SHOWNOACTIVATE);
    }
    if (hide) return;

    // Yield to other topmost windows, except the taskbar which we want to stay above.
    if (g_keepOnTop && (taskbar || !fgTopmost))
    {
        RaisePet();
    }
}

void CALLBACK OnForegroundChanged(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD)
{
    CheckForeground();
}

std::wstring ExePath()
{
    wchar_t buf[MAX_PATH * 4];
    DWORD n = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(std::size(buf)));
    return std::wstring(buf, n);
}
}

namespace Platform
{
fs::path ConfigDir()
{
    PWSTR appData = nullptr;
    fs::path dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appData)))
    {
        dir = fs::path(appData) / L"Kujira";
    }
    CoTaskMemFree(appData);
    if (dir.empty())
    {
        dir = FileUtil::ExecutableDir() / L"config";
    }
    return dir;
}

fs::path HomeDir()
{
    PWSTR profile = nullptr;
    fs::path dir;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Profile, 0, nullptr, &profile)))
    {
        dir = fs::path(profile);
    }
    CoTaskMemFree(profile);
    return dir;
}

bool AcquireSingleInstance()
{
    // Intentionally leaked: the mutex lives as long as the process.
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"Local\\KujiraDesktopPet");
    return mutex != nullptr && GetLastError() != ERROR_ALREADY_EXISTS;
}

void MakeToolWindow(GLFWwindow* window)
{
    HWND hwnd = glfwGetWin32Window(window);
    LONG_PTR ex = GetWindowLongPtrW(hwnd, GWL_EXSTYLE);
    ex = (ex | WS_EX_TOOLWINDOW) & ~static_cast<LONG_PTR>(WS_EX_APPWINDOW);
    SetWindowLongPtrW(hwnd, GWL_EXSTYLE, ex);
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED | SWP_NOACTIVATE);
}

uint64_t ProcessMemoryBytes()
{
    // Private working set: the "Memory" column in Task Manager. The field exists since
    // Windows 10 1809 (PROCESS_MEMORY_COUNTERS_EX2); declared here so older SDKs build.
    struct Counters
    {
        PROCESS_MEMORY_COUNTERS_EX base;
        SIZE_T privateWorkingSetSize;
        ULONG64 sharedCommitUsage;
    } pmc{};
    pmc.base.cb = sizeof(pmc);
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc)))
    {
        return pmc.privateWorkingSetSize;
    }
    PROCESS_MEMORY_COUNTERS basic{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &basic, sizeof(basic)))
    {
        return basic.WorkingSetSize;
    }
    return 0;
}

bool AutostartSupported()
{
    return true;
}

bool IsAutostartEnabled()
{
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_READ, &key) != ERROR_SUCCESS)
    {
        return false;
    }
    DWORD type = 0;
    LONG r = RegQueryValueExW(key, kRunValue, nullptr, &type, nullptr, nullptr);
    RegCloseKey(key);
    return r == ERROR_SUCCESS;
}

bool SetAutostart(bool enable)
{
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS)
    {
        return false;
    }
    LONG r;
    if (enable)
    {
        std::wstring cmd = L"\"" + ExePath() + L"\"";
        r = RegSetValueExW(key, kRunValue, 0, REG_SZ, reinterpret_cast<const BYTE*>(cmd.c_str()),
                           static_cast<DWORD>((cmd.size() + 1) * sizeof(wchar_t)));
    }
    else
    {
        r = RegDeleteValueW(key, kRunValue);
        if (r == ERROR_FILE_NOT_FOUND) r = ERROR_SUCCESS;
    }
    RegCloseKey(key);
    return r == ERROR_SUCCESS;
}

std::vector<std::pair<fs::path, int>> CjkFontCandidates()
{
    wchar_t winDir[MAX_PATH];
    GetWindowsDirectoryW(winDir, MAX_PATH);
    fs::path fonts = fs::path(winDir) / L"Fonts";
    return {
        { fonts / L"msyh.ttc", 0 },    // Microsoft YaHei
        { fonts / L"msyh.ttf", 0 },
        { fonts / L"simhei.ttf", 0 },
        { fonts / L"simsun.ttc", 0 },
    };
}

MappedFile MapFile(const fs::path& path)
{
    MappedFile file;
    HANDLE handle = CreateFileW(path.wstring().c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return file;
    LARGE_INTEGER size{};
    if (GetFileSizeEx(handle, &size) && size.QuadPart > 0)
    {
        HANDLE mapping = CreateFileMappingW(handle, nullptr, PAGE_READONLY, 0, 0, nullptr);
        if (mapping)
        {
            void* data = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, 0);
            if (data)
            {
                file.data = data;
                file.size = static_cast<size_t>(size.QuadPart);
                file.handle = mapping;
            }
            else
            {
                CloseHandle(mapping);
            }
        }
    }
    CloseHandle(handle);
    return file;
}

void UnmapFile(MappedFile& file)
{
    if (file.data) UnmapViewOfFile(file.data);
    if (file.handle) CloseHandle(static_cast<HANDLE>(file.handle));
    file = {};
}

void WaitEvents(double timeout)
{
    // High resolution waitable timer (Windows 10 1803+): precise wake-ups without
    // raising the system-wide timer rate. Older systems fall back to timeBeginPeriod.
    static HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (!timer)
    {
        static const bool raised = timeBeginPeriod(1) == TIMERR_NOERROR;
        (void)raised;
        glfwWaitEventsTimeout(timeout);
        return;
    }
    LARGE_INTEGER due{};
    due.QuadPart = -std::max<LONGLONG>(1, static_cast<LONGLONG>(timeout * 1e7));  // relative, 100 ns units
    if (SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE))
    {
        MsgWaitForMultipleObjects(1, &timer, FALSE, INFINITE, QS_ALLINPUT);
    }
    glfwPollEvents();
}

void SetStayOnTop(GLFWwindow* window, bool keepOnTop, bool hideForFullscreen)
{
    g_pet = glfwGetWin32Window(window);
    g_keepOnTop = keepOnTop;
    g_hideForFullscreen = hideForFullscreen;
    const bool wanted = keepOnTop || hideForFullscreen;
    if (wanted && !g_foregroundHook)
    {
        // Out-of-context hook: delivered through this thread's message loop, no DLL injection.
        g_foregroundHook = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr,
                                           OnForegroundChanged, 0, 0, WINEVENT_OUTOFCONTEXT | WINEVENT_SKIPOWNPROCESS);
    }
    else if (!wanted && g_foregroundHook)
    {
        UnhookWinEvent(g_foregroundHook);
        g_foregroundHook = nullptr;
    }
    if (!hideForFullscreen && g_hiddenForFullscreen)
    {
        g_hiddenForFullscreen = false;
        ShowWindow(g_pet, SW_SHOWNOACTIVATE);
    }
    CheckForeground();
}

void UpdateStayOnTop()
{
    if (g_foregroundHook) CheckForeground();
}

void OpenFolder(const fs::path& folder)
{
    ShellExecuteW(nullptr, L"open", folder.wstring().c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

std::string GetEnv(const char* name)
{
    std::wstring wname = Widen(name);
    DWORD n = GetEnvironmentVariableW(wname.c_str(), nullptr, 0);
    if (n == 0) return {};
    std::wstring value(n, L'\0');
    n = GetEnvironmentVariableW(wname.c_str(), value.data(), n);
    value.resize(n);
    return Narrow(value);
}
}
