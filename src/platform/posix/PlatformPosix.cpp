// Linux (development build) and the parts macOS shares with it; macOS-only code is in mac/PlatformMac.mm.
#include "platform/Platform.h"

#include <cstdlib>
#include <fstream>
#include <sstream>

#include <fcntl.h>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#ifdef __APPLE__
#include <mach/mach.h>
#endif

#include <GLFW/glfw3.h>

namespace Platform
{
bool AcquireSingleInstance()
{
    std::error_code ec;
    fs::create_directories(ConfigDir(), ec);
    const std::string lockPath = (ConfigDir() / "kujira.lock").string();
    // Intentionally kept open for the life of the process.
    int fd = open(lockPath.c_str(), O_CREAT | O_RDWR, 0600);
    return fd >= 0 && flock(fd, LOCK_EX | LOCK_NB) == 0;
}

#ifndef __APPLE__  // macOS: PlatformMac.mm
void MakeToolWindow(GLFWwindow*)
{
    // GLFW has no portable "skip taskbar" hint; nothing to do here yet.
}
#endif

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

#ifndef __APPLE__  // macOS: PlatformMac.mm
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
        // PingFang moved out of /System/Library/Fonts in macOS 10.15; these two ship everywhere.
        { "/System/Library/Fonts/Hiragino Sans GB.ttc", 0 },
        { "/System/Library/Fonts/STHeiti Medium.ttc", 0 },
        { "/System/Library/Fonts/PingFang.ttc", 0 },
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

MappedFile MapFile(const fs::path& path)
{
    MappedFile file;
    int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return file;
    struct stat st{};
    if (fstat(fd, &st) == 0 && st.st_size > 0)
    {
        void* data = mmap(nullptr, static_cast<size_t>(st.st_size), PROT_READ, MAP_PRIVATE, fd, 0);
        if (data != MAP_FAILED)
        {
            file.data = data;
            file.size = static_cast<size_t>(st.st_size);
        }
    }
    close(fd);
    return file;
}

void UnmapFile(MappedFile& file)
{
    if (file.data) munmap(file.data, file.size);
    file = {};
}

void WaitEvents(double timeout)
{
    glfwWaitEventsTimeout(timeout);
}

#ifndef __APPLE__  // macOS: PlatformMac.mm
void SetStayOnTop(GLFWwindow*, bool, bool) {}
void UpdateStayOnTop() {}
#endif

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
