#include <cstring>

#include "Game.h"
#include "Renderer.h"

int main(int argc, char** argv)
{
    Renderer renderer;

    if (!renderer.registerClass())
    {
        return 1;
    }

    if (!renderer.create("Running Game", static_cast<int>(World::VIEW_WIDTH), static_cast<int>(World::VIEW_HEIGHT)))
    {
        return 1;
    }

    // "--watch" starts the first level playing itself, the same as pressing P once
    // the window is up. Anything else is ignored, so the plain run is unchanged.
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--watch") == 0)
        {
            renderer.startWatchMode();
            break;
        }
    }

    return renderer.run();
}
