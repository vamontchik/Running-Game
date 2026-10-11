#ifndef AUTOPLAY_H
#define AUTOPLAY_H

#include <vector>

#include "Game.h"

// Works out, then plays back, a complete run of a level with no player involved.
//
// The game is a precision platformer: the runner hops between floating platforms
// over lava, and touching the lava or a spike ends the run. Playing it therefore
// means choosing, on every frame, whether to jump. This plans those choices by
// searching the real simulation, and then hands them back one frame at a time, so
// anything holding a Game can be driven by it. The console playthrough and the
// window's watch mode both use this one implementation, so the route that is
// worked out and the route that is played cannot drift apart.
//
// Sprint is held for the whole run, the way a player who has learned the level
// holds it. The search then only has to pick the frames on which to press jump,
// and, where a single jump cannot do the job, the frame on which to take the
// mid-air jump as well.
class Autoplay
{
public:
    // The fixed simulation step the route is worked out and played back at.
    //
    // Small enough that nothing tunnels through a platform at sprint speed, and
    // the same step the unit tests use. Playback has to advance the game by
    // exactly this: the plan is a list of frame numbers, and at any other step
    // those numbers would name different points in the jump.
    static constexpr float STEP = 1.0f / 120.0f;

    // One jump of a planned route.
    struct Jump
    {
        // The frame, counted in fixed steps from the start, on which the jump is
        // pressed. This is the whole reason the plan is a plan: playback is just
        // "press jump on these frames".
        int step;

        // Where the runner was when the jump was pressed, and where it landed.
        // The landing is the finish when this was the jump that ended the level.
        float fromX;
        float fromY;
        float toX;
        float toY;

        // True when the press is a mid-air jump, taken on top of a ground jump.
        bool midAir;
    };

    Autoplay();

    // Works out a route through the level `game` is sitting at the start of.
    // Returns false when no route was found. `game` is left untouched: the search
    // runs entirely on copies of it.
    bool plan(const Game& game);

    // True once a route has been worked out.
    bool hasPlan() const;

    // The route, for reporting where each jump lands.
    const std::vector<Jump>& jumps() const;

    // Rewinds playback to the first frame of the route.
    void rewind();

    // True once every planned frame has been played.
    bool done() const;

    // The input to use for the next fixed step. Advances one step per call, so it
    // is called exactly once per Game::update(STEP, ...). `game` is only read, to
    // stop pressing jump once the run is over.
    Input nextInput(const Game& game);

    // How many jumps the route has, and how many have been played so far. For a
    // progress readout.
    int jumpCount() const;
    int jumpsPlayed() const;

private:
    // The route, in the order the jumps are pressed, for reporting where each
    // jump lands. Each entry is one jump, which may be two presses: the ground
    // jump and, where it has one, the mid-air jump.
    std::vector<Jump> route;

    // Every frame on which jump is pressed, ascending. This is what playback
    // follows, since a mid-air jump is a second press on its own frame and the
    // route entries alone do not carry it.
    std::vector<int> presses;

    // Cursors into `presses` and `route`, and the frame about to be played with
    // one past the last frame of the route.
    size_t pressNext;
    size_t jumpNext;
    int step;
    int end;
};

#endif // AUTOPLAY_H
