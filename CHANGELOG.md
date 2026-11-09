# Changelog

## 1.1.0 — 2026-11-08
- New installer (`TouchSplit-Setup-1.1.0.exe`): per-user, no admin rights, Start menu entry, optional start-with-Windows
  and desktop shortcut, uninstall from *Settings > Apps*. Upgrades close the running copy cleanly and keep your settings.
- Portable zip still available.
- Tray menu now shows the version number.
- Touch-screen windows are released cleanly when Windows shuts down or you sign out.
- GitHub Actions builds and attaches the installer, portable zip and checksums to each release.

## 1.0.1 — 2026-11-07
- Minimised windows are no longer marked as touch-screen windows, so they restore normally from the taskbar.

## 1.0 — 2026-11-07
- First release: pointer return after touch, focus return after touch, non-activating touch-screen windows,
  automatic touch-screen detection, tray menu, Ctrl+Alt+F12 toggle, start with Windows.
