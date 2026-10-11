// Plays a complete run of the whole game, without a window.
//
// Each level's route is worked out by Autoplay, which searches the real physics,
// and this reports it and then plays it back through the same interface the
// window's watch mode uses. So the numbers printed here are the frames the
// window would press jump on, and the run is proved by the replay at the end of
// each level rather than taken on trust.
//
// For watching the same thing drawn in the game window, run the game and press P
// (or launch it with --watch): that uses this same planner, stepped at the same
// fixed rate, and plays every level through to the end before looping.
//
// Run from the project directory: Game reads levelN.txt relative to the working
// directory.

#include <cstdio>

#include "Autoplay.h"
#include "Game.h"

// Plans and plays one level of `game`, which must be sitting at its first frame.
// Returns false if the level could not be solved or the playthrough died.
static bool playLevel(Game& game)
{
    Autoplay autoplay;
    if (!autoplay.plan(game))
    {
        std::printf("  no route found through level %d\n", game.getLevel());
        return false;
    }

    std::printf("Level %d route: %d jumps\n", game.getLevel(),
        autoplay.jumpCount());

    const std::vector<Autoplay::Jump>& route = autoplay.jumps();
    for (size_t i = 0; i < route.size(); ++i)
    {
        std::printf("  jump %2d: x %7.1f -> %7.1f   y %6.1f   %s\n",
            static_cast<int>(i) + 1,
            static_cast<double>(route[i].fromX),
            static_cast<double>(route[i].toX),
            static_cast<double>(route[i].toY),
            route[i].midAir ? "mid-air" : "");
    }

    // Play the route back the way the window does, a fixed step at a time, so the
    // plan is shown to complete the level and not merely to look right.
    autoplay.rewind();
    while (!game.isFinished() && !game.isDead() && !autoplay.done())
    {
        Input input = autoplay.nextInput(game);
        game.update(Autoplay::STEP, input);
    }

    if (game.isDead() || !game.isFinished())
    {
        std::printf("  level %d failed: dead=%s finished=%s\n", game.getLevel(),
            game.isDead() ? "yes" : "no", game.isFinished() ? "yes" : "no");
        return false;
    }

    std::printf("  cleared at %.2fs\n\n", game.getElapsedTime());
    return true;
}

int main()
{
    // A fresh run, standing at the first frame of level one.
    Game game;

    // One pass through every level, in order, until the last is cleared. The
    // run timer spans the lot, so the time reported at the end is the whole
    // attempt rather than a single level.
    while (true)
    {
        if (!playLevel(game))
        {
            return 1;
        }

        if (game.isGameComplete())
        {
            break;
        }

        game.advanceLevel();
    }

    std::printf("Game complete: all %d levels, %.2fs, %.0f units\n",
        World::LAST_LEVEL, game.getElapsedTime(),
        static_cast<double>(World::FINISH_DISTANCE) * World::LAST_LEVEL);

    return 0;
}
