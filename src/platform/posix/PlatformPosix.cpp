// Linux (development build) and a provisional macOS path until the Metal port (M3).
#include "platform/Platform.h"

#include <cstdlib>
#include <fstream>
#include <sstream>

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

#ifdef __APPLE__
#include <mach/mach.h>
#endif

#include <GLFW/glfw3.h>

namespace Platform
{
fs::path HomeDir()
{
    const char* home = std::getenv("HOME");
    return home ? fs::path(home) : fs::current_path();
}

fs::path ConfigDir()
{
#ifdef __APPLE__
    return HomeDir() / "Library" / "Application Support" / "Kujira";
#else
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    fs::path base = (xdg && *xdg) ? fs::path(xdg) : HomeDir() / ".config";
    return base / "kujira";
#endif
}

bool AcquireSingleInstance()
{
    std::error_code ec;
    fs::create_directories(ConfigDir(), ec);
    const std::string lockPath = (ConfigDir() / "kujira.lock").string();
    // Intentionally kept open for the life of the process.
    int fd = open(lockPath.c_str(), O_CREAT | O_RDWR, 0600);
    return fd >= 0 && flock(fd, LOCK_EX | LOCK_NB) == 0;
}

void MakeToolWindow(GLFWwindow*)
{
    // GLFW has no portable "skip taskbar" hint; nothing to do here yet.
}

uint64_t ProcessMemoryBytes()
{
#ifdef __APPLE__
    task_vm_info_data_t info;
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&info), &count) == KERN_SUCCESS)
    {
        return info.phys_footprint;
    }
    return 0;
#else
    std::ifstream in("/proc/self/status");
    std::string line;
    while (std::getline(in, line))
    {
        if (line.rfind("VmRSS:", 0) == 0)
        {
            std::istringstream ss(line.substr(6));
            uint64_t kb = 0;
            ss >> kb;
            return kb * 1024;
        }
    }
    return 0;
#endif
}

#ifdef __APPLE__
bool AutostartSupported() { return false; }
bool IsAutostartEnabled() { return false; }
bool SetAutostart(bool) { return false; }
#else
static fs::path AutostartFile()
{
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    fs::path base = (xdg && *xdg) ? fs::path(xdg) : HomeDir() / ".config";
    return base / "autostart" / "kujira.desktop";
}

bool AutostartSupported() { return true; }

bool IsAutostartEnabled()
{
    return fs::exists(AutostartFile());
}

bool SetAutostart(bool enable)
{
    std::error_code ec;
    if (!enable)
    {
        fs::remove(AutostartFile(), ec);
        return !ec;
    }
    std::string exe = (FileUtil::ExecutableDir() / "Kujira").string();
    std::string text = "[Desktop Entry]\nType=Application\nName=Kujira\nExec=\"" + exe + "\"\nX-GNOME-Autostart-enabled=true\n";
    return FileUtil::WriteTextAtomic(AutostartFile(), text);
}
#endif

std::vector<std::pair<fs::path, int>> CjkFontCandidates()
{
#ifdef __APPLE__
    return {
        { "/System/Library/Fonts/PingFang.ttc", 0 },
        { "/System/Library/Fonts/STHeiti Medium.ttc", 0 },
        { "/System/Library/Fonts/Hiragino Sans GB.ttc", 0 },
    };
#else
    return {
        { "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc", 2 },  // SC face
        { "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc", 2 },
        { "/usr/share/fonts/google-noto-cjk/NotoSansCJK-Regular.ttc", 2 },
        { "/usr/share/fonts/truetype/wqy/wqy-microhei.ttc", 0 },
    };
#endif
}

void OpenFolder(const fs::path& folder)
{
#ifdef __APPLE__
    std::string cmd = "open \"" + folder.string() + "\" &";
#else
    std::string cmd = "xdg-open \"" + folder.string() + "\" >/dev/null 2>&1 &";
#endif
    int r = std::system(cmd.c_str());
    (void)r;
}

std::string GetEnv(const char* name)
{
    const char* v = std::getenv(name);
    return v ? v : "";
}
}
