#include "events/HookForward.h"

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#endif

#include <httplib.h>
#include <nlohmann/json.hpp>

#include "core/Config.h"
#include "platform/Platform.h"

namespace
{
constexpr size_t kMaxBody = 16 * 1024 * 1024;

std::string ReadStdin()
{
    std::string body;
    char buf[64 * 1024];
#ifdef _WIN32
    // GUI-subsystem executable: read the inherited handle directly rather than
    // relying on the C runtime having set up stdin.
    HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
    if (in == nullptr || in == INVALID_HANDLE_VALUE) return body;
    DWORD n = 0;
    while (body.size() < kMaxBody && ReadFile(in, buf, sizeof(buf), &n, nullptr) && n > 0)
    {
        body.append(buf, n);
    }
#else
    size_t n = 0;
    while (body.size() < kMaxBody && (n = std::fread(buf, 1, sizeof(buf), stdin)) > 0)
    {
        body.append(buf, n);
    }
#endif
    return body;
}
}

int HookForward::Run(const char* agent)
{
    const std::string body = ReadStdin();
    if (body.empty()) return 0;

    // Read-only: port and token come from the pet's own config, so reinstalling
    // hooks is not needed when they change.
    const Config config = Config::Load();
    httplib::Client client("127.0.0.1", config.port);
    client.set_connection_timeout(1, 0);
    client.set_read_timeout(2, 0);
    client.set_write_timeout(2, 0);
    const httplib::Headers headers = { { "X-Kujira-Token", config.token } };
    const std::string path = std::string("/v1/events/") + agent;
    if (client.Post(path, headers, body, "application/json")) return 0;

    // She isn't running. Optionally start her when a new agent session begins,
    // then deliver this event so she greets it.
    if (!config.launchWithAgent) return 0;
    const auto event = nlohmann::json::parse(body, nullptr, false);
    if (!event.is_object() || event.value("hook_event_name", "") != "SessionStart") return 0;
    // Start this very executable: it is the one the hook points at, so the path
    // is right by construction.
    if (!Platform::LaunchDetached(FileUtil::ExecutablePath())) return 0;
    for (int i = 0; i < 100; ++i)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        if (auto res = client.Get("/v1/health"); res && res->status == 200)
        {
            client.Post(path, headers, body, "application/json");
            break;
        }
    }
    return 0;
}
