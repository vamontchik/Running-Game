#include "Renderer.h"

#include <dwmapi.h>

#include <cmath>
#include <cstdio>
#include <cstring>

namespace
{
    const char* const WINDOW_CLASS_NAME = "RunningGameWindow";

    // Runner draw size comes from World::RUNNER_SIZE.

    const COLORREF BACKGROUND_COLOR = RGB(0, 0, 0);
    const COLORREF BLOCK_COLOR = RGB(150, 110, 80);
    const COLORREF WALL_COLOR = RGB(110, 80, 55);
    const COLORREF RUNNER_COLOR = RGB(230, 60, 60);
    const COLORREF RUNNER_OUTLINE_COLOR = RGB(255, 170, 170);

    // Sprint colors. Blue reads clearly against the black background and is
    // unmistakable next to the default red.
    const COLORREF RUNNER_SPRINT_COLOR = RGB(60, 120, 240);
    const COLORREF RUNNER_SPRINT_OUTLINE_COLOR = RGB(170, 200, 255);

    // Lava fills the ground from LAVA_START_X onward. Drawn in screen space
    // from the lava's left edge to the right of the view, so it scrolls with
    // the world without being a finite block.
    const COLORREF LAVA_COLOR = RGB(190, 45, 20);
    const COLORREF LAVA_GLOW_COLOR = RGB(255, 140, 40);

    // The block the runner spawns standing on is tinted green, so the start of
    // the level reads at a glance without a separate marker drawn over it.
    const COLORREF SPAWN_BLOCK_COLOR = RGB(70, 165, 95);

    // The spike triangles, white as asked. The brightest thing drawn in the world
    // on purpose: they are lethal and have to be read at a glance while moving.
    const COLORREF SPIKE_COLOR = RGB(255, 255, 255);

    // The finish post. Green as well, since it means the same thing: this is the
    // point that matters in the level.
    const COLORREF FINISH_MARKER_COLOR = RGB(90, 220, 120);

    // The final screen. Gold, so the end of the game is distinguishable from an
    // ordinary level clear at a glance.
    const COLORREF ALL_LEVELS_COLOR = RGB(255, 220, 80);

    // Buttons are plain filled panels, since the window has no dialog controls
    // in it. Hovered is a lighter fill and a brighter border, which is the only
    // cue available: there is no pressed state and no system hover highlight.
    const COLORREF BUTTON_FILL = RGB(38, 44, 54);
    const COLORREF BUTTON_HOVER_FILL = RGB(58, 78, 104);
    const COLORREF BUTTON_BORDER = RGB(120, 132, 150);
    const COLORREF BUTTON_HOVER_BORDER = RGB(190, 210, 235);
    const COLORREF BUTTON_TEXT = RGB(232, 238, 246);
    const int BUTTON_BORDER_WIDTH = 2;

    // Button size, and how far apart the game-complete screen's two are.
    const int BUTTON_WIDTH = 240;
    const int BUTTON_HEIGHT = 44;
    const int BUTTON_GAP = 16;

    // Cap frame delta so a stall doesn't produce a huge physics step.
    const double MIN_FRAME_SECONDS = 0.001;

    // How long the end of a watched run is held before it loops, and the most
    // time watch mode will try to catch up on after a stall. The cap stops a long
    // pause in the loop from turning into one enormous burst of simulation.
    const double AUTOPLAY_HOLD_SECONDS = 1.5;
    const double AUTOPLAY_MAX_CATCHUP = 0.25;

    // Pace the loop against the display's refresh. Sleep() alone cannot do this:
    // its granularity is roughly 15ms, so a 144 Hz target (~7ms) is unreachable
    // and the frame rate settles wherever the rounding lands. DwmFlush blocks
    // until the compositor has finished a present, which self-corrects.

    // The one face the UI uses, so the menu title, the pause title and the body
    // text cannot drift onto different fonts. Built here and held by the renderer
    // rather than created and destroyed on every repaint of the screen using it.
    HFONT createUiFont(int height, int weight)
    {
        return CreateFontA(
            height, 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, "Segoe UI");
    }

    // True when the client-space point is inside the rectangle, edges excluded on
    // the far side so two abutting buttons cannot both claim one click.
    bool pointInRect(const RECT& rect, int x, int y)
    {
        return x >= rect.left && x < rect.right
            && y >= rect.top && y < rect.bottom;
    }

    // Darkens the frame buffer in place, so the level shows through the pause
    // overlay dimmed rather than being covered by it.
    //
    // Done per pixel on the buffer's own bits rather than by painting a hatch or
    // a translucent fill over the top. Both of those are approximations: a hatch
    // reads as visible diagonal lines across the level, and GDI's blend modes go
    // from fully on to fully off with nothing usable in between. Scaling each
    // pixel's colour toward black is uniform, so the level behind the overlay
    // just gets quieter.
    //
    // `keep` is the fraction of brightness left in each channel: 0.45 is dim
    // enough for the overlay's text and buttons to read clearly, and light
    // enough that the platforms behind are still identifiable.
    void dimPixels(void* pixels, int width, int height, double keep)
    {
        if (pixels == nullptr || width <= 0 || height <= 0)
        {
            return;
        }

        // A BI_RGB 32bpp buffer stores each pixel as 0x00RRGGBB in a DWORD, so
        // red is the high byte of the three and blue the low.
        DWORD* pixel = static_cast<DWORD*>(pixels);
        const int count = width * height;

        for (int i = 0; i < count; ++i)
        {
            const DWORD source = pixel[i];

            const int red = static_cast<int>(((source >> 16) & 0xFF) * keep);
            const int green = static_cast<int>(((source >> 8) & 0xFF) * keep);
            const int blue = static_cast<int>((source & 0xFF) * keep);

            // Each channel goes back to the byte it was read from, so the pixel
            // is dimmed in place rather than having its colours rearranged.
            //
            // The top byte stays zero: the buffer is BI_RGB, which has no alpha
            // channel, and the final blit to the window copies it regardless.
            pixel[i] = (static_cast<DWORD>(red) << 16)
                | (static_cast<DWORD>(green) << 8)
                | static_cast<DWORD>(blue);
        }
    }
}

Renderer::Renderer()
    : restartRequested(false)
    , pauseRequested(false)
    , autoplayRequested(false)
    , autopilotOn(false)
    , autoplayAccumulator(0.0)
    , autoplayHold(0.0)
    , screen(Screen::Menu)
    , hovered(ButtonNone)
    , pendingClick(ButtonNone)
    , fps(0.0)
    , instance(GetModuleHandle(nullptr))
    , window(nullptr)
    , width(0)
    , height(0)
    , bufferDc(nullptr)
    , bufferBitmap(nullptr)
    , bufferOldBitmap(nullptr)
    , bufferBits(nullptr)
    , bufferWidth(0)
    , bufferHeight(0)
    , menuTitleFont(nullptr)
    , pauseTitleFont(nullptr)
    , bodyFont(nullptr)
{
    // Built once here rather than per frame: the menu and the pause overlay would
    // otherwise create their fonts on every repaint while they are up.
    menuTitleFont = createUiFont(64, FW_BOLD);
    pauseTitleFont = createUiFont(48, FW_BOLD);
    bodyFont = createUiFont(20, FW_NORMAL);
}

Renderer::~Renderer()
{
    releaseGdiObjects();
    releaseBuffer();

    if (window != nullptr)
    {
        DestroyWindow(window);
        window = nullptr;
    }
}

void Renderer::fillRect(HDC dc, int left, int top, int width, int height, COLORREF color)
{
    const HBRUSH brush = brushFor(color);
    if (brush == nullptr)
    {
        return;
    }

    RECT rect;
    rect.left = left;
    rect.top = top;
    rect.right = left + width;
    rect.bottom = top + height;
    FillRect(dc, &rect, brush);
}

HBRUSH Renderer::brushFor(COLORREF color)
{
    const std::map<COLORREF, HBRUSH>::iterator found = brushes.find(color);
    if (found != brushes.end())
    {
        return found->second;
    }

    const HBRUSH brush = CreateSolidBrush(color);

    // A failed brush is not cached, so the next frame tries again rather than
    // remembering the failure for good.
    if (brush != nullptr)
    {
        brushes[color] = brush;
    }

    return brush;
}

HPEN Renderer::penFor(COLORREF color)
{
    const std::map<COLORREF, HPEN>::iterator found = pens.find(color);
    if (found != pens.end())
    {
        return found->second;
    }

    const HPEN pen = CreatePen(PS_SOLID, 1, color);

    if (pen != nullptr)
    {
        pens[color] = pen;
    }

    return pen;
}

void Renderer::releaseGdiObjects()
{
    for (std::map<COLORREF, HBRUSH>::iterator it = brushes.begin();
        it != brushes.end(); ++it)
    {
        DeleteObject(it->second);
    }
    brushes.clear();

    for (std::map<COLORREF, HPEN>::iterator it = pens.begin();
        it != pens.end(); ++it)
    {
        DeleteObject(it->second);
    }
    pens.clear();

    if (menuTitleFont != nullptr)
    {
        DeleteObject(menuTitleFont);
        menuTitleFont = nullptr;
    }

    if (pauseTitleFont != nullptr)
    {
        DeleteObject(pauseTitleFont);
        pauseTitleFont = nullptr;
    }

    if (bodyFont != nullptr)
    {
        DeleteObject(bodyFont);
        bodyFont = nullptr;
    }
}

bool Renderer::registerClass()
{
    WNDCLASSEX wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = windowProc;
    wc.hInstance = instance;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    // A black brush, so any area not yet covered by paint() does not flash
    // white while the window is being created or resized.
    wc.hbrBackground = CreateSolidBrush(BACKGROUND_COLOR);
    wc.lpszClassName = WINDOW_CLASS_NAME;

    return RegisterClassEx(&wc) != 0;
}

bool Renderer::create(const char* title, int clientWidth, int clientHeight)
{
    // The caller specifies the client area, so convert to the outer window size
    // the window manager expects. Without this the title bar and borders eat
    // into the client area, and the ceiling would be drawn off-screen because
    // the y-flip assumes the client area matches the level height.
    // No thick frame and no maximize box: resizing would break the 1:1 mapping
    // between world units and pixels that the y-flip depends on.
    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;

    RECT outer;
    outer.left = 0;
    outer.top = 0;
    outer.right = clientWidth;
    outer.bottom = clientHeight;
    AdjustWindowRectEx(&outer, style, FALSE, 0);

    window = CreateWindowEx(
        0,
        WINDOW_CLASS_NAME,
        title,
        style,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        outer.right - outer.left,
        outer.bottom - outer.top,
        nullptr,
        nullptr,
        instance,
        this);

    if (window == nullptr)
    {
        return false;
    }

    // Read the real client size back, rather than trusting the request, and use
    // it for the y-flip.
    RECT client;
    GetClientRect(window, &client);
    width = client.right - client.left;
    height = client.bottom - client.top;

    ShowWindow(window, SW_SHOW);
    UpdateWindow(window);
    return true;
}

int Renderer::run()
{
    LARGE_INTEGER frequency;
    LARGE_INTEGER counter;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&counter);

    double last = static_cast<double>(counter.QuadPart) / static_cast<double>(frequency.QuadPart);

    MSG message;
    bool running = true;

    // The code the quit was posted with, kept here because the WM_QUIT that
    // carries it is consumed by PeekMessage below and cannot be read afterwards.
    int exitCode = 0;

    while (running)
    {
        while (PeekMessage(&message, nullptr, 0, 0, PM_REMOVE))
        {
            if (message.message == WM_QUIT)
            {
                exitCode = static_cast<int>(message.wParam);
                running = false;
                break;
            }

            TranslateMessage(&message);
            DispatchMessage(&message);
        }

        if (!running)
        {
            break;
        }

        QueryPerformanceCounter(&counter);
        const double now = static_cast<double>(counter.QuadPart) / static_cast<double>(frequency.QuadPart);
        double dt = now - last;
        last = now;

        if (dt < MIN_FRAME_SECONDS)
        {
            dt = MIN_FRAME_SECONDS;
        }

        // Escape toggles the pause overlay, handled before anything else so the
        // toggle lands on this frame rather than a frame later. Only meaningful
        // for a run in progress: there is nothing to pause on the menu, and the
        // end screens are already stopped, so ESC is left alone there.
        if (pauseRequested)
        {
            pauseRequested = false;

            if (screen == Screen::Playing && !game.isDead() && !game.isFinished())
            {
                setScreen(Screen::Paused);

                // Drop held keys, so the runner does not carry its momentum into
                // the resume and bolt off before the player has seen the overlay.
                input.clear();
            }
            else if (screen == Screen::Paused)
            {
                setScreen(Screen::Playing);
                input.clear();
            }
        }

        // Watch mode is toggled on this frame's boundary too, for the same reason:
        // so the first planned frame and the last rendered one line up.
        if (autoplayRequested)
        {
            autoplayRequested = false;
            toggleAutopilot();
        }

        // A click is acted on here rather than in message dispatch, so a button
        // press changes state on the same schedule as everything else the loop
        // does. Which buttons exist depends on the screen.
        if (pendingClick != ButtonNone)
        {
            const ButtonId clicked = pendingClick;
            pendingClick = ButtonNone;

            if (screen == Screen::Menu && clicked == ButtonStartGame)
            {
                // A fresh attempt: level one with the clock at zero. The game is
                // already sitting there from construction, so this only has to
                // hand over the screen.
                game.restartRun();
                setScreen(Screen::Playing);
                input.clear();
            }
            else if (screen == Screen::Paused)
            {
                if (clicked == ButtonResume)
                {
                    // Back to the same run, timer included: pausing is not a
                    // penalty and the clock has already stopped.
                    setScreen(Screen::Playing);
                }
                else if (clicked == ButtonRestartRun)
                {
                    // Restart means the whole run, not this level alone, so the
                    // timer clears with it. Otherwise a player could bank a fast
                    // opening level and carry that time into later attempts.
                    game.restartRun();
                    setScreen(Screen::Playing);
                }
                else if (clicked == ButtonMainMenu)
                {
                    game.restartRun();
                    setScreen(Screen::Menu);
                }

                input.clear();
            }
            else if (game.isFinished() && clicked == ButtonRestartRun)
            {
                // The run is over and finished, so back to level one with the
                // clock cleared: the next attempt is a whole new one.
                game.restartRun();
                input.clear();
            }
            else if (game.isFinished() && clicked == ButtonMainMenu)
            {
                // Leave the finished state behind rather than keeping it on the
                // menu, so returning to play starts a clean attempt.
                game.restartRun();
                setScreen(Screen::Menu);
                input.clear();
            }
        }

        if (screen == Screen::Menu)
        {
            // Nothing simulates behind the menu, so the run timer does not start
            // counting until the player asks for it.
            restartRequested = false;
            input.clear();
        }
        else if (screen == Screen::Paused)
        {
            // The run is frozen: no update means no movement and, just as
            // importantly, no elapsed time. TAB is inert here too, since there is
            // no finished run to advance.
            restartRequested = false;
            input.clear();
        }
        else if (autopilotOn)
        {
            // Watch mode drives the run from the route rather than the keyboard,
            // and restarts it when it ends, so the demo plays on. TAB has nothing
            // to do here: a watched run is never waiting for input to move on.
            restartRequested = false;
            advanceAutopilot(dt);
        }
        else if (game.isDead() || game.isFinished())
        {
            // A finished or dead run waits for TAB to restart. Deliberately a key
            // the player never presses during play, so the input that ended the
            // run cannot also restart it.
            if (restartRequested)
            {
                if (game.isFinished())
                {
                    game.advanceLevel();
                }
                else
                {
                    // Death ends the whole attempt, not just this level: back to
                    // level one with the timer cleared. A run is timed end to
                    // end, so retrying the level you died on with the clock still
                    // running would make the time a measure of how many times you
                    // fell rather than of the run.
                    game.restartRun();
                }

                restartRequested = false;

                // Drop any keys still held from the run that just ended, so the
                // runner does not start moving before the player asks it to.
                input.clear();
            }
        }
        else
        {
            // TAB during play is ignored, but clear the flag so it cannot be
            // carried into the next end screen and restart instantly.
            restartRequested = false;

            // update() spends the jump presses it acts on, so there is nothing to
            // clear afterwards: any press it could not use has already been
            // dropped rather than left to fire on the next landing.
            game.update(static_cast<float>(dt), input);
        }

        // Smooth the framerate reading so the HUD number is readable rather
        // than flickering between values.
        if (dt > 0.0)
        {
            const double instant = 1.0 / dt;
            fps += (instant - fps) * 0.1;
        }

        InvalidateRect(window, nullptr, FALSE);

        // Wait for the current frame to be presented before starting the next.
        // This is what caps the frame rate to the monitor's refresh rate. If
        // the flush fails or the window closed underneath us, fall back to a
        // short sleep so the loop cannot spin.
        const HRESULT presentResult = DwmFlush();

        if (FAILED(presentResult))
        {
            Sleep(1);
        }
    }

    // The quit was already consumed above, so there is nothing for GetMessage to
    // retrieve: calling it here would block on an empty queue forever, leaving the
    // process alive with its window gone. Return the code the quit carried instead.
    return exitCode;
}

LRESULT CALLBACK Renderer::windowProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (msg == WM_NCCREATE)
    {
        CREATESTRUCT* create = reinterpret_cast<CREATESTRUCT*>(lParam);
        SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }

    Renderer* self = reinterpret_cast<Renderer*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));

    if (self != nullptr)
    {
        return self->handleMessage(hwnd, msg, wParam, lParam);
    }

    return DefWindowProc(hwnd, msg, wParam, lParam);
}

void Renderer::updateInput(UINT message, WPARAM wParam, bool repeat)
{
    switch (wParam)
    {
    case VK_LEFT:
    case 'A':
        input.left = (message == WM_KEYDOWN);
        break;

    case VK_RIGHT:
    case 'D':
        input.right = (message == WM_KEYDOWN);
        break;

    case VK_SPACE:
    case VK_UP:
    case 'W':
        // Counted per press rather than held, so a double jump is two taps and a
        // held key is one jump. Auto-repeat is filtered out here, and that matters
        // more now that there is an allowance to spend: an unfiltered held key
        // repeats after a few hundred milliseconds and burns the mid-air jump on
        // the way past, exactly when the player was saving it for a landing.
        //
        // Capped at two, which is all any one frame can use, so mashing the key
        // cannot bank presses for later.
        if (message == WM_KEYDOWN && !repeat
            && input.jumpsPressed < Input::MAX_PENDING_JUMPS)
        {
            ++input.jumpsPressed;
        }
        break;

    case VK_SHIFT:
    case VK_LSHIFT:
    case VK_RSHIFT:
        // Level-triggered: held state, so it drives both speed and color.
        input.sprint = (message == WM_KEYDOWN);
        break;

    default:
        break;
    }
}

void Renderer::startWatchMode()
{
    // Enabling is what toggling off -> on does, so route through it rather than
    // repeating the work here.
    autopilotOn = false;
    toggleAutopilot();
}

void Renderer::toggleAutopilot()
{
    if (autopilotOn)
    {
        // Hand control back where the watched run is, rather than resetting it:
        // taking over from the demo mid-level is the useful case.
        autopilotOn = false;
        input.clear();
        return;
    }

    // Start from the first level, first frame, so the watched run is the clean
    // one the route was worked out for.
    game.restartRun();

    if (!planCurrentLevel())
    {
        return;
    }

    autopilotOn = true;
    setScreen(Screen::Playing);
    input.clear();
}

bool Renderer::planCurrentLevel()
{
    Autoplay candidate;
    if (!candidate.plan(game))
    {
        // No route: leave watch mode off rather than run a level that cannot be
        // crossed. The player is left on the start of the level, as if they had
        // asked for a fresh run.
        return false;
    }

    autopilot = candidate;
    autopilot.rewind();
    autoplayAccumulator = 0.0;
    autoplayHold = 0.0;
    return true;
}

void Renderer::advanceAutopilot(double dt)
{
    if (game.isFinished() || game.isDead())
    {
        // Hold the end screen for a moment, so the clear is visible rather than
        // flashing past, then move on: to the next level, or, once the last level
        // is cleared, back to the first, so the demo plays the whole game on a
        // loop.
        autoplayHold += dt;
        if (autoplayHold >= AUTOPLAY_HOLD_SECONDS)
        {
            if (game.isFinished() && !game.isGameComplete())
            {
                game.advanceLevel();
            }
            else
            {
                game.restartRun();
            }

            // The next level is a different layout, so it needs its own route.
            if (!planCurrentLevel())
            {
                autopilotOn = false;
            }
        }
        return;
    }

    autoplayAccumulator += dt;
    if (autoplayAccumulator > AUTOPLAY_MAX_CATCHUP)
    {
        autoplayAccumulator = AUTOPLAY_MAX_CATCHUP;
    }

    // Whole fixed steps only: the route is a list of frame numbers at this step,
    // so the game has to be advanced by exactly this much for those numbers to
    // mean the same thing they meant when the route was worked out.
    while (autoplayAccumulator >= Autoplay::STEP && !game.isFinished()
        && !game.isDead())
    {
        autoplayAccumulator -= Autoplay::STEP;
        Input planned = autopilot.nextInput(game);
        game.update(Autoplay::STEP, planned);
    }
}

LRESULT Renderer::handleMessage(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_MOUSEMOVE:
    {
        // Hover is the only feedback a button gives, so it has to track the
        // pointer exactly.
        const int x = LOWORD(lParam);
        const int y = HIWORD(lParam);
        updateHover(x, y);

        // Opt in to WM_MOUSELEAVE, which is not sent unless asked for. Without it
        // a button stays lit after the pointer has left the window.
        TRACKMOUSEEVENT track;
        track.cbSize = sizeof(track);
        track.dwFlags = TME_LEAVE;
        track.hwndTrack = hwnd;
        track.dwHoverTime = 0;
        TrackMouseEvent(&track);
        return 0;
    }

    case WM_LBUTTONDOWN:
    {
        const int x = LOWORD(lParam);
        const int y = HIWORD(lParam);
        updateHover(x, y);

        // Recorded for the game loop rather than acted on here, so the click
        // lands on the same frame boundary as everything else.
        pendingClick = buttonAt(x, y);
        return 0;
    }

    case WM_SETCURSOR:
        // The window has no controls, so the pointer is changed by hand to show
        // that a button is under it. Returns DefWindowProc otherwise, which is
        // what restores the arrow.
        if (hovered != ButtonNone)
        {
            SetCursor(LoadCursor(nullptr, IDC_HAND));
            return TRUE;
        }
        return DefWindowProc(hwnd, msg, wParam, lParam);

    case WM_MOUSELEAVE:
        // Drop the hover when the pointer leaves, so the button does not stay
        // lit for a cursor that is no longer on it.
        hovered = ButtonNone;
        InvalidateRect(window, nullptr, FALSE);
        return 0;

    case WM_KEYDOWN:
    {
        // The keys that move past an end-of-run screen, and the key that pauses.
        // Recorded rather than acted on here, so they take effect on the game
        // loop's schedule rather than inside message dispatch.
        //
        // Bit 30 of lParam is set on auto-repeat, so a held key does not fire
        // repeatedly: a held Escape would otherwise pause and immediately resume.
        const bool repeat = (lParam & (1 << 30)) != 0;

        if (!repeat)
        {
            // Tab, Enter and Caps all mean "carry on", and share the one flag so
            // the end screens behave identically whichever is pressed. Enter is
            // there because it is under the right hand where the movement keys
            // are, and Tab is not.
            if (wParam == VK_TAB || wParam == VK_RETURN || wParam == VK_CAPITAL)
            {
                restartRequested = true;
            }
            else if (wParam == VK_ESCAPE)
            {
                pauseRequested = true;
            }
            else if (wParam == 'P')
            {
                // Watch mode. Not acted on here, so it lands on the game loop's
                // schedule like the pause toggle does.
                autoplayRequested = true;
            }
        }

        updateInput(msg, wParam, repeat);
        return 0;
    }

    case WM_KEYUP:
        // Key release never auto-repeats, so the flag is meaningless here.
        updateInput(msg, wParam, false);
        return 0;

    case WM_KILLFOCUS:
        // Release held keys, so alt-tabbing away doesn't leave the runner
        // running forever.
        input.clear();
        return 0;

    case WM_ACTIVATE:
        // A button that was lit when focus was lost is stale by the time focus
        // comes back, since the pointer may have moved in between.
        if (LOWORD(wParam) == WA_INACTIVE)
        {
            hovered = ButtonNone;
            InvalidateRect(window, nullptr, FALSE);
        }
        return 0;

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        paint(dc);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;

    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
}

void Renderer::paint(HDC dc)
{
    // Draw into an off-screen bitmap, then blit it in one step. Painting
    // straight to the window shows partially-drawn frames, which reads as
    // flickering.
    RECT client;
    GetClientRect(window, &client);
    const int clientWidth = client.right - client.left;
    const int clientHeight = client.bottom - client.top;

    if (clientWidth <= 0 || clientHeight <= 0)
    {
        return;
    }

    // The buffer is kept between frames and only built or rebuilt here. No buffer
    // at all means there is nothing to draw into, and an empty frame is better
    // than a crash inside the paint handler.
    ensureBuffer(dc, clientWidth, clientHeight);
    if (bufferDc == nullptr)
    {
        return;
    }

    fillRect(bufferDc, 0, 0, clientWidth, clientHeight, BACKGROUND_COLOR);

    // The menu replaces the view rather than covering it: there is no run in
    // progress, so showing the level behind it would only be misleading.
    if (screen == Screen::Menu)
    {
        paintMenu(bufferDc, clientWidth, clientHeight);

        BitBlt(dc, 0, 0, clientWidth, clientHeight, bufferDc, 0, 0, SRCCOPY);
        return;
    }

    // World geometry, scrolled by the camera. Skip blocks outside the view so
    // pruning can lag behind without costing fill time.
    const Map& map = game.getMap();
    for (size_t i = 0; i < map.getBlockCount(); ++i)
    {
        const Block& block = map.getBlock(i);
        if (block.getX() + block.getWidth() < game.getCameraX()
            || block.getX() > game.getCameraX() + clientWidth)
        {
            continue;
        }

        paintBlock(bufferDc, block);
    }

    // Lava sits under the platforms but over the background, so platforms
    // remain visible above it.
    paintLava(bufferDc, clientWidth);

    // Spikes stand on top of the platforms, so they go after both. Before the
    // markers, which are unreadable underneath anything anyway, and before the
    // runner, so a spike is never hidden by the thing it just killed.
    paintSpikes(bufferDc);

    // Landmarks sit over the lava and the platforms, since they have to stay
    // readable from any position in the level.
    paintMarkers(bufferDc);

    paintWalls(bufferDc);

    if (!game.isDead())
    {
        paintRunner(bufferDc, game.getRunner());
    }

    // The end screens dim the view, so they go on after everything they cover.
    if (game.isDead())
    {
        paintGameOver(bufferDc, clientWidth, clientHeight);
    }
    else if (game.isFinished())
    {
        paintFinished(bufferDc, clientWidth, clientHeight);
    }

    // The pause overlay dims the level, so it goes on after everything it covers.
    // Drawn before the HUD, so the timer stays readable on top of the dim: knowing
    // the clock is stopped is part of what pausing means.
    if (screen == Screen::Paused)
    {
        paintPaused(bufferDc, clientWidth, clientHeight, bufferBits);
    }

    paintHud(bufferDc, fps);

    BitBlt(dc, 0, 0, clientWidth, clientHeight, bufferDc, 0, 0, SRCCOPY);
}

void Renderer::ensureBuffer(HDC dc, int clientWidth, int clientHeight)
{
    // Already the right size, which after the first frame it always is: the
    // window is not resizable, so nothing here changes between frames.
    if (bufferDc != nullptr && bufferWidth == clientWidth
        && bufferHeight == clientHeight)
    {
        return;
    }

    releaseBuffer();

    bufferDc = CreateCompatibleDC(dc);
    if (bufferDc == nullptr)
    {
        return;
    }

    // A DIB section rather than CreateCompatibleBitmap, because the pause
    // overlay needs to reach the buffer's pixels to dim them in place. A
    // compatible bitmap has no addressable bits: GDI owns them, and there is no
    // way to read or scale them. A section hands back a pointer to its own
    // memory, which is what dimPixels walks.
    BITMAPINFO bufferInfo;
    ZeroMemory(&bufferInfo, sizeof(bufferInfo));
    bufferInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bufferInfo.bmiHeader.biWidth = clientWidth;
    // Negative height so row 0 is the top row, matching how the rest of the
    // drawing addresses the buffer.
    bufferInfo.bmiHeader.biHeight = -clientHeight;
    bufferInfo.bmiHeader.biPlanes = 1;
    bufferInfo.bmiHeader.biBitCount = 32;
    bufferInfo.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(
        dc, &bufferInfo, DIB_RGB_COLORS, &bits, nullptr, 0);

    // No section means no buffer, and there is nothing to draw into. Better an
    // empty frame than a crash inside the paint handler.
    if (bitmap == nullptr || bits == nullptr)
    {
        if (bitmap != nullptr)
        {
            DeleteObject(bitmap);
        }

        DeleteDC(bufferDc);
        bufferDc = nullptr;
        return;
    }

    // Selected once and left selected: every frame draws into it, so there is no
    // reason to swap the original bitmap back in between paints. bufferOldBitmap
    // is what releaseBuffer restores before the bitmap can be deleted.
    bufferOldBitmap = SelectObject(bufferDc, bitmap);

    bufferBitmap = bitmap;
    bufferBits = bits;
    bufferWidth = clientWidth;
    bufferHeight = clientHeight;
}

void Renderer::releaseBuffer()
{
    if (bufferDc != nullptr)
    {
        // The bitmap cannot be deleted while it is selected into the DC, so the
        // original is put back first.
        if (bufferOldBitmap != nullptr)
        {
            SelectObject(bufferDc, bufferOldBitmap);
            bufferOldBitmap = nullptr;
        }

        DeleteDC(bufferDc);
        bufferDc = nullptr;
    }

    if (bufferBitmap != nullptr)
    {
        DeleteObject(bufferBitmap);
        bufferBitmap = nullptr;
    }

    bufferBits = nullptr;
    bufferWidth = 0;
    bufferHeight = 0;
}

void Renderer::paintWalls(HDC dc)
{
    // Drawn in screen space, so the walls stay put while the world scrolls
    // past them. The runner is clamped to stay inside these.
    const int thickness = static_cast<int>(World::WALL_THICKNESS);
    const int clientHeight = toScreenY(0.0f);

    fillRect(dc, 0, 0, thickness, clientHeight, WALL_COLOR);

    const int rightEdge = toScreenX(game.getCameraX() + World::VIEW_WIDTH);
    fillRect(dc, rightEdge - thickness, 0, thickness, clientHeight, WALL_COLOR);

    // The ceiling is an infinite band, not a map block, so it is drawn here
    // alongside the walls rather than in the world-geometry loop. It spans the
    // full window width and its underside lines up with the collision constant.
    fillRect(dc, 0, 0, toScreenX(game.getCameraX() + World::VIEW_WIDTH), thickness, WALL_COLOR);
}

void Renderer::paintMarkers(HDC dc)
{
    // Only the finish needs a marker now: the start is shown by tinting the
    // block the runner spawns on, which scrolls away on its own as a block does.
    paintMarker(dc, World::FINISH_X, FINISH_MARKER_COLOR, "FINISH");
}

void Renderer::paintMarker(HDC dc, float worldX, COLORREF color, const char* label)
{
    const int screenX = toScreenX(worldX);

    // Skip once the post is off screen, so its label does not linger pinned to
    // the window edge after the landmark has scrolled away.
    if (screenX < -static_cast<int>(World::MARKER_HALF_WIDTH) || screenX > width)
    {
        return;
    }

    const int postWidth = static_cast<int>(World::MARKER_HALF_WIDTH) * 2;
    const int postTop = toScreenY(World::CEILING_UNDERSIDE);
    const int postBottom = toScreenY(World::FLOOR_TOP);

    fillRect(dc, screenX - static_cast<int>(World::MARKER_HALF_WIDTH),
        postTop, postWidth, postBottom - postTop, color);

    // Label above the post, near the ceiling. Only drawn while the post itself
    // is on screen, and only when the text fits, so it never spills past the
    // window edge.
    const int labelLength = static_cast<int>(std::strlen(label));
    const int labelWidth = labelLength * 8;
    const int labelLeft = screenX - labelWidth / 2;

    if (labelLeft < 0 || labelLeft + labelWidth > width)
    {
        return;
    }

    const int labelTop = static_cast<int>(World::MARKER_LABEL_TOP);

    // A black backing bar, since the post and the level behind it are dark and
    // the text has to stay legible over either.
    fillRect(dc, labelLeft - 4, labelTop - 2, labelWidth + 8, 20, BACKGROUND_COLOR);

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    TextOutA(dc, labelLeft, labelTop, label, labelLength);
}

void Renderer::paintPaused(HDC dc, int clientWidth, int clientHeight, void* pixels)
{
    // Dim the frozen level rather than blanking it. The end screens hide the
    // level outright because a finished run has nothing left to look at; a pause
    // has the run sitting right there, and seeing it is part of knowing where you
    // picked up.
    //
    // Applied to the buffer's own pixels rather than painted over the top, which
    // would need either a hatch or a blend mode, and both of those are visibly
    // approximate. Scaling the colours is uniform, so the platforms behind the
    // overlay read as the same platforms, just quieter.
    dimPixels(pixels, clientWidth, clientHeight, 0.45);

    // The same bold face the menu title uses, so the overlay looks like a screen
    // of the game rather than a dialog pasted over it. Cached with the window, not
    // built here.
    RECT titleBounds;
    titleBounds.left = 0;
    titleBounds.right = clientWidth;
    titleBounds.top = clientHeight / 2 - 92;
    titleBounds.bottom = titleBounds.top + 60;
    drawCenteredText(dc, titleBounds, "PAUSED", ALL_LEVELS_COLOR, pauseTitleFont);

    // Resume first, since it is what most pauses are for.
    paintButton(dc, pauseButton(ButtonResume), "Resume", hovered == ButtonResume);

    // Restart the run, not the level: a run is timed end to end, so the timer has
    // to go back to zero with it.
    paintButton(dc, pauseButton(ButtonRestartRun), "Restart Run",
        hovered == ButtonRestartRun);

    paintButton(dc, pauseButton(ButtonMainMenu), "Exit to Menu",
        hovered == ButtonMainMenu);

    // Escape is the way in and out, so it is the way out again rather than being
    // the only way to resume by keyboard.
    RECT hintBounds;
    hintBounds.left = 0;
    hintBounds.right = clientWidth;
    hintBounds.top = pauseButton(ButtonMainMenu).bottom + 28;
    hintBounds.bottom = hintBounds.top + 20;
    drawCenteredText(dc, hintBounds, "Press ESC to resume", RGB(140, 150, 165), nullptr);
}

void Renderer::paintMenu(HDC dc, int clientWidth, int clientHeight)
{
    // A larger font for the title, so the menu reads as a front door rather than
    // as more HUD. Both faces are cached with the window rather than built here.
    RECT titleBounds;
    titleBounds.left = 0;
    titleBounds.right = clientWidth;
    titleBounds.top = clientHeight / 6;
    titleBounds.bottom = titleBounds.top + 80;
    drawCenteredText(dc, titleBounds, "RUNNING GAME", ALL_LEVELS_COLOR, menuTitleFont);

    RECT promptBounds;
    promptBounds.left = 0;
    promptBounds.right = clientWidth;
    promptBounds.top = titleBounds.bottom + 16;
    promptBounds.bottom = promptBounds.top + 28;
    drawCenteredText(dc, promptBounds,
        "Reach the end of every level without falling in the lava or touching a spike.",
        RGB(200, 208, 220), bodyFont);

    // The one thing to do here.
    const RECT start = menuStartButton();
    paintButton(dc, start, "Start Game", hovered == ButtonStartGame);

    // The controls, so the menu is the one place that has to explain them. Two
    // lines rather than one because the double jump needs saying, and the lot of
    // it set on a single line ran past the window edge.
    RECT controlsBounds;
    controlsBounds.left = 0;
    controlsBounds.right = clientWidth;
    controlsBounds.top = start.bottom + 44;
    controlsBounds.bottom = controlsBounds.top + 28;
    drawCenteredText(dc, controlsBounds,
        "Move: arrows or WASD    Jump: Space / W / Up    Sprint: Shift",
        RGB(140, 150, 165), bodyFont);

    RECT jumpHintBounds;
    jumpHintBounds.left = 0;
    jumpHintBounds.right = clientWidth;
    jumpHintBounds.top = controlsBounds.bottom + 4;
    jumpHintBounds.bottom = jumpHintBounds.top + 28;
    drawCenteredText(dc, jumpHintBounds,
        "Tap jump again while in the air to double jump",
        RGB(140, 150, 165), bodyFont);
}

void Renderer::paintButton(HDC dc, const RECT& bounds, const char* label, bool isHovered)
{
    const int buttonWidth = bounds.right - bounds.left;
    const int buttonHeight = bounds.bottom - bounds.top;

    fillRect(dc, bounds.left, bounds.top, buttonWidth, buttonHeight,
        isHovered ? BUTTON_HOVER_FILL : BUTTON_FILL);

    // Border as four fills, which keeps the face and the outline from needing
    // separate pens and brushes selected around each other.
    const COLORREF borderColor = isHovered ? BUTTON_HOVER_BORDER : BUTTON_BORDER;
    fillRect(dc, bounds.left, bounds.top, buttonWidth, BUTTON_BORDER_WIDTH, borderColor);
    fillRect(dc, bounds.left, bounds.bottom - BUTTON_BORDER_WIDTH,
        buttonWidth, BUTTON_BORDER_WIDTH, borderColor);
    fillRect(dc, bounds.left, bounds.top, BUTTON_BORDER_WIDTH, buttonHeight, borderColor);
    fillRect(dc, bounds.right - BUTTON_BORDER_WIDTH, bounds.top,
        BUTTON_BORDER_WIDTH, buttonHeight, borderColor);

    drawCenteredText(dc, bounds, label, BUTTON_TEXT, nullptr);
}

void Renderer::drawCenteredText(HDC dc, const RECT& bounds, const char* text,
    COLORREF color, HFONT font)
{
    HGDIOBJ oldFont = font != nullptr ? SelectObject(dc, font) : nullptr;

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);

    const int length = static_cast<int>(std::strlen(text));

    // Measured rather than assumed: the menu uses two different fonts, and a
    // fixed character width would centre the title wrongly.
    SIZE measured;
    GetTextExtentPoint32A(dc, text, length, &measured);

    const int x = bounds.left + ((bounds.right - bounds.left) - measured.cx) / 2;
    const int y = bounds.top + ((bounds.bottom - bounds.top) - measured.cy) / 2;

    TextOutA(dc, x, y, text, length);

    if (oldFont != nullptr)
    {
        SelectObject(dc, oldFont);
    }
}

RECT Renderer::menuStartButton() const
{
    RECT bounds;
    bounds.left = (width - BUTTON_WIDTH) / 2;
    bounds.right = bounds.left + BUTTON_WIDTH;
    bounds.top = height / 2 + 10;
    bounds.bottom = bounds.top + BUTTON_HEIGHT;
    return bounds;
}

RECT Renderer::pauseButton(ButtonId id) const
{
    // Stacked in the order they appear in the enum, so the top button is Resume
    // and the bottom is the way out of the run.
    int index = 0;
    if (id == ButtonRestartRun)
    {
        index = 1;
    }
    else if (id == ButtonMainMenu)
    {
        index = 2;
    }

    RECT bounds;
    bounds.left = (width - BUTTON_WIDTH) / 2;
    bounds.right = bounds.left + BUTTON_WIDTH;
    bounds.top = height / 2 + index * (BUTTON_HEIGHT + BUTTON_GAP);
    bounds.bottom = bounds.top + BUTTON_HEIGHT;
    return bounds;
}

RECT Renderer::completeButton(ButtonId id) const
{
    // Side by side and centred, the first of the pair at id == ButtonRestartRun.
    const int totalWidth = BUTTON_WIDTH * 2 + BUTTON_GAP;
    const int left = (width - totalWidth) / 2;

    const int index = (id == ButtonRestartRun) ? 0 : 1;

    RECT bounds;
    bounds.left = left + index * (BUTTON_WIDTH + BUTTON_GAP);
    bounds.right = bounds.left + BUTTON_WIDTH;
    bounds.top = height / 2 + 48;
    bounds.bottom = bounds.top + BUTTON_HEIGHT;
    return bounds;
}

Renderer::ButtonId Renderer::buttonAt(int x, int y) const
{
    if (screen == Screen::Menu)
    {
        return pointInRect(menuStartButton(), x, y) ? ButtonStartGame : ButtonNone;
    }

    // The pause overlay's three, all stacked down the middle.
    if (screen == Screen::Paused)
    {
        if (pointInRect(pauseButton(ButtonResume), x, y))
        {
            return ButtonResume;
        }

        if (pointInRect(pauseButton(ButtonRestartRun), x, y))
        {
            return ButtonRestartRun;
        }

        if (pointInRect(pauseButton(ButtonMainMenu), x, y))
        {
            return ButtonMainMenu;
        }

        return ButtonNone;
    }

    // The two end-of-game choices, but only once the game is actually complete:
    // a mid-run level clear is dismissed with TAB as before.
    if (game.isGameComplete())
    {
        if (pointInRect(completeButton(ButtonRestartRun), x, y))
        {
            return ButtonRestartRun;
        }

        if (pointInRect(completeButton(ButtonMainMenu), x, y))
        {
            return ButtonMainMenu;
        }
    }

    return ButtonNone;
}

void Renderer::setScreen(Screen next)
{
    screen = next;
    hovered = ButtonNone;
}

void Renderer::updateHover(int x, int y)
{
    const ButtonId now = buttonAt(x, y);

    // Only repaint when the answer actually changes. Repainting on every mouse
    // move would cost a full frame per message for no visible gain.
    if (now != hovered)
    {
        hovered = now;
        InvalidateRect(window, nullptr, FALSE);
    }
}

void Renderer::paintGameOver(HDC dc, int clientWidth, int clientHeight)
{
    // Named after the hazard that ended the run. The two deaths are otherwise
    // identical, so only the headline differs and the wording is otherwise the
    // same.
    if (game.getDeathCause() == DeathCause::Spike)
    {
        paintEndScreen(dc, clientWidth, clientHeight,
            "SPIKED!  Press TAB, ENTER or CAPS to try again",
            RGB(255, 200, 120));

        return;
    }

    paintEndScreen(dc, clientWidth, clientHeight,
        "BURNED!  Press TAB, ENTER or CAPS to try again",
        RGB(255, 200, 120));
}

void Renderer::paintFinished(HDC dc, int clientWidth, int clientHeight)
{
    // Clearing the last level is the end of the game; clearing any other one is
    // a level clear. The wording differs, and so does how you move on: the end
    // of the game offers a choice, while a level clear is TAB and nothing else.
    if (game.isGameComplete())
    {
        paintEndScreen(dc, clientWidth, clientHeight,
            "ALL LEVELS COMPLETE!", ALL_LEVELS_COLOR);

        // Two ways out, since a finished run is as good a time to change your
        // mind as any.
        paintButton(dc, completeButton(ButtonRestartRun), "Play Again",
            hovered == ButtonRestartRun);
        paintButton(dc, completeButton(ButtonMainMenu), "Main Menu",
            hovered == ButtonMainMenu);
    }
    else
    {
        paintEndScreen(dc, clientWidth, clientHeight,
            "LEVEL CLEAR!  Press TAB, ENTER or CAPS for the next level", FINISH_MARKER_COLOR);
    }
}

// Shared by the two ways a run can end, which differ only in wording and color.
// Both dim the view so the text reads over the level, and both report the time
// and distance, since a run that has ended is the one being scored.
void Renderer::paintEndScreen(HDC dc, int clientWidth, int clientHeight,
    const char* message, COLORREF color)
{
    // Dim the whole view so the text reads clearly over the level.
    fillRect(dc, 0, 0, clientWidth, clientHeight, RGB(0, 0, 0));

    const int messageLength = static_cast<int>(std::strlen(message));

    // Center the text horizontally and sit it above the middle of the view.
    const int x = (clientWidth - messageLength * 8) / 2;
    const int y = clientHeight / 2 - 10;

    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    TextOutA(dc, x, y, message, messageLength);

    // Show the final time, which is the score for the run that just ended.
    const double seconds = game.getElapsedTime();
    char summary[160];
    std::snprintf(
        summary,
        sizeof(summary),
        "Time: %d:%02d.%02d   Level: %d/%d   Distance: %.0f",
        static_cast<int>(seconds / 60.0),
        static_cast<int>(seconds) % 60,
        static_cast<int>((seconds - static_cast<int>(seconds)) * 100.0),
        game.getLevel(),
        World::LAST_LEVEL,
        static_cast<double>(game.getDistanceTravelled()));

    const int summaryLength = static_cast<int>(std::strlen(summary));
    SetTextColor(dc, RGB(255, 255, 255));
    TextOutA(
        dc,
        (clientWidth - summaryLength * 8) / 2,
        y + 22,
        summary,
        summaryLength);
}

void Renderer::paintLava(HDC dc, int clientWidth)
{
    // Lava occupies the ground from its start to the right edge of the view.
    // Its top edge is at floor height, so it meets the solid floor exactly.
    const int left = toScreenX(World::LAVA_START_X);
    if (left >= clientWidth)
    {
        return;
    }

    const int top = toScreenY(World::LAVA_TOP);
    const int bottom = toScreenY(0.0f);

    // Clip against the window so the fill stays in bounds.
    const int clippedLeft = left > 0 ? left : 0;
    const int width = clientWidth - clippedLeft;

    fillRect(dc, clippedLeft, top, width, bottom - top, LAVA_COLOR);

    // A brighter band along the surface reads as glowing lava.
    const int glow = 3;
    fillRect(dc, clippedLeft, top, width, glow, LAVA_GLOW_COLOR);
}

// True for the block the runner spawns standing on: the one whose top surface
// meets the spawn point's feet. Matched on geometry rather than on being the
// floor, so it stays correct if the spawn ever moves or the floor is resegmented.
bool Renderer::isSpawnBlock(const Block& block) const
{
    const float spawnLeft = block.getX();
    const float spawnRight = spawnLeft + block.getWidth();

    // The spawn x has to fall within the block.
    if (World::START_X < spawnLeft || World::START_X > spawnRight)
    {
        return false;
    }

    // And its top has to be the surface the runner's feet rest on.
    const float top = block.getY() + block.getHeight();
    return std::fabs(top - World::FLOOR_TOP) < 0.01f;
}

void Renderer::paintSpikes(HDC dc)
{
    // Read from the level's layout rather than the map: the map prunes platforms
    // behind the camera, and a spike still has to be drawn while the runner is
    // beside it on the way back.
    const std::vector<Platform>& platforms = game.getPlatforms();

    // One brush for the whole pass rather than one per spike, and selected once
    // outside the loops so the drawing state is not churned per triangle.
    //
    // White, as asked, which also happens to be the most readable thing on screen:
    // it has to read against the brown of a platform, the red of the lava and the
    // black behind both.
    const HBRUSH spikeBrush = brushFor(SPIKE_COLOR);
    const HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(dc, spikeBrush));

    // Null pen as well, so the triangles have no outline. The fill is the whole
    // shape, and an outline would draw an edge the collision does not have.
    const HPEN oldPen
        = static_cast<HPEN>(SelectObject(dc, GetStockObject(NULL_PEN)));

    for (size_t i = 0; i < platforms.size(); ++i)
    {
        const Platform& platform = platforms[i];

        if (platform.spikes <= 0)
        {
            continue;
        }

        const float left = platform.block.getX();
        const float right = left + platform.block.getWidth();

        if (right < game.getCameraX() || left > game.getCameraX() + World::VIEW_WIDTH)
        {
            continue;
        }

        const float top = platform.block.getY() + platform.block.getHeight();

        for (int spike = 0; spike < platform.spikes; ++spike)
        {
            // The same call the simulation collides against, so what is drawn here
            // is exactly what kills.
            const World::Triangle triangle = World::spikeAt(
                left, platform.block.getWidth(), top, spike, platform.spikes,
                platform.spikeOffset);

            POINT corners[3];
            corners[0].x = toScreenX(triangle.ax);
            corners[0].y = toScreenY(triangle.ay);
            corners[1].x = toScreenX(triangle.bx);
            corners[1].y = toScreenY(triangle.by);
            corners[2].x = toScreenX(triangle.cx);
            corners[2].y = toScreenY(triangle.cy);

            Polygon(dc, corners, 3);
        }
    }

    // Restored so the drawing state is left as it was found. The brush is cached
    // and outlives the call, so unlike the old per-pass brush it is not deleted.
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
}

void Renderer::paintBlock(HDC dc, const Block& block)
{
    // Blocks are (x, y) bottom-left in world space, so flip both corners.
    const int left = toScreenX(block.getX());
    const int right = toScreenX(block.getX() + block.getWidth());
    const int top = toScreenY(block.getY() + block.getHeight());
    const int bottom = toScreenY(block.getY());

    fillRect(dc, left, top, right - left, bottom - top,
        isSpawnBlock(block) ? SPAWN_BLOCK_COLOR : BLOCK_COLOR);
}

void Renderer::paintRunner(HDC dc, const Runner& runner)
{
    // The runner's position is its feet, so the box is drawn above it and
    // lines up with the top surface of the block it stands on.
    const int left = toScreenX(runner.getX() - World::RUNNER_HALF_SIZE);
    const int right = toScreenX(runner.getX() + World::RUNNER_HALF_SIZE);
    const int bottom = toScreenY(runner.getY());
    const int top = toScreenY(runner.getY() + World::RUNNER_SIZE);

    const bool sprinting = game.isSprinting();
    const COLORREF fillColor = sprinting ? RUNNER_SPRINT_COLOR : RUNNER_COLOR;
    const COLORREF outlineColor = sprinting
        ? RUNNER_SPRINT_OUTLINE_COLOR
        : RUNNER_OUTLINE_COLOR;

    fillRect(dc, left, top, right - left, bottom - top, fillColor);

    // Outline so the runner reads clearly against the dark background. The pen is
    // cached, so it is restored below but not deleted.
    const HPEN pen = penFor(outlineColor);
    const HPEN oldPen = static_cast<HPEN>(SelectObject(dc, pen));
    const HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(dc, GetStockObject(NULL_BRUSH)));
    RECT outline;
    outline.left = left;
    outline.top = top;
    outline.right = right;
    outline.bottom = bottom;
    Rectangle(dc, outline.left, outline.top, outline.right, outline.bottom);
    SelectObject(dc, oldBrush);
    SelectObject(dc, oldPen);
}

void Renderer::paintHud(HDC dc, double fps)
{
    // Split into two lines: the controls line is long, and adding a timer to it
    // pushed it past the window width.
    char controls[160];
    std::snprintf(
        controls,
        sizeof(controls),
        "Controls: A/D or arrows | Jump: Space/W/Up (tap again in air to "
        "double jump) | Sprint: Shift | Watch: P");

    // Minutes, seconds and hundredths, so the timer ticks visibly rather than
    // only moving once a second.
    const double seconds = game.getElapsedTime();
    const int minutes = static_cast<int>(seconds / 60.0);
    const int wholeSeconds = static_cast<int>(seconds) % 60;
    const int hundredths = static_cast<int>((seconds - static_cast<int>(seconds)) * 100.0);

    char stats[160];
    std::snprintf(
        stats,
        sizeof(stats),
        "Time: %d:%02d.%02d | Distance: %.0f / %.0f | Level: %d/%d | FPS: %.0f",
        minutes,
        wholeSeconds,
        hundredths,
        static_cast<double>(game.getDistanceTravelled()),
        static_cast<double>(World::FINISH_DISTANCE),
        game.getLevel(),
        World::LAST_LEVEL,
        fps);

    SetBkMode(dc, TRANSPARENT);

    SetTextColor(dc, RGB(255, 255, 255));
    TextOutA(dc, 10, 10, controls, static_cast<int>(std::strlen(controls)));

    // The timer sits on its own line and gets a highlight color, since it is
    // the number a run is actually scored on.
    SetTextColor(dc, RGB(255, 220, 120));
    TextOutA(dc, 10, 26, stats, static_cast<int>(std::strlen(stats)));

    // A watched run says so, and how far into the route it is, so it is obvious
    // the window is playing itself and which jump is coming.
    if (autopilotOn)
    {
        char watching[128];
        std::snprintf(
            watching,
            sizeof(watching),
            "WATCHING: autoplay  jump %d/%d   (press P to take back control)",
            autopilot.jumpsPlayed(),
            autopilot.jumpCount());

        SetTextColor(dc, RGB(150, 255, 180));
        TextOutA(dc, 10, 42, watching, static_cast<int>(std::strlen(watching)));
    }
}

int Renderer::toScreenX(float worldX) const
{
    // Subtract the camera to scroll the world. The level has no right edge, so
    // there is no clamp here.
    return static_cast<int>(worldX - game.getCameraX());
}

int Renderer::toScreenY(float worldY) const
{
    // World y grows upward; screen y grows downward. There is no vertical
    // scrolling, so this does not depend on the camera.
    return height - static_cast<int>(worldY);
}
