#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <thread>

#include "core/EventQueue.h"
#include "events/PetEvent.h"

namespace httplib { class Server; }

// Local HTTP endpoint for agent hooks, bound to 127.0.0.1 only.
// POST /v1/events/<agent> -> adapter -> PetEvent -> queue. Always answers at once
// with an empty JSON object, so hooks never slow the agent down or change its behaviour.
class EventServer
{
public:
    EventServer(EventQueue<PetEvent>& queue);
    ~EventServer();

    bool Start(int port, const std::string& token);
    void Stop();

    bool IsRunning() const { return _running; }
    int Port() const { return _port; }
    const std::string& LastError() const { return _lastError; }
    uint64_t ReceivedCount() const { return _received; }

private:
    EventQueue<PetEvent>& _queue;
    std::unique_ptr<httplib::Server> _server;
    std::thread _thread;
    std::atomic<bool> _running{ false };
    std::atomic<uint64_t> _received{ 0 };
    int _port = 0;
    std::string _token;
    std::string _lastError;
};
