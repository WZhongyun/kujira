#pragma once

#include <string>

// Agent-neutral event. Every adapter translates its native payload into this
// before anything reaches the state machine.
struct PetEvent
{
    enum class Kind
    {
        SessionStart,
        SessionEnd,
        PromptSubmit,   // user sent an instruction
        ToolRead,       // reading / searching files or the web
        ToolWrite,      // editing files
        ToolRun,        // running a shell command
        ToolOther,      // any other tool: treated as "working"
        ToolDone,       // a tool finished
        Attention,      // waiting for permission or input
        Stop,           // the agent finished its reply
    };

    Kind kind = Kind::ToolOther;
    std::string agent;      // adapter id, e.g. "claude-code"
    std::string sessionId;  // agent's own session id
    std::string detail;     // native event / tool name, for display
    std::string text;       // short human-readable target or message for the speech bubble:
                            // a file name, a command description, a notification, the final reply
};

const char* PetEventKindName(PetEvent::Kind kind);
