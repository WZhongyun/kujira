#include "core/Config.h"

#include <algorithm>
#include <random>

#include <nlohmann/json.hpp>

#include "platform/Platform.h"

using json = nlohmann::ordered_json;

namespace
{
std::string RandomToken()
{
    static const char kChars[] = "abcdefghijklmnopqrstuvwxyz0123456789";
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<int> dist(0, static_cast<int>(sizeof(kChars) - 2));
    std::string token;
    for (int i = 0; i < 24; ++i)
    {
        token += kChars[dist(gen)];
    }
    return token;
}

template <typename T>
void Get(const json& j, const char* key, T& out)
{
    auto it = j.find(key);
    if (it != j.end() && !it->is_null())
    {
        try
        {
            out = it->get<T>();
        }
        catch (const std::exception&)
        {
            // keep default on type mismatch
        }
    }
}
}

fs::path Config::FilePath()
{
    return Platform::ConfigDir() / "config.json";
}

void Config::ApplyDefaultActions()
{
    // Defaults tuned for the DS whale-girl model; missing names are ignored at runtime.
    const std::map<std::string, StateAction> defaults = {
        { "greeting",  { "开心兴奋", "", 3.0f } },
        { "listening", { "星星眼", "", 1.5f } },
        { "thinking",  { "", "", 0.0f } },
        { "reading",   { "圆眼镜", "", 0.0f } },
        { "writing",   { "画笔", "", 0.0f } },
        { "running",   { "流汗", "", 0.0f } },
        { "attention", { "感叹号", "喷水", 0.0f } },
        { "done",      { "双手比耶", "", 4.0f } },
        { "farewell",  { "爱心眼", "", 2.0f } },
        { "sleeping",  { "闭眼口水", "", 0.0f } },
        { "idle",      { "", "", 0.0f } },
        { "poke",      { "脸红", "", 2.0f } },
    };
    for (const auto& [state, action] : defaults)
    {
        if (actions.find(state) == actions.end())
        {
            actions[state] = action;
        }
    }
}

Config Config::Load()
{
    Config c;
    auto text = FileUtil::ReadText(FilePath());
    if (text)
    {
        json j = json::parse(*text, nullptr, false);
        if (j.is_object())
        {
            Get(j, "windowX", c.windowX);
            Get(j, "windowY", c.windowY);
            Get(j, "windowHeight", c.windowHeight);
            Get(j, "topmost", c.topmost);
            Get(j, "clickThrough", c.clickThrough);
            Get(j, "lookAtMouse", c.lookAtMouse);
            Get(j, "autostart", c.autostart);
            Get(j, "activeFps", c.activeFps);
            Get(j, "idleFps", c.idleFps);
            Get(j, "sleepMinutes", c.sleepMinutes);
            Get(j, "modelDir", c.modelDir);
            Get(j, "idleMotion", c.idleMotion);
            Get(j, "port", c.port);
            Get(j, "token", c.token);
            Get(j, "claudeHookMode", c.claudeHookMode);
            auto it = j.find("actions");
            if (it != j.end() && it->is_object())
            {
                for (auto& [state, a] : it->items())
                {
                    StateAction action;
                    Get(a, "expression", action.expression);
                    Get(a, "motion", action.motion);
                    Get(a, "hold", action.holdSeconds);
                    c.actions[state] = action;
                }
            }
        }
    }
    if (c.token.empty())
    {
        c.token = RandomToken();
    }
    c.windowHeight = std::clamp(c.windowHeight, 120, 1200);
    c.activeFps = std::clamp(c.activeFps, 15, 60);
    c.idleFps = std::clamp(c.idleFps, 5, 30);
    c.sleepMinutes = std::clamp(c.sleepMinutes, 1, 120);
    if (c.port < 1024 || c.port > 65535) c.port = 38111;
    if (c.claudeHookMode != "command") c.claudeHookMode = "http";
    c.ApplyDefaultActions();
    return c;
}

bool Config::Save() const
{
    json j;
    j["windowX"] = windowX;
    j["windowY"] = windowY;
    j["windowHeight"] = windowHeight;
    j["topmost"] = topmost;
    j["clickThrough"] = clickThrough;
    j["lookAtMouse"] = lookAtMouse;
    j["autostart"] = autostart;
    j["activeFps"] = activeFps;
    j["idleFps"] = idleFps;
    j["sleepMinutes"] = sleepMinutes;
    j["modelDir"] = modelDir;
    j["idleMotion"] = idleMotion;
    j["port"] = port;
    j["token"] = token;
    j["claudeHookMode"] = claudeHookMode;
    json a = json::object();
    for (const auto& [state, action] : actions)
    {
        a[state] = { { "expression", action.expression }, { "motion", action.motion }, { "hold", action.holdSeconds } };
    }
    j["actions"] = a;
    return FileUtil::WriteTextAtomic(FilePath(), j.dump(2, ' ', false, json::error_handler_t::replace));
}
