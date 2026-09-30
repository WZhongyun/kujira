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
    // Combinations follow how the model's author uses them in VTube Studio: several
    // expressions stacked, motions layered over the idle motion (which animates the
    // hearts, paws and flowers), and the water spout only shows on the whale on her head.
    const std::map<std::string, StateAction> defaults = {
        { "greeting",  { { "鲸鱼", "开心兴奋" }, "喷水", true, 3.0f } },
        { "listening", { { "星星眼", "感叹号" }, "", false, 1.5f } },
        { "thinking",  { {}, "chuipaopao", true, 0.0f } },
        { "reading",   { { "圆眼镜" }, "", false, 0.0f } },
        { "writing",   { { "画笔", "点菜按下" }, "", false, 0.0f } },
        { "running",   { {}, "番茄酱", true, 0.0f } },
        { "attention", { { "鲸鱼", "问号" }, "喷水", true, 0.0f } },
        { "done",      { { "双手比耶", "开心兴奋", "love" }, "", false, 4.0f } },
        { "farewell",  { { "喵喵手~喵~动画", "脸红" }, "", false, 2.5f } },
        { "sleeping",  { { "闭眼口水", "吐魂" }, "", false, 0.0f } },
        { "idle",      { {}, "", false, 0.0f } },
        { "poke",      { {}, "aidale", false, 4.8f } },
        { "drag",      { { "晕晕" }, "", false, 0.8f } },
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
            Get(j, "keepOnTop", c.keepOnTop);
            Get(j, "hideForFullscreen", c.hideForFullscreen);
            Get(j, "clickThrough", c.clickThrough);
            Get(j, "lookAtMouse", c.lookAtMouse);
            Get(j, "autostart", c.autostart);
            Get(j, "activeFps", c.activeFps);
            Get(j, "idleFps", c.idleFps);
            Get(j, "sleepMinutes", c.sleepMinutes);
            Get(j, "bubbleMode", c.bubbleMode);
            Get(j, "bubbleSeconds", c.bubbleSeconds);
            Get(j, "chatMinutes", c.chatMinutes);
            Get(j, "interactionText", c.interactionText);
            Get(j, "quietMode", c.quietMode);
            Get(j, "showToolbar", c.showToolbar);
            Get(j, "modelDir", c.modelDir);
            Get(j, "idleMotion", c.idleMotion);
            Get(j, "outfit", c.outfit);
            Get(j, "actionsVersion", c.actionsVersion);
            Get(j, "port", c.port);
            Get(j, "token", c.token);
            Get(j, "claudeHookMode", c.claudeHookMode);
            Get(j, "launchWithAgent", c.launchWithAgent);
            auto it = j.find("actions");
            if (it != j.end() && it->is_object())
            {
                for (auto& [state, a] : it->items())
                {
                    StateAction action;
                    Get(a, "expressions", action.expressions);
                    std::string single;  // before version 2: one expression per state
                    Get(a, "expression", single);
                    if (action.expressions.empty() && !single.empty()) action.expressions.push_back(single);
                    Get(a, "motion", action.motion);
                    Get(a, "loop", action.loop);
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
    c.activeFps = std::clamp(c.activeFps, 5, 120);
    c.idleFps = std::clamp(c.idleFps, 5, 120);
    if (c.bubbleMode != "all" && c.bubbleMode != "important" && c.bubbleMode != "off") c.bubbleMode = "all";
    c.bubbleSeconds = std::clamp(c.bubbleSeconds, 2.0f, 30.0f);
    c.chatMinutes = std::clamp(c.chatMinutes, 0, 120);
    c.sleepMinutes = std::clamp(c.sleepMinutes, 1, 120);
    if (c.port < 1024 || c.port > 65535) c.port = 38111;
    if (c.claudeHookMode != "command") c.claudeHookMode = "app";
    if (c.actionsVersion < kActionsVersion)
    {
        // Saved actions came from older defaults that could only show one expression;
        // switch to the new combinations (they can be edited again in settings).
        c.actions.clear();
        c.actionsVersion = kActionsVersion;
    }
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
    j["keepOnTop"] = keepOnTop;
    j["hideForFullscreen"] = hideForFullscreen;
    j["clickThrough"] = clickThrough;
    j["lookAtMouse"] = lookAtMouse;
    j["autostart"] = autostart;
    j["activeFps"] = activeFps;
    j["idleFps"] = idleFps;
    j["sleepMinutes"] = sleepMinutes;
    j["bubbleMode"] = bubbleMode;
    j["bubbleSeconds"] = bubbleSeconds;
    j["chatMinutes"] = chatMinutes;
    j["interactionText"] = interactionText;
    j["quietMode"] = quietMode;
    j["showToolbar"] = showToolbar;
    j["modelDir"] = modelDir;
    j["idleMotion"] = idleMotion;
    j["outfit"] = outfit;
    j["port"] = port;
    j["token"] = token;
    j["claudeHookMode"] = claudeHookMode;
    j["launchWithAgent"] = launchWithAgent;
    json a = json::object();
    for (const auto& [state, action] : actions)
    {
        a[state] = { { "expressions", action.expressions }, { "motion", action.motion }, { "loop", action.loop },
                     { "hold", action.holdSeconds } };
    }
    j["actions"] = a;
    j["actionsVersion"] = actionsVersion;
    return FileUtil::WriteTextAtomic(FilePath(), j.dump(2, ' ', false, json::error_handler_t::replace));
}
