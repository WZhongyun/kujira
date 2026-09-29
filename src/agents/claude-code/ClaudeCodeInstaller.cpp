// Adds/removes Kujira's hooks in Claude Code's user settings.json.
//
// Safety rules:
//  * never write a file we could not parse (comments, broken JSON, wrong shape);
//  * back the file up before every change (last 5 backups kept);
//  * only touch hook entries that point at our own endpoint, keep everything else,
//    including key order;
//  * write to a temp file and rename, so a crash never leaves a half-written file.
#include "agents/claude-code/ClaudeCodeAdapter.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

#include <nlohmann/json.hpp>

#include "platform/Platform.h"

using ojson = nlohmann::ordered_json;

namespace
{
const char* kEndpoint = "/v1/events/claude-code";

// Long-standing events only: an unknown event name in settings.json would be
// flagged by older Claude Code versions.
const char* kEvents[] = {
    "SessionStart", "SessionEnd", "UserPromptSubmit", "PreToolUse", "PostToolUse", "Notification", "Stop",
};

std::string Url(const Config& c)
{
    return "http://127.0.0.1:" + std::to_string(c.port) + kEndpoint;
}

const char* kHookArg = "--hook";
const char* kAgentId = "claude-code";

ojson DesiredEntry(const Config& c)
{
    if (c.claudeHookMode == "command")
    {
        // Compatibility mode for older Claude Code versions without exec-form hooks.
        // They ran hooks through bash (Git Bash on Windows); `|| true` keeps the
        // exit code at 0 when the pet is not running.
        std::string cmd = "curl -s -m 2 -X POST -H \"Content-Type: application/json\" -H \"X-Kujira-Token: " + c.token +
                          "\" --data-binary @- " + Url(c) + " >/dev/null 2>&1 || true";
        return { { "type", "command" }, { "command", cmd }, { "async", true }, { "timeout", 5 } };
    }
    // Claude Code spawns this executable directly (exec form, no shell), so it
    // behaves the same under bash and PowerShell. It forwards the event and
    // always exits 0: a closed pet never shows up as a hook error, which a
    // plain HTTP hook would (connection refused). HTTP hooks also don't run on
    // SessionStart. async: Claude Code never waits for it.
    return { { "type", "command" },
             { "command", FileUtil::ToUtf8(FileUtil::ExecutablePath()) },
             { "args", { kHookArg, kAgentId } },
             { "async", true },
             { "timeout", 5 } };
}

bool IsOurs(const ojson& entry)
{
    if (!entry.is_object()) return false;
    auto type = entry.value("type", "");
    if (type == "http")
    {
        // Installed by earlier versions.
        const std::string url = entry.value("url", "");
        return url.find("127.0.0.1") != std::string::npos && url.find(kEndpoint) != std::string::npos;
    }
    if (type != "command") return false;
    const std::string command = entry.value("command", "");
    if (command.find("127.0.0.1") != std::string::npos && command.find(kEndpoint) != std::string::npos) return true;
    // Kujira --hook claude-code, wherever the executable lives.
    auto args = entry.find("args");
    if (args == entry.end() || !args->is_array() || args->size() != 2) return false;
    std::string name = FileUtil::ToUtf8(FileUtil::FromUtf8(command).stem());
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return name == "kujira" && (*args)[0] == kHookArg && (*args)[1] == kAgentId;
}

// Removes our entries in place; returns how many were removed.
int RemoveOurs(ojson& root)
{
    auto hooksIt = root.find("hooks");
    if (hooksIt == root.end() || !hooksIt->is_object()) return 0;
    ojson& hooks = *hooksIt;

    int removed = 0;
    std::vector<std::string> emptyEvents;
    for (auto& [event, groups] : hooks.items())
    {
        if (!groups.is_array()) continue;
        int removedHere = 0;
        for (size_t g = 0; g < groups.size();)
        {
            ojson& group = groups[g];
            auto listIt = group.is_object() ? group.find("hooks") : group.end();
            if (!group.is_object() || listIt == group.end() || !listIt->is_array())
            {
                ++g;
                continue;
            }
            ojson& list = *listIt;
            const size_t before = list.size();
            ojson kept = ojson::array();
            for (auto& entry : list)
            {
                if (!IsOurs(entry)) kept.push_back(entry);
            }
            const size_t gone = before - kept.size();
            if (gone > 0)
            {
                removed += static_cast<int>(gone);
                removedHere += static_cast<int>(gone);
                list = kept;
            }
            if (gone > 0 && list.empty())
            {
                groups.erase(g);
            }
            else
            {
                ++g;
            }
        }
        if (removedHere > 0 && groups.empty()) emptyEvents.push_back(event);
    }
    for (const auto& e : emptyEvents) hooks.erase(e);
    if (removed > 0 && hooks.empty()) root.erase("hooks");
    return removed;
}

enum class LoadResult { Ok, Missing, Unreadable };

LoadResult LoadSettings(const fs::path& path, ojson& out, std::string* message)
{
    std::error_code ec;
    if (!fs::exists(path, ec))
    {
        out = ojson::object();
        return LoadResult::Missing;
    }
    auto text = FileUtil::ReadText(path);
    if (!text)
    {
        if (message) *message = "无法读取 " + FileUtil::ToUtf8(path);
        return LoadResult::Unreadable;
    }
    std::string trimmed = *text;
    trimmed.erase(0, trimmed.find_first_not_of(" \t\r\n\xEF\xBB\xBF"));
    if (trimmed.empty())
    {
        out = ojson::object();
        return LoadResult::Ok;
    }
    out = ojson::parse(*text, nullptr, false);
    if (out.is_discarded())
    {
        ojson withComments = ojson::parse(*text, nullptr, false, true);
        if (message)
        {
            *message = withComments.is_discarded()
                           ? "settings.json 不是有效的 JSON，为安全起见没有修改。"
                           : "settings.json 含有注释，改写会丢失注释，为安全起见没有修改。请手动添加 hook。";
        }
        return LoadResult::Unreadable;
    }
    if (!out.is_object() || (out.contains("hooks") && !out["hooks"].is_object()))
    {
        if (message) *message = "settings.json 的结构不是预期的格式，为安全起见没有修改。";
        return LoadResult::Unreadable;
    }
    return LoadResult::Ok;
}

std::string Timestamp()
{
    std::time_t t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    std::ostringstream ss;
    ss << std::put_time(&tm, "%Y%m%d-%H%M%S");
    return ss.str();
}

bool Backup(const fs::path& path, std::string* message)
{
    std::error_code ec;
    if (!fs::exists(path, ec)) return true;
    fs::path backup = path;
    backup += ".kujira-backup-" + Timestamp();
    fs::copy_file(path, backup, fs::copy_options::overwrite_existing, ec);
    if (ec)
    {
        if (message) *message = "备份失败，没有修改：" + ec.message();
        return false;
    }
    // Keep the newest five backups.
    std::vector<fs::path> backups;
    const std::string prefix = path.filename().string() + ".kujira-backup-";
    for (const auto& entry : fs::directory_iterator(path.parent_path(), ec))
    {
        if (entry.path().filename().string().rfind(prefix, 0) == 0) backups.push_back(entry.path());
    }
    std::sort(backups.begin(), backups.end());
    while (backups.size() > 5)
    {
        fs::remove(backups.front(), ec);
        backups.erase(backups.begin());
    }
    return true;
}

bool Write(const fs::path& path, const ojson& root, std::string* message)
{
    std::string error;
    std::string text = root.dump(2, ' ', false, ojson::error_handler_t::replace) + "\n";
    if (!FileUtil::WriteTextAtomic(path, text, &error))
    {
        if (message) *message = "写入失败：" + error;
        return false;
    }
    return true;
}
}

fs::path ClaudeCodeAdapter::ConfigFile() const
{
    std::string dir = Platform::GetEnv("CLAUDE_CONFIG_DIR");
    fs::path base = dir.empty() ? Platform::HomeDir() / ".claude" : FileUtil::FromUtf8(dir);
    return base / "settings.json";
}

HookStatus ClaudeCodeAdapter::Status(const Config& config) const
{
    HookStatus status;
    ojson root;
    std::string message;
    LoadResult r = LoadSettings(ConfigFile(), root, &message);
    if (r == LoadResult::Unreadable)
    {
        status.state = HookStatus::State::Unreadable;
        status.message = message;
        return status;
    }

    const ojson desired = DesiredEntry(config);
    int ours = 0, exact = 0, eventsCovered = 0;
    if (root.contains("hooks"))
    {
        for (const char* event : kEvents)
        {
            bool covered = false;
            auto it = root["hooks"].find(event);
            if (it == root["hooks"].end() || !it->is_array()) continue;
            for (const auto& group : *it)
            {
                if (!group.is_object() || !group.contains("hooks") || !group["hooks"].is_array()) continue;
                for (const auto& entry : group["hooks"])
                {
                    if (!IsOurs(entry)) continue;
                    ++ours;
                    if (entry == desired) { ++exact; covered = true; }
                }
            }
            if (covered) ++eventsCovered;
        }
    }

    if (ours == 0)
    {
        status.state = HookStatus::State::NotInstalled;
    }
    else if (eventsCovered == static_cast<int>(std::size(kEvents)) && exact == ours)
    {
        status.state = HookStatus::State::Installed;
    }
    else
    {
        status.state = HookStatus::State::Outdated;
        status.message = "已安装的 hook 与当前接入方式、端口或程序位置不一致，重新安装即可更新。";
    }
    if (root.value("disableAllHooks", false))
    {
        status.message = "注意：settings.json 里设置了 disableAllHooks，hook 不会运行。";
    }
    return status;
}

bool ClaudeCodeAdapter::Install(const Config& config, std::string* message) const
{
    const fs::path path = ConfigFile();
    ojson root;
    if (LoadSettings(path, root, message) == LoadResult::Unreadable) return false;
    if (!Backup(path, message)) return false;

    RemoveOurs(root);
    if (!root.contains("hooks")) root["hooks"] = ojson::object();
    for (const char* event : kEvents)
    {
        ojson& groups = root["hooks"][event];
        if (!groups.is_array()) groups = ojson::array();
        groups.push_back({ { "hooks", ojson::array({ DesiredEntry(config) }) } });
    }
    if (!Write(path, root, message)) return false;

    if (Status(config).state != HookStatus::State::Installed)
    {
        if (message) *message = "写入后校验失败，可以从备份恢复：" + FileUtil::ToUtf8(path) + ".kujira-backup-*";
        return false;
    }
    if (message) *message = "已安装。备份在 settings.json 同目录，文件名以 .kujira-backup- 开头。";
    return true;
}

bool ClaudeCodeAdapter::Uninstall(const Config& config, std::string* message) const
{
    const fs::path path = ConfigFile();
    ojson root;
    LoadResult r = LoadSettings(path, root, message);
    if (r == LoadResult::Unreadable) return false;
    if (r == LoadResult::Missing || RemoveOurs(root) == 0)
    {
        if (message) *message = "没有找到鲸鱼娘的 hook，无需卸载。";
        return true;
    }
    // RemoveOurs already modified `root`; back up the original before writing.
    if (!Backup(path, message)) return false;
    if (!Write(path, root, message)) return false;
    if (message) *message = "已卸载，只删除了鲸鱼娘自己的条目。";
    return true;
}
