// Paths and process launching, shared by Kujira and the kujira-hook forwarder
// (which must not pull in GLFW or OpenGL).
#include "platform/Platform.h"

#include <cstdlib>
#include <fcntl.h>
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
