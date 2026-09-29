#include "core/FileUtil.h"

#include <fstream>
#include <iterator>
#include <sstream>

#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#include <climits>
#else
#include <unistd.h>
#include <climits>
#endif

namespace FileUtil
{
fs::path FromUtf8(const std::string& utf8)
{
#if defined(__cpp_char8_t)
    return fs::path(std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
#else
    return fs::u8path(utf8);
#endif
}

std::string ToUtf8(const fs::path& path)
{
#if defined(__cpp_char8_t)
    const std::u8string s = path.u8string();
    return std::string(reinterpret_cast<const char*>(s.data()), s.size());
#else
    return path.u8string();
#endif
}

std::optional<std::string> ReadText(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        return std::nullopt;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool WriteTextAtomic(const fs::path& path, const std::string& text, std::string* error)
{
    std::error_code ec;
    if (path.has_parent_path())
    {
        fs::create_directories(path.parent_path(), ec);
    }
    fs::path tmp = path;
    tmp += ".kujira-tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out)
        {
            if (error) *error = "cannot open " + ToUtf8(tmp);
            return false;
        }
        out.write(text.data(), static_cast<std::streamsize>(text.size()));
        out.flush();
        if (!out)
        {
            if (error) *error = "write failed: " + ToUtf8(tmp);
            return false;
        }
    }
    fs::rename(tmp, path, ec);
    if (ec)
    {
        fs::remove(tmp, ec);
        if (error) *error = "rename failed: " + ec.message();
        return false;
    }
    return true;
}

fs::path ExecutablePath()
{
#ifdef _WIN32
    wchar_t buf[MAX_PATH * 4];
    DWORD n = GetModuleFileNameW(nullptr, buf, static_cast<DWORD>(std::size(buf)));
    return fs::path(std::wstring(buf, n));
#elif defined(__APPLE__)
    char buf[PATH_MAX * 2];
    uint32_t size = sizeof(buf);
    if (_NSGetExecutablePath(buf, &size) == 0)
    {
        return fs::weakly_canonical(fs::path(buf));
    }
    return {};
#else
    char buf[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (n > 0)
    {
        return fs::path(std::string(buf, static_cast<size_t>(n)));
    }
    return {};
#endif
}

fs::path ExecutableDir()
{
    fs::path exe = ExecutablePath();
    return exe.empty() ? fs::current_path() : exe.parent_path();
}
}
