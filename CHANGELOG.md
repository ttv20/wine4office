# Changelog

## Unreleased

- Document initial Microsoft Teams support and currently working features.
- Detect Office crashes and XWayland hangs and keep a bounded local trace.
- Let users review, edit, attach context or a file, and manually send reports.
- Add first-open reliability and daily update-check choices; updates remain manual.

## 0.1.10 — 2026-08-04 (prerelease)

- Bundle and install Wine Gecko for both 32-bit and 64-bit Office components
  when creating a Wine environment.
- Run background services through a dedicated lightweight preload helper,
  show accurate RAM usage, and keep their controls stable during refreshes.
- Fix the **Enable & start** and **Stop & disable** button labels.
- Make standalone removal safely clean up services and shortcuts, with options
  to preserve or remove the runner and Wine environment.
- Improve OpenGL and native Wayland window, popup, and embedded-surface
  stability for Office.
- Let Office installers and updaters replace signed files while Wine verifies
  them.
- Add the Windows AppCapability access operations used by modern Office
  components.
- Add Media Foundation sensor monitoring and Office network-manager detection.
- Make Teams camera video render through shared NV12 textures.
- Place DirectComposition notifications at their requested desktop coordinates.
- Expose the WLAN interface setter used when Teams starts calls.
- Keep DirectComposition apps visible and correctly identified in KDE's taskbar,
  including working minimize and restore behavior.
- Restore theme-aware window controls for DirectComposition apps on Wayland.
- Keep DirectComposition content correctly sized when windows maximize or restore.
- Keep DirectComposition window controls aligned for both left-to-right and
  right-to-left layouts.
- Match DirectComposition window controls to tall custom title bars.
- Keep delegated DirectComposition windows synchronized when maximizing or
  restoring them.
- Prevent modern Teams calls from crashing when C++ thread operations report
  an error.
- Open CSV files in Excel and Office documents directly from archives browsed
  in KDE Dolphin.

[Full changes](https://github.com/ttv20/wine4office/compare/wine4office-v0.1.9...wine4office-v0.1.10)

## 0.1.9 — 2026-08-02 (prerelease)

- Replace the four background-service controls with **Enable & start** and
  **Stop & disable**.
- Refresh background-service status silently without replacing the visible
  message every few seconds.
- Make **Stop Wine** and runner updates pause and verify the background service
  before shutting down Wine, with graceful and bounded fallback stages.
- Add an opt-in Maintenance option to include the newest GitHub prerelease in
  update checks.
- Show a live progress popup after an update is approved, including download
  percentage, the active installation stage, operation details, and cancellation.
- Make Office save/discard exit dialogs respond immediately on native Wayland
  and prevent their unpainted first frame from flashing on XWayland.
- Stop forcing new Wine prefixes to use light application and system themes,
  allowing host color-scheme synchronization to choose the initial theme.

[Full changes](https://github.com/ttv20/wine4office/compare/wine4office-v0.1.8...wine4office-v0.1.9)

## 0.1.8 — 2026-08-01

- Reduce blank windows and flicker when Office starts.
- Improve menu and popup reliability on Wayland and X11.
- Prevent freezes caused by popup stacking on Wayland.

[Full changes](https://github.com/ttv20/wine4office/compare/wine4office-v0.1.7...wine4office-v0.1.8)

## 0.1.7 — 2026-07-31

- Complete updates started by older managers on the first restart: stop Wine,
  run `wineboot -u` with the new runner, and restore active background services.
- Use tag-specific artifact URLs for prereleases.

[Full changes](https://github.com/ttv20/wine4office/compare/wine4office-v0.1.6...wine4office-v0.1.7)

## 0.1.6 — 2026-07-31

- Update the Wine prefix and safely rebind active background services after a
  runner update.
- Restart background services after **Stop Wine** and allow stopping an older
  runner binding.
- Simplify the background-services controls and update progress in the Manager.
- Fix the repeated update prompt and show updates in the Maintenance view.

[Full changes](https://github.com/ttv20/wine4office/compare/wine4office-v0.1.5...wine4office-v0.1.6)

## 0.1.5 — 2026-07-31

- Update the runner to Wine 11.14.
- Preload App-V with Click-to-Run for faster Office startup.
- Improve Office rendering and window behavior on Wayland, including mixed
  GDI/OpenGL child windows.
- Fix several Wayland surface-locking and first-paint issues.

[Full changes](https://github.com/ttv20/wine4office/compare/wine4office-v0.1.4...wine4office-v0.1.5)

## 0.1.4 — 2026-07-31

- Add an opt-in login service that keeps Office Click-to-Run ready without
  starting Word.
- Speed up Office startup by deferring printer discovery and caching unchanged
  cloud-font scans.
- Launch Office shortcuts directly and require NTSYNC-enabled runners.
- Add per-prefix Office settings, scheduled update checks, and post-update
  maintenance hooks.

[Full changes](https://github.com/ttv20/wine4office/compare/wine4office-v0.1.3...wine4office-v0.1.4)
