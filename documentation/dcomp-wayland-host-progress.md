# DComp Wayland host implementation progress

Implementation branch: `feat/dcomp-wayland-host-20260909`.
Baseline: `origin/main` at `347abf611ff61dcdaada20e0c1faed08303b8d21`.

This file records completed evidence and open gates for the implementation
contract in `plans-to-impl/dcomp-wayland-host-20260909*.md`. A successful probe
or transport fixture is not Outlook support.

## Current work

- Added the initial `winewayland-host` built-in executable and Unix library.
- The first probe opens its own Wayland connection, verifies that the selected
  endpoint is a local Unix socket, protects against a path replacement during
  connection, and records compositor, shared-memory and seat globals.
- The executable can consume a startup permit through inherited handles,
  independently re-probe the Wayland endpoint, register with wineserver and
  publish Ready. The launcher/child path is currently exposed only by
  `--registration-test`; no application path launches a resident host yet.
  The host still does not create a native role, accept content or take window
  ownership.
- Wineserver now owns one host registration per Windows desktop. A 128-bit
  startup permit is bound to the requesting process, its direct child, the
  desktop/session and a verified endpoint/seat tuple. Successful registration
  consumes the permit and returns a server-issued `host_epoch`; stale epochs,
  duplicate hosts and mismatched endpoints are rejected. Process exit clears
  startup permits and active registration, and unused permits expire after 30
  seconds, without unpinning the desktop's native-display identity.
- Wineserver now also owns the first per-root scene transaction. Only the
  logical top-level window owner can publish `Empty` or `Hidden`, publication
  uses an expected generation and a strictly increasing owner revision, and
  only the current Ready host can query or acknowledge that generation. A
  replacement host cannot replay an old scene; the owner must publish it for
  the replacement host epoch. This tranche carries no buffers and makes no GPU
  transport claim.
- Each root now has a lazily allocated registry capped at 16 contributors.
  The logical owner creates a DComp contributor and receives a random 128-bit
  one-use grant. A producer consumes that grant to receive server-issued stream
  and binding IDs tied to the current host epoch. The host can enumerate both
  state and immutable owner/producer identity. Owner revoke, producer exit and
  host replacement reject later stream validation before publishing an
  `Empty` generation when that stream supplied the current hosted scene.
  Revoked entries remain as tombstones until the current host acknowledges
  them, after which their bounded slots can be reused.

## Contributor interception inventory

The initial source inspection found these visible-content publication points:

- DComp target and visual state: `dcomp_device_CreateTargetForHwnd()`,
  `dcomp_visual_SetContent()`, `dcomp_device_Commit()` and
  `dcomp_visual_bind_content()` in `dlls/dcomp/device.c`.
- Composition swapchain creation and the private helper HWND in
  `dlls/dxgi/factory.c`; composition binding and present-result handling in
  `dlls/dxgi/swapchain.c`.
- The Vulkan CPU composition step in
  `wined3d_swapchain_vk_apply_composition()` and final Vulkan presentation in
  `dlls/wined3d/swapchain.c`.
- GDI/window-surface publication in `WAYLAND_CreateWindowSurface()` and
  `wayland_window_surface_flush()` in
  `dlls/winewayland.drv/window_surface.c`.
- Direct Vulkan surface creation in `dlls/winewayland.drv/vulkan.c`, with the
  final queue submission crossing `win32u_vkQueuePresentKHR()` in
  `dlls/win32u/vulkan.c`.
- Direct OpenGL/EGL surface creation in `dlls/winewayland.drv/opengl.c`, with
  swaps crossing `win32u_wglSwapBuffers()` in `dlls/win32u/opengl.c`.

This list is not yet the complete admission proof. Child/owner topology, both
DComp target layers, multiple DComp devices, visibility changes and popup
families still need deterministic fixtures before generic applications can be
admitted.

## Verification record

- Source baseline and open PR heads fetched on 2026-09-09.
- PRs 29 through 32 were reviewed as sources of tests and authorization rules.
  Their helper-window ownership model and public HWND-property authority are
  intentionally not ported.
- The canonical remote build clone rebuilt `programs/winewayland-host/all` for
  x86-64, i386 and Unix after an eight-command focused dry run. A second build
  after endpoint validation changes required four commands.
- The x86-64 and i386 probes both connected to `/tmp/runtime-wine365/wayland-0`
  in the task-owned KDE environment. Both reported endpoint device 60, inode
  102742083, seat global 11, and capabilities `0xf`.
- An absolute non-socket endpoint failed with `STATUS_OBJECT_TYPE_MISMATCH`.
  A relative endpoint without `XDG_RUNTIME_DIR` failed with
  `STATUS_OBJECT_PATH_NOT_FOUND`.
- Runtime output and binary hashes are retained in
  `/workspace/artifacts/winewayland-host-probe-{x64,i386}.log` and
  `/workspace/artifacts/winewayland-host-build.sha256` in environment
  `dcomp-host-probe-20260909`.
- A public DComp host oracle now runs a window owner and composition producer
  as separate processes. Native Windows x86-64 and i386 each passed 36 checks.
  Windows rejects a foreign-process `CreateTargetForHwnd()` with
  `E_ACCESSDENIED`; the producer can still create windowless composition
  content and owns no visible top-level window. The logical owner preserves
  its HWND identity, publishes `SetRoot(NULL)` without another Present, can
  cancel `WM_CLOSE`, and receives `S_OK` from a hidden-window Present.
- The same oracle on Wine matched those results in both architectures. The
  initial Wine run contained one test-harness failure because the matching
  foreign-target rejection was incorrectly marked `todo_wine`; no product
  mismatch was found.
- The host-registration server and win32u test executables rebuilt for x86-64
  and i386. Both architecture tests passed concurrently on isolated Windows
  desktops. They cover startup contention, direct-child authentication,
  endpoint mismatch, readiness authority, peer-exit cleanup, replacement with
  a fresh epoch, stale-epoch rejection and startup cancellation.
- The real host registration fixture passed in the task-owned KDE environment
  for x86-64 and i386. In both runs the child host independently verified
  endpoint device 60, inode 102742083 and seat 11, registered epoch 1 in its
  isolated prefix, published Ready, and released its registration on exit.
  Logs are retained as
  `/workspace/artifacts/winewayland-host-registration-{x64,i386}.log`; the
  coherent reflink runner and hashes are retained at
  `/workspace/runner-dcomp-host-registration` and
  `/workspace/artifacts/registration-runner-layout.sha256`.
- The minimum scene-authority regression passed on the same KDE desktop for
  x86-64 and i386, with 66 checks and zero failures in each architecture. It
  covers publication before host readiness, invalid dispositions, generation
  compare-and-swap, monotonic owner revisions, foreign publication/query
  rejection, stale/current application acknowledgements, `Empty`/`Hidden`, and
  mandatory owner replay after host replacement. The focused headless build
  could compile both tests but could not create their HWND; the same binaries
  then passed with a real display. Logs and exact test binaries are retained at
  `/workspace/artifacts/wayland-scene-{x64,i386-debug}.log` and
  `/workspace/artifacts/scene-authority-runner`.
- The contributor/stream-authority extension passed 189 checks with zero
  failures in both x86-64 and i386, including a concurrent run on isolated
  Windows desktops. It covers foreign owner/host operations,
  invalid and consumed grants, server-issued identities, stale bindings,
  hosted scene publication, revoke-before-new-validation, producer-exit and
  host-replacement revocation, identity queries, registry exhaustion at 16
  entries, tombstone acknowledgement and slot reuse. Exact logs are retained
  as `/workspace/artifacts/wayland-contributor6-{x64,i386}.log`; the matching
  binaries and hashes are under
  `/workspace/artifacts/contributor-authority-runner`.
- Outlook topology and timing baselines are pending.

## Reproduce the current probe

The environment uses runner
`runner-wine4office-0-0-0-main-347abf611ff6`. After placing the two PE files
and Unix library recorded in `winewayland-host-build.sha256` into that
task-owned runner, run:

```sh
tools/office-test-env/office-exec.sh dcomp-host-probe-20260909 -- \
  /usr/bin/env HOME=/workspace/home USER=tester LOGNAME=tester \
  XDG_RUNTIME_DIR=/tmp/runtime-wine365 WAYLAND_DISPLAY=wayland-0 \
  WINEPREFIX=/workspace/home/.wine4office WINEDEBUG=-all \
  /workspace/runner-wine4office-0-0-0-main-347abf611ff6/bin/wine \
  winewayland-host.exe --probe
```

Pass the explicit i386 PE path in place of `winewayland-host.exe` to exercise
the WoW64 Unix-call table.
