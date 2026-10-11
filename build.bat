@echo off
REM Builds RunningGame with MinGW GCC (installed via scoop).
REM
REM If 'g++' is not found, open a NEW terminal so the updated PATH is picked up.
REM
REM Pass "tests" to build and run the unit tests instead of the game.
REM Pass "autoplay" to build and run the level-1 playthrough instead.

setlocal

if /i "%~1"=="tests" goto tests
if /i "%~1"=="autoplay" goto autoplay

g++ -std=c++17 -Wall -Wextra -O2 -mwindows ^
    Runner.cpp Block.cpp Map.cpp Game.cpp Renderer.cpp Autoplay.cpp main.cpp ^
    -o RunningGame.exe -luser32 -lgdi32 -ldwmapi

if errorlevel 1 (
    echo.
    echo BUILD FAILED
    exit /b 1
)

echo.
echo BUILD OK - run RunningGame.exe
goto done

:tests
REM The tests link only the simulation, not Renderer, so they build as a console
REM program and need none of the window libraries. Run from this directory: Game
REM reads level1.txt relative to the working directory.
g++ -std=c++17 -Wall -Wextra -O2 ^
    Runner.cpp Block.cpp Map.cpp Game.cpp tests.cpp ^
    -o RunningGameTests.exe

if errorlevel 1 (
    echo.
    echo TEST BUILD FAILED
    exit /b 1
)

echo.
echo TEST BUILD OK - running
echo.
RunningGameTests.exe
if errorlevel 1 (
    echo.
    echo TESTS FAILED
    exit /b 1
)
goto done

:autoplay
REM The playthrough links only the simulation, like the tests: it needs no window,
REM since it drives Game directly. Run from this directory so level1.txt is found.
g++ -std=c++17 -Wall -Wextra -O2 ^
    Runner.cpp Block.cpp Map.cpp Game.cpp Autoplay.cpp playthrough.cpp ^
    -o RunningGameAutoplay.exe

if errorlevel 1 (
    echo.
    echo AUTOPLAY BUILD FAILED
    exit /b 1
)

echo.
echo AUTOPLAY BUILD OK - running
echo.
RunningGameAutoplay.exe
if errorlevel 1 (
    echo.
    echo AUTOPLAY FAILED
    exit /b 1
)
goto done

:done
endlocal
