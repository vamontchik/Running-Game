// Unit tests for the parts of RunningGame that can be tested without a window.
//
// Hand-rolled harness rather than a test framework, because the project has no
// third-party dependencies and adding one for this would change that.
//
// What is covered: the World geometry helpers, the Runner, Block and Map value
// types, and the simulation rules in Game that a fixed sequence of inputs can
// reach deterministically.
//
// What is not covered: anything in Renderer, which needs a live HDC and a window
// message loop. The spike hazard is covered both through Game::spikesHit, the
// query checkSpikes is built on, and end to end through the placeRunner() test
// seam, which drives the real hit-to-death path inside checkSpikes.
//
// Run from the project directory: Game reads level1.txt relative to the working
// directory on construction, so the levels have to be alongside the executable.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "Block.h"
#include "Game.h"
#include "Map.h"
#include "Runner.h"

namespace
{
    int checksRun = 0;
    int failures = 0;
    const char* currentTest = "";

    void reportFailure(const char* file, int line, const std::string& detail)
    {
        ++failures;
        std::printf("  FAIL  %s\n        %s:%d\n        %s\n",
            currentTest, file, line, detail.c_str());
    }

    void check(bool passed, const char* expression, const char* file, int line)
    {
        ++checksRun;
        if (!passed)
        {
            reportFailure(file, line, expression);
        }
    }

    // Float comparison with a tolerance. Exact equality is the wrong test for
    // accumulated physics; the tolerance is loose enough for float drift over a
    // few hundred simulation steps and tight enough to catch a real error.
    bool nearly(float actual, float expected, float tolerance)
    {
        return std::fabs(actual - expected) <= tolerance;
    }

    void checkNear(
        float actual, float expected, float tolerance, const char* expression,
        const char* file, int line)
    {
        ++checksRun;
        if (!nearly(actual, expected, tolerance))
        {
            char detail[256];
            std::snprintf(detail, sizeof(detail), "%s  (got %.4f, want %.4f)",
                expression, static_cast<double>(actual),
                static_cast<double>(expected));
            reportFailure(file, line, detail);
        }
    }

    void beginTest(const char* name)
    {
        currentTest = name;
        std::printf("- %s\n", name);
    }

    // A fixed simulation step. Small enough that nothing tunnels through a
    // platform at sprint speed: 520 units/second for 1/120th of a second is about
    // 4 units, less than the thinnest thing in the world.
    const float STEP = 1.0f / 120.0f;

    // Advances the game by `frames` steps, reporting the highest the runner got.
    // Used for the jump tests, where the peak height is the thing being measured.
    //
    // `pressFrames` lists the frames on which a jump press is offered, and it
    // matters that they are exact frames rather than a span. Two presses on
    // consecutive frames is a ground jump followed immediately by the mid-air one,
    // which is a different manoeuvre from a ground jump followed by a second jump
    // at the top of the rise. Each listed frame offers exactly one press, and a
    // frame's press is spent by the update it is offered to, so presses do not add
    // up into a held key.
    // Whether frame `frame` carries one of the listed jump presses.
    bool pressesOn(const std::vector<int>& pressFrames, int frame)
    {
        return std::find(pressFrames.begin(), pressFrames.end(), frame)
            != pressFrames.end();
    }

    float peakHeight(Game& game, int frames, Input& input,
        const std::vector<int>& pressFrames)
    {
        float peak = game.getRunner().getY();

        for (int frame = 0; frame < frames; ++frame)
        {
            input.jumpsPressed = pressesOn(pressFrames, frame) ? 1 : 0;
            game.update(STEP, input);
            input.clear();

            const float y = game.getRunner().getY();
            if (y > peak)
            {
                peak = y;
            }
        }

        return peak;
    }

    // Every frame from `from` to `from + count`, for the mash tests.
    std::vector<int> frameRange(int from, int count)
    {
        std::vector<int> frames;
        for (int i = 0; i < count; ++i)
        {
            frames.push_back(from + i);
        }
        return frames;
    }

    // How far short of the analytic jump height the simulation actually gets.
    //
    // update() applies gravity before moving the runner, which is semi-implicit
    // Euler, and that lands a little low: a jump taken at speed v loses about
    // 0.5 * dt * v over its rise. Both are deliberate parts of the feel, and both
    // are stable, so the tests measure against the analytic height with this as
    // the tolerance rather than pretending the integration is exact.
    float integrationShortfall(float launchSpeed)
    {
        return 0.5f * STEP * launchSpeed;
    }

    // -----------------------------------------------------------------------
    // World geometry
    // -----------------------------------------------------------------------

    void testBoundsOf()
    {
        beginTest("World::boundsOf");

        const World::Rect rect = World::boundsOf(10.0f, 20.0f, 30.0f, 40.0f);
        check(rect.left == 10.0f, "left is x", __FILE__, __LINE__);
        check(rect.bottom == 20.0f, "bottom is y", __FILE__, __LINE__);
        check(rect.right == 40.0f, "right is x + width", __FILE__, __LINE__);
        check(rect.top == 60.0f, "top is y + height", __FILE__, __LINE__);

        // A zero-sized rectangle is a point, not an area.
        const World::Rect point = World::boundsOf(5.0f, 5.0f, 0.0f, 0.0f);
        check(point.left == point.right, "zero width has no horizontal extent",
            __FILE__, __LINE__);
    }

    void testOverlapsRect()
    {
        beginTest("World::overlaps");

        const World::Rect unit = World::boundsOf(0.0f, 0.0f, 10.0f, 10.0f);
        const World::Rect shifted = World::boundsOf(5.0f, 0.0f, 10.0f, 10.0f);

        check(World::overlaps(unit, shifted), "partly overlapping", __FILE__, __LINE__);
        check(World::overlaps(unit, unit), "a rectangle overlaps itself", __FILE__, __LINE__);

        // Touching is not overlapping. This is the rule that lets a runner rest on
        // a platform instead of sticking to it, so it is worth pinning down.
        const World::Rect toTheRight = World::boundsOf(10.0f, 0.0f, 10.0f, 10.0f);
        check(!World::overlaps(unit, toTheRight), "touching right edges is clear",
            __FILE__, __LINE__);

        const World::Rect above = World::boundsOf(0.0f, 10.0f, 10.0f, 10.0f);
        check(!World::overlaps(unit, above), "touching top edges is clear",
            __FILE__, __LINE__);

        const World::Rect cornerTouch = World::boundsOf(10.0f, 10.0f, 10.0f, 10.0f);
        check(!World::overlaps(unit, cornerTouch), "touching at one corner is clear",
            __FILE__, __LINE__);

        const World::Rect apart = World::boundsOf(11.0f, 0.0f, 10.0f, 10.0f);
        check(!World::overlaps(unit, apart), "a one unit gap is clear", __FILE__, __LINE__);

        const World::Rect below = World::boundsOf(0.0f, -10.0f, 10.0f, 10.0f);
        check(!World::overlaps(unit, below), "touching bottom edges is clear",
            __FILE__, __LINE__);
    }

    void testSpikesThatFit()
    {
        beginTest("World::spikesThatFit");

        // One spike needs its own width of platform; each extra needs another
        // pitch in front of it.
        check(World::spikesThatFit(0.0f) == 0, "no width fits nothing",
            __FILE__, __LINE__);
        check(World::spikesThatFit(19.0f) == 0, "narrower than a spike fits nothing",
            __FILE__, __LINE__);
        check(World::spikesThatFit(World::SPIKE_WIDTH) == 1,
            "exactly one spike wide fits one", __FILE__, __LINE__);
        check(World::spikesThatFit(World::SPIKE_PITCH) == 1,
            "one pitch holds one", __FILE__, __LINE__);
        check(World::spikesThatFit(World::SPIKE_WIDTH + World::SPIKE_PITCH) == 2,
            "a width plus a pitch fits two", __FILE__, __LINE__);
        check(World::spikesThatFit(100.0f) == 3, "100 wide fits three", __FILE__, __LINE__);

        // Monotonic, and never more than the whole width divided by one spike's
        // worth of room, which is what stops a run from overlapping itself.
        int previous = 0;
        for (float width = 0.0f; width <= 300.0f; width += 1.0f)
        {
            const int fitting = World::spikesThatFit(width);
            check(fitting >= previous, "fitting count never decreases as width grows",
                __FILE__, __LINE__);
            check(fitting <= static_cast<int>(width / World::SPIKE_WIDTH) + 1,
                "fitting count stays within the platform", __FILE__, __LINE__);
            previous = fitting;
        }
    }

    void testSpikeAt()
    {
        beginTest("World::spikeAt");

        // A single spike at an offset: base at the offset, apex over its middle.
        const World::Triangle one
            = World::spikeAt(100.0f, 100.0f, 50.0f, 0, 1, 0.0f);
        checkNear(one.ax, 100.0f, 0.01f, "single spike left edge", __FILE__, __LINE__);
        checkNear(one.bx, 100.0f + World::SPIKE_WIDTH, 0.01f,
            "single spike right edge", __FILE__, __LINE__);
        checkNear(one.ay, 50.0f, 0.01f, "base sits on the platform top",
            __FILE__, __LINE__);
        checkNear(one.by, 50.0f, 0.01f, "base is level", __FILE__, __LINE__);
        checkNear(one.cy, 50.0f + World::SPIKE_HEIGHT, 0.01f,
            "apex is SPIKE_HEIGHT above the top", __FILE__, __LINE__);
        checkNear(one.cx, one.ax + World::SPIKE_WIDTH * 0.5f, 0.01f,
            "apex is over the base's middle", __FILE__, __LINE__);

        // An offset moves the spike, and the offset is measured to the base's left
        // edge rather than to the apex.
        const World::Triangle offset
            = World::spikeAt(100.0f, 100.0f, 50.0f, 0, 1, 30.0f);
        checkNear(offset.ax, 130.0f, 0.01f, "offset moves the base's left edge",
            __FILE__, __LINE__);

        // A run of two steps by the pitch from the first.
        const World::Triangle runFirst
            = World::spikeAt(100.0f, 100.0f, 50.0f, 0, 2, 0.0f);
        const World::Triangle runSecond
            = World::spikeAt(100.0f, 100.0f, 50.0f, 1, 2, 0.0f);
        checkNear(runSecond.ax - runFirst.ax, World::SPIKE_PITCH, 0.01f,
            "consecutive spikes are one pitch apart", __FILE__, __LINE__);

        // Consecutive spikes leave a gap, rather than merging into a wall.
        check(runFirst.bx < runSecond.ax, "there is a gap between spikes",
            __FILE__, __LINE__);

        // An offset past the right edge slides the run left so it ends flush
        // inside, rather than hanging off the end.
        const World::Triangle clamped
            = World::spikeAt(100.0f, 100.0f, 50.0f, 0, 1, 90.0f);
        checkNear(clamped.bx, 200.0f, 0.01f, "an overhanging run is pulled inside",
            __FILE__, __LINE__);

        // An offset before the left edge is held at zero.
        const World::Triangle negative
            = World::spikeAt(100.0f, 100.0f, 50.0f, 0, 1, -30.0f);
        checkNear(negative.ax, 100.0f, 0.01f, "a negative offset is held at the left edge",
            __FILE__, __LINE__);

        // The last spike of a run is always within the platform, for any offset.
        for (float offset = -50.0f; offset <= 250.0f; offset += 5.0f)
        {
            for (int count = 1; count <= 4; ++count)
            {
                const int fitting = World::spikesThatFit(100.0f);
                if (count > fitting)
                {
                    continue;
                }

                const World::Triangle last
                    = World::spikeAt(100.0f, 100.0f, 50.0f, count - 1, count, offset);

                check(last.ax >= 100.0f - 0.01f, "no spike starts before the platform",
                    __FILE__, __LINE__);
                check(last.bx <= 200.0f + 0.01f, "no spike ends past the platform",
                    __FILE__, __LINE__);
            }
        }
    }

    void testOverlapsTriangle()
    {
        beginTest("World::overlapsTriangle");

        // An upright spike with its base at y = 0 and its apex at 16, which is the
        // shape spikeAt builds.
        const World::Triangle spike = World::spikeAt(0.0f, 20.0f, 0.0f, 0, 1, 0.0f);

        // Standing on the spike's base, inside it.
        check(World::overlapsTriangle(
                  World::boundsOf(2.0f, 0.0f, 16.0f, 16.0f), spike),
            "a runner standing on a spike's base is hit", __FILE__, __LINE__);

        // Beside the spike, on the platform.
        check(!World::overlapsTriangle(
                  World::boundsOf(25.0f, 0.0f, 16.0f, 16.0f), spike),
            "a runner beside the spike is clear", __FILE__, __LINE__);

        // Underneath it, having jumped up from below.
        check(!World::overlapsTriangle(
                  World::boundsOf(5.0f, -20.0f, 10.0f, 16.0f), spike),
            "a runner below the base is clear", __FILE__, __LINE__);

        // Above the apex.
        check(!World::overlapsTriangle(
                  World::boundsOf(5.0f, 20.0f, 10.0f, 10.0f), spike),
            "a runner over the apex is clear", __FILE__, __LINE__);

        // The case a bounding box gets wrong. Near y = 8 the triangle is only 10
        // wide, from x = 5 to x = 15, but its bounding box is the full 20 wide. A
        // runner at x 19 is inside the bounding box and outside the triangle.
        check(!World::overlapsTriangle(
                  World::boundsOf(19.0f, 8.0f, 6.0f, 8.0f), spike),
            "beside the apex but inside the bounding box is clear", __FILE__, __LINE__);

        // Directly under the apex tip is a hit, even though the runner is barely
        // wider than the tip.
        check(World::overlapsTriangle(
                  World::boundsOf(8.0f, 15.0f, 4.0f, 4.0f), spike),
            "a runner at the apex is hit", __FILE__, __LINE__);

        // Swallowing the whole spike is a hit.
        check(World::overlapsTriangle(
                  World::boundsOf(-10.0f, -10.0f, 60.0f, 60.0f), spike),
            "a runner enclosing the spike is hit", __FILE__, __LINE__);

        // Touching the base from below is not a hit, matching the rule that
        // overlaps() uses for rectangles.
        check(!World::overlapsTriangle(
                  World::boundsOf(5.0f, -10.0f, 10.0f, 10.0f), spike),
            "a runner whose corners touch the base is clear", __FILE__, __LINE__);

        // A spike far to the side of the runner.
        check(!World::overlapsTriangle(
                  World::boundsOf(500.0f, 0.0f, 16.0f, 16.0f), spike),
            "a spike far away is clear", __FILE__, __LINE__);

        // A degenerate spike, with its base collapsed to a point. Must not report
        // a hit on its own or divide by zero.
        World::Triangle point;
        point.ax = 10.0f;
        point.ay = 0.0f;
        point.bx = 10.0f;
        point.by = 0.0f;
        point.cx = 10.0f;
        point.cy = 16.0f;

        check(!World::overlapsTriangle(
                  World::boundsOf(20.0f, 0.0f, 16.0f, 16.0f), point),
            "a spike beside a degenerate one is clear", __FILE__, __LINE__);
    }

    // -----------------------------------------------------------------------
    // Value types
    // -----------------------------------------------------------------------

    void testRunner()
    {
        beginTest("Runner");

        Runner runner;
        checkNear(runner.getX(), 0.0f, 0.001f, "starts at the origin x",
            __FILE__, __LINE__);
        checkNear(runner.getY(), 0.0f, 0.001f, "starts at the origin y",
            __FILE__, __LINE__);
        checkNear(runner.getVelocity(), 0.0f, 0.001f, "starts still",
            __FILE__, __LINE__);

        runner.setX(100.0f);
        runner.setY(50.0f);
        runner.setVelocity(200.0f);
        checkNear(runner.getX(), 100.0f, 0.001f, "setX", __FILE__, __LINE__);
        checkNear(runner.getY(), 50.0f, 0.001f, "setY", __FILE__, __LINE__);
        checkNear(runner.getVelocity(), 200.0f, 0.001f, "setVelocity",
            __FILE__, __LINE__);

        // update advances x by velocity * dt and touches nothing else.
        runner.update(0.5f);
        checkNear(runner.getX(), 200.0f, 0.001f, "x advances by velocity * dt",
            __FILE__, __LINE__);
        checkNear(runner.getY(), 50.0f, 0.001f, "update leaves y alone",
            __FILE__, __LINE__);

        // A negative velocity moves backwards, which is what running left is.
        runner.setVelocity(-200.0f);
        runner.update(0.25f);
        checkNear(runner.getX(), 150.0f, 0.001f, "a negative velocity moves left",
            __FILE__, __LINE__);

        runner.setVelocity(0.0f);
        runner.update(1.0f);
        checkNear(runner.getX(), 150.0f, 0.001f, "no velocity, no movement",
            __FILE__, __LINE__);
    }

    void testBlock()
    {
        beginTest("Block");

        Block block;
        checkNear(block.getX(), 0.0f, 0.001f, "default x", __FILE__, __LINE__);
        checkNear(block.getY(), 0.0f, 0.001f, "default y", __FILE__, __LINE__);
        checkNear(block.getWidth(), 0.0f, 0.001f, "default width", __FILE__, __LINE__);
        checkNear(block.getHeight(), 0.0f, 0.001f, "default height", __FILE__, __LINE__);

        const Block sized(10.0f, 20.0f, 30.0f, 40.0f);
        checkNear(sized.getX(), 10.0f, 0.001f, "x", __FILE__, __LINE__);
        checkNear(sized.getY(), 20.0f, 0.001f, "y", __FILE__, __LINE__);
        checkNear(sized.getWidth(), 30.0f, 0.001f, "width", __FILE__, __LINE__);
        checkNear(sized.getHeight(), 40.0f, 0.001f, "height", __FILE__, __LINE__);
    }

    void testMap()
    {
        beginTest("Map");

        Map map;
        check(map.getBlockCount() == 0, "a new map is empty", __FILE__, __LINE__);

        map.addBlock(Block(0.0f, 0.0f, 10.0f, 10.0f));
        map.addBlock(Block(20.0f, 0.0f, 30.0f, 10.0f));
        check(map.getBlockCount() == 2, "addBlock appends", __FILE__, __LINE__);

        checkNear(map.getBlock(0).getWidth(), 10.0f, 0.001f, "first block",
            __FILE__, __LINE__);
        checkNear(map.getBlock(1).getX(), 20.0f, 0.001f, "second block",
            __FILE__, __LINE__);

        // Out of range is a default block rather than a crash, and removing out of
        // range reports failure instead.
        checkNear(map.getBlock(99).getWidth(), 0.0f, 0.001f,
            "an out of range read is a default block", __FILE__, __LINE__);
        check(!map.removeBlock(99), "removing out of range fails", __FILE__, __LINE__);
        check(map.getBlockCount() == 2, "a failed removal changes nothing",
            __FILE__, __LINE__);

        // Removal closes the gap, so the block after the removed one shifts down.
        check(map.removeBlock(0), "removing a real index succeeds", __FILE__, __LINE__);
        check(map.getBlockCount() == 1, "removal shrinks the map", __FILE__, __LINE__);
        checkNear(map.getBlock(0).getX(), 20.0f, 0.001f,
            "the remaining block shifted into the gap", __FILE__, __LINE__);

        map.clear();
        check(map.getBlockCount() == 0, "clear empties the map", __FILE__, __LINE__);
    }

    void testInput()
    {
        beginTest("Input");

        Input input;
        check(!input.left && !input.right && !input.sprint, "nothing held by default",
            __FILE__, __LINE__);
        check(input.jumpsPressed == 0, "no jump presses by default", __FILE__, __LINE__);

        input.left = true;
        input.right = true;
        input.sprint = true;
        input.jumpsPressed = 2;
        input.clear();
        check(!input.left && !input.right && !input.sprint, "clear releases the keys",
            __FILE__, __LINE__);
        check(input.jumpsPressed == 0, "clear drops pending presses", __FILE__, __LINE__);

        check(Input::MAX_PENDING_JUMPS >= World::MAX_AIR_JUMPS + 1,
            "the press cap can hold a ground jump and a mid-air jump",
            __FILE__, __LINE__);
    }

    void testPlatform()
    {
        beginTest("Platform");

        const Platform spiked(100.0f, 50.0f, 120.0f, 2, 30.0f);
        checkNear(spiked.block.getX(), 100.0f, 0.001f, "block x", __FILE__, __LINE__);
        checkNear(spiked.block.getWidth(), 120.0f, 0.001f, "block width",
            __FILE__, __LINE__);
        checkNear(spiked.block.getHeight(), World::PLATFORM_THICKNESS, 0.001f,
            "a platform always has the standard thickness", __FILE__, __LINE__);
        check(spiked.spikes == 2, "spike count", __FILE__, __LINE__);
        checkNear(spiked.spikeOffset, 30.0f, 0.001f, "spike offset", __FILE__, __LINE__);

        const Platform plain;
        check(plain.spikes == 0, "a default platform has no spikes", __FILE__, __LINE__);
        checkNear(plain.spikeOffset, 0.0f, 0.001f,
            "a default platform has no offset", __FILE__, __LINE__);
    }

    // -----------------------------------------------------------------------
    // Game
    // -----------------------------------------------------------------------

    void testGameStartsOnLevelOne()
    {
        beginTest("Game starts on level one");

        Game game;
        check(game.getLevel() == World::FIRST_LEVEL, "starts on the first level",
            __FILE__, __LINE__);
        check(!game.isDead(), "starts alive", __FILE__, __LINE__);
        check(!game.isFinished(), "starts unfinished", __FILE__, __LINE__);
        check(!game.isGameComplete(), "a fresh run is not complete", __FILE__, __LINE__);
        checkNear(game.getElapsedTime(), 0.0, 0.0001, "the run timer starts at zero",
            __FILE__, __LINE__);
        checkNear(game.getDistanceTravelled(), 0.0, 0.001f,
            "the distance starts at zero", __FILE__, __LINE__);
        checkNear(game.getRunner().getX(), World::START_X, 0.001f,
            "the runner starts at START_X", __FILE__, __LINE__);
        checkNear(game.getRunner().getY(), World::FLOOR_TOP, 0.001f,
            "the runner starts on the floor", __FILE__, __LINE__);

        // The floor and the level's own platforms are both in the map, so a level
        // file that failed to load would show up here as a missing floor.
        check(game.getMap().getBlockCount() > 1,
            "the map holds the floor and the level's platforms", __FILE__, __LINE__);

        // Every spiked platform in the level must have survived loading with a
        // spike count its own width can hold, which is the invariant the loader
        // maintains and the collision depends on.
        const std::vector<Platform>& platforms = game.getPlatforms();
        for (size_t i = 0; i < platforms.size(); ++i)
        {
            check(platforms[i].spikes >= 0, "no platform has a negative spike count",
                __FILE__, __LINE__);
            check(platforms[i].spikes
                    <= World::spikesThatFit(platforms[i].block.getWidth()),
                "every platform's spike count fits its width", __FILE__, __LINE__);
        }
    }

    void testGameMoves()
    {
        beginTest("Game moves the runner");

        Game game;
        Input input;

        // Nothing held: the runner stays put, though gravity still applies and the
        // floor holds it up.
        for (int frame = 0; frame < 30; ++frame)
        {
            game.update(STEP, input);
        }
        checkNear(game.getRunner().getX(), World::START_X, 0.5f,
            "no input means no movement", __FILE__, __LINE__);
        checkNear(game.getRunner().getY(), World::FLOOR_TOP, 0.5f,
            "the runner stays on the floor", __FILE__, __LINE__);
        check(!game.isSprinting(), "not sprinting with no sprint held",
            __FILE__, __LINE__);

        // Holding right moves at the walking speed.
        input.right = true;
        for (int frame = 0; frame < 60; ++frame)
        {
            game.update(STEP, input);
        }
        const float walked = game.getRunner().getX() - World::START_X;
        checkNear(walked, World::MOVE_SPEED * 60.0f * STEP, 1.0f,
            "walking covers speed * time", __FILE__, __LINE__);
        check(game.isSprinting() == false, "walking is not sprinting", __FILE__, __LINE__);

        // Releasing stops it dead, since movement is instant rather than ramped.
        input.right = false;
        const float stoppedAt = game.getRunner().getX();
        game.update(STEP, input);
        checkNear(game.getRunner().getX(), stoppedAt, 0.001f,
            "releasing stops the runner immediately", __FILE__, __LINE__);

        // Sprinting is the faster of the two, and only with the key held.
        input.sprint = true;
        input.right = true;
        game.update(STEP, input);
        check(game.isSprinting(), "sprinting while the key is held", __FILE__, __LINE__);

        const float sprintStart = game.getRunner().getX();
        for (int frame = 0; frame < 60; ++frame)
        {
            game.update(STEP, input);
        }
        checkNear(game.getRunner().getX() - sprintStart,
            World::SPRINT_SPEED * 60.0f * STEP, 1.0f,
            "sprinting covers speed * time", __FILE__, __LINE__);
    }

    void testGameJumpPhysics()
    {
        beginTest("Game jump height");

        // A single jump rises by JUMP_SPEED squared over twice the gravity, from the
        // floor it started on. One press, on one frame, is a single jump: a second
        // press on the next frame would spend the mid-air jump immediately and
        // measure a different manoeuvre.
        Game single;
        Input input;
        const float singlePeak = peakHeight(single, 240, input, { 0 });
        const float expectedSingle
            = World::FLOOR_TOP + World::JUMP_SPEED * World::JUMP_SPEED
                / (2.0f * -World::GRAVITY);

        checkNear(singlePeak, expectedSingle,
            integrationShortfall(World::JUMP_SPEED) + 0.5f,
            "a single jump peaks at speed squared over two g", __FILE__, __LINE__);

        // The top of that rise is half a second in, which is where a player taps
        // for the extra height. Pressing there adds the mid-air jump's own rise on
        // top of the first, rather than replacing the remaining fall speed.
        const int apexFrame = static_cast<int>(
            World::JUMP_SPEED / -World::GRAVITY / STEP + 0.5f);

        Game doubled;
        Input doubleInput;
        const float doublePeak
            = peakHeight(doubled, 240, doubleInput, { 0, apexFrame });
        const float expectedDouble = expectedSingle
            + World::AIR_JUMP_SPEED * World::AIR_JUMP_SPEED / (2.0f * -World::GRAVITY);

        checkNear(doublePeak, expectedDouble,
            integrationShortfall(World::JUMP_SPEED)
                + integrationShortfall(World::AIR_JUMP_SPEED)
                + 0.5f,
            "a second jump adds its own rise", __FILE__, __LINE__);
        check(doublePeak > singlePeak + 50.0f,
            "the second jump is worth real height", __FILE__, __LINE__);

        // The mid-air jump is deliberately weaker than the ground jump, so it
        // extends a fall rather than replacing a well-timed sprint jump.
        check(World::AIR_JUMP_SPEED < World::JUMP_SPEED,
            "the mid-air jump is weaker than the ground jump", __FILE__, __LINE__);

        // One jump per fall, so mashing cannot chain. Both jumps spent at the top
        // of the rise, then a press on every frame for the rest of it: the peak
        // must stay at the double jump's, and not creep up frame by frame.
        Game mashed;
        Input mashInput;
        std::vector<int> mashFrames = { 0, apexFrame };
        const std::vector<int> laterFrames = frameRange(apexFrame + 1, 150);
        mashFrames.insert(mashFrames.end(), laterFrames.begin(), laterFrames.end());

        const float mashedPeak = peakHeight(mashed, 240, mashInput, mashFrames);
        checkNear(mashedPeak, doublePeak, 1.0f,
            "pressing every frame after both jumps adds nothing", __FILE__, __LINE__);

        // Mashing from the very first frame spends both jumps at once, which is a
        // real double jump taken at ground level and so peaks far lower than one
        // taken at the top of the rise. Worth pinning down, because it is the
        // reason the allowance is counted rather than held.
        Game immediate;
        Input immediateInput;
        const float immediatePeak
            = peakHeight(immediate, 240, immediateInput, frameRange(0, 240));
        check(immediatePeak < doublePeak - 50.0f,
            "two jumps at once peak lower than one then one at the top",
            __FILE__, __LINE__);

        // The runner comes back down to the floor it left, from a jump taken on the
        // first frame and nothing after it.
        Game falling;
        Input landed;
        peakHeight(falling, 400, landed, { 0 });
        checkNear(falling.getRunner().getY(), World::FLOOR_TOP, 0.5f,
            "the runner lands back on the floor", __FILE__, __LINE__);

        // Landing refills the allowance, so the next jump after a double jump and a
        // landing is a full ground jump again, not a spent mid-air one.
        //
        // Measured from the landing onwards rather than from the start, because a
        // peak measured over the whole run would just be reporting the double jump
        // that came before. Rather than guess the frame the runner touches down on,
        // step until it is back on the floor and count that as the landing.
        Game refilled;
        Input refillInput;
        peakHeight(refilled, apexFrame + 1, refillInput, { 0, apexFrame });

        int framesToLand = 0;
        while (refilled.getRunner().getY() > World::FLOOR_TOP + 0.5f
            && framesToLand < 600)
        {
            refillInput.jumpsPressed = 0;
            refilled.update(STEP, refillInput);
            ++framesToLand;
        }

        check(framesToLand < 600, "the runner lands again after a double jump",
            __FILE__, __LINE__);

        const float afterRefill = peakHeight(refilled, 240, refillInput, { 0 });
        checkNear(afterRefill, expectedSingle,
            integrationShortfall(World::JUMP_SPEED) + 0.5f,
            "landing refills the jump allowance", __FILE__, __LINE__);
    }

    void testGameTimer()
    {
        beginTest("Game run timer");

        Game game;
        Input input;

        game.update(STEP, input);
        game.update(STEP, input);
        checkNear(game.getElapsedTime(), STEP * 2.0, 0.0001,
            "the timer accumulates simulated time", __FILE__, __LINE__);

        // A long frame is clamped, so the timer cannot be inflated by a stall. The
        // clamp is 0.05 seconds.
        const double before = game.getElapsedTime();
        game.update(10.0f, input);
        checkNear(static_cast<float>(game.getElapsedTime() - before), 0.05f, 0.0001,
            "a stalled frame is clamped to 50ms", __FILE__, __LINE__);

        // reset replays the level and keeps the timer: a level advance is progress
        // within one attempt, not a new one.
        const double kept = game.getElapsedTime();
        game.reset();
        checkNear(game.getElapsedTime(), kept, 0.0001,
            "reset keeps the run timer", __FILE__, __LINE__);
        check(game.getLevel() == World::FIRST_LEVEL, "reset keeps the level",
            __FILE__, __LINE__);
        checkNear(game.getRunner().getX(), World::START_X, 0.001f,
            "reset puts the runner back at the start", __FILE__, __LINE__);

        // restartRun is the death path: level one and the timer cleared.
        game.restartRun();
        checkNear(game.getElapsedTime(), 0.0, 0.0001,
            "restartRun clears the run timer", __FILE__, __LINE__);
        check(game.getLevel() == World::FIRST_LEVEL, "restartRun returns to level one",
            __FILE__, __LINE__);

        // A dead run stops simulating, so the timer cannot keep accruing behind
        // an end screen. Covered against the lava death test as well; here it is
        // only that update() on a live run keeps counting.
        Game alive;
        Input idle;
        const double beforeIdle = alive.getElapsedTime();
        alive.update(STEP, idle);
        checkNear(static_cast<float>(alive.getElapsedTime() - beforeIdle), STEP,
            0.0001, "a live run keeps counting", __FILE__, __LINE__);
    }

    void testGameLevelAdvance()
    {
        beginTest("Game level advance");

        Game game;

        // advanceLevel is guarded on having cleared the level, so calling it
        // mid-run cannot skip a level. This is the only part of the advance path
        // reachable from outside: finishing needs the runner 10000 units along,
        // there is no public way to set its position, and every route there goes
        // through level geometry that a fixed input sequence cannot survive on
        // purpose. See the note at the top of this file.
        game.advanceLevel();
        check(game.getLevel() == World::FIRST_LEVEL,
            "advanceLevel does nothing while the level is not cleared", __FILE__, __LINE__);

        // Repeated calls stay a no-op, rather than advancing one level per call
        // until they run off the end.
        game.advanceLevel();
        game.advanceLevel();
        check(game.getLevel() == World::FIRST_LEVEL,
            "repeated advanceLevel calls do not skip ahead", __FILE__, __LINE__);

        // A run that is not on the last level is not complete, however far it has
        // come or however long it has been running.
        game.reset();
        check(!game.isGameComplete(),
            "a run in progress is not the end of the game", __FILE__, __LINE__);

        // Distance is measured from the start point, so it reads zero on a fresh
        // level and is otherwise the runner's travel along x.
        checkNear(game.getDistanceTravelled(), 0.0, 0.001f,
            "distance is measured from the start of the level", __FILE__, __LINE__);

        Input input;
        input.right = true;
        for (int frame = 0; frame < 120; ++frame)
        {
            game.update(STEP, input);
        }
        checkNear(game.getDistanceTravelled(), World::MOVE_SPEED * 120.0f * STEP,
            1.0f, "distance follows the runner rightwards", __FILE__, __LINE__);

        input.right = false;
        input.left = true;
        for (int frame = 0; frame < 60; ++frame)
        {
            game.update(STEP, input);
        }
        checkNear(game.getDistanceTravelled(),
            World::MOVE_SPEED * 60.0f * STEP, 1.0f,
            "distance falls again when the runner comes back", __FILE__, __LINE__);
    }

    void testGameLavaDeath()
    {
        beginTest("Game lava death");

        // Run right off the end of the floor. The floor stops at LAVA_START_X and
        // there is nothing past it, so the runner falls into the lava.
        Game game;
        Input input;
        input.right = true;
        input.sprint = true;

        int frames = 0;
        while (!game.isDead() && frames < 20000)
        {
            game.update(STEP, input);
            ++frames;
        }

        check(game.isDead(), "running right off the end is fatal", __FILE__, __LINE__);
        check(game.getDeathCause() == DeathCause::Lava,
            "falling in lava reports the lava", __FILE__, __LINE__);

        // A dead run stops simulating, so the timer freezes behind the end screen.
        const double frozen = game.getElapsedTime();
        game.update(STEP, input);
        checkNear(game.getElapsedTime(), frozen, 0.0001,
            "the timer freezes once dead", __FILE__, __LINE__);

        // restartRun puts it back on level one with a clear timer.
        game.restartRun();
        check(!game.isDead(), "restartRun revives the runner", __FILE__, __LINE__);
        check(game.getLevel() == World::FIRST_LEVEL, "restartRun returns to level one",
            __FILE__, __LINE__);
        checkNear(game.getElapsedTime(), 0.0, 0.0001, "restartRun clears the timer",
            __FILE__, __LINE__);
    }

    void testGameSpikes()
    {
        beginTest("Game spike hazard");

        // The hazard is queried through Game::spikesHit, which is what checkSpikes
        // is built on. The runner's position cannot be set from outside Game, so
        // this drives the query with rectangles placed against the level's own
        // authored spikes rather than walking a runner onto them. The geometry
        // that has already been checked directly (spikeAt, overlapsTriangle) is
        // used to work out where those rectangles go, so a failure here means the
        // wiring or the hazard band is wrong, not the triangle maths.
        Game game;
        const std::vector<Platform>& platforms = game.getPlatforms();

        int spikedPlatforms = 0;
        int spikesTested = 0;

        for (size_t i = 0; i < platforms.size(); ++i)
        {
            const Platform& platform = platforms[i];
            const float top = platform.block.getY() + platform.block.getHeight();

            if (platform.spikes <= 0)
            {
                // A box sitting where a spike would stand, on a platform that has
                // none, is clear: the early-out for a plain platform.
                const World::Rect onTop = World::boundsOf(
                    platform.block.getX() + 4.0f, top,
                    World::RUNNER_SIZE, World::RUNNER_SIZE);
                check(!game.spikesHit(onTop),
                    "a platform with no spikes is not a hazard", __FILE__, __LINE__);
                continue;
            }

            ++spikedPlatforms;

            for (int spike = 0; spike < platform.spikes; ++spike)
            {
                const World::Triangle triangle = World::spikeAt(
                    platform.block.getX(), platform.block.getWidth(), top,
                    spike, platform.spikes, platform.spikeOffset);

                // A runner with its feet on the base, centred under the apex.
                const World::Rect onSpike = World::boundsOf(
                    triangle.cx - World::RUNNER_HALF_SIZE, top,
                    World::RUNNER_SIZE, World::RUNNER_SIZE);

                check(game.spikesHit(onSpike),
                    "a runner standing on a spike is hit", __FILE__, __LINE__);
                ++spikesTested;
            }

            // Consecutive spikes leave a gap wider than the runner, and the query
            // has to match the drawing by leaving it clear rather than merging the
            // pair into one wall.
            if (platform.spikes >= 2)
            {
                const World::Triangle first = World::spikeAt(
                    platform.block.getX(), platform.block.getWidth(), top,
                    0, platform.spikes, platform.spikeOffset);
                const World::Triangle second = World::spikeAt(
                    platform.block.getX(), platform.block.getWidth(), top,
                    1, platform.spikes, platform.spikeOffset);

                const float gapCentre = (first.bx + second.ax) * 0.5f;
                const World::Rect inGap = World::boundsOf(
                    gapCentre - World::RUNNER_HALF_SIZE, top,
                    World::RUNNER_SIZE, World::RUNNER_SIZE);

                check(!game.spikesHit(inGap),
                    "a runner in the gap between spikes is clear",
                    __FILE__, __LINE__);
            }
        }

        // The level is the fixture, so it has to actually contain spikes, or the
        // loop above would pass by never testing anything.
        check(spikedPlatforms > 0, "the level has spiked platforms",
            __FILE__, __LINE__);
        check(spikesTested > 0, "spikes were exercised", __FILE__, __LINE__);

        // Empty space is clear, so the query is not simply always true.
        check(!game.spikesHit(World::boundsOf(-500.0f, -500.0f, 10.0f, 10.0f)),
            "a rectangle in empty space is clear", __FILE__, __LINE__);
    }

    void testGameSpikeDeath()
    {
        beginTest("Game spike death");

        // The end-to-end path: place the runner on a real spike from the level's
        // own layout, run one frame, and check that checkSpikes recorded a spike
        // death. This reaches the kill() call that spikesHit alone does not, via
        // the placeRunner() test seam.
        Game game;
        const std::vector<Platform>& platforms = game.getPlatforms();

        // The first spike in the level, and the first platform without any, both
        // taken from the authored layout so the placement is on real geometry.
        bool foundSpike = false;
        float spikeCentreX = 0.0f;
        float spikeBaseY = 0.0f;

        bool foundPlain = false;
        float plainCentreX = 0.0f;
        float plainBaseY = 0.0f;

        for (size_t i = 0; i < platforms.size(); ++i)
        {
            const Platform& platform = platforms[i];
            const float top = platform.block.getY() + platform.block.getHeight();

            if (platform.spikes > 0 && !foundSpike)
            {
                const World::Triangle triangle = World::spikeAt(
                    platform.block.getX(), platform.block.getWidth(), top,
                    0, platform.spikes, platform.spikeOffset);
                spikeCentreX = triangle.cx;
                spikeBaseY = top;
                foundSpike = true;
            }
            else if (platform.spikes == 0 && !foundPlain)
            {
                plainCentreX
                    = platform.block.getX() + platform.block.getWidth() * 0.5f;
                plainBaseY = top;
                foundPlain = true;
            }
        }

        check(foundSpike, "the level has a spike to stand on", __FILE__, __LINE__);
        check(foundPlain, "the level has a plain platform to stand on",
            __FILE__, __LINE__);

        // Standing on the spike: one frame kills, and blames the spike.
        Game fatal;
        fatal.placeRunner(spikeCentreX, spikeBaseY);

        Input input;
        fatal.update(STEP, input);

        check(fatal.isDead(), "standing on a spike is fatal", __FILE__, __LINE__);
        check(fatal.getDeathCause() == DeathCause::Spike,
            "the death is attributed to the spike", __FILE__, __LINE__);

        // The same placement on a platform with no spikes survives, so the death
        // above is the spike rather than simply the placement.
        Game safe;
        safe.placeRunner(plainCentreX, plainBaseY);

        Input safeInput;
        safe.update(STEP, safeInput);

        check(!safe.isDead(), "a platform with no spikes is survivable",
            __FILE__, __LINE__);
    }

    void testGameClampsToView()
    {
        beginTest("Game walls");

        // The walls are fixed to the window rather than the world, so the runner
        // has to stay inside the view however long it runs in one direction.
        Game game;
        Input input;
        input.left = true;
        input.sprint = true;

        for (int frame = 0; frame < 600; ++frame)
        {
            game.update(STEP, input);
        }

        checkNear(game.getRunner().getX(), game.getCameraX() + World::WALL_INSET,
            0.5f, "the runner is held off the left wall", __FILE__, __LINE__);

        // The camera follows back the other way too, so running left does not leave
        // the runner off the left edge of the screen. Reaching the right wall is
        // deliberately not tested: it means crossing a level, and the lava at 800
        // is in the way of any fixed input sequence.
        check(game.getCameraX() <= game.getRunner().getX() + 0.5f,
            "the camera does not scroll past the runner", __FILE__, __LINE__);
        check(game.getCameraX() >= 0.0f,
            "the camera does not scroll left of the start of the level",
            __FILE__, __LINE__);

        // The runner stays inside the view for the whole of that, never pinned to
        // either edge of the window itself.
        check(game.getRunner().getX() >= game.getCameraX(),
            "the runner is inside the view", __FILE__, __LINE__);
        check(game.getRunner().getX()
                <= game.getCameraX() + World::VIEW_WIDTH,
            "the runner is inside the view the other way", __FILE__, __LINE__);
    }

    void testGameStaticLayout()
    {
        beginTest("Game levels are static");

        // A restart replays the identical level: same platforms, same spikes, same
        // offsets. This is the whole reason the layouts are read from files.
        Game game;
        std::vector<Platform> first = game.getPlatforms();

        game.restartRun();
        const std::vector<Platform>& second = game.getPlatforms();

        check(first.size() == second.size(), "a restart loads the same platform count",
            __FILE__, __LINE__);

        for (size_t i = 0; i < first.size() && i < second.size(); ++i)
        {
            checkNear(first[i].block.getX(), second[i].block.getX(), 0.001f,
                "same platform x after a restart", __FILE__, __LINE__);
            checkNear(first[i].block.getY(), second[i].block.getY(), 0.001f,
                "same platform y after a restart", __FILE__, __LINE__);
            checkNear(first[i].block.getWidth(), second[i].block.getWidth(), 0.001f,
                "same platform width after a restart", __FILE__, __LINE__);
            check(first[i].spikes == second[i].spikes,
                "same spike count after a restart", __FILE__, __LINE__);
            checkNear(first[i].spikeOffset, second[i].spikeOffset, 0.001f,
                "same spike offset after a restart", __FILE__, __LINE__);
        }
    }
}

int main()
{
    std::printf("RunningGame unit tests\n\n");

    testBoundsOf();
    testOverlapsRect();
    testSpikesThatFit();
    testSpikeAt();
    testOverlapsTriangle();

    testRunner();
    testBlock();
    testMap();
    testInput();
    testPlatform();

    testGameStartsOnLevelOne();
    testGameMoves();
    testGameJumpPhysics();
    testGameTimer();
    testGameLevelAdvance();
    testGameLavaDeath();
    testGameSpikes();
    testGameSpikeDeath();
    testGameClampsToView();
    testGameStaticLayout();

    std::printf("\n%d checks, %d failures\n", checksRun, failures);

    return failures == 0 ? 0 : 1;
}
