#include "core/StateMachine.h"

#include <algorithm>

namespace
{
// A working state with no event for this long is assumed stale (a missed Stop).
constexpr double kStaleWorkingSeconds = 10 * 60;
constexpr double kStaleAttentionSeconds = 60 * 60;
// Idle sessions are forgotten after this long.
constexpr double kForgetSeconds = 2 * 60 * 60;
}

const char* PetEventKindName(PetEvent::Kind kind)
{
    switch (kind)
    {
    case PetEvent::Kind::SessionStart: return "SessionStart";
    case PetEvent::Kind::SessionEnd: return "SessionEnd";
    case PetEvent::Kind::PromptSubmit: return "PromptSubmit";
    case PetEvent::Kind::ToolRead: return "ToolRead";
    case PetEvent::Kind::ToolWrite: return "ToolWrite";
    case PetEvent::Kind::ToolRun: return "ToolRun";
    case PetEvent::Kind::ToolOther: return "ToolOther";
    case PetEvent::Kind::ToolDone: return "ToolDone";
    case PetEvent::Kind::Attention: return "Attention";
    case PetEvent::Kind::Stop: return "Stop";
    }
    return "?";
}

const std::vector<StateMachine::StateInfo>& StateMachine::States()
{
    static const std::vector<StateInfo> states = {
        { State::Greeting,  "greeting",  "会话开始",   true },
        { State::Listening, "listening", "收到指令",   true },
        { State::Thinking,  "thinking",  "思考中",     false },
        { State::Reading,   "reading",   "阅读文件",   false },
        { State::Writing,   "writing",   "修改文件",   false },
        { State::Running,   "running",   "执行命令",   false },
        { State::Attention, "attention", "需要关注",   false },
        { State::Done,      "done",      "完成",       true },
        { State::Farewell,  "farewell",  "会话结束",   true },
        { State::Idle,      "idle",      "空闲",       false },
        { State::Sleeping,  "sleeping",  "睡眠",       false },
    };
    return states;
}

const char* StateMachine::Key(State state)
{
    for (const auto& info : States())
    {
        if (info.state == state) return info.key;
    }
    return "idle";
}

StateMachine::StateMachine(double now)
    : _lastEventTime(now)
    , _lastActivity(now)
{
}

int StateMachine::Priority(State state)
{
    switch (state)
    {
    case State::Attention: return 5;
    case State::Running:
    case State::Writing:
    case State::Reading: return 4;
    case State::Thinking:
    case State::Listening: return 3;
    case State::Done:
    case State::Greeting:
    case State::Farewell: return 2;
    case State::Idle: return 1;
    case State::Sleeping: return 0;
    }
    return 0;
}

void StateMachine::SetHoldSeconds(const std::string& key, float seconds)
{
    _holds[key] = seconds;
}

float StateMachine::Hold(State state) const
{
    auto it = _holds.find(Key(state));
    float hold = it != _holds.end() ? it->second : 0.0f;
    return std::max(hold, 0.5f);
}

void StateMachine::SetSessionState(Session& s, State state, double now)
{
    s.state = state;
    s.since = now;
    s.lastEvent = now;
}

void StateMachine::OnEvent(const PetEvent& event, double now)
{
    _lastEventTime = now;
    _lastActivity = now;
    _lastEventDetail = event.detail.empty() ? PetEventKindName(event.kind) : event.detail;

    Session& s = _sessions[event.agent + ":" + event.sessionId];
    switch (event.kind)
    {
    case PetEvent::Kind::SessionStart: SetSessionState(s, State::Greeting, now); break;
    case PetEvent::Kind::SessionEnd:   SetSessionState(s, State::Farewell, now); break;
    case PetEvent::Kind::PromptSubmit: SetSessionState(s, State::Listening, now); break;
    case PetEvent::Kind::ToolRead:     SetSessionState(s, State::Reading, now); break;
    case PetEvent::Kind::ToolWrite:    SetSessionState(s, State::Writing, now); break;
    case PetEvent::Kind::ToolRun:      SetSessionState(s, State::Running, now); break;
    case PetEvent::Kind::ToolOther:    SetSessionState(s, State::Thinking, now); break;
    case PetEvent::Kind::ToolDone:     SetSessionState(s, State::Thinking, now); break;
    case PetEvent::Kind::Attention:    SetSessionState(s, State::Attention, now); break;
    case PetEvent::Kind::Stop:         SetSessionState(s, State::Done, now); break;
    }
    // Same state again while it is on screen: the key won't change, so flag it for
    // the caller to replay a one-shot animation that has already ended.
    if (_currentKey == Key(s.state) && now >= _overlayUntil) _retriggered = true;
}

void StateMachine::ShowOverlay(const std::string& key, float seconds, double now)
{
    _lastActivity = now;
    _overlayKey = key;
    _overlayUntil = now + std::max(seconds, 0.5f);
}

bool StateMachine::IsBusy() const
{
    return _currentState != State::Idle && _currentState != State::Sleeping;
}

void StateMachine::Update(double now, int sleepMinutes)
{
    for (auto it = _sessions.begin(); it != _sessions.end();)
    {
        Session& s = it->second;
        const double age = now - s.since;
        const double quiet = now - s.lastEvent;
        bool erase = false;
        switch (s.state)
        {
        case State::Greeting:
        case State::Done:
            if (age >= Hold(s.state)) { s.state = State::Idle; s.since = now; }
            break;
        case State::Listening:
            if (age >= Hold(s.state)) { s.state = State::Thinking; s.since = now; }
            break;
        case State::Farewell:
            erase = age >= Hold(s.state);
            break;
        case State::Thinking:
        case State::Reading:
        case State::Writing:
        case State::Running:
            if (quiet >= kStaleWorkingSeconds) { s.state = State::Idle; s.since = now; }
            break;
        case State::Attention:
            if (quiet >= kStaleAttentionSeconds) { s.state = State::Idle; s.since = now; }
            break;
        case State::Idle:
        case State::Sleeping:
            erase = quiet >= kForgetSeconds;
            break;
        }
        it = erase ? _sessions.erase(it) : std::next(it);
    }

    State best = State::Idle;
    for (const auto& [id, s] : _sessions)
    {
        if (Priority(s.state) > Priority(best)) best = s.state;
    }
    if (best == State::Idle && now - _lastActivity >= sleepMinutes * 60.0)
    {
        best = State::Sleeping;
    }

    std::string key = Key(best);
    if (now < _overlayUntil)
    {
        key = _overlayKey;
    }
    _currentState = best;
    if (key != _currentKey)
    {
        _currentKey = key;
        _changed = true;
    }
}

bool StateMachine::ConsumeRetriggered()
{
    bool r = _retriggered;
    _retriggered = false;
    return r;
}

bool StateMachine::ConsumeChanged()
{
    bool c = _changed;
    _changed = false;
    return c;
}
