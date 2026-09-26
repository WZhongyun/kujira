#include "platform/Platform.h"

#include <iterator>

#include <windows.h>
#include <psapi.h>
#include <shellapi.h>
#include <shlobj.h>

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
    PROCESS_MEMORY_COUNTERS_EX pmc{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc), sizeof(pmc)))
    {
        return pmc.PrivateUsage;
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
