#include "events/EventServer.h"

#include <httplib.h>

#include "agents/AgentAdapter.h"

EventServer::EventServer(EventQueue<PetEvent>& queue)
    : _queue(queue)
{
}

EventServer::~EventServer()
{
    Stop();
}

bool EventServer::Start(int port, const std::string& token)
{
    Stop();
    _port = port;
    _token = token;
    _lastError.clear();

    _server = std::make_unique<httplib::Server>();
    _server->set_payload_max_length(16 * 1024 * 1024);  // PostToolUse may carry whole files
    _server->set_read_timeout(2, 0);
    _server->set_write_timeout(2, 0);
    _server->set_keep_alive_max_count(100);

    _server->Get("/v1/health", [](const httplib::Request&, httplib::Response& res) {
        res.set_content("{\"app\":\"kujira\",\"version\":\"" KUJIRA_VERSION "\"}", "application/json");
    });

    _server->Post(R"(/v1/events/([a-z0-9\-]+))", [this](const httplib::Request& req, httplib::Response& res) {
        // Reply first with an empty object: observe only, never influence the agent.
        res.set_content("{}", "application/json");
        if (!_token.empty() && req.get_header_value("X-Kujira-Token") != _token)
        {
            res.status = 403;
            return;
        }
        const AgentAdapter* adapter = AgentRegistry::Find(req.matches[1]);
        if (!adapter)
        {
            res.status = 404;
            return;
        }
        if (auto event = adapter->Translate(req.body))
        {
            ++_received;
            _queue.Push(std::move(*event));
        }
    });

    if (!_server->bind_to_port("127.0.0.1", port))
    {
        _lastError = "端口 " + std::to_string(port) + " 已被占用或无法监听";
        _server.reset();
        return false;
    }
    _running = true;
    _thread = std::thread([this] {
        _server->listen_after_bind();
        _running = false;
    });
    return true;
}

void EventServer::Stop()
{
    if (_server)
    {
        _server->stop();
    }
    if (_thread.joinable())
    {
        _thread.join();
    }
    _server.reset();
    _running = false;
}
