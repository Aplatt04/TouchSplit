TouchSplit - use your touch screen and your mouse at the same time
=====================================================================

A free, open-source tray app (MIT licence) that stops your touch screen from
moving the mouse pointer or stealing focus from your game.

WHAT IT DOES
  - When you tap the touch screen, the mouse pointer goes straight back to
    where your mouse left it, so your game/app doesn't see the pointer jump.
  - Windows on the touch screen are set so tapping them doesn't take keyboard
    focus away from your game.
  - If something on the touch screen does grab focus anyway, focus is handed
    back to the app you were using as soon as you lift your finger.

USING IT
  - Installed version: TouchSplit is in the Start menu. Portable version:
    double-click TouchSplit.exe. If Windows says "Windows protected your PC",
    click More info -> Run anyway (the app is not code-signed).
  - A blue icon appears in the system tray.
    (Grey icon = paused.)
  - Click the tray icon for the menu:
      TouchSplit on ..................... master switch (also Ctrl+Alt+F12)
      Put the mouse pointer back ........ the cursor-return behaviour
      Give focus back to my game/app .... the focus-return behaviour
      Stop touch-screen windows taking focus
      Start with Windows
      Open settings & log folder
  - The touch screen is detected automatically. If detection picks the wrong
    screen, set TouchMonitor in TouchSplit.ini to the right one,
    e.g. TouchMonitor=\\.\DISPLAY3 (the log lists them), then restart.

SETTINGS (TouchSplit.ini, next to TouchSplit.exe - with the installer that's
%LOCALAPPDATA%\Programs\TouchSplit)
  Enabled=1
  RestoreCursor=1
  RestoreFocus=1
  NoActivateOnTouchScreen=1
  ReleaseDelayMs=80      how long after the last touch before the pointer
                         returns (raise it if a double-tap misbehaves)
  LogTouches=1           write each touch to TouchSplit.log (first 300 per run)
  TouchMonitor=auto

GOOD TO KNOW
  - Works best with games in borderless / windowed-fullscreen mode. Exclusive
    fullscreen games can minimise the moment anything else is touched.
  - Windows that are set not to take focus can't receive typing. For a
    dashboard that's fine; untick the option if you need to type on the touch
    screen.
  - Windows running as administrator can't be changed by a normal app.
  - Exiting TouchSplit puts every window back the way it was.

UNINSTALLING
  - Installed version: Settings > Apps > TouchSplit > Uninstall.
  - Portable version: exit it from the tray menu and delete the folder.
