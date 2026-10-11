#include "Game.h"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

namespace World
{
    Rect boundsOf(float x, float y, float width, float height)
    {
        Rect rect;
        rect.left = x;
        rect.bottom = y;
        rect.right = x + width;
        rect.top = y + height;
        return rect;
    }

    int spikesThatFit(float platformWidth)
    {
        // One spike needs SPIKE_WIDTH of platform, and each further one needs
        // another SPIKE_PITCH in front of it. Zero on anything too narrow to hold
        // a single spike.
        if (platformWidth < SPIKE_WIDTH)
        {
            return 0;
        }

        return static_cast<int>((platformWidth - SPIKE_WIDTH) / SPIKE_PITCH) + 1;
    }

    Triangle spikeAt(float platformX, float platformWidth, float platformTop,
        int index, int count, float offset)
    {
        // The whole run is placed and then stepped through, rather than each spike
        // being placed on its own, so the gap between them stays even wherever the
        // run starts.
        const float runWidth
            = SPIKE_WIDTH + (static_cast<float>(count) - 1.0f) * SPIKE_PITCH;

        // Clamped into the platform: the upper bound so the last spike cannot hang
        // off the right edge, the lower so the first cannot start before the left
        // one. The loader caps `count` at what fits, so the run is never wider
        // than the platform and these two bounds cannot conflict.
        float start = offset;
        if (start > platformWidth - runWidth)
        {
            start = platformWidth - runWidth;
        }
        if (start < 0.0f)
        {
            start = 0.0f;
        }

        const float left
            = platformX + start + static_cast<float>(index) * SPIKE_PITCH;

        Triangle spike;
        spike.ax = left;
        spike.ay = platformTop;
        spike.bx = left + SPIKE_WIDTH;
        spike.by = platformTop;
        spike.cx = left + SPIKE_WIDTH * 0.5f;
        spike.cy = platformTop + SPIKE_HEIGHT;
        return spike;
    }

    bool overlapsTriangle(const Rect& box, const Triangle& triangle)
    {
        // Both shapes are convex, so they overlap exactly when some axis fails to
        // separate them. The rectangle contributes its two axes and the triangle
        // one normal per edge; nothing else can separate this pair.
        //
        // The rectangle's corners, as points, so they can be projected the same
        // way as the triangle's.
        const float boxX[4] = { box.left, box.right, box.left, box.right };
        const float boxY[4] = { box.bottom, box.bottom, box.top, box.top };

        const float triangleX[3] = { triangle.ax, triangle.bx, triangle.cx };
        const float triangleY[3] = { triangle.ay, triangle.by, triangle.cy };

        for (int axis = 0; axis < 5; ++axis)
        {
            float axisX = 0.0f;
            float axisY = 0.0f;

            if (axis == 0)
            {
                axisX = 1.0f;
            }
            else if (axis == 1)
            {
                axisY = 1.0f;
            }
            else
            {
                // The normal of the edge from vertex i to vertex j, i and j being
                // the two that are not the remaining vertex. Swapping the edge
                // direction's components negates it, which is the other of the two
                // possible normals and separates the pair no differently, so the
                // sign does not matter and neither does the length.
                const int i = axis - 2;
                const int j = (i + 1) % 3;
                axisX = -(triangleY[j] - triangleY[(j + 1) % 3]);
                axisY = triangleX[j] - triangleX[(j + 1) % 3];
            }

            // Degenerate normal, which happens only if a spike were drawn with two
            // of its corners in the same place. Nothing can be separated along it,
            // so testing it would wrongly call the pair apart.
            if (axisX == 0.0f && axisY == 0.0f)
            {
                continue;
            }

            // Extent of each shape along this axis, as the range of its projected
            // corners.
            float boxMin = 0.0f;
            float boxMax = 0.0f;
            for (int corner = 0; corner < 4; ++corner)
            {
                const float projection = boxX[corner] * axisX + boxY[corner] * axisY;
                if (corner == 0 || projection < boxMin)
                {
                    boxMin = projection;
                }
                if (corner == 0 || projection > boxMax)
                {
                    boxMax = projection;
                }
            }

            float spikeMin = 0.0f;
            float spikeMax = 0.0f;
            for (int corner = 0; corner < 3; ++corner)
            {
                const float projection
                    = triangleX[corner] * axisX + triangleY[corner] * axisY;
                if (corner == 0 || projection < spikeMin)
                {
                    spikeMin = projection;
                }
                if (corner == 0 || projection > spikeMax)
                {
                    spikeMax = projection;
                }
            }

            // Touching counts as separated, to match overlaps(). That is not just
            // consistency: two shapes sharing a positive area must overlap by a
            // positive amount along every axis, so an axis where the projections
            // merely meet proves they share at most an edge. The alternative, a
            // strict test, calls a zero-area graze a hit and kills a runner who
            // passed a spike without touching it.
            if (boxMax <= spikeMin || spikeMax <= boxMin)
            {
                return false;
            }
        }

        return true;
    }
}

Platform::Platform()
    : block()
    , spikes(0)
    , spikeOffset(0.0f)
{
}

Platform::Platform(float x, float y, float width, int spikeCount, float spikeStart)
    : block(x, y, width, World::PLATFORM_THICKNESS)
    , spikes(spikeCount)
    , spikeOffset(spikeStart)
{
}

Input::Input()
    : left(false)
    , right(false)
    , sprint(false)
    , jumpsPressed(0)
{
}

void Input::clear()
{
    left = false;
    right = false;
    sprint = false;
    jumpsPressed = 0;
}

Game::Game()
    : verticalVelocity(0.0f)
    , grounded(true)
    , sprinting(false)
    , dead(false)
    , finished(false)
    , deathCause(DeathCause::Lava)
    , airJumpsLeft(World::MAX_AIR_JUMPS)
    , cameraX(0.0f)
    , currentLevel(World::FIRST_LEVEL)
{
    restartRun();
}

void Game::restartRun()
{
    // Back to the first level with the timer cleared. This, and not reset(), is
    // what starts a run, because a run is only ever measured from level one.
    currentLevel = World::FIRST_LEVEL;
    elapsedTime = 0.0;

    reset();
}

void Game::reset()
{
    // Deliberately does not touch currentLevel or elapsedTime. This is "replay
    // the level I am on", and both callers that do want those changed set them
    // before getting here, so the timer survives a level advance instead of
    // being reset by whatever happens to restart the runner.

    runner.setX(World::START_X);
    runner.setY(World::FLOOR_TOP);
    runner.setVelocity(0.0f);

    verticalVelocity = 0.0f;
    grounded = true;
    sprinting = false;
    dead = false;
    finished = false;

    // Reset with the death flag. The cause is unread while alive, so this is only
    // so a stale one cannot show through if a later death leaves it unset.
    deathCause = DeathCause::Lava;

    // Standing on a platform with the full allowance available, so a fresh level
    // starts with the jump it should.
    airJumpsLeft = World::MAX_AIR_JUMPS;

    cameraX = 0.0f;

    // The level is a fixed layout read from its file, refilled on every reset,
    // so a restart replays the identical level.
    loadLevel(currentLevel);

    map.clear();

    // The safe floor, which stops exactly where the lava begins. Past that
    // there is deliberately no floor, so the runner falls in. Added after the
    // clear because the floor is not part of the level file.
    map.addBlock(Block(0.0f, 0.0f, World::LAVA_START_X, World::WALL_THICKNESS));

    for (size_t i = 0; i < levelPlatforms.size(); ++i)
    {
        map.addBlock(levelPlatforms[i].block);
    }
}

void Game::update(float dt, Input& input)
{
    // A run that has ended does not simulate. The renderer also stops calling
    // update once the runner is dead or finished, but guarding here keeps the
    // final state fixed, timer included, no matter what the caller does. A
    // caller that forgets to stop would otherwise keep accumulating time for a
    // run that is already over.
    if (dead || finished)
    {
        return;
    }

    // Track sprint state for the renderer. Recorded even when no direction key
    // is held, so the color reflects the key rather than the motion.
    sprinting = input.sprint;

    // Clamp dt so a stalled frame cannot tunnel the runner through the floor.
    if (dt > 0.05f)
    {
        dt = 0.05f;
    }

    // Run timer. Accumulated after the clamp so the displayed time matches the
    // physics actually simulated.
    elapsedTime += dt;

    applyHorizontalMovement(dt, input);
    updateCamera();

    // Clamp before vertical movement, not after. Collision resolves against the
    // runner's x, so clamping afterwards lets a frame detect at an out-of-bounds
    // x, miss the ground, and then fall too far to ever land again.
    clampToView();

    applyVerticalMovement(dt, input);
    checkLava();

    // After lava so a fall into the lava has already decided it, and either way
    // the flag is the same.
    checkSpikes();

    checkFinish();

    // The whole layout is loaded up front, so only pruning needs to run per
    // frame: it keeps the map from holding every platform in the level at once.
    pruneBehindCamera();
}

void Game::applyHorizontalMovement(float dt, const Input& input)
{
    // Direction of the requested move, 0 when neither key is held.
    float direction = 0.0f;
    if (input.left)
    {
        direction -= 1.0f;
    }
    if (input.right)
    {
        direction += 1.0f;
    }

    // Instant movement: velocity is set directly rather than ramped, and
    // releasing both keys stops the runner dead.
    const float speed = input.sprint ? World::SPRINT_SPEED : World::MOVE_SPEED;

    runner.setVelocity(direction * speed);

    // Integrate first, then push back out of anything the move ran into, so the
    // runner ends the frame standing against a wall rather than inside one.
    const float previousX = runner.getX();
    runner.update(dt);

    resolveSideCollision(previousX);
}

void Game::updateCamera()
{
    // Keep the runner at a fixed fraction across the view, so most of the
    // window shows the road ahead. The camera follows in both directions: the
    // walls are fixed to the window, so a camera that only advanced would turn
    // the left wall into a barrier that traps the runner.
    cameraX = runner.getX() - World::VIEW_WIDTH * World::CAMERA_LEAD;

    // The level is endless to the right but bounded to the left, so never
    // scroll back past the start.
    if (cameraX < 0.0f)
    {
        cameraX = 0.0f;
    }
}

void Game::clampToView()
{
    // The walls are fixed to the window, so the runner is confined to the view.
    // Normally the camera keeps the runner at CAMERA_LEAD and neither limit is
    // reached, but this guarantees it can never be drawn inside a wall even if
    // the camera is clamped at the start of the level.
    const float minX = cameraX + World::WALL_INSET;
    const float maxX = cameraX + World::VIEW_WIDTH - World::WALL_INSET;

    float x = runner.getX();
    float velocity = runner.getVelocity();

    if (x < minX)
    {
        x = minX;
        if (velocity < 0.0f)
        {
            velocity = 0.0f;
        }
    }
    else if (x > maxX)
    {
        x = maxX;
        if (velocity > 0.0f)
        {
            velocity = 0.0f;
        }
    }

    runner.setX(x);
    runner.setVelocity(velocity);
}

void Game::applyVerticalMovement(float dt, Input& input)
{
    // At most one jump per frame, from a single press. Deliberately not
    // "while there are presses left": spending two in one frame would grant the
    // ground jump and the mid-air jump without the runner ever leaving the
    // ground, which is not a double jump.
    if (input.jumpsPressed > 0)
    {
        // Spent whether or not it can be used. A press with no jump available is
        // dropped rather than kept, so mashing in mid-air does not bank a jump
        // that fires the instant the runner lands.
        --input.jumpsPressed;

        if (grounded)
        {
            verticalVelocity = World::JUMP_SPEED;
            grounded = false;

            // This fall starts here, so the mid-air allowance is full again. Done
            // on the jump rather than on landing so a jump taken from a ledge
            // still has its second jump available.
            airJumpsLeft = World::MAX_AIR_JUMPS;
        }
        else if (airJumpsLeft > 0)
        {
            --airJumpsLeft;

            // Set outright rather than added, so the second jump is worth the same
            // height wherever in the fall it is taken. That keeps the allowance
            // about being airborne rather than about timing.
            verticalVelocity = World::AIR_JUMP_SPEED;
        }
    }

    verticalVelocity += World::GRAVITY * dt;

    const float previousY = runner.getY();
    const float newY = previousY + verticalVelocity * dt;
    runner.setY(newY);

    // Falling off a ledge leaves nothing to stand on. Note this does not touch the
    // mid-air allowance: walking off a ledge is still a fall, and the runner should
    // get their jump.
    grounded = false;
    resolveGroundCollision(previousY);
}

// The runner's four corners as a rectangle. The runner's position is its feet
// and the centre of its box, matching how it is drawn: the box spans x -/+ half
// the size and y up to y + the size.
World::Rect Game::getRunnerBounds() const
{
    return World::boundsOf(
        runner.getX() - World::RUNNER_HALF_SIZE,
        runner.getY(),
        World::RUNNER_SIZE,
        World::RUNNER_SIZE);
}

void Game::resolveSideCollision(float previousX)
{
    const float half = World::RUNNER_HALF_SIZE;
    const World::Rect runnerRect = getRunnerBounds();

    for (size_t i = 0; i < map.getBlockCount(); ++i)
    {
        const Block& block = map.getBlock(i);
        const World::Rect rect = World::boundsOf(
            block.getX(), block.getY(), block.getWidth(), block.getHeight());

        // Only blocks the runner's box actually intersects. Corner-based, so a
        // platform the runner is standing on does not count: resting on a top
        // surface leaves the bottom corners exactly touching, not overlapping.
        if (!World::overlaps(runnerRect, rect))
        {
            continue;
        }

        // Resolve along x only, and only in the direction of travel, so the
        // runner is pushed out to the near face rather than teleported through
        // to the far one.
        const float previousLeft = previousX - half;
        const float previousRight = previousX + half;

        if (runner.getVelocity() > 0.0f)
        {
            // Moving right into the block's left face: stop just short of it.
            runner.setX(rect.left - half);
            runner.setVelocity(0.0f);
            return;
        }

        if (runner.getVelocity() < 0.0f)
        {
            // Moving left into the block's right face.
            runner.setX(rect.right + half);
            runner.setVelocity(0.0f);
            return;
        }

        // Stationary but intersecting, which happens when a platform is placed
        // on top of the runner. Push out toward whichever face is nearer, so
        // the runner is never left embedded in solid geometry.
        if (previousRight <= rect.left || previousLeft >= rect.right)
        {
            continue;
        }

        runner.setX(previousLeft >= rect.right ? rect.right + half : rect.left - half);
        return;
    }
}

void Game::resolveGroundCollision(float previousY)
{
    // The ceiling is an infinite band at a fixed height, so it is resolved
    // against a constant rather than a block. Without this the runner would
    // rise forever past the end of the level.
    if (verticalVelocity > 0.0f)
    {
        const float head = runner.getY() + World::RUNNER_SIZE;
        const float previousHead = previousY + World::RUNNER_SIZE;

        if (head >= World::CEILING_UNDERSIDE && previousHead <= World::CEILING_UNDERSIDE)
        {
            runner.setY(World::CEILING_UNDERSIDE - World::RUNNER_SIZE);
            verticalVelocity = 0.0f;
            return;
        }
    }

    const float runnerY = runner.getY();
    const World::Rect runnerRect = getRunnerBounds();

    for (size_t i = 0; i < map.getBlockCount(); ++i)
    {
        const Block& block = map.getBlock(i);
        const World::Rect rect = World::boundsOf(
            block.getX(), block.getY(), block.getWidth(), block.getHeight());

        // Corner-in-region test against the platform's bounds. Replaces the old
        // single-point x check, which ignored the width of the runner and let
        // its edges clip into a platform it was not really over.
        if (!World::overlaps(runnerRect, rect))
        {
            continue;
        }

        // When moving downward, land on top edges.
        if (verticalVelocity <= 0.0f)
        {
            // The previous feet position must have been at or above the top, so
            // a runner already below the surface cannot snap up onto it. This
            // is also what stops a fast fall tunnelling through a thin
            // platform: the runner starts above and ends overlapping, so the
            // crossing is caught.
            if (runnerY > rect.top || previousY < rect.top)
            {
                continue;
            }

            runner.setY(rect.top);
            verticalVelocity = 0.0f;
            grounded = true;

            // A landing ends the fall, so the mid-air allowance is full again for
            // the next one. Refilled here rather than anywhere else, so touching
            // down is the one thing that restores it.
            airJumpsLeft = World::MAX_AIR_JUMPS;
            return;
        }

        // When moving upward, hit the underside of the block if the runner's
        // head reaches it. The drawn box goes up to y + RUNNER_SIZE, so the
        // runner's head is at y + RUNNER_SIZE.
        const float runnerHead = runnerY + World::RUNNER_SIZE;
        const float previousHead = previousY + World::RUNNER_SIZE;

        if (runnerHead < rect.bottom || previousHead > rect.bottom)
        {
            continue;
        }

        // Stop upward motion at the block's bottom surface.
        runner.setY(rect.bottom - World::RUNNER_SIZE);
        verticalVelocity = 0.0f;
        return;
    }
}

void Game::kill(DeathCause cause)
{
    dead = true;
    deathCause = cause;
}

void Game::checkLava()
{
    // The floor stops at LAVA_START_X, so anything that is past that point and
    // below the lava surface has fallen in. The surface sits at floor height,
    // so simply being past the edge and below the floor counts.
    if (runner.getX() >= World::LAVA_START_X && runner.getY() < World::LAVA_TOP)
    {
        kill(DeathCause::Lava);
    }
}

void Game::checkSpikes()
{
    // The detection itself lives in spikesHit, so it can be tested against the
    // level's real geometry; there is no public way to position the runner, and
    // keeping the query separate is what makes it reachable. Here it is only the
    // death the contact causes.
    if (spikesHit(getRunnerBounds()))
    {
        kill(DeathCause::Spike);
    }
}

bool Game::spikesHit(const World::Rect& bounds) const
{
    // Tested against the level's own layout rather than the map's. The map drops
    // platforms behind the camera, and a hazard has to stay live wherever it was
    // authored: a spike the runner lands on after running back a little would
    // otherwise be missing from the map's copy at the moment it matters.
    for (size_t i = 0; i < levelPlatforms.size(); ++i)
    {
        const Platform& platform = levelPlatforms[i];

        if (platform.spikes <= 0)
        {
            continue;
        }

        // The spikes stand on the top surface, so that is the height they are
        // built from rather than the platform's bottom.
        const float platformTop
            = platform.block.getY() + platform.block.getHeight();

        // Cheap rectangle reject first, over the band the spikes actually occupy:
        // the platform's width, and the SPIKE_HEIGHT above its top surface.
        //
        // Deliberately not the platform's own rectangle. The spikes are all above
        // the top surface, and the runner standing on a platform has its bottom
        // corners exactly on that surface, which overlaps() counts as clear. Using
        // the platform's rectangle would reject every contact from someone actually
        // standing on the platform, which is the only way to reach a spike at all.
        const World::Rect hazardBounds = World::boundsOf(
            platform.block.getX(), platformTop, platform.block.getWidth(),
            World::SPIKE_HEIGHT);

        if (!World::overlaps(bounds, hazardBounds))
        {
            continue;
        }

        for (int spike = 0; spike < platform.spikes; ++spike)
        {
            const World::Triangle triangle = World::spikeAt(
                platform.block.getX(), platform.block.getWidth(), platformTop,
                spike, platform.spikes, platform.spikeOffset);

            if (World::overlapsTriangle(bounds, triangle))
            {
                // Fatal on the same terms as the lava: the run is measured end to
                // end, so touching a spike voids the attempt rather than costing a
                // platform.
                return true;
            }
        }
    }

    return false;
}

void Game::placeRunner(float x, float y)
{
    // A test seam, not part of the simulation: see the header. Motion is cleared
    // so the placement is a still point rather than the tail of whatever the last
    // frame left behind, and nothing else is disturbed.
    runner.setX(x);
    runner.setY(y);
    runner.setVelocity(0.0f);
    verticalVelocity = 0.0f;
}

void Game::checkFinish()
{
    // The level ends at a fixed distance rather than running forever. Crossing
    // the finish line wins the run, whatever height the runner happens to be at.
    if (getDistanceTravelled() >= World::FINISH_DISTANCE)
    {
        finished = true;
    }
}

void Game::loadLevel(int level)
{
    const std::string name = levelFilename(level);
    std::ifstream file(name.c_str());

    // No generator to fall back on, so a level that is not there is fatal.
    // Levels are data; running a level that was never authored would leave the
    // runner crossing lava with nothing to jump to.
    if (!file.is_open())
    {
        failMissingLevel(name.c_str(), "cannot be opened");
    }

    std::vector<Platform> loaded;

    // Parsed line by line rather than with stream extraction alone, so a
    // malformed or hand-edited file can be skipped instead of silently
    // swallowing the rest of the level.
    std::string line;
    while (std::getline(file, line))
    {
        // Tolerate CRLF, which is what a file edited on Windows will have.
        if (!line.empty() && line[line.size() - 1] == '\r')
        {
            line.erase(line.size() - 1);
        }

        // Skip blanks and comments, so the file can be annotated.
        const size_t first = line.find_first_not_of(" \t");
        if (first == std::string::npos || line[first] == '#')
        {
            continue;
        }

        float x = 0.0f;
        float y = 0.0f;
        float width = 0.0f;

        // Optional fourth field: how many spikes to stand on the platform. Absent
        // means none, which keeps every existing line valid as written.
        int spikes = 0;

        // Optional fifth field: how far in from the platform's left edge the first
        // spike's left edge sits. Absent means flush with the left edge, which is
        // where a runner arriving from the left meets the platform first.
        float spikeOffset = 0.0f;

        char trailing = 0;
        const int matched = std::sscanf(line.c_str(), "%f %f %f %d %f %c",
            &x, &y, &width, &spikes, &spikeOffset, &trailing);
        if (matched != 3 && matched != 4 && matched != 5)
        {
            continue;
        }

        // Reject values that would break the level: a non-positive width has no
        // surface to stand on, and anything outside the world is unreachable or
        // off screen.
        if (width <= 0.0f || y < 0.0f || y + World::PLATFORM_THICKNESS > World::CEILING_UNDERSIDE)
        {
            continue;
        }

        // A negative count is a typo rather than a request, and one meant as "all
        // the spikes this platform can hold" is not what the field means: it is
        // how many, so a file asking for more than fits gets the most that fit.
        if (spikes < 0)
        {
            spikes = 0;
        }

        const int fitting = World::spikesThatFit(width);
        if (spikes > fitting)
        {
            spikes = fitting;
        }

        loaded.push_back(Platform(x, y, width, spikes, spikeOffset));
    }

    // Nothing usable in the file is as broken as nothing in it: a level with no
    // platforms in it cannot be played.
    if (loaded.empty())
    {
        failMissingLevel(name.c_str(), "contains no usable platforms");
    }

levelPlatforms.swap(loaded);
}

// Reports an unusable level and kills the process. There is nothing sensible to
// carry on with: the runner has no floor past the lava, so a level that failed
// to load would just run it into a death it cannot do anything about, and
// generating one instead would hand the player a layout nobody authored.
void Game::failMissingLevel(const char* name, const char* reason) const
{
    std::fprintf(stderr, "RunningGame: level file %s %s.\n", name, reason);
    std::fflush(stderr);

    // abort() rather than exit(): this is a broken install, and a non-zero code
    // with no message would be much harder to trace back to a missing file.
    std::abort();
}

std::string Game::levelFilename(int level)
{
    std::string name("level");
    name += std::to_string(level);
    name += ".txt";
    return name;
}

void Game::advanceLevel()
{
    if (!finished)
    {
        // Only a cleared level moves on. Death is restartRun()'s job, so calling
        // this mid-run is a no-op rather than a surprise skip.
        return;
    }

    if (currentLevel < World::LAST_LEVEL)
    {
        ++currentLevel;
    }
    else
    {
        // The last level has been cleared, so there is nothing left to advance
        // to. Wrap back to the first, which is what makes TAB on the final
        // screen start a fresh run through every level again.
        currentLevel = World::FIRST_LEVEL;

        // Wrapping ends the attempt as surely as dying did: a completed run is
        // timed, so the next one starts from zero. Done here rather than in
        // restartRun() so the ordinary advance keeps its time.
        elapsedTime = 0.0;
    }

    // reset() loads the new level's own layout from currentLevel, so what to load
    // is decided by the level number.
    //
    // The timer is deliberately left alone: one attempt is timed from the first
    // level to the last, and clearing a level is progress within it rather than a
    // new attempt.
    reset();
}

int Game::getLevel() const
{
    return currentLevel;
}

bool Game::isGameComplete() const
{
    return finished && currentLevel == World::LAST_LEVEL;
}

void Game::pruneBehindCamera()
{
    // Discard platforms the runner has left well behind, so the map does not
    // grow without bound. The margin keeps a platform the runner is standing on
    // alive while they are still near it. The floor is skipped: it is a single
    // block anchored at the start and must never be removed.
    const float cutoff = cameraX - World::PRUNE_MARGIN;

    for (size_t i = map.getBlockCount(); i > 0; --i)
    {
        const size_t index = i - 1;
        const Block& block = map.getBlock(index);

        if (block.getX() + block.getWidth() > cutoff)
        {
            continue;
        }

        if (block.getY() < 0.01f)
        {
            continue;
        }

        map.removeBlock(index);
    }
}

const Runner& Game::getRunner() const
{
    return runner;
}

const std::vector<Platform>& Game::getPlatforms() const
{
    return levelPlatforms;
}

const Map& Game::getMap() const
{
    return map;
}

bool Game::isGrounded() const
{
    return grounded;
}

bool Game::isSprinting() const
{
    return sprinting;
}

bool Game::isFinished() const
{
    return finished;
}

bool Game::isDead() const
{
    return dead;
}

DeathCause Game::getDeathCause() const
{
    return deathCause;
}

float Game::getCameraX() const
{
    return cameraX;
}

float Game::getDistanceTravelled() const
{
    return runner.getX() - World::START_X;
}

double Game::getElapsedTime() const
{
    return elapsedTime;
}
