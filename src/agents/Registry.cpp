#include "agents/AgentAdapter.h"

#include "agents/claude-code/ClaudeCodeAdapter.h"

namespace AgentRegistry
{
const std::vector<std::unique_ptr<AgentAdapter>>& All()
{
    // New agents: add a folder under agents/ and one line here.
    static const std::vector<std::unique_ptr<AgentAdapter>> adapters = [] {
        std::vector<std::unique_ptr<AgentAdapter>> v;
        v.push_back(std::make_unique<ClaudeCodeAdapter>());
        return v;
    }();
    return adapters;
}

const AgentAdapter* Find(const std::string& id)
{
    for (const auto& a : All())
    {
        if (id == a->Id()) return a.get();
    }
    return nullptr;
}
}
