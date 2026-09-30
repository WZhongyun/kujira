// Paths and process launching, shared by Kujira and the kujira-hook forwarder
// (which must not pull in GLFW or OpenGL).
#include "platform/Platform.h"

#include <cstdlib>
#include <chrono>
#include <fcntl.h>
#include <sys/file.h>
#include <thread>
#include <sys/wait.h>
#include <unistd.h>

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

static std::string LockPath()
{
    return (ConfigDir() / "kujira.lock").string();
}

bool AcquireSingleInstance()
{
    std::error_code ec;
    fs::create_directories(ConfigDir(), ec);
    // Intentionally kept open for the life of the process.
    const int fd = open(LockPath().c_str(), O_CREAT | O_RDWR, 0600);
    if (fd < 0) return false;
    // kujira-hook probes this lock for a moment (IsPetRunning); retry briefly so a
    // probe never makes a starting pet think another one is running.
    for (int attempt = 0; attempt < 5; ++attempt)
    {
        if (flock(fd, LOCK_EX | LOCK_NB) == 0) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return false;
}

bool IsPetRunning()
{
    const int fd = open(LockPath().c_str(), O_RDWR);
    if (fd < 0) return false;
    const bool held = flock(fd, LOCK_EX | LOCK_NB) != 0;
    close(fd);  // releases the probe lock if we got it
    return held;
}

bool LaunchDetached(const fs::path& exe, const std::string& arg)
{
    const std::string path = exe.string();
    const std::string dir = exe.parent_path().string();
    const pid_t pid = fork();
    if (pid < 0) return false;
    if (pid == 0)
    {
        // Double fork + new session: the pet is re-parented to init/launchd and
        // keeps nothing of ours, in particular not the hook's stdin/stdout pipes,
        // which the agent may be waiting on to close.
        setsid();
        if (fork() != 0) _exit(0);
        const int null = open("/dev/null", O_RDWR);
        if (null >= 0)
        {
            dup2(null, 0);
            dup2(null, 1);
            dup2(null, 2);
        }
        for (int fd = 3; fd < 1024; ++fd) close(fd);
        if (chdir(dir.c_str()) != 0) { /* not fatal */ }
        if (arg.empty()) execl(path.c_str(), path.c_str(), static_cast<char*>(nullptr));
        else execl(path.c_str(), path.c_str(), arg.c_str(), static_cast<char*>(nullptr));
        _exit(127);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}
}
