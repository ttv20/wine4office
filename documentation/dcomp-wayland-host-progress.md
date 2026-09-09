# DComp Wayland host implementation progress

Implementation branch: `feat/dcomp-wayland-host-20260909`.
Baseline: `origin/main` at `347abf611ff61dcdaada20e0c1faed08303b8d21`.
Generated server protocol version: 980.

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
- The host now probes a native Vulkan device before advertising transport
  support. Admission requires a graphics queue that can present to the probed
  Wayland connection, opaque-FD import and export for the fixed BGRA8 transfer
  image and semaphore type, timeline semaphores, swapchain support and a
  nonzero device UUID. It also creates and destroys a logical Vulkan device
  with that exact extension set. Failure leaves the transport capability off
  and reports the rejected requirement; basic host registration and local
  fallback remain available.
- Vulkan transport admission is now bound to the probed physical device. The
  startup permit records the host's 16-byte device UUID, registration must
  present the same UUID, and host queries return the registered identity.
  Hosts without Vulkan transport must use an all-zero UUID. Wineserver accepts
  a transport pool only when the registered host advertised Vulkan transport
  and the pool UUID exactly matches that host device, so a pool cannot cross
  GPUs or outlive a fallback-only admission decision.
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
  only the current Ready host can query or acknowledge that generation.
  Non-hosted scene state is retained when no host is Ready and is made visible
  to a replacement host without changing the semantic generation. Authenticated
  `HostedContent` from an old epoch is never replayed; its stream is revoked and
  the retained state becomes `Empty`. This tranche carries no buffers and makes
  no GPU transport claim.
- Each root now has a lazily allocated registry capped at 16 contributors.
  The logical owner creates a DComp contributor and receives a random 128-bit
  one-use grant. A producer consumes that grant to receive server-issued stream
  and binding IDs tied to the current host epoch. The host can enumerate both
  state and immutable owner/producer identity. Owner revoke, producer exit and
  host replacement reject later stream validation before publishing an
  `Empty` generation when that stream supplied the current hosted scene.
  Revoked entries remain as tombstones until the current host acknowledges
  them, after which their bounded slots can be reused.
- An authenticated producer can now reserve transport-pool metadata under its
  bound stream. The server accepts only opaque BGRA8 pools with three slots, a
  nonzero GPU UUID, bounded dimensions, allocation size and frame credits. It
  retains at most two live pool generations per contributor and enforces
  256 MiB per-root and 1 GiB per-desktop reservation budgets. The current host
  can enumerate the metadata in generation order, while the producer can
  retire an old generation without allowing generation reuse. This stage does
  not yet accept resource handles, acknowledge GPU import or submit frames;
  pool metadata alone never authorizes `HostedContent`.
- Each pool now has three resource slots. The authenticated producer registers
  one D3DKMT resource plus separate ready and reuse synchronization objects per
  slot. Wineserver validates the exact object types, pins the underlying
  objects after the producer closes its handles or exits, and duplicates them
  only into the current Ready host. Explicit pool retirement releases the
  pins; revoked contributors retain them until the replacement host
  acknowledges the tombstone. Window destruction releases all remaining
  pools. Registration still does not prove native GPU import or authorize a
  transport-backed `HostedContent` scene.
- The current Ready host can now record one terminal import result for each
  registered slot. Successful and failed imports are counted separately,
  repeated identical results are idempotent, and a conflicting result or an
  acknowledgement for a revoked stream is rejected. Pool enumeration packs
  the bounded registered/imported/failed counters into one fixed-width field.
  The test host asserts import success after validating the duplicated object
  types; the real `winewayland-host` renderer still does not import Vulkan
  memory or synchronization objects, so this acknowledgement does not yet
  authorize hosted presentation.
- Producers can now submit frames through a server-authorized bounded queue
  after all three slots in a pool have imported successfully. Frame, ready and
  reuse values are nonzero and monotonic, each slot admits only one active
  frame, and source-slot reuse is independent from terminal backend completion.
  Credits remain occupied until the host reports `Presented`, `Discarded` or
  `Failed`; producers consume each result once. Pool retirement rejects live
  frame records, while contributor revocation or host replacement cancels the
  queue and credits without releasing pinned resources before tombstone
  acknowledgement. This is still an authority and lifetime contract. The real
  host does not yet wait on or copy pixels from these frames.
- DComp now publishes committed per-root scene state at the successful
  `Commit()` boundary. Targets above and below one HWND share a private scene
  transaction even when they belong to different DComp devices. A root in the
  committed tree publishes `LocalFallback`; only removal of the final layer
  publishes `Empty`. Neither disposition claims a hosted GPU stream, and the
  legacy local presentation path remains active. Publication is edge-triggered:
  setters and unchanged/visual-only commits perform no host IPC. Releasing an
  applied target publishes the same committed removal after local unbinding,
  without requiring another Commit. The server validates the real top-level
  HWND owner; child-window targets remain local and are not admitted by this
  first topology.
- A failed publication returns the server's current scene generation and
  owner revision in the fixed reply so the owner can resynchronize its
  compare-and-swap once without exposing the scene to another process. The
  server now accepts committed non-hosted dispositions before host startup and
  attaches the latest one to a new host epoch at `HostReady`. Repeated
  `HostReady` calls are idempotent, and activation neither increments the scene
  generation nor republishes over a newer `Hidden` or authenticated
  `HostedContent` disposition. DComp Commit remains edge-triggered and does not
  poll for host state.

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
- The committed DComp scene adapter passed 218 checks with zero failures in
  both x86-64 and i386 on the task KDE desktop. The fixture uses below and
  above targets owned by separate DComp devices and verifies
  `LocalFallback -> Empty`, no partial publication when only one layer is
  removed, no generation change for an unchanged Commit, CAS generation
  continuity, target-release removal without another Commit, and a fresh
  transaction after host replacement. The public
  cross-process DComp oracle also remained at 36 checks with zero failures in
  both architectures. Logs are retained as
  `/workspace/artifacts/dcomp-scene-cas-{x64,i386}.log` and
  `/workspace/artifacts/dcomp-scene-cas-dcomp-host-{x64,i386}.log`; the
  coherent runner, exact test binaries and hashes are retained at
  `/workspace/runner-dcomp-empty-scene` and
  `/workspace/artifacts/dcomp-empty-scene-runner`.
- The retained-scene activation extension passed 238 checks with zero failures
  in both x86-64 and i386 on the task KDE desktop. It commits a DComp root before
  any host exists, verifies delivery when the host becomes Ready, repeats
  `HostReady` without changing generation, preserves newer `Hidden` and
  authenticated `HostedContent` state, and delivers the retained current scene
  to a replacement host without another application Commit. The public
  cross-process DComp oracle remained at 36 checks with zero failures in both
  architectures. Logs are retained as
  `/workspace/artifacts/dcomp-scene-replay-{x64b,i386b}.log` and
  `/workspace/artifacts/dcomp-scene-replay-dcomp-host-{x64,i386}.log`; the
  coherent runner and exact test binaries are retained at
  `/workspace/runner-dcomp-scene-replay` and
  `/workspace/artifacts/scene-replay-win32u-test-{x64,i386}.exe`.
- The initial transport-pool authority passed 269 checks with zero failures in
  both x86-64 and i386 on protocol 975. It covers producer-only registration,
  host-only enumeration, fixed BGRA8/three-slot metadata, truncated metadata,
  zero UUID, invalid dimensions, excessive frame credits and allocation size,
  per-root budget exhaustion, the two-live-generation limit, monotonic
  generation reuse prevention, retirement and ordered enumeration. The public
  DComp oracle remained at 36 checks with zero failures in both architectures.
  Logs are retained as
  `/workspace/artifacts/dcomp-pool-authority-{x64c,i386c}.log` and
  `/workspace/artifacts/dcomp-pool-authority-dcomp-host-{x64,i386}.log`; the
  coherent runner and exact test binaries are retained at
  `/workspace/runner-dcomp-pool-authority` and
  `/workspace/artifacts/pool-authority-win32u-test-{x64,i386}.exe`.
- The resource-slot authority passed 324 checks with zero failures in both
  x86-64 and i386 on protocol 976. It rejects foreign producers, wrong object
  types, shared ready/reuse objects, duplicate or aliased slots and out-of-range slots;
  registers all three slots; duplicates pinned objects into the host; and
  proves the objects remain alive after producer-handle closure. Global-object
  queries also prove release after explicit pool retirement and after the
  replacement host acknowledges a revoked contributor. The public DComp host
  oracle remained at 36 checks with zero failures in both architectures. Logs
  are retained as `/workspace/artifacts/dcomp-slot-authority-{x64,i386}.log`
  and `/workspace/artifacts/dcomp-slot-authority-dcomp-host-{x64,i386}.log`;
  the coherent runner, exact test binaries and hashes are retained at
  `/workspace/runner-dcomp-slot-authority`,
  `/workspace/artifacts/slot-authority-win32u-test-{x64,i386}.exe` and
  `/workspace/artifacts/dcomp-slot-authority-SHA256SUMS`.
- The host-import acknowledgement extension passed 363 checks with zero
  failures in both x86-64 and i386 on protocol 977. It covers invalid and
  foreign results, successful and failed terminal imports, idempotent replay,
  conflicting-result rejection, per-pool counters, all-three-slot completion,
  and rejection of a replacement host's attempt to change a revoked stream.
  The public DComp host oracle remained at 36 checks with zero failures in both
  architectures. Logs are retained as
  `/workspace/artifacts/dcomp-import-authority-{x64,i386}.log` and
  `/workspace/artifacts/dcomp-import-authority-dcomp-host-{x64,i386}.log`; the
  coherent runner and exact test binaries are retained at
  `/workspace/runner-dcomp-import-authority` and
  `/workspace/artifacts/import-authority-win32u-test-{x64,i386}.exe`.
- The bounded frame-queue extension passed 457 checks with zero failures in
  both x86-64 and i386 on protocol 978. It covers malformed and unauthorized
  submissions, all-slot import gating, monotonic frame/ready/reuse values,
  slot exclusion, credit retention after source reuse, reverse-order reuse,
  idempotent and conflicting completion, one-time producer result consumption,
  pool-retirement blocking and cancellation on host replacement. The public
  cross-process DComp oracle remained at 36 checks with zero failures in both
  architectures. Logs are retained as
  `/workspace/artifacts/dcomp-frame-authority-{x64,i386}.log` and
  `/workspace/artifacts/dcomp-frame-authority-dcomp-host-{x64,i386}.log`; the
  coherent runner, exact test binaries and hashes are retained at
  `/workspace/runner-dcomp-frame-authority`,
  `/workspace/artifacts/frame-authority-win32u-test-{x64,i386}.exe` and
  `/workspace/artifacts/dcomp-frame-authority-SHA256SUMS`.
- The native Vulkan admission probe rebuilt without new warnings and the
  protocol-979 authority fixture passed 457 checks with zero failures in both
  x86-64 and i386, including positive server acceptance of the new transport
  capability. The public DComp oracle remained at 36 checks with zero failures
  in both architectures. On the task Radeon HD 5670 environment, both real
  host probes correctly withheld transport capability because Vulkan reported
  zero opaque-FD external-semaphore features; fallback host registration then
  succeeded in both architectures with capabilities `0xf`. Logs are retained
  as `/workspace/artifacts/dcomp-vulkan-{probe,registration,authority,oracle}-{x64,i386}.log`.
- The device-binding extension passed 466 checks with zero failures in both
  x86-64 and i386 on protocol 980. It covers short and oversized UUID payloads,
  startup and registration UUID retention, registration mismatch, public
  host-query identity, and rejection of a producer pool whose UUID differs
  from the registered host GPU. The real Radeon HD 5670
  probe and registration remained on the honest fallback path with
  capabilities `0xf` and an all-zero UUID. The public DComp oracle remained at
  36 checks with zero failures in both architectures. Logs and exact test
  binaries are retained as
  `/workspace/artifacts/dcomp-device-binding-final-authority-{x64,i386}.log`,
  `/workspace/artifacts/dcomp-device-binding-{probe,registration,oracle}-{x64,i386}.log`,
  `/workspace/artifacts/device-binding-final-win32u-test-{x64,i386}.exe`, and the
  coherent runner is `/workspace/runner-dcomp-device-binding`.
- The broader x86-64 DComp device pixel test remains unsuitable as a clean
  gate in this KDE/R600 environment: the task runner reported three existing
  transform/opacity/composite pixel failures, while the unchanged baseline
  runner reported five in the same areas. The task-specific authority and
  public contract fixtures are unaffected.
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
