#ifndef RENDERER_H
#define RENDERER_H

#include <windows.h>

#include <map>

#include "Autoplay.h"
#include "Block.h"
#include "Game.h"
#include "Runner.h"

// Converts world coordinates (y-up) to window pixels (y-down) and draws the
// game objects with GDI. The left and right walls are fixed to the window
// rather than the world, so they never scroll. No third-party dependencies.
class Renderer
{
public:
    Renderer();
    ~Renderer();

    // Registers the window class. Call once before create().
    bool registerClass();

    // Creates the window. Returns false on failure.
    bool create(const char* title, int width, int height);

    // Runs the message loop until the window closes.
    int run();

    // Starts watch mode before the loop begins, as if P had been pressed: works
    // out a route for the first level and starts playing it. Used by the --watch
    // launch option, so the demo can be started without touching the keyboard.
    void startWatchMode();

private:
    // Which screen the window is showing.
    //
    // The menu and the pause overlay are the two states the simulation has no
    // opinion about: both exist around a run rather than inside it, and the game
    // itself sits idle for the whole of each.
    enum class Screen
    {
        Menu,

        // A run in progress: the level, updating.
        Playing,

        // A run in progress, frozen. Distinct from the end screens, which are not
        // a live run at all: escaping from one of those is not a thing.
        Paused
    };

    // The clickable buttons, which are what the mouse drives. Kept as an enum
    // rather than screen coordinates so a click resolves to an intent that the
    // game loop can act on, instead of being handled where the pointer happens
    // to be.
    enum ButtonId
    {
        ButtonNone = -1,

        // Menu: start a fresh run from level one.
        ButtonStartGame,

        // Paused: carry on with the same run, timer and all.
        ButtonResume,

        // Paused and game complete: back to level one with the timer cleared.
        // One id for both, since it is the same action either way.
        ButtonRestartRun,

        // Paused and game complete: back to the menu.
        ButtonMainMenu
    };

    static LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    LRESULT handleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    void paint(HDC dc);

    // Fills a rectangle with a solid brush from the cache rather than one created
    // and destroyed for the call, since every colour this draws with is a fixed
    // constant and the same handful of them recur every frame.
    void fillRect(HDC dc, int left, int top, int width, int height, COLORREF color);

    // The cached solid brush, and a one-pixel pen, for `color`. Each is created
    // the first time it is asked for and held until shutdown.
    HBRUSH brushFor(COLORREF color);
    HPEN penFor(COLORREF color);

    // Frees every cached GDI object: the brushes, the pens and the UI fonts.
    // Called from the destructor.
    void releaseGdiObjects();

    void paintRunner(HDC dc, const Runner& runner);
    void paintBlock(HDC dc, const Block& block);

    // The white spike triangles standing on the platforms that ask for them.
    void paintSpikes(HDC dc);

    // The menu, drawn instead of the level: there is no run in progress to show.
    void paintMenu(HDC dc, int clientWidth, int clientHeight);

    // The pause overlay: the frozen level dimmed, with the resume, restart and
    // exit buttons over it. `pixels` is the frame buffer's own bits, so the dim
    // can be applied to the level that is already drawn rather than painted over
    // the top of it.
    void paintPaused(HDC dc, int clientWidth, int clientHeight, void* pixels);

    // A button as a filled panel with a border and a centred label. Hovered is
    // what makes it read as clickable, since the window has no real controls in
    // it and no system cursor change to rely on alone.
    void paintButton(HDC dc, const RECT& bounds, const char* label, bool hovered);

    // Draws `text` centred inside `bounds`, measuring it rather than assuming a
    // fixed character width, and puts `font` back before returning.
    void drawCenteredText(HDC dc, const RECT& bounds, const char* text,
        COLORREF color, HFONT font);

    // Buttons for the current screen, in screen pixels. Laid out from the client
    // size on demand rather than stored, so they cannot drift out of step with
    // the window. None are visible outside the menu, the pause overlay and the
    // game-complete screen.
    RECT menuStartButton() const;

    // Three stacked buttons down the middle of the pause overlay.
    RECT pauseButton(ButtonId id) const;

    // Two side-by-side buttons under the end-of-game headline.
    RECT completeButton(ButtonId id) const;

    // The button at a client-space point, or ButtonNone. Only considers the ones
    // the current screen actually shows, so an invisible button is not clickable.
    ButtonId buttonAt(int x, int y) const;

    // Recomputes which button the pointer is over, repainting only when that
    // answer changes.
    void updateHover(int x, int y);

    // Switches screens. Clears the hovered button on the way, since the layouts
    // differ and the last one lit would otherwise stay lit on a screen where that
    // button does not exist, along with the hand cursor that goes with it.
    void setScreen(Screen next);

    // True for the block the runner spawns standing on, which is tinted green.
    bool isSpawnBlock(const Block& block) const;
    void paintHud(HDC dc, double fps);

    // Creates the off-screen frame buffer if it is missing or the wrong size,
    // and otherwise reuses the one already held. The buffer is what paint() draws
    // into and what the pause overlay dims, and it is kept between frames rather
    // than rebuilt in every one: its size and format never change, since the
    // window is not resizable.
    void ensureBuffer(HDC dc, int clientWidth, int clientHeight);

    // Destroys the cached frame buffer, returning its GDI objects.
    void releaseBuffer();

    void paintGameOver(HDC dc, int clientWidth, int clientHeight);
    void paintFinished(HDC dc, int clientWidth, int clientHeight);

    // Shared wording-free body for both end-of-run screens, which differ only in
    // their headline and color.
    void paintEndScreen(HDC dc, int clientWidth, int clientHeight,
        const char* message, COLORREF color);

    // The two window-anchored walls, drawn in screen space.
    void paintWalls(HDC dc);

    // The start and finish markers: a vertical post at each landmark, labelled,
    // so the origin of the run and the end of the level are both readable on
    // screen rather than only implied by distance travelled.
    void paintMarker(HDC dc, float worldX, COLORREF color, const char* label);
    void paintMarkers(HDC dc);

    // Lava surface, drawn from its start to the right edge of the view.
    void paintLava(HDC dc, int clientWidth);

    // Translates arrow / WASD messages into the Input the game reads. `repeat` is
    // true for a key auto-repeat message, which is not a fresh press and must not
    // be counted as one.
    void updateInput(UINT message, WPARAM wParam, bool repeat);

    // Starts or stops watch mode. Starting works out a route for the level from
    // its first frame, so the run being watched is a clean one, and turns the
    // screen over to it; stopping hands control back to the player where the run
    // currently is.
    void toggleAutopilot();

    // Works out a route for the level `game` is currently standing at the start
    // of, and rewinds playback to it. The game must already be on the first frame
    // of the level. Returns false, leaving watch mode off, if no route is found.
    bool planCurrentLevel();

    // Advances a watched run by whatever whole fixed steps fit into the elapsed
    // time, using the route's own frame numbers, and loops the run once it ends.
    // The fixed step is what keeps the played frames identical to the planned
    // ones; the elapsed time only sets how fast they are watched.
    void advanceAutopilot(double dt);

    // Moves past an end-of-run screen: next level on a clear, a fresh run on a
    // death. Set by Tab, Enter or Caps, none of which are pressed during play, so
    // the key that ended the run cannot also restart it.
    bool restartRequested;

    // Toggles the pause overlay. Set by ESC, which is likewise not a movement
    // key, so the key that pauses cannot also be one the game reads as input.
    bool pauseRequested;

    // Toggles watch mode. Set by P, which is not used during play, so the key
    // that starts or stops the demo cannot also be one the game reads as input.
    bool autoplayRequested;

    // True while the window is playing the level itself. The route, the point
    // reached in it, and the leftover time from the last rendered frame, which
    // decides how many fixed steps to take next.
    bool autopilotOn;
    Autoplay autopilot;
    double autoplayAccumulator;

    // Seconds the end of a watched run is held on screen before it loops, so the
    // finish is visible rather than flashing past.
    double autoplayHold;

    // Which screen is up. Starts at the menu, so the game is only reachable by
    // asking for it.
    Screen screen;

    // Which button the pointer is over, so the hovered one can be drawn to match.
    ButtonId hovered;

    // The button the player clicked, recorded here and acted on by the game
    // loop rather than inside message dispatch, so a click changes the screen on
    // the same schedule as everything else the loop does.
    ButtonId pendingClick;

    // world y-up -> pixels y-down, subtracting the camera for x
    int toScreenX(float worldX) const;
    int toScreenY(float worldY) const;

    Game game;
    Input input;

    // Smoothed frames per second, shown in the HUD.
    double fps;

    HINSTANCE instance;
    HWND window;
    int width;
    int height;

    // The cached off-screen frame buffer. bufferDc has bufferBitmap selected into
    // it, with bufferOldBitmap held so it can be swapped back before the bitmap is
    // deleted. bufferBits is the section's own memory, which the pause overlay
    // dims in place.
    HDC bufferDc;
    HBITMAP bufferBitmap;
    HGDIOBJ bufferOldBitmap;
    void* bufferBits;
    int bufferWidth;
    int bufferHeight;

    // Cached GDI drawing objects. The colours and fonts the window uses are fixed
    // and few, so the same brush, pen or font would otherwise be created and
    // destroyed on every frame that needs it. Brushes and pens are keyed by
    // colour and built on first use; the fonts are built with the window.
    std::map<COLORREF, HBRUSH> brushes;
    std::map<COLORREF, HPEN> pens;
    HFONT menuTitleFont;
    HFONT pauseTitleFont;
    HFONT bodyFont;
};

#endif // RENDERER_H
