#pragma once

#include <map>
#include <string>
#include <vector>

#include "events/PetEvent.h"

// Unified events -> one displayed pet state.
// Several agent sessions can run at once; the most important one wins.
class StateMachine
{
public:
    enum class State
    {
        Sleeping,
        Idle,
        Greeting,
        Farewell,
        Done,
        Listening,
        Thinking,
        Reading,
        Writing,
        Running,
        Attention,
    };

    struct StateInfo
    {
        State state;
        const char* key;    // config key, e.g. "reading"
        const char* label;  // shown in the settings window
        bool transient;     // falls back on its own after the hold time
    };

    // All states in settings order, plus the pseudo state "poke" (user click).
    static const std::vector<StateInfo>& States();
    static const char* Key(State state);
    static const char* PokeKey() { return "poke"; }

    explicit StateMachine(double now);

    void SetHoldSeconds(const std::string& key, float seconds);
    void OnEvent(const PetEvent& event, double now);
    // Shows an action key (a state key or "poke") for `seconds`, over whatever is going on.
    void ShowOverlay(const std::string& key, float seconds, double now);
    void Update(double now, int sleepMinutes);

    // Key of the action to show right now.
    const std::string& CurrentKey() const { return _currentKey; }
    State CurrentState() const { return _currentState; }
    bool IsBusy() const;
    // True once after the displayed key changed.
    bool ConsumeChanged();

    size_t SessionCount() const { return _sessions.size(); }
    double LastEventTime() const { return _lastEventTime; }
    const std::string& LastEventDetail() const { return _lastEventDetail; }

private:
    struct Session
    {
        State state = State::Idle;
        double since = 0;
        double lastEvent = 0;
    };

    static int Priority(State state);
    float Hold(State state) const;
    void SetSessionState(Session& s, State state, double now);

    std::map<std::string, Session> _sessions;
    std::map<std::string, float> _holds;
    double _lastEventTime;
    double _lastActivity;  // agent events and user interaction; drives sleep
    std::string _lastEventDetail;
    std::string _overlayKey;
    double _overlayUntil = 0;
    State _currentState = State::Idle;
    std::string _currentKey = "idle";
    bool _changed = true;
};
