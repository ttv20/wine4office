# Changelog

## Unreleased

- Reduce Word CPU use by coalescing Office timers that allow delayed delivery.
- Respect Direct2D stroke join styles while keeping sharp WordArt corners bounded.
- Keep Word responsive while Microsoft 365 tokens are checked or refreshed in the background.
- Keep Microsoft 365 account and token data consistent when sign-in and background refresh overlap.
- Prevent Word from getting stuck on "Just a moment" after signing in to Microsoft 365.
- Build Wine4Office releases and agent runners from the same reproducible 18-job environment.
- Display background-service RAM without reclaimable inactive file cache and hide it when the service is stopped.
- Activate Microsoft 365 licenses after signing in to Office.
- Keep Wine environment recreation responsive and show live progress and logs in the Manager.
- Prevent Word from crashing when opening Microsoft 365 sign-in.
- Keep Word responsive and fully render its welcome screen after updating to WineHQ 11.16.
- Reduce Office redraw work for partial document and ribbon updates.
- Prevent Word from crashing when opening the References tab.
- Fix Microsoft 365 sign-in for Outlook.com accounts.
- Keep Office responsive while DirectX completion events are pending or OpenGL windows are covered on Wayland.
- Keep keyboard input synchronized with the active host layout when returning to Wine Wayland windows.

## 0.2.1-beta.1 — 2026-08-23

- Update the Wine base to WineHQ 11.16.
- Enable VA-API hardware H.264 video decoding with Vulkan-shared surfaces.
- Update bundled Wine Mono to 11.3.0 and improve ARM64EC exception handling.
- Include WineHQ fixes for WebView2 input, Wine Wayland, WPF, TLS,
  certificates, and other application compatibility issues.
- Open Microsoft 365 sign-in pages correctly in Word.
- Allow 60 seconds for the Office installer to open and report startup timeouts clearly.
- Show localized graceful-close and forced-kill progress while stopping the selected Wine environment.
- Improve Wine runner and application-status translations.
- Keep Word responsive while loading Office Click-to-Run component manifests,
  presenting startup frames, and drawing antialiased content.
- Install exact tagged releases and safely close active Wine4Office sessions
  before updating.
- Let opted-in prerelease updates pass verification and installation.

[Full changes](https://github.com/ttv20/wine4office/compare/0.2.0-beta.1...0.2.1-beta.1)

## 0.2.0-beta.1 — 2026-08-19

- Let Office finish proofing-language updates without crashing Click-to-Run.
- Let Office updates detect and request closing running Office apps.
- Keep Office color palettes visible while moving the pointer and reveal them
  after their XWayland transition completes.
- Prevent the first Word Design-tab load from stalling on queued Direct2D work.
- Respect application frame preferences and prevent Office window borders from blinking.
- Stop only the selected Wine environment, with a graceful ten-second deadline.
- Fully remove Manager files after installs and updates, with visible removal progress.

[Full changes](https://github.com/ttv20/wine4office/compare/0.2.0-alpha.2...0.2.0-beta.1)

## 0.2.0-alpha.2 — 2026-08-18

- Restore Office proofing and language-pack discovery.
- Make Office grace periods start, persist, count down, and expire like Windows.
- Restore App-V preload through Office's logical Click-to-Run package path.
- Fix Office color picker popups on Wayland.
- Fix resizing Office windows with DirectComposition content on Wayland.
- Keep Wine and background-service shutdowns bounded and environment-specific.
- Fix Office dialogs turning white during pointer movement.

[Full changes](https://github.com/ttv20/wine4office/compare/0.2.0-alpha...0.2.0-alpha.2)

## 0.2.0-alpha — 2026-08-11

- Make release archives reproducible without breaking linked files.
- Make release packaging failures report the exact check that failed.
- Add a new Wine4Office website with installation guidance.
- Make updates, removal, background services, and recovery safer.
- Improve Office installation and Click-to-Run reliability.
- Improve Teams installation and keep progress visible.
- Install the components required by Teams automatically.
- Improve Microsoft 365 sign-in and account security.
- Improve Teams calls, notifications, and background activity.
- Improve modern Office and Teams application support.
- Prevent damaged or unsafe application packages from being installed.
- Improve downloads used by Office installation and updates.
- Improve Office graphics across OpenGL, Vulkan, and Direct3D.
- Improve Teams window rendering, resizing, ordering, and input.
- Improve screen sharing and capture across multiple displays.
- Fix more popup, notification-area, and window-management problems on Wayland.
- Prevent PowerPoint ribbon galleries and transition previews from freezing.
- Fix white Microsoft 365 sign-in windows in Word.
- Improve Hebrew fonts, application icons, and desktop shortcuts.
- Improve compatibility with Office documents, XML features, and rich text.
- Prevent Wine4Office from closing unrelated applications.
- Improve stability when Office processes stop, restart, or crash.

[Full changes](https://github.com/ttv20/wine4office/compare/wine4office-v0.1.11-rc.2...0.2.0-alpha)

## 0.1.11-rc.2 — 2026-08-07 (prerelease)

- Add Teams shortcuts and show application compatibility in the Manager.
- Restart the Manager in place after installing an update.
- Do not treat an app display-mode change as a different background-service environment.
- Show persistent action status and an Office-installer startup popup in the Manager.
- Add Access, OneNote, Publisher, Visio, and Project shortcuts, plus Visio and Project install choices.
- Store every Manager translation in its own locale file and localize all application statuses.
- Prevent regular DXGI windows from being hidden while presenting without a
  DirectComposition target, fixing Office startup windows that stayed blank.
- Disable XVidMode by default for new and existing X11 Office environments to
  prevent XRDP gamma failures from crashing installers and Office apps.
- Fix Microsoft 365 sign-in and activation by returning WAM token expiration
  values in Windows epoch seconds and using Office's working network fallback.

[Full changes](https://github.com/ttv20/wine4office/compare/wine4office-v0.1.11-rc.1...wine4office-v0.1.11-rc.2)

## 0.1.11-rc.1 — 2026-08-06 (prerelease)

- Make the standalone Manager use KDE's native theme while preserving its
  standard Qt behavior on other desktops.
- Translate the Manager into 47 languages with correct right-to-left layouts.
- Install Office directly without asking users to save generated ODT XML.
- Install Teams separately with Microsoft's standalone bootstrapper.
- Add opt-in incident reports with bounded local traces, review, editing, and
  attachments.
- Add first-open reliability choices and optional daily update checks.
- Document initial Microsoft Teams support and currently working features.

[Full changes](https://github.com/ttv20/wine4office/compare/wine4office-v0.1.10...wine4office-v0.1.11-rc.1)

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
