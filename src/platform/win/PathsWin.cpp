// Paths and process launching, shared by Kujira and the kujira-hook forwarder
// (which must not pull in GLFW or OpenGL).
#include "platform/Platform.h"

#include <windows.h>
#include <shlobj.h>

namespace
{
const wchar_t* kInstanceMutex = L"Local\\KujiraDesktopPet";
}

namespace Platform
{
bool AcquireSingleInstance()
{
    // Intentionally leaked: the mutex lives as long as the process.
    HANDLE mutex = CreateMutexW(nullptr, TRUE, kInstanceMutex);
    return mutex != nullptr && GetLastError() != ERROR_ALREADY_EXISTS;
}

bool IsPetRunning()
{
    // Only looks: opening an existing mutex doesn't take it.
    HANDLE mutex = OpenMutexW(SYNCHRONIZE, FALSE, kInstanceMutex);
    if (!mutex) return false;
    CloseHandle(mutex);
    return true;
}

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

bool LaunchDetached(const fs::path& exe, const std::string& arg)
{
    std::wstring cmd = L"\"" + exe.wstring() + L"\"";
    if (!arg.empty()) cmd += L" " + std::wstring(arg.begin(), arg.end());  // ASCII flags only
    const std::wstring dir = exe.parent_path().wstring();
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_FORCEOFFFEEDBACK;  // no busy cursor while she starts
    PROCESS_INFORMATION pi{};
    // Leave the agent's job object if allowed, so closing the agent doesn't close her.
    DWORD flags = CREATE_NEW_PROCESS_GROUP | CREATE_BREAKAWAY_FROM_JOB;
    BOOL ok = CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, flags, nullptr, dir.c_str(), &si, &pi);
    if (!ok && GetLastError() == ERROR_ACCESS_DENIED)
    {
        flags &= ~CREATE_BREAKAWAY_FROM_JOB;
        ok = CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, flags, nullptr, dir.c_str(), &si, &pi);
    }
    if (!ok) return false;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}
}
