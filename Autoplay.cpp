#include "Autoplay.h"

#include <cmath>
#include <map>

namespace
{
    // Longest a jump may be planned ahead of the last frame the runner could
    // still have jumped from. Well past any real gap: the search stops early once
    // the platform is gone, so these only bound the loops.
    const int MAX_GROUND_DELAY = 400;
    const int MAX_AIR_DELAY = 200;

    // Advances one frame holding right and sprint, pressing jump if asked. The
    // single place the search and the playback both step the game, so they cannot
    // disagree about what a frame is.
    void step(Game& game, bool jump)
    {
        Input input;
        input.right = true;
        input.sprint = true;
        input.jumpsPressed = jump ? 1 : 0;
        game.update(Autoplay::STEP, input);
    }

    // Index of the platform the runner is standing on, or -1 for none. The floor
    // is -2, since it is not one of the level's platforms.
    int platformUnder(const Game& game, float x, float y)
    {
        if (std::fabs(y - World::FLOOR_TOP) < 0.5f && x < World::LAVA_START_X)
        {
            return -2;
        }

        const std::vector<Platform>& platforms = game.getPlatforms();
        for (size_t i = 0; i < platforms.size(); ++i)
        {
            const Platform& platform = platforms[i];
            const float top = platform.block.getY() + platform.block.getHeight();
            const float left = platform.block.getX();
            const float right = left + platform.block.getWidth();

            // The runner is still supported with its centre up to half its own
            // size past an edge, which is the reach the physics itself uses.
            if (std::fabs(y - top) < 0.5f && x >= left - World::RUNNER_HALF_SIZE
                && x <= right + World::RUNNER_HALF_SIZE)
            {
                return static_cast<int>(i);
            }
        }

        return -1;
    }

    // World x past which the runner is no longer supported by platform `index`.
    float supportRight(const Game& game, int index)
    {
        if (index == -2)
        {
            return World::LAVA_START_X;
        }
        if (index < 0)
        {
            return 0.0f;
        }

        const Platform& platform = game.getPlatforms()[static_cast<size_t>(index)];
        return platform.block.getX() + platform.block.getWidth()
            + World::RUNNER_HALF_SIZE;
    }

    enum Flight
    {
        FlightDead,
        FlightLost,
        FlightLanded
    };

    // Runs an airborne state forward until it lands, filling `after` with the
    // state at the landing, or at the finish if the line is crossed first.
    Flight fly(const Game& start, Game& after)
    {
        Game game = start;

        // A jump lasts at most about two seconds, so this is comfortably long.
        for (int frame = 0; frame < 400; ++frame)
        {
            step(game, false);

            if (game.isFinished())
            {
                after = game;
                return FlightLanded;
            }
            if (game.isDead())
            {
                return FlightDead;
            }
            if (game.isGrounded())
            {
                after = game;
                return FlightLanded;
            }
        }

        return FlightLost;
    }

    // A landing worth continuing from: still alive, and either finished the level
    // or reached a platform further along than the one jumped from.
    bool worthwhile(const Game& after, float fromRight)
    {
        if (after.isDead())
        {
            return false;
        }
        if (after.isFinished())
        {
            return true;
        }
        return after.getRunner().getX() > fromRight;
    }

    // A state is its landing position: everything else about a game at rest is
    // fixed. Two landings at the same spot have the same future, so a state
    // reached twice need not be searched twice.
    long long stateKey(const Game& game)
    {
        return static_cast<long long>(platformUnder(game, game.getRunner().getX(),
            game.getRunner().getY()) + 3) * 1000000LL
            + static_cast<long long>(game.getRunner().getX() * 2.0f);
    }

    // Depth-first search for a jump sequence that finishes the level.
    //
    // Single jumps are tried before mid-air ones, so the route uses the ordinary
    // hop wherever it can and only takes the mid-air jump where a single jump
    // cannot reach. The two halves of each entry are the frame counts the route
    // came out as: how long to wait before the ground jump, and how many frames
    // after it to take the mid-air jump (-1 for none).
    bool solve(const Game& game, std::vector<int>& planGround,
        std::vector<int>& planAir, std::map<long long, bool>& visited, int depth)
    {
        if (game.isFinished() && !game.isDead())
        {
            return true;
        }
        if (game.isDead() || depth > 400)
        {
            return false;
        }

        const long long key = stateKey(game);
        if (visited.find(key) != visited.end())
        {
            return false;
        }
        visited[key] = true;

        const float fromRight = supportRight(game,
            platformUnder(game, game.getRunner().getX(),
                game.getRunner().getY()));

        // Single jumps. The wait is advanced one frame at a time from the current
        // position, which is the only place a jump can start.
        Game waiting = game;
        for (int ground = 0; ground <= MAX_GROUND_DELAY; ++ground)
        {
            Game jumped = waiting;
            step(jumped, true);

            if (jumped.isFinished() && !jumped.isDead())
            {
                planGround.push_back(ground);
                planAir.push_back(-1);
                return true;
            }

            if (!jumped.isDead())
            {
                Game after;
                if (fly(jumped, after) == FlightLanded
                    && worthwhile(after, fromRight))
                {
                    planGround.push_back(ground);
                    planAir.push_back(-1);
                    if (solve(after, planGround, planAir, visited, depth + 1))
                    {
                        return true;
                    }
                    planGround.pop_back();
                    planAir.pop_back();
                }
            }

            // Take the wait one frame further. Once the platform is gone there is
            // nothing to jump from, so every longer wait fails the same way.
            step(waiting, false);
            if (waiting.isDead() || !waiting.isGrounded())
            {
                break;
            }
        }

        // Mid-air jumps, taken on top of a ground jump.
        Game waitingAgain = game;
        for (int ground = 0; ground <= MAX_GROUND_DELAY; ++ground)
        {
            Game jumped = waitingAgain;
            step(jumped, true);

            if (!jumped.isDead() && !jumped.isFinished())
            {
                // Walk the flight and try the mid-air press on each of its frames.
                Game airborne = jumped;
                for (int air = 0; air <= MAX_AIR_DELAY; ++air)
                {
                    Game pressed = airborne;
                    step(pressed, true);

                    Game after;
                    bool ok = false;
                    if (pressed.isFinished())
                    {
                        after = pressed;
                        ok = !pressed.isDead();
                    }
                    else if (fly(pressed, after) == FlightLanded)
                    {
                        ok = worthwhile(after, fromRight);
                    }

                    if (ok)
                    {
                        planGround.push_back(ground);
                        planAir.push_back(air);
                        if (solve(after, planGround, planAir, visited, depth + 1))
                        {
                            return true;
                        }
                        planGround.pop_back();
                        planAir.pop_back();
                    }

                    step(airborne, false);
                    if (airborne.isDead() || airborne.isFinished()
                        || airborne.isGrounded())
                    {
                        break;
                    }
                }
            }

            step(waitingAgain, false);
            if (waitingAgain.isDead() || !waitingAgain.isGrounded())
            {
                break;
            }
        }

        return false;
    }

    // Replays a plan to turn its per-jump frame counts into the absolute frames
    // the route is stored as, recording where each jump started and landed. Also
    // reports the frame the run ended on.
    void recordRoute(const Game& start, const std::vector<int>& planGround,
        const std::vector<int>& planAir, std::vector<Autoplay::Jump>& route,
        std::vector<int>& presses, int& end)
    {
        Game game = start;
        int frame = 0;

        for (size_t i = 0; i < planGround.size(); ++i)
        {
            const float fromX = game.getRunner().getX();
            const float fromY = game.getRunner().getY();
            const bool midAir = planAir[i] >= 0;

            for (int wait = 0; wait < planGround[i]; ++wait)
            {
                step(game, false);
                ++frame;
            }

            // The ground press is taken on this frame: `frame` counts the steps
            // already run, so it is the number the next step lands on.
            const int pressStep = frame;
            presses.push_back(pressStep);
            step(game, true);
            ++frame;

            if (midAir)
            {
                for (int wait = 0; wait < planAir[i] && !game.isGrounded()
                        && !game.isDead() && !game.isFinished(); ++wait)
                {
                    step(game, false);
                    ++frame;
                }

                // The mid-air jump is a second press on its own frame.
                presses.push_back(frame);
                step(game, true);
                ++frame;
            }

            while (!game.isFinished() && !game.isDead() && !game.isGrounded())
            {
                step(game, false);
                ++frame;
            }

            Autoplay::Jump jump;
            jump.step = pressStep;
            jump.fromX = fromX;
            jump.fromY = fromY;
            jump.toX = game.getRunner().getX();
            jump.toY = game.getRunner().getY();
            jump.midAir = midAir;
            route.push_back(jump);

            if (game.isFinished() || game.isDead())
            {
                break;
            }
        }

        end = frame;
    }
}

Autoplay::Autoplay()
    : pressNext(0)
    , jumpNext(0)
    , step(0)
    , end(0)
{
}

bool Autoplay::plan(const Game& game)
{
    route.clear();
    presses.clear();
    pressNext = 0;
    jumpNext = 0;
    step = 0;
    end = 0;

    // Solve on a copy, so the caller's game is left exactly as it was.
    Game start = game;

    std::vector<int> planGround;
    std::vector<int> planAir;
    std::map<long long, bool> visited;

    if (!solve(start, planGround, planAir, visited, 0))
    {
        return false;
    }

    recordRoute(start, planGround, planAir, route, presses, end);
    return true;
}

bool Autoplay::hasPlan() const
{
    return !route.empty();
}

const std::vector<Autoplay::Jump>& Autoplay::jumps() const
{
    return route;
}

void Autoplay::rewind()
{
    pressNext = 0;
    jumpNext = 0;
    step = 0;
}

bool Autoplay::done() const
{
    return step >= end;
}

Input Autoplay::nextInput(const Game& game)
{
    Input input;
    input.right = true;
    input.sprint = true;

    // Nothing left to drive once the run has ended. Right and sprint are harmless
    // either way, so they stay on.
    if (game.isFinished() || game.isDead())
    {
        return input;
    }

    if (pressNext < presses.size() && step == presses[pressNext])
    {
        // One press. A mid-air jump is its own frame and so its own entry, which
        // is why the presses outnumber the jumps on any route that uses one.
        input.jumpsPressed = 1;
        ++pressNext;
    }

    // Count the jump itself once its ground press has gone, for the progress
    // readout: a jump's mid-air press is still part of the same jump.
    if (jumpNext < route.size() && step == route[jumpNext].step)
    {
        ++jumpNext;
    }

    ++step;
    return input;
}

int Autoplay::jumpCount() const
{
    return static_cast<int>(route.size());
}

int Autoplay::jumpsPlayed() const
{
    return static_cast<int>(jumpNext);
}
