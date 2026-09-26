#pragma once

#include "agents/AgentAdapter.h"

// Claude Code: events arrive through its official hooks, configured in
// ~/.claude/settings.json (or $CLAUDE_CONFIG_DIR/settings.json).
class ClaudeCodeAdapter : public AgentAdapter
{
public:
    const char* Id() const override { return "claude-code"; }
    const char* DisplayName() const override { return "Claude Code"; }

    std::optional<PetEvent> Translate(const std::string& body) const override;

    fs::path ConfigFile() const override;
    HookStatus Status(const Config& config) const override;
    bool Install(const Config& config, std::string* message) const override;
    bool Uninstall(const Config& config, std::string* message) const override;
};
