#include "agents/claude-code/ClaudeCodeAdapter.h"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <set>
#include <sstream>

#include <nlohmann/json.hpp>

#include "core/FileUtil.h"

using json = nlohmann::json;

namespace
{
std::string Str(const json& j, const char* key)
{
    auto it = j.find(key);
    return (it != j.end() && it->is_string()) ? it->get<std::string>() : std::string();
}

std::string FileName(const std::string& path)
{
    const size_t slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

// Cuts a UTF-8 string to at most `maxBytes` without splitting a character.
std::string Truncate(std::string s, size_t maxBytes)
{
    if (s.size() <= maxBytes) return s;
    size_t cut = maxBytes;
    while (cut > 0 && (static_cast<unsigned char>(s[cut]) & 0xC0) == 0x80) --cut;
    s.resize(cut);
    return s + "…";
}

std::string OneLine(std::string s)
{
    for (char& c : s)
    {
        if (c == '\n' || c == '\r' || c == '\t') c = ' ';
    }
    return s;
}

// What the tool is working on, for "reading App.cpp" / "running: npm test".
std::string ToolTarget(const std::string& tool, const json& input)
{
    if (!input.is_object()) return {};
    for (const char* key : { "file_path", "notebook_path", "path" })
    {
        std::string v = Str(input, key);
        if (!v.empty()) return FileName(v);
    }
    // Bash usually carries a short description written by the agent; prefer it over the raw command.
    std::string description = Str(input, "description");
    if (!description.empty()) return Truncate(OneLine(description), 80);
    for (const char* key : { "command", "pattern", "query", "url" })
    {
        std::string v = Str(input, key);
        if (!v.empty()) return Truncate(OneLine(v), 60);
    }
    return tool;
}

// Text of the last assistant message in a Claude Code transcript (JSON lines).
// Only the tail of the file is read; transcripts can grow large.
std::string LastAssistantText(const std::string& transcriptPath)
{
    std::ifstream in(FileUtil::FromUtf8(transcriptPath), std::ios::binary);
    if (!in) return {};
    in.seekg(0, std::ios::end);
    const std::streamoff size = in.tellg();
    const std::streamoff start = std::max<std::streamoff>(0, size - 512 * 1024);
    in.seekg(start);
    std::string tail((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    std::string result;
    std::istringstream lines(tail);
    std::string line;
    while (std::getline(lines, line))
    {
        if (line.find("\"assistant\"") == std::string::npos) continue;
        json j = json::parse(line, nullptr, false);
        if (!j.is_object() || Str(j, "type") != "assistant") continue;
        auto message = j.find("message");
        if (message == j.end() || !message->is_object()) continue;
        auto content = message->find("content");
        if (content == message->end()) continue;
        std::string text;
        if (content->is_string())
        {
            text = content->get<std::string>();
        }
        else if (content->is_array())
        {
            for (const auto& part : *content)
            {
                if (part.is_object() && Str(part, "type") == "text") text += Str(part, "text");
            }
        }
        if (!text.empty()) result = text;
    }
    return result;
}

PetEvent::Kind ToolKind(const std::string& tool)
{
    static const std::set<std::string> read = { "Read", "Grep", "Glob", "LS", "NotebookRead", "WebFetch", "WebSearch" };
    static const std::set<std::string> write = { "Edit", "Write", "MultiEdit", "NotebookEdit" };
    static const std::set<std::string> run = { "Bash", "PowerShell", "BashOutput" };
    if (read.count(tool)) return PetEvent::Kind::ToolRead;
    if (write.count(tool)) return PetEvent::Kind::ToolWrite;
    if (run.count(tool)) return PetEvent::Kind::ToolRun;
    return PetEvent::Kind::ToolOther;
}
}

std::optional<PetEvent> ClaudeCodeAdapter::Translate(const std::string& body) const
{
    json j = json::parse(body, nullptr, false);
    if (!j.is_object())
    {
        return std::nullopt;
    }

    PetEvent e;
    e.agent = Id();
    e.sessionId = Str(j, "session_id");
    const std::string name = Str(j, "hook_event_name");
    const std::string tool = Str(j, "tool_name");
    e.detail = tool.empty() ? name : name + " · " + tool;

    if (name == "SessionStart") e.kind = PetEvent::Kind::SessionStart;
    else if (name == "SessionEnd") e.kind = PetEvent::Kind::SessionEnd;
    else if (name == "UserPromptSubmit") e.kind = PetEvent::Kind::PromptSubmit;
    else if (name == "PreToolUse") e.kind = ToolKind(tool);
    else if (name == "PostToolUse" || name == "PostToolUseFailure") e.kind = PetEvent::Kind::ToolDone;
    else if (name == "PermissionRequest") e.kind = PetEvent::Kind::Attention;
    else if (name == "Notification")
    {
        // Older versions send no notification_type; treat every notification as "look at me".
        const std::string type = Str(j, "notification_type");
        static const std::set<std::string> attention = {
            "", "permission_prompt", "idle_prompt", "elicitation_dialog", "elicitation_url_dialog", "agent_needs_input" };
        if (!attention.count(type)) return std::nullopt;
        e.kind = PetEvent::Kind::Attention;
    }
    else if (name == "Stop" || name == "StopFailure") e.kind = PetEvent::Kind::Stop;
    else return std::nullopt;

    // Text for the speech bubble.
    switch (e.kind)
    {
    case PetEvent::Kind::ToolRead:
    case PetEvent::Kind::ToolWrite:
    case PetEvent::Kind::ToolRun:
        e.text = ToolTarget(tool, j.value("tool_input", json::object()));
        break;
    case PetEvent::Kind::Attention:
        e.text = Str(j, "message");
        if (e.text.empty() && !tool.empty()) e.text = "需要你确认：" + tool;
        e.text = Truncate(e.text, 300);
        break;
    case PetEvent::Kind::Stop:
        if (name == "Stop")
        {
            // Newer versions include the reply; otherwise read it from the transcript.
            e.text = Str(j, "last_assistant_message");
            if (e.text.empty()) e.text = LastAssistantText(Str(j, "transcript_path"));
            e.text = Truncate(e.text, 600);
        }
        break;
    default:
        break;
    }

    return e;
}
