#include "core/Dialogue.h"

#include <nlohmann/json.hpp>

#include "platform/Platform.h"

using json = nlohmann::ordered_json;

const std::vector<Dialogue::Category>& Dialogue::Categories()
{
    static const std::vector<Category> categories = {
        { "greeting", "打招呼", "启动时",
          { "你好呀，今天也一起加油吧～", "我来啦！", "嗨，又见面了" } },
        { "morning", "早上", "6 点到 10 点的闲聊",
          { "早上好！吃早饭了吗？", "新的一天开始啦", "早安，今天也要元气满满哦" } },
        { "noon", "中午", "11 点到 13 点的闲聊",
          { "到饭点了，你吃饭了吗？", "中午记得休息一下哦", "午饭吃什么好呢～" } },
        { "afternoon", "下午", "14 点到 17 点的闲聊",
          { "下午容易犯困，喝口水吧", "要不要起来活动一下？", "来杯下午茶怎么样？" } },
        { "evening", "晚上", "18 点到 22 点的闲聊",
          { "晚饭吃了吗？", "今天辛苦啦", "晚上也要注意眼睛哦" } },
        { "night", "深夜", "23 点到 5 点的闲聊",
          { "夜深了，早些休息吧", "还不睡吗？熬夜对身体不好哦", "我都要困了……" } },
        { "idle", "随时", "任何时段的闲聊",
          { "有什么需要帮忙的吗？", "好无聊啊～", "喝口水吧", "坐久了要起来走走哦", "你在想什么呢？" } },
        { "hover", "鼠标移到她身上", "停留一会儿后",
          { "干嘛呢，把手拿开～", "嗯？找我有事吗？", "不要一直盯着我看啦" } },
        { "click", "点她", "单击时",
          { "别乱点！", "哎呀，好痛", "再点我就生气了哦", "干嘛啦～" } },
        { "drag", "拖动她", "放下时",
          { "要带我去哪里呀？", "晕乎乎的……", "这里风景不错" } },
        { "sleep", "犯困", "进入睡眠时",
          { "好困……我先睡一会儿", "Zzz……" } },
        { "wake", "睡醒", "从睡眠中醒来时",
          { "唔……我睡着了吗？", "啊，我醒着呢！" } },
        { "agentStart", "Agent 开始", "Agent 会话开始时",
          { "开始工作啦！", "今天要做什么呢？" } },
        { "reading", "Agent 在看", "读文件或搜索时，{target} 是文件名",
          { "我看看 {target}", "在读 {target}" } },
        { "writing", "Agent 在改", "修改文件时，{target} 是文件名",
          { "在改 {target}", "正在修改 {target}" } },
        { "running", "Agent 在运行", "运行命令时，{target} 是命令说明",
          { "正在运行：{target}", "{target}" } },
        { "attention", "Agent 等你处理", "没有具体提示时",
          { "需要你来看一下！", "快来看看，等你确认呢" } },
        { "done", "Agent 完成", "拿不到回复内容时",
          { "搞定啦！", "完成了，来看看吧" } },
        { "farewell", "Agent 结束", "Agent 会话结束时",
          { "辛苦啦，下次见～", "拜拜～" } },
    };
    return categories;
}

const Dialogue::Category* Dialogue::FindCategory(const std::string& key)
{
    for (const auto& c : Categories())
    {
        if (key == c.key) return &c;
    }
    return nullptr;
}

fs::path Dialogue::FilePath()
{
    return Platform::ConfigDir() / "dialogue.json";
}

Dialogue Dialogue::Load()
{
    Dialogue d;
    for (const auto& c : Categories()) d._lines[c.key] = c.defaults;

    auto text = FileUtil::ReadText(FilePath());
    if (!text) return d;
    json j = json::parse(*text, nullptr, false);
    if (!j.is_object()) return d;
    for (const auto& c : Categories())
    {
        auto it = j.find(c.key);
        if (it == j.end() || !it->is_array()) continue;
        std::vector<std::string> lines;
        for (const auto& line : *it)
        {
            if (line.is_string()) lines.push_back(line.get<std::string>());
        }
        d._lines[c.key] = std::move(lines);
    }
    return d;
}

bool Dialogue::Save() const
{
    json j = json::object();
    for (const auto& c : Categories())
    {
        auto it = _lines.find(c.key);
        j[c.key] = it != _lines.end() ? it->second : c.defaults;
    }
    std::error_code ec;
    fs::create_directories(FilePath().parent_path(), ec);
    return FileUtil::WriteTextAtomic(FilePath(), j.dump(2));
}

const std::vector<std::string>& Dialogue::Lines(const std::string& key) const
{
    static const std::vector<std::string> kEmpty;
    auto it = _lines.find(key);
    return it != _lines.end() ? it->second : kEmpty;
}

void Dialogue::SetLines(const std::string& key, std::vector<std::string> lines)
{
    _lines[key] = std::move(lines);
}

void Dialogue::ResetToDefault(const std::string& key)
{
    if (const Category* c = FindCategory(key)) _lines[key] = c->defaults;
}

std::string Dialogue::Pick(const std::string& key, const std::string& target)
{
    const auto& lines = Lines(key);
    if (lines.empty()) return {};
    std::string line;
    if (lines.size() == 1)
    {
        line = lines.front();
    }
    else
    {
        std::uniform_int_distribution<size_t> dist(0, lines.size() - 1);
        for (int attempt = 0; attempt < 4; ++attempt)
        {
            line = lines[dist(_rng)];
            if (line != _last[key]) break;
        }
    }
    _last[key] = line;
    const std::string placeholder = "{target}";
    for (size_t pos = line.find(placeholder); pos != std::string::npos; pos = line.find(placeholder, pos + target.size()))
    {
        line.replace(pos, placeholder.size(), target);
    }
    return line;
}

const char* Dialogue::TimeOfDayKey(int hour)
{
    if (hour >= 6 && hour < 11) return "morning";
    if (hour >= 11 && hour < 14) return "noon";
    if (hour >= 14 && hour < 18) return "afternoon";
    if (hour >= 18 && hour < 23) return "evening";
    return "night";
}
