# Move the Mouse

A small Windows app that keeps your PC looking active by gently moving the
mouse cursor - handy for stopping the screen from locking or a status indicator
flipping to "idle/away".

Unlike a dumb always-on jiggler, it's **scheduled**: you pick a start and end
date/time, and it only moves the mouse during that window, then stops on its own.

> **Personal project, shared as-is.** An old Win32 experiment, put on GitHub in
> case it's useful. No support or warranty - use at your own risk.

## What it does
- Set a **start** and **end** date (calendars) and time (HH:MM fields).
- Click **Start Mouse Mover**. It waits until the start time, then during the
  scheduled window nudges the cursor with small random movements (plus the
  occasional bigger jump), cycling roughly 5 s of motion and 5 s of pause.
- It stops automatically at the end time - or when you click **Stop** or close
  the window. A status line shows the current state (Ready / Scheduled / Active).

## Download
Grab `MoveTheMouse-x64.exe` (or `-x86.exe`) from the [latest release](../../releases/latest)
- it's rebuilt and re-published automatically on every change. Single portable
executable, no installer. Because it's unsigned, Windows SmartScreen may warn on
first run (More info -> Run anyway).

## Build
Open `WindowsProject1.sln` in **Visual Studio 2022** (with the *Desktop
development with C++* workload) and build the **Release** configuration.

Or from a *Developer Command Prompt / PowerShell*:

```bat
msbuild WindowsProject1.sln /p:Configuration=Release /p:Platform=x64
```

CI (`.github/workflows/build.yml`) builds x64 + x86 on every push. The date/time
common controls are pulled in with `#pragma comment(lib, "Comctl32.lib")`, so no
extra project setup is needed.

## How it works
Win32 GUI (`WindowsProject1.cpp`): a 1 s timer watches for the scheduled start
time; once inside the window, a 5 ms timer calls `SendInput` with small random
offsets (with periodic larger jumps), alternating move/pause phases until the
end time is reached.
