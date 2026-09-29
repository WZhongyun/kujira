#pragma once

// Hook helper mode: the agent runs `Kujira --hook <agent>` for each event and
// pipes the event JSON to stdin. We forward it to the running pet over
// localhost and always exit 0, so a closed pet never shows up as a hook error.
namespace HookForward
{
int Run(const char* agent);
}
