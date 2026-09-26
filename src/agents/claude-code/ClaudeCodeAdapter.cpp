#include "agents/claude-code/ClaudeCodeAdapter.h"

#include <set>

#include <nlohmann/json.hpp>

using json = nlohmann::json;

namespace
{
std::string Str(const json& j, const char* key)
{
    auto it = j.find(key);
    return (it != j.end() && it->is_string()) ? it->get<std::string>() : std::string();
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

    return e;
}
