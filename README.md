<p align="center"><img src="docs/icon.png" width="96" alt="TouchSplit icon"></p>

# TouchSplit

**Use a touchscreen and a mouse at the same time on Windows, without the touchscreen hijacking your mouse pointer or your game.**

By default, Windows moves the mouse pointer to wherever you touch and hands keyboard focus to whatever you tapped. If your touchscreen is a control panel (a stream deck, a sim-racing or flight-sim button box, a mixer, a dashboard), that ruins whatever you're doing on your main screen: the cursor jumps, and your game loses focus.

TouchSplit fixes that. It's a small, free tray app with no install and no dependencies.

## What it does

| | Windows default | With TouchSplit |
|---|---|---|
| Tap the touchscreen | Mouse pointer jumps to the tap | Pointer goes straight back to where your mouse was |
| Tap a window on the touchscreen | It takes keyboard focus | Your game/app keeps focus |
| Something on the touchscreen grabs focus anyway | Stays there | Focus is handed back when you lift your finger |

- Detects your touchscreen automatically, even with several monitors.
- Tray icon: blue means on, grey means paused. **Ctrl+Alt+F12** toggles it.
- Each behaviour can be switched on or off separately from the tray menu.
- Optional "Start with Windows".
- Proper installer and uninstaller, or a portable zip if you prefer.
- Exiting puts every window back exactly as it was.

## Install

Download from the [latest release](../../releases/latest):

- **`TouchSplit-Setup-x.y.z.exe`** (recommended). A normal installer: no admin rights needed, adds a Start menu entry, can start TouchSplit with Windows, and appears in *Settings > Apps* so you can uninstall it like any other app. Running a newer installer upgrades in place and keeps your settings.
- **`TouchSplit-x.y.z-portable.zip`**: no install, just extract and run `TouchSplit.exe`. Settings are saved next to the exe.

Windows may show **"Windows protected your PC"** the first time. TouchSplit isn't code-signed (certificates cost money), so click **More info > Run anyway**. The full source is in this repo, and every release file is built from it by GitHub Actions, with SHA-256 checksums in `SHA256SUMS.txt`.

Requires Windows 10 or 11 (64-bit).

For silent installs (e.g. deploying to several PCs): `TouchSplit-Setup-x.y.z.exe /S`. Uninstall silently with `"%LOCALAPPDATA%\Programs\TouchSplit\Uninstall.exe" /S`.

## Settings

The tray menu covers everything. `TouchSplit.ini` is created next to the exe for fine-tuning:

| Setting | Default | Meaning |
|---|---|---|
| `Enabled` | `1` | Master switch |
| `RestoreCursor` | `1` | Put the mouse pointer back after a touch |
| `RestoreFocus` | `1` | Hand focus back to your app after a touch |
| `NoActivateOnTouchScreen` | `1` | Windows on the touchscreen don't take focus when tapped |
| `ReleaseDelayMs` | `80` | How long after the last touch before the pointer returns. Raise it if double-taps misbehave |
| `LogTouches` | `1` | Write touches to `TouchSplit.log` (first 300 per run) for troubleshooting |
| `TouchMonitor` | `auto` | Force a specific display, e.g. `\\.\DISPLAY3` (the log lists them) |

## Good to know

- **Games:** works best with games in *borderless* / *windowed fullscreen*. Exclusive-fullscreen games can minimise whenever anything else is touched.
- **Typing on the touchscreen:** windows set to not take focus can't receive keyboard input. Fine for dashboards; untick the option if you need to type there.
- **Admin windows:** apps running as administrator can't be adjusted by a normal app (Windows security). Run TouchSplit as admin if you need that.
- **Antivirus:** TouchSplit uses a low-level mouse hook (the standard Windows way to tell touch from mouse). A few antivirus products are wary of hooks in unsigned apps. Building from source is always an option.

## How it works

- A **low-level mouse hook** (`WH_MOUSE_LL`) sees every mouse event. Events that Windows synthesises from touch or pen carry a signature (`0xFF5157xx`) in `dwExtraInfo`, so real-mouse and touch input can be told apart. The hook only reads a few fields and returns, so it adds no noticeable input latency.
- When a touch ends, the pointer is moved back with `SetCursorPos`, and if focus was pulled onto the touchscreen it's restored to the previous foreground window.
- If you move the real mouse while the pointer is still parked on the touchscreen, the first movement is used to snap it back first. Movement then carries on from where your mouse was, not from where you touched.
- Top-level windows on the touchscreen get `WS_EX_NOACTIVATE`, which lets them receive taps without taking focus. The style is removed when they leave that screen, are minimised, or when TouchSplit exits.
- Raw Input on the HID digitizer usage page is used as a second touch signal.
- The touchscreen is identified with `GetPointerDevices`, or learned from the first touch.

All of this is in one C file: [`src/touchsplit.c`](src/touchsplit.c).

## Build from source

**Linux / macOS / WSL** (needs `mingw-w64`, plus `nsis` and `zip` for the installer):
```sh
make          # TouchSplit.exe
make dist     # + installer, portable zip and SHA256SUMS.txt in dist/
```

**Windows** with [w64devkit](https://github.com/skeeto/w64devkit) or MSYS2 MinGW on PATH:
```bat
build.bat
```

The version number lives in one place: [`src/version.h`](src/version.h).

### Making a release

1. Bump the version in `src/version.h` and add a line to `CHANGELOG.md`, then commit.
2. On GitHub: **Releases > Draft a new release**, create a tag matching the version (e.g. `v1.1.0`) and publish.
3. GitHub Actions builds the installer, portable zip and checksums and attaches them to the release (usually within a couple of minutes).

## Licence

[MIT](LICENSE). Free to use, change and share.

*TouchSplit is an independent project, not affiliated with any commercial touchscreen software.*
