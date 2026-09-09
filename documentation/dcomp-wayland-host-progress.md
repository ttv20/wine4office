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
- The executable currently runs only with `--probe`. It does not register with
  wineserver, create a native role, accept content, or take window ownership.

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
- Native Windows fixture and Outlook baseline are pending.
