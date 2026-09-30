#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <cstring>

#include "core/App.h"

int main(int argc, char** argv)
{
    bool launchedByAgent = false;
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--launched-by-agent") == 0) launchedByAgent = true;
    }
    App app(launchedByAgent);
    return app.Run();
}
