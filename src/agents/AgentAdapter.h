#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "core/Config.h"
#include "core/FileUtil.h"
#include "events/PetEvent.h"

struct HookStatus
{
    enum class State
    {
        NotInstalled,
        Installed,
        Outdated,     // installed, but pointing at another port / token / mode
        Unreadable,   // the agent's config file exists but is not valid JSON
    };
    State state = State::NotInstalled;
    std::string message;
};

// One per supported agent. Everything agent specific stays behind this interface:
// how its native events look, and how to add/remove our hook in its config.
class AgentAdapter
{
public:
    virtual ~AgentAdapter() = default;

    virtual const char* Id() const = 0;           // URL segment: /v1/events/<id>
    virtual const char* DisplayName() const = 0;

    // Native event payload -> unified event. nullopt = ignore this event.
    virtual std::optional<PetEvent> Translate(const std::string& body) const = 0;

    virtual fs::path ConfigFile() const = 0;
    virtual HookStatus Status(const Config& config) const = 0;
    virtual bool Install(const Config& config, std::string* message) const = 0;
    virtual bool Uninstall(const Config& config, std::string* message) const = 0;
};

namespace AgentRegistry
{
const std::vector<std::unique_ptr<AgentAdapter>>& All();
const AgentAdapter* Find(const std::string& id);
}
