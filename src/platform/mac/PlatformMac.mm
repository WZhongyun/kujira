// macOS-only parts of the platform layer: Dock visibility, window level and
// autostart. Everything shared with Linux stays in PlatformPosix.cpp.
#include "platform/Platform.h"

#import <Cocoa/Cocoa.h>

#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

#include <mach-o/dyld.h>
#include <climits>

namespace
{
NSWindow* g_pet = nil;
bool g_hideForFullscreen = true;

void ApplyCollectionBehavior()
{
    if (!g_pet) return;
    // On every desktop (Space), not part of Mission Control or Cmd-` cycling.
    // Fullscreen apps live in their own Space; without FullScreenAuxiliary she
    // simply is not shown there.
    NSWindowCollectionBehavior behavior = NSWindowCollectionBehaviorCanJoinAllSpaces |
                                          NSWindowCollectionBehaviorStationary |
                                          NSWindowCollectionBehaviorIgnoresCycle;
    if (!g_hideForFullscreen) behavior |= NSWindowCollectionBehaviorFullScreenAuxiliary;
    [g_pet setCollectionBehavior:behavior];
}

fs::path LaunchAgentFile()
{
    return Platform::HomeDir() / "Library" / "LaunchAgents" / "com.kujira.pet.plist";
}

std::string XmlEscape(const std::string& s)
{
    std::string out;
    for (char c : s)
    {
        switch (c)
        {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        default: out += c;
        }
    }
    return out;
}
}

namespace Platform
{
void MakeToolWindow(GLFWwindow* window)
{
    // Accessory app: no Dock icon and no menu bar, like a menu bar utility.
    [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
    g_pet = glfwGetCocoaWindow(window);
    ApplyCollectionBehavior();
}

void SetStayOnTop(GLFWwindow* window, bool keepOnTop, bool hideForFullscreen)
{
    g_pet = glfwGetCocoaWindow(window);
    g_hideForFullscreen = hideForFullscreen;
    ApplyCollectionBehavior();
    // GLFW_FLOATING already puts her above normal windows. Status level also
    // keeps her above the Dock, the macOS counterpart of the Windows taskbar.
    if (keepOnTop) [g_pet setLevel:NSStatusWindowLevel];
}

void UpdateStayOnTop() {}

bool AutostartSupported() { return true; }

bool IsAutostartEnabled()
{
    std::error_code ec;
    return fs::exists(LaunchAgentFile(), ec);
}

bool SetAutostart(bool enable)
{
    std::error_code ec;
    if (!enable)
    {
        fs::remove(LaunchAgentFile(), ec);
        return !ec;
    }
    char buf[PATH_MAX * 2];
    uint32_t size = sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) != 0) return false;
    const std::string exe = fs::weakly_canonical(fs::path(buf)).string();
    const std::string plist =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
        "<plist version=\"1.0\">\n<dict>\n"
        "  <key>Label</key><string>com.kujira.pet</string>\n"
        "  <key>ProgramArguments</key><array><string>" + XmlEscape(exe) + "</string></array>\n"
        "  <key>RunAtLoad</key><true/>\n"
        "  <key>ProcessType</key><string>Interactive</string>\n"
        "</dict>\n</plist>\n";
    fs::create_directories(LaunchAgentFile().parent_path(), ec);
    return FileUtil::WriteTextAtomic(LaunchAgentFile(), plist);
}
}
