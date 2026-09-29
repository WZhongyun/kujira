#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <cstring>

#include "core/App.h"
#include "events/HookForward.h"

int main(int argc, char** argv)
{
    // `Kujira --hook <agent>`: run by the agent's hook, forwards one event and exits.
    if (argc >= 3 && std::strcmp(argv[1], "--hook") == 0)
    {
        return HookForward::Run(argv[2]);
    }
    App app;
    return app.Run();
}
