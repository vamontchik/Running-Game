#ifndef GAME_H
#define GAME_H

#include <string>
#include <vector>

#include "Map.h"
#include "Runner.h"

// World-space (y-up) constants and helpers shared by the simulation and the
// renderer. The level scrolls right indefinitely, so world x grows without
// bound while the visible window stays a fixed size.
namespace World
{
    // Applies gravity: world y-up means down is negative.
    const float GRAVITY = -1800.0f;          // units per second squared

    // Horizontal movement tuning. Movement is instant: pressing a direction
    // sets the velocity outright, and releasing sets it to zero.
    const float MOVE_SPEED = 260.0f;         // units per second at full speed

    // Speed while the sprint key is held. Instant movement means this swaps in
    // the moment shift goes down, with no ramp.
    const float SPRINT_SPEED = 520.0f;

    // Jump tuning. JUMP_SPEED is an initial upward velocity, in units/second.
    // Peak height is JUMP_SPEED^2 / (2 * -GRAVITY), so this is the single
    // knob controlling jump height.
    const float JUMP_SPEED = 900.0f;

    // One jump is allowed per fall, on top of the jump off the ground. So 1 here
    // is a double jump, 2 a triple, and so on.
    const int MAX_AIR_JUMPS = 1;

    // The mid-air jump's own upward velocity. Deliberately weaker than the ground
    // jump: the second jump is for gaining height back or saving a fall, not for
    // replacing a well-timed sprint jump over a wide gap. At 0.85 of the ground
    // jump it still clears 162 units from the apex on its own, which is most of
    // a platform.
    const float AIR_JUMP_SPEED = JUMP_SPEED * 0.85f;

    // The runner is a single point in world space, drawn as a small box. The
    // point is the box's center, so the drawn box extends half its size to
    // either side.
    const float RUNNER_SIZE = 16.0f;
    const float RUNNER_HALF_SIZE = RUNNER_SIZE * 0.5f;

    // Size of the visible window in world units, and the window's height. The
    // window is sized so its client area matches, which keeps one world unit
    // equal to one pixel.
    const float VIEW_WIDTH = 800.0f;
    const float VIEW_HEIGHT = 600.0f;

    // The finish line is reached when the runner travels this far from the
    // start. At that point the level ends and can be reset.
    const float FINISH_DISTANCE = 10000.0f;

    // Thickness of the level's border walls, drawn like any other block.
    const float WALL_THICKNESS = 8.0f;

    // The left and right walls are fixed to the window rather than the world,
    // so they never scroll. The runner is kept this far inside them.
    const float WALL_INSET = WALL_THICKNESS + RUNNER_HALF_SIZE;

    // Where the runner sits horizontally within the view, as a fraction from
    // the left. The rest of the view is the road ahead.
    const float CAMERA_LEAD = 0.35f;

    // Starting world position, on the floor.
    const float START_X = 120.0f;

    // World x of the finish line, derived from the distance travelled.
    const float FINISH_X = START_X + FINISH_DISTANCE;

    // Landmark markers. Each landmark gets a vertical post spanning the level,
    // plus a label near the ceiling. Sized in world units, which are pixels
    // here, since the view is 1:1.
    const float MARKER_HALF_WIDTH = 3.0f;
    const float MARKER_LABEL_TOP = 12.0f;

    // The safe floor runs from the start to LAVA_START_X, at this height. Past
    // LAVA_START_X there is no floor at all: the runner falls into lava.
    const float FLOOR_TOP = WALL_THICKNESS;

    // Lava begins immediately to the right of the starting screen, so its
    // surface starts exactly where the solid floor ends. It is at the same
    // height as the floor, making the handover seamless.
    const float LAVA_START_X = VIEW_WIDTH;
    const float LAVA_TOP = FLOOR_TOP;

    // The ceiling is an infinite band at a fixed height. It is not a map block:
    // being unbounded, it is checked directly against a constant and drawn in
    // screen space, so it needs no generation and can never be pruned away.
    const float CEILING_UNDERSIDE = VIEW_HEIGHT - WALL_THICKNESS;

    // How far behind the camera to start discarding platforms, so one never
    // vanishes while the runner is still near it.
    const float PRUNE_MARGIN = 400.0f;

    // Every platform is this thick. Not stored in the level files, since it is
    // the same for every platform in every level and a per-line field would only
    // give the file a second way to disagree with itself.
    const float PLATFORM_THICKNESS = 20.0f;

    // Levels are stored one per file, named "levelN.txt", so each is editable on
    // its own and can be hand-authored without touching the others. Clearing
    // the last one ends the run.
    const int FIRST_LEVEL = 1;
    const int LAST_LEVEL = 4;

    // Axis-aligned bounds of a rectangle given by its bottom-left corner.
    struct Rect
    {
        float left;
        float bottom;
        float right;
        float top;
    };

    // A triangle given by its three corners.
    struct Triangle
    {
        float ax;
        float ay;
        float bx;
        float by;
        float cx;
        float cy;
    };

    // Builds bounds for a block stored as (bottom-left, size).
    Rect boundsOf(float x, float y, float width, float height);

    // True when the two rectangles share any area, which is the case exactly
    // when one of the first rectangle's corners lies inside the second. Edges
    // that merely touch do not count: a runner resting on a platform's top
    // surface has its bottom corners exactly on that surface, and treating that
    // as an overlap would leave it permanently stuck.
    inline bool overlaps(const Rect& a, const Rect& b)
    {
        return a.left < b.right && a.right > b.left
            && a.bottom < b.top && a.top > b.bottom;
    }

    // Spikes: the white triangles that stand on a platform's top surface and kill
    // on contact.
    const float SPIKE_WIDTH = 20.0f;
    const float SPIKE_HEIGHT = 16.0f;

    // How far apart consecutive spikes stand, measured left edge to left edge.
    // Wider than a spike, so a run of them leaves a landing gap between each pair
    // rather than merging into a single wall.
    const float SPIKE_PITCH = 40.0f;

    // The most spikes that fit on a platform at SPIKE_PITCH, starting anywhere.
    // Zero on a platform too narrow to hold one, which is how a level file asking
    // for spikes on a sliver gets none rather than a triangle hanging off both
    // sides.
    int spikesThatFit(float platformWidth);

    // The corners of spike `index` of `count`, the run beginning `offset` units in
    // from the platform's left edge.
    //
    // `offset` is what stops every spiked platform looking identical. A level
    // author places the run by hand, so one platform can have its spikes flush
    // against the edge the runner lands on and the next can have them waiting at
    // the far end.
    //
    // A run that would hang off the right is shifted left to end flush inside, and
    // the first is never placed before the platform's edge, so a spike is always
    // wholly on the platform it belongs to.
    //
    // Both the simulation and the renderer call this rather than each working it
    // out, so the triangle that is drawn and the triangle that kills cannot drift
    // apart.
    Triangle spikeAt(float platformX, float platformWidth, float platformTop,
        int index, int count, float offset);

    // True when the rectangle and the triangle share any area.
    //
    // Exact, rather than a bounding box around the triangle. A box around a spike
    // is much larger than the spike itself near its apex, and that surplus area is
    // exactly where a player would believe they had cleared it and had not.
    //
    // Both shapes are convex, so testing every candidate axis is enough: if none
    // of them separates the pair, the pair overlaps. The only axes that can
    // separate a rectangle and a triangle are the rectangle's two and one normal
    // per triangle edge.
    //
    // Sharing a boundary without sharing area does not count, so a graze is clear.
    // That matches overlaps() and keeps the two consistent about what touching
    // means; the runner can stand with a corner exactly on a spike's base without
    // that being a hit.
    bool overlapsTriangle(const Rect& box, const Triangle& triangle);
}

// One line of a level file: a platform and the spikes standing on it.
struct Platform
{
    Block block;
    int spikes;

    // Distance from the platform's left edge to the first spike's left edge. Held
    // per platform rather than derived, so where a run sits on its platform is
    // authored, not computed from the platform's width.
    float spikeOffset;

    Platform();
    Platform(float x, float y, float width, int spikeCount, float spikeStart);
};

// Player input for one frame.
struct Input
{
    // Jump presses waiting to be spent, rather than a single flag. A double jump
    // is two presses, and two presses arriving inside one frame would otherwise
    // collapse into one flag and cost the player the second jump: easy to hit on a
    // slow frame, or on a quick double tap. Counted so none are lost.
    //
    // Capped, so mashing the key cannot bank presses for later.
    static const int MAX_PENDING_JUMPS = 2;

    Input();

    bool left;
    bool right;
    bool sprint;

    int jumpsPressed;

    void clear();
};

// What killed the runner, which the death screen names. Both are fatal in exactly
// the same way: the run is over either way, and both restart it the same. Only the
// wording differs, so this is a label on the death rather than a second kind of
// death.
enum class DeathCause
{
    Lava,
    Spike
};

class Game
{
public:
    Game();

    // Restarts the current level from the beginning, keeping both the level
    // number and the run timer as they are. Level changes and the run restart
    // are the two callers that need to touch those, so they set them before
    // calling this rather than this resetting them on everyone's behalf.
    void reset();

    // Starts the whole run over: back to the first level with the timer at zero.
    //
    // This is the death path. A run is timed end to end across every level, so
    // falling in the lava ends the attempt rather than costing one level: the
    // time already spent is void and the next attempt is a clean one from level
    // one.
    void restartRun();

    // Moves on to the next level and starts it. Does nothing unless the current
    // level was actually cleared, so calling it mid-run cannot skip a level.
    //
    // The run timer keeps counting: one attempt is measured from the first level
    // to the last, and each clear is part of the same attempt.
    void advanceLevel();

    // Advances simulation by dt seconds using the given input.
    //
    // `input` is not const because a jump press is spent by the frame that acts on
    // it. At most one jump is granted per frame, and a frame that cannot jump
    // discards the press rather than banking it, so holding or mashing the key
    // cannot end in an automatic jump on the next landing.
    void update(float dt, Input& input);

    const Runner& getRunner() const;
    const Map& getMap() const;

    // The level's layout exactly as authored, spikes included.
    //
    // Not the same as getMap(): the map holds only what is still in view, since
    // platforms behind the camera are pruned, whereas a hazard has to be testable
    // wherever it was authored. Held separately so the two cannot be confused.
    const std::vector<Platform>& getPlatforms() const;

    // True when a spike in the current level's authored layout overlaps `bounds`.
    //
    // Checked against the level's own platforms rather than the map, for the same
    // reason getPlatforms() explains: a hazard has to be live wherever it was
    // authored, and the map prunes what is behind the camera. The detection is
    // separated from the death it causes so the hazard can be tested against the
    // level's real geometry, since nothing public can position the runner.
    bool spikesHit(const World::Rect& bounds) const;

    // Test seam. Moves the runner to (x, y) in world space and clears its motion,
    // so a test can drive the hazard checks from a chosen point in a level. The
    // simulation never calls this: the runner's position is otherwise only ever
    // set by reset() and by the physics. It is named for its one purpose so it
    // cannot be mistaken for part of the game's own API.
    void placeRunner(float x, float y);

    // True while the runner is standing on something.
    bool isGrounded() const;

    // True while the sprint key is held. Drives the runner's color.
    bool isSprinting() const;

    // True once the runner has reached the finish line.
    bool isFinished() const;

    // Which level is being played, 1-based, and the highest one that exists.
    // The finish of the last level ends the game rather than advancing.
    int getLevel() const;

    // True once every level has been cleared, which is the true end of a run.
    // Reached only by finishing the last level; a finish on any other level is
    // a level clear, not the end.
    bool isGameComplete() const;

    // True once the runner has fallen into the lava. The caller is expected to
    // call restartRun() to start over.
    bool isDead() const;

    // Which hazard ended the run, for the death screen's wording. Only meaningful
    // while isDead() is true; Lava otherwise, since that is what ends a run first.
    DeathCause getDeathCause() const;

    // World x of the view's left edge. The renderer subtracts this to scroll.
    float getCameraX() const;

    // How far the runner has travelled from the start.
    float getDistanceTravelled() const;

    // Seconds of simulated time in the current attempt. Runs from the start of
    // the first level through to the finish of the last, so it does not reset
    // when a level is cleared. Frozen while dead, since update() is not called
    // between death and restart.
    double getElapsedTime() const;

private:
    // Accelerates the runner horizontally toward MOVE_SPEED.
    void applyHorizontalMovement(float dt, const Input& input);
    void updateCamera();
    void clampToView();

    // The runner's four corners as a rectangle, for corner-in-region tests
    // against platforms.
    World::Rect getRunnerBounds() const;

    // Horizontal motion, resolved against platform sides as well as the view.
    void resolveSideCollision(float previousX);

    // Vertical motion, including jumping and landing. Spends a pending jump press.
    void applyVerticalMovement(float dt, Input& input);
    void resolveGroundCollision(float previousY);

    // Records a fatal hazard contact: sets the death flag and which hazard caused
    // it, in one place, so a new hazard cannot set the flag without also saying
    // what it was.
    void kill(DeathCause cause);

    // Lava is a hazard, not a platform: falling past its surface kills.
    void checkLava();

    // Touching a spike kills, the same as the lava does.
    //
    // Checked against the level's own layout rather than the map's, because a
    // hazard has to be live wherever it was authored and the map prunes what is
    // behind the camera.
    void checkSpikes();

    // Crossing the finish distance ends the level.
    void checkFinish();

    // Replaces the platform list from the file for `level`, one platform per
    // line as "x y width", optionally followed by a spike count and a spike
    // offset: "x y width spikes" or "x y width spikes offset", where `offset` is
    // how far in from the platform's left edge the first spike sits.
    //
    // A level is data, not something the game makes up: there is no generator
    // and no fallback. A missing or unusable file is a broken install rather
    // than a missing feature, so it aborts with the reason instead of starting
    // the runner over a level that is not there.
    void loadLevel(int level);

    // Reports an unusable level file and aborts. Aborting rather than returning
    // a failure is deliberate: nothing the game can do produces a playable
    // level, and carrying on would put the runner in front of lava with no way
    // to cross it.
    void failMissingLevel(const char* name, const char* reason) const;

    // File name for a level, "levelN.txt". Built by hand rather than formatted,
    // so the level name is always produced the same way in every caller.
    static std::string levelFilename(int level);

    void pruneBehindCamera();

    Runner runner;
    Map map;

    // Upward velocity, in units per second. Negative while falling.
    float verticalVelocity;

    bool grounded;
    bool sprinting;
    bool dead;
    bool finished;

    // What set `dead`. Kept alongside the flag rather than derived from it, so the
    // death screen can say which hazard without inspecting the level.
    DeathCause deathCause;

    // Mid-air jumps still available in the current fall. Refilled on every landing
    // and on every ground jump, spent one per mid-air jump, and reset with the
    // level. Counted rather than a single spent flag so the allowance is one
    // number in World rather than a limit baked into the branching here.
    int airJumpsLeft;

    float cameraX;

    // Seconds of play in the current attempt, accumulated in update().
    //
    // Spans every level rather than resetting per level: it starts at zero on
    // level one and stops on the finish of the last, so the whole run has one
    // time. Cleared only by restartRun(), and by wrapping past the last level.
    double elapsedTime;

    // The level's layout, held in memory and replayed into the map on every
    // reset. Refilled from the level file on each reset, so a restart replays
    // the identical level.
    //
    // The spikes ride along with each platform rather than in a parallel array, so
    // a platform and its hazard cannot be separated by an editing slip.
    std::vector<Platform> levelPlatforms;

    // Which level is being played, 1-based. Holds a value in
    // [FIRST_LEVEL, LAST_LEVEL]; advancing past the last is not allowed, since
    // clearing the final level ends the game instead.
    int currentLevel;
};

#endif // GAME_H
