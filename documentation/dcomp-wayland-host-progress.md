# DComp Wayland host implementation progress

Implementation branch: `feat/dcomp-wayland-host-20260909`.
Baseline: `origin/main` at `347abf611ff61dcdaada20e0c1faed08303b8d21`.
Generated server protocol version: 998.

This file records completed evidence and open gates for the implementation
contract in `plans-to-impl/dcomp-wayland-host-20260909*.md`. A successful probe
or transport fixture is not Outlook support.

## Current work

- Added the initial `winewayland-host` built-in executable and Unix library.
- The first probe opens its own Wayland connection, verifies that the selected
  endpoint is a local Unix socket, protects against a path replacement during
  connection, and records compositor, shared-memory and seat globals.
- The executable consumes a startup permit through inherited handles,
  independently re-probes the Wayland endpoint, registers with wineserver and
  publishes Ready. DComp launches it on demand for the first eligible commit.
  Concurrent launchers converge on one detached Wine system process, which
  remains resident until the matching wineserver shuts down. The host binds
  the version-1 `xdg_wm_base` subset, creates retained native roots after an
  authenticated ownership transfer, answers compositor pings and pumps
  Wayland events without blocking the server scan.
- The host now probes a native Vulkan device before advertising transport
  support. Admission requires a graphics queue that can present to the probed
  Wayland connection and to a real unmapped `wl_surface` created on that exact
  socket. The surface must expose an sRGB BGRA8 format and both color-attachment
  and transfer-destination usage. Admission also requires opaque-FD import and
  export for the fixed BGRA8 transfer image and timeline semaphore type,
  Vulkan 1.2, swapchain support and a nonzero device UUID. It creates and
  destroys a logical Vulkan device with that exact extension set. Failure
  leaves the transport capability off and reports the rejected requirement;
  basic host registration and local fallback remain available.
- The host Unix renderer now has a bounded native Vulkan import primitive. It
  recreates the fixed BGRA8 optimal-tiling transfer image, requires the exact
  producer allocation size and memory-type index, imports opaque-FD dedicated
  memory, and imports separate ready/reuse timeline semaphores transactionally.
  At most two three-slot pool generations remain live; duplicate metadata is
  idempotent, conflicting metadata is rejected, and pool retirement or renderer
  teardown destroys every partial or complete import and closes unconsumed FDs.
  The registered host also recreates this renderer on the exact admitted GPU
  before publishing Ready. The renderer now checks ready timelines without
  waiting, submits an external-ownership acquire and image copy only after the
  requested value is visible, and signals the matching reuse timeline when that
  GPU submission completes. It retains a bounded host-owned image for later WSI
  work. Teardown polls all tracked fences and also retains self-test exporter
  objects across a bounded-wait timeout rather than destroying a live device.
  The transport self-test clears the producer image to a known color and reads
  every copied host pixel back through a host-visible staging buffer.
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
- Pool metadata now includes the producer's Vulkan memory-type index. Opaque-FD
  memory deliberately cannot be queried with `vkGetMemoryFdPropertiesKHR`, so
  the allocation size and GPU UUID are not enough to reconstruct a valid
  `VkMemoryAllocateInfo` in the host. Wineserver bounds the index to Vulkan's
  32 memory types and returns it in the unused high byte of the existing packed
  pool-info field without enlarging the fixed server reply.
- Each pool now has three resource slots. The authenticated producer registers
  one D3DKMT resource plus separate ready and reuse synchronization objects per
  slot. Wineserver validates the exact object types, pins the underlying
  objects after the producer closes its handles or exits, and duplicates them
  only into the current Ready host. Explicit pool retirement releases the
  pins; revoked contributors retain them until the replacement host
  acknowledges the tombstone. Window destruction releases all remaining
  pools. Registration still does not authorize a transport-backed
  `HostedContent` scene.
- The current Ready host can now record one terminal import result for each
  registered slot. Successful and failed imports are counted separately,
  repeated identical results are idempotent, and a conflicting result or an
  acknowledgement for a revoked stream is rejected. Pool enumeration packs
  the bounded registered/imported/failed counters into one fixed-width field.
  The real host reports success only after importing Vulkan memory and both
  synchronization objects; this acknowledgement does not yet authorize hosted
  presentation.
- The server now lets only the current Ready host enumerate top-level roots
  that carry scene or contributor state. The registered host walks each root,
  contributor, pool and slot, converts the three duplicated D3DKMT handles to
  owned Unix FDs, and calls the native Vulkan importer. It records `IMPORTED`
  only after memory plus both timeline semaphores succeed, records `FAILED` on
  a terminal conversion/import failure, and closes every duplicated handle and
  unconsumed FD. Host-local pool identities keep equal producer generation
  numbers from aliasing across roots or contributors. A complete scan also
  retires native imports for pools that disappeared; contributor revocation
  retires its imports before acknowledging the server tombstone. Pool and
  copied-frame budgets are partitioned per root rather than shared across the
  desktop.
- Root enumeration now returns a server shared-object identity and the USER
  handle lifetime generation in addition to the HWND. The renderer keys native
  roots by that pair, not by the reusable HWND value. A transport-capable host
  creates and retains one unmapped `wl_surface`, `xdg_surface` and
  `xdg_toplevel` per enumerated root, dispatches its configure events, and
  destroys the native objects when the root disappears from a complete scan.
  Repeating a root update is idempotent, while a new lifetime generation first
  replaces the old native objects. The root remains unmapped until a later
  configured extent explicitly creates and presents through its WSI.
- Each persistent root now also owns a `VkSurfaceKHR`. Supplying a configured
  extent creates a bounded FIFO swapchain with at most eight BGRA8 sRGB images;
  changing the requested extent creates a replacement with the old swapchain
  passed to Vulkan for retirement. The first WSI primitive probes acquisition
  with timeout zero, clears one acquired image, submits the layout transitions,
  and presents it. A queue-ordered fence retains the command buffer and binary
  semaphores until Vulkan has processed the presentation operation. A
  nonblocking poll returns pending and prevents root or renderer destruction
  while those objects are still in flight. The same primitive can copy a completed
  host-owned transport frame instead of clearing the WSI image. That source
  frame is pinned through the queue-ordered fence, so pool retirement cannot
  destroy it during the WSI read. Frame release now explicitly destroys the
  immutable host copy after its final WSI reader. Queue submission and
  `vkQueuePresentKHR` run on each root's bounded 18-entry executor instead of
  the host event-loop thread. Each root and copied frame retains its own
  completion state, device and queue, so the event loop and unrelated roots
  continue progressing while one Vulkan present is pending.
  Transport admission also requires `VK_KHR_present_id` and
  `VK_KHR_present_wait`. Every WSI submission carries a nonzero per-root
  present ID, and the executor waits up to one second for that exact ID before
  publishing a terminal backend result. A timeout retains the geometry token
  and requeues a bounded wait-only job; it does not report `Presented` or let
  newer geometry overtake the unresolved native commit. Terminal wait and
  queue failures drain or retire the affected swapchain resources before
  releasing the token.
- Producers can now submit frames through a server-authorized bounded queue
  after all three slots in a pool have imported successfully. Frame, ready and
  reuse values are nonzero and monotonic, each slot admits only one active
  frame, and source-slot reuse is independent from terminal backend completion.
  Credits remain occupied until the host reports `Presented`, `Discarded` or
  `Failed`; producers consume each result once. Pool retirement rejects live
  frame records, while contributor revocation or host replacement cancels the
  queue and credits without releasing pinned resources before tombstone
  acknowledgement. The real host now enumerates these accepted frames and
  copies each ready producer slot into a host-owned record, with no more than
  16 records admitted for any one root.
  It marks the source slot reusable only after fence completion. A frame-level
  failure cannot become terminal until the host has made the source slot safe.
  If local allocation, command preparation, executor admission or Vulkan queue
  submission fails after producer readiness, the host signals the imported
  reuse timeline without reading the source. It then destroys any partial
  immutable copy and reports one terminal failure; an unsuccessful reuse
  signal remains pending instead of releasing the producer unsafely.
- Each accepted frame now records the aggregate scene generation and binding
  generation observed by its producer. The server accepts it only while the
  current scene is `HostedContent` for the same contributor, stream and
  binding under the current host epoch. `Empty`, `Hidden`, `LocalFallback`, a
  stale scene generation and a mismatched binding are rejected before queue
  state changes. Host enumeration returns both generations, including the
  original scene generation after a newer scene replaces an already accepted
  frame, so the host can safely drain and discard stale GPU work.
- The real host work scan now connects those accepted frames to each retained
  root WSI. It copies a ready source, reports the transport slot reusable,
  rejects a frame whose recorded scene or binding no longer matches, sizes the
  swapchain from the authorized pool, and submits the immutable host copy.
  `Presented`, `Discarded` and `Failed` are reported only after the WSI read is
  terminal and the native frame release succeeds. A revoke or disappearing
  root keeps the renderer pool pinned until an already submitted read drains.
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
- DComp now admits the first real hosted producer topology. The committed
  scene must contain exactly one visible, child-free, identity-mapped BGRA8
  composition swapchain whose extent matches the root client area. DComp
  creates the contributor, grant and binding, publishes `HostedContent`, and
  passes the server-issued tuple into DXGI and WineD3D. Removal or fallback
  revokes that tuple. Other visual trees continue through local presentation.
- WineD3D's Vulkan backend now allocates a three-slot opaque-FD transport pool
  on the admitted GPU. Each slot owns an exportable BGRA8 image and independent
  ready/reuse timeline semaphores. Present copies the application backbuffer,
  submits the authorized frame to wineserver and leaves the public Present
  record pending. A bounded completion worker wakes only while frames are in
  flight, consumes the host's terminal result, completes the matching Present
  record and returns one frame-latency credit exactly once. Binding replacement
  and shutdown retain the tuple needed to drain already accepted frames.
- Wineserver now publishes an authorized logical-window snapshot to the current
  host. The snapshot includes a monotonic state revision, styles, outer and
  client rectangles, and a bounded UTF-16 title. Title, style and geometry
  changes advance the revision. The host converts the title to bounded UTF-8,
  applies it to its `xdg_toplevel`, and sets matching xdg window geometry when
  it creates or resizes the WSI swapchain. Foreign processes cannot query the
  snapshot.
- An xdg-toplevel close event now crosses an authenticated server operation
  before it reaches the application. The request carries the host epoch, shared
  root identity, USER handle generation and a monotonic request ID. Wineserver
  validates all four, then posts `WM_CLOSE` to the existing owner thread's
  normal Windows message queue. Replaying the same ID is idempotent and an
  older ID or recycled-root tuple is rejected, so a host retry cannot deliver
  two close requests or close a new window that reused the HWND value.
- Xdg configure events now cross a bounded request/apply/ack path instead of
  being acknowledged immediately by the host. The Unix renderer retains the
  raw Wayland serial locally and exposes only a host-monotonic request ID,
  dimensions and shell state. Wineserver authenticates the host epoch plus the
  shared root identity and USER generation, keeps one coalescing configure slot
  per logical window, and posts a payload-free driver doorbell to the owner
  thread. `winewayland.drv` pulls that slot, applies sizing through the normal
  Wine window path, and publishes the resulting state revision and logical
  extent. The host acknowledges only the matching retained serial after that
  applied ID returns. Stale applies are harmless, conflicting replays fail,
  and a replacement host can restart its request numbering without accepting
  an old epoch's response. The current host reports scale 120 because its
  minimal shell adapter does not yet bind fractional-scale output state.
- Window metadata and transport geometry now use separate revisions. Title,
  style and other semantic changes still advance the window-state revision,
  while only a client-extent change advances the geometry revision that binds
  a pool and frame. A title-only update therefore cannot invalidate an
  otherwise current transport pool.
- WineD3D reserves each hosted frame with wineserver before submitting its GPU
  copy. A server rejection is returned as the real failure instead of silently
  presenting through the local helper window. If command-buffer allocation or
  GPU submission then fails, the producer cancels the accepted frame through
  an authenticated idempotent request and recovers its credit.
- The host now applies non-content scenes to the native root. `Empty` submits
  a current-generation black output without waiting for a producer frame.
  `Hidden` and `LocalFallback` drain presentation, unmap the root with a null
  attachment, and wait for an asynchronous display-sync callback before
  reporting `SceneApplied`. Remapping performs the xdg-shell-required
  bufferless commit, receives and applies a fresh configure, and only then
  recreates WSI and presents. Xdg title and app-id state are restored because
  the protocol discards toplevel attributes on unmap.
- Win32 visibility now gates a hosted root without changing its committed
  DComp scene generation. Hiding the logical root rejects new producer frames
  and native input, drains the current presentation and unmaps the xdg root.
  The host continues enumerating contributors and pools while hidden, so it
  preserves valid Vulkan imports and still observes pool retirement or
  contributor revocation. Showing the window first remaps the last authorized
  frame snapshot through a fresh xdg configure, then resumes queued producer
  frames only after that presentation completes.
- The resident host now binds the exact admitted `wl_seat` and drains bounded
  256-entry keyboard and pointer queues. Pointer motion coalesces at the tail;
  a queued motion is evicted before a new button, wheel or key edge when the
  queue is full. The PE side translates surface-local pointer coordinates,
  Linux button codes and evdev key codes into Wine's normal hardware-input
  path. Wineserver accepts an event only from the current Ready host for the
  exact root identity and USER lifetime generation while the scene and native
  lease are both Hosted, and rejects duplicate or stale event IDs. Stale
  events are discarded rather than retried. Keyboard leave, seat capability
  loss and focused-root retirement now queue an authenticated reset that
  releases Wine's depressed keys or pointer buttons. If a pure-edge burst
  fills the queue, the host replaces one edge with a full input reset instead
  of risking a permanently pressed key. Physical modifier reconciliation,
  IME/text input and a compositor-originated pointer-recipient fixture remain
  pending.
- Each native root now owns a separate Vulkan logical device, presentation
  queue, command pool and bounded worker. Imported pool slots and immutable
  host copies are bound to that root's device, and root retirement waits until
  their last frame and WSI reader are gone before destroying it. A stalled
  present-wait on one root therefore cannot block GPU submission or present
  completion on another root. The renderer ABI carries the server-issued root
  identity on every import. Host-owned frame records are partitioned into a
  16-frame budget per root, so one producer cannot consume the global record
  pool. Wineserver now admits independent contributors on multiple logical
  roots in the same desktop.
- Native non-client rendering is part of the host WSI image. DComp may admit
  an identity composition swapchain whose extent matches the client area even
  when the root has a title bar, border or resize frame. The guest invalidates
  the complete frame after the successful Commit. Wineserver keeps the local
  native lease active until the host has a snapshot from the current geometry,
  so a missing or stale frame cannot expose an undecorated hosted root.
- Protocol 995 adds the immutable software frame-snapshot authority boundary.
  The logical root owner may replace only a strictly newer BGRA8
  premultiplied snapshot for the current geometry revision. Wineserver pins
  the section after the publisher closes its handle, limits it to 64 MiB per
  root and 512 MiB per desktop, and duplicates read-only handles only to the
  current Ready host. The Wayland window-surface flush now copies the complete
  software frame, makes the GPU-covered client rectangle transparent and
  publishes it after a DComp target exists. The host maps the section only
  long enough to upload an immutable coherent Vulkan buffer. A WSI command
  copies that full frame first and the host-owned GPU image into the exact
  client offset second. Snapshot, content and geometry revisions are checked
  together; replacement waits until an older Present stops reading the
  buffer. DComp exposes a process-local hosted-frame hint only after the
  server accepts `HostedContent`, and removes it on fallback or scene release;
  ordinary local DComp windows therefore do not pay for snapshot allocation
  and copying. The server still supplies all authority.
- DComp admission now rejects layered logical roots and visible HWND children
  or owned windows, while excluding only its identified internal DXGI helper.
  Wineserver independently invalidates an already hosted scene when such a
  visible family member appears, revokes the active contributor and starts the
  whole-root return to `LocalFallback`. Removing the child and recommitting an
  identity scene creates a fresh binding; a later visible owned popup causes
  the same authoritative fallback. Hidden input-only child HWNDs retain normal
  Windows focus routing without claiming visible content coverage.
- Protocol 996 replaces the resident host's unconditional 20 ms wineserver
  scan with a server-owned auto-reset work event. Registration validates and
  retains an event handle, and every scene, root, contributor, pool, frame,
  snapshot, configure and native-lease mutation that can change host work
  signals it. The host scans immediately after a signal, then coalesces scans
  to at most one every 20 ms during a one-second activity interval so local
  Vulkan fences and presentation completions still advance. At idle it performs
  one reconciliation scan per second to recover from a missed signal without
  continuously waking the server. Closing the registering process's handle
  cannot invalidate the server-held event object.
- Protocol 998 tracks direct WGL and Vulkan client surfaces in wineserver.
  Only the HWND owner may change the bounded 64-surface count. The first
  surface on a hosted root, descendant or directly owned window immediately
  revokes the hosted contributor and moves the complete root to
  `LocalFallback`; a family with any retained direct surface cannot create a
  new contributor or publish `HostedContent`. The identified internal DXGI
  helper remains excluded. `winewayland.drv` registers the shared client
  surface used by both OpenGL and Vulkan after its Wayland objects succeed,
  retains that exclusion while an unused surface is cached, and unregisters
  it on final detach or destruction using the original HWND identity.

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

This admission inventory now has deterministic authority coverage for both
DComp target layers, multiple DComp devices, visibility transitions, visible
children and owned popups, and real direct WGL and Vulkan client surfaces.
Unsupported combinations return the complete root to the legacy local path.

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
  as separate processes. Its initial Native Windows x86-64 and i386 runs each
  passed 36 checks.
  Windows rejects a foreign-process `CreateTargetForHwnd()` with
  `E_ACCESSDENIED`; the producer can still create windowless composition
  content and owns no visible top-level window. The logical owner preserves
  its HWND identity, publishes `SetRoot(NULL)` without another Present, can
  cancel `WM_CLOSE`, and receives `S_OK` from a hidden-window Present.
- The same public oracle now also injects a native A key-down while child A is
  focused, changes focus to child B, and injects key-up. Windows routes the
  release to B rather than pinning it to the key-down recipient. The extended
  oracle passed 43 checks with zero failures in both x86-64 and i386 on
  `testing-laptop`, and 43 checks with zero failures in the x86-64 Wine
  comparison. Native evidence remains at
  `C:\wine365-tests\dcomp-host-20260909-{x64,i386}\native-interactive.log`;
  the Wine binary is `/workspace/artifacts/dcomp-focus-oracle-test-x64.exe` in
  environment `dcomp-host-probe-20260909`.
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
- The opaque-FD memory-type extension passed 467 checks with zero failures in
  both x86-64 and i386 on protocol 981, including rejection of index 32 and
  exact host enumeration of a valid index. Host probe/registration remained on
  the Radeon fallback path and the public DComp oracle remained at 36 checks
  with zero failures in both architectures. Logs and binaries are retained as
  `/workspace/artifacts/dcomp-memory-type-{authority,probe,registration,oracle}-{x64,i386}.log`,
  `/workspace/artifacts/memory-type-win32u-test-{x64,i386}.exe`, and
  `/workspace/runner-dcomp-memory-type`.
- The native import primitive compiled through the real Vulkan headers for the
  Unix library and both PE architectures. An isolated Intel Iris Xe run created
  one exportable 64x64 BGRA8 image with dedicated device memory, exported its
  opaque FD plus two timeline-semaphore FDs, imported all three into new Vulkan
  objects, bound the imported memory, observed the producer's distinct timeline
  values 7 and 11 through the imported semaphores, retired the imported pool
  and cleaned up.
  The same self-test passed through x86-64 and the i386 WoW64 Unix-call table.
  Evidence is retained on `elkana-scadasudo` under
  `/home/ttv20/Projects/wine4office-testing/dcomp-host-import-20260910/artifacts/transport-timeline-final4-{x64,i386}.log`,
  with hashes in `transport-timeline-final-SHA256SUMS`.
  The isolated virtual KWin backend did not expose a Vulkan Wayland presentation
  queue, so this is import interoperability evidence, not native WSI admission.
  On the task Radeon environment the final host continued to withhold Vulkan
  transport and registration passed in both architectures; logs are retained
  as `/workspace/artifacts/dcomp-native-import-final4-{probe,renderer,registration}-{x64,i386}.log`,
  with hashes in `dcomp-native-import-final-SHA256SUMS`.
- Protocol 982 root discovery and the host-side server-handle import path built
  for x86-64 and i386. The authority regression passed 475 checks with zero
  failures in both architectures, including rejection of a non-host enumerator
  and complete discovery of retained scene roots. The updated real host
  registration fixture also passed in both architectures on the Radeon
  fallback path. Evidence is retained as
  `/workspace/artifacts/dcomp-host-work-{authority,registration}-{x64,i386}.log`,
  with hashes in `/workspace/artifacts/dcomp-host-work-SHA256SUMS` and the
  coherent runner at `/workspace/runner-dcomp-host-work`.
- The Vulkan frame synchronization path rebuilt the Unix library and both PE
  architectures. On Intel Iris Xe, x86-64 and i386 each proved that an
  unavailable ready value returns pending without allocating a record, the
  matching ready value submits a real image copy, and the producer's reuse
  semaphore reaches 11 only after the copy fence completes. Logs and hashes are
  retained on `elkana-scadasudo` under
  `/home/ttv20/Projects/wine4office-testing/dcomp-host-import-20260910/artifacts/frame-copy-final-{x64,i386}.log`
  and `frame-copy-final-SHA256SUMS`. On the Radeon fallback environment, host
  registration remained ready with capabilities `0xf` in both architectures,
  and the protocol-982 authority fixture remained at 475 checks with zero
  failures in both architectures. Evidence and runner hashes are retained as
  `/workspace/artifacts/frame-copy-{authority-{x64,i386},final-registration-{x64,i386}}.log`
  and `/workspace/artifacts/dcomp-frame-copy-final-SHA256SUMS` in
  `dcomp-host-probe-20260909`.
- The Intel transport fixture now proves copied content rather than submission
  alone. It clears the exportable BGRA8 producer image to opaque red, imports
  and copies it through the host frame path, then copies the retained host image
  to coherent mapped memory and validates all 4,096 pixels as
  `B=0, G=0, R=255, A=255`. The x86-64 and i386 WoW64 paths both passed. Logs,
  runner binaries and hashes are retained on `elkana-scadasudo` under
  `/home/ttv20/Projects/wine4office-testing/dcomp-host-import-20260910/artifacts/pixel-copy-{x64,i386}.log`
  and `pixel-copy-SHA256SUMS`.
- Vulkan transport admission now creates a temporary compositor surface and a
  matching `VkSurfaceKHR`, then checks the selected queue, usages and format
  against that object before advertising transport. The surface remains
  unmapped and has no shell role. The updated headless pixel regression still
  passed in x86-64 and i386 on Intel. On the Radeon Wayland environment, both
  probes passed the new surface checks and then honestly withheld transport at
  the later unsupported opaque-FD semaphore gate, retaining capabilities
  `0xf`. Logs and hashes are retained as `surface-gate-*` under the existing
  Intel artifact directory and `/workspace/artifacts/` in
  `dcomp-host-probe-20260909`.
- The host build now generates a private minimal xdg-shell client protocol for
  `xdg_wm_base`, `xdg_surface` and `xdg_toplevel`. A Ready transport host keeps
  the compositor and shell globals from its verified connection and services
  the display FD with a zero-timeout prepare/read/dispatch cycle on every host
  iteration. The full Unix library and both PE programs rebuilt. Radeon
  fallback registration remained ready in x86-64 and i386 after binding the
  shell global; logs and hashes are retained as
  `/workspace/artifacts/xdg-dispatch-registration-{x64,i386}.log` and
  `/workspace/artifacts/xdg-dispatch-SHA256SUMS`.
- A dedicated shell fixture now creates an unmapped `wl_surface`, assigns
  `xdg_surface` and `xdg_toplevel` roles, commits without a buffer, dispatches
  the first configure and acknowledges its serial. It passed on the task KWin
  compositor in x86-64 and i386, proving the minimal protocol's wire layout and
  bounded event pump without creating a visible window. Evidence and hashes
  are retained as `/workspace/artifacts/xdg-toplevel-{x64,i386}.log` and
  `/workspace/artifacts/xdg-toplevel-SHA256SUMS`.
- Protocol 983 adds explicit server root identity and lifetime generation.
  The complete x86-64 authority regression passed 476 checks with zero
  failures on the Radeon task environment. The new persistent-root fixture
  passed through both x86-64 and i386 Unix-call paths on Intel Iris Xe; each
  received and acknowledged one initial configure, preserved the same native
  objects on an idempotent update, replaced them for a new generation, and
  proved that retiring the old generation does not destroy the replacement.
  Evidence and hashes are retained as
  `/workspace/artifacts/root-identity-{authority-x64.log,SHA256SUMS}` in
  `dcomp-host-probe-20260909`, and under
  `/home/ttv20/Projects/wine4office-testing/dcomp-host-import-20260910/artifacts/root-identity-{self-test-x64.log,self-test-i386.log,SHA256SUMS}`.
  Both PE architectures compiled. The standalone i386 authority executable
  could not run in the available pure-win64 test prefixes because their WoW64
  system directory lacks a 32-bit `kernel32.dll`; this was a harness failure
  before test entry, not a product result.
- The persistent-root WSI fixture passed on Intel Iris Xe through both x86-64
  and i386 Unix-call paths. Each run created a four-image FIFO swapchain,
  acquired, cleared and presented a 64x64 image, recreated the swapchain at
  96x80, presented again, and retired the root without an in-flight resource.
  This verifies real `VkSurfaceKHR` and swapchain lifetime on an isolated
  virtual KWin compositor. It does not prove a DComp frame reached that
  swapchain, a present-wait commit boundary, or scanout. Logs and hashes are
  retained on `elkana-scadasudo` under
  `/home/ttv20/Projects/wine4office-testing/dcomp-host-import-20260910/artifacts/wsi-lifecycle-{x64.log,i386.log,SHA256SUMS}`.
- The combined transport-to-WSI fixture passed through x86-64 and i386 on the
  same Intel setup. It exported an opaque-FD producer image, synchronized and
  copied it through the real host transport path, verified every red pixel in
  the immutable host record, copied that record into an acquired WSI image and
  completed `vkQueuePresentKHR`. Both runs used four-image swapchains and also
  repeated the independent 96x80 recreation/present step. Evidence and hashes
  are retained as `transport-wsi-pipeline-{x64.log,i386.log,SHA256SUMS}` in the
  Intel artifact directory. The fixture is not yet a server-authorized DComp
  Present and does not claim scanout or a present-wait commit boundary.
- Protocol 984 frame-scene authority passed 491 checks with zero failures in
  x86-64 on the Radeon task environment. It covers frame rejection for
  `Hidden`, `LocalFallback` and `Empty` scenes, stale scene and binding
  generations, exact generation round trips, and retention of the accepted
  generation across a later scene replacement. Both PE architectures and the
  coupled wineserver, ntdll and host targets rebuilt. The matching runner,
  test binaries, log and hashes are retained as
  `/workspace/runner-dcomp-frame-scene` and
  `/workspace/artifacts/frame-scene-{authority-x64b.log,win32u-test-x64.exe,win32u-test-i386.exe,SHA256SUMS}`
  in environment `dcomp-host-probe-20260909`.
- The nonblocking WSI and explicit-release fixture passed through both x86-64
  and i386 Unix-call tables on Intel Iris Xe. Each run copied and verified all
  4,096 red pixels, submitted that immutable frame to a four-image FIFO
  swapchain, proved release stayed pending while the WSI fence owned the
  source, then released it and recreated the swapchain from 64x64 to 96x80.
  Logs and hashes are retained under the Intel task directory as
  `artifacts/production-wsi-nonblocking-{x64,i386}.log` and
  `artifacts/production-wsi-release-SHA256SUMS`. The Radeon x86-64 and i386
  registration fixtures also remained on the honest fallback path with
  capabilities `0xf`.
- The hosted-producer authority extension passed 514 checks with zero failures
  in both x86-64 and i386 on the Radeon task environment. The coupled DComp,
  DXGI and WineD3D targets rebuilt for both PE architectures, including the
  Vulkan Unix backend. On Intel Iris Xe, the real x86-64 D3D11/DXGI/DComp
  fixture created the authorized three-slot pool, imported all slots and
  presented seven frames through the server and host WSI. This covers more
  than two full slot-reuse cycles and proves that asynchronous host completion
  returns producer credits. The retained log is
  `/home/ttv20/Projects/wine4office-testing/dcomp-host-import-20260910/artifacts/dcomp-pipeline-reuse-x64.log`.
  The existing Intel i386 prefix failed during Wine display initialization
  before entering the fixture, so no i386 end-to-end result is claimed yet.
- The renderer queue executor rebuilt for the Unix library and both PE
  architectures. The transport-to-WSI lifecycle fixture passed through both
  x86-64 and i386 Unix-call tables on Intel Iris Xe, including pixel
  verification, source pinning, two swapchain extents and terminal cleanup.
  The real x86-64 DComp fixture again imported three slots and presented seven
  frames without discard or failure. Evidence is retained as
  `artifacts/wsi-executor-{x64,i386}.log`,
  `artifacts/dcomp-pipeline-executor-x64.log` and
  `artifacts/executor-x64-SHA256SUMS` in the Intel task directory.
- The present-ID/present-wait boundary passed the same WSI fixture through
  x86-64 and i386 on Intel Iris Xe. The real x86-64 DComp path then imported
  all three slots and completed six frames only after their matching present
  IDs reached the compositor boundary, with no discard or failure. Evidence is
  retained as `artifacts/present-wait-wsi-{x64,i386}.log`,
  `artifacts/present-wait-dcomp-x64.log` and
  `artifacts/present-wait-SHA256SUMS` in the Intel task directory.
- Protocol 985 window-state authority passed 521 checks with zero failures in
  both x86-64 and i386 on the Radeon task environment. It rejects non-host
  queries, returns the original title and client extent, and advances the
  revision while returning updated title and outer bounds after real User32
  mutations. The coupled wineserver, ntdll, host and tests rebuilt for both PE
  architectures. Evidence is retained as
  `/workspace/artifacts/window-state-authority-{x64,i386}.log`, with exact
  binaries and hashes under `/workspace/artifacts/window-state-*` and the
  coherent runner at `/workspace/runner-window-state`.
- Protocol 986 authenticated close delivery passed 530 checks with zero
  failures in both x86-64 and i386 on the Radeon task environment. The tests
  cover foreign-host rejection, mismatched shared identity, one normal
  `WM_CLOSE` delivery, idempotent replay and stale request rejection. Evidence
  is retained as `/workspace/artifacts/native-close-authority-{x64,i386}.log`,
  with exact binaries and hashes under `/workspace/artifacts/native-close-*`.
- Protocol 987 configure-token authority and owner-thread dispatch passed 565
  checks with zero failures and zero skips in both x86-64 and i386 on the
  Radeon task environment. The tests cover foreign host and owner rejection,
  root identity mismatch, invalid scale, one-slot coalescing, stale and
  conflicting apply, zero-size configure, exact applied geometry, and request
  numbering across a replacement host epoch. The coupled wineserver, ntdll,
  host, win32u tests and Wayland driver rebuilt for both PE architectures and
  the Unix libraries. Evidence is retained as
  `/workspace/artifacts/configure-token-authority-{x64,i386}.log`, with exact
  binaries and hashes under `/workspace/artifacts/configure-token-*` and the
  coherent runner at `/workspace/runner-configure-token`.
- Protocol 992 scene-transition authority passed 581 checks with zero failures
  and zero skips in both x86-64 and i386 on the Radeon task environment. It
  covers semantic frame admission, title-only metadata changes, geometry-bound
  pools, pre-GPU cancellation, idempotent cancellation and credit recovery.
  Final logs, test binaries and hashes are retained as
  `/workspace/artifacts/scene-transition-{authority-x64.log,authority-i386.log,win32u-test-x64.exe,win32u-test-i386.exe}`
  and `/workspace/artifacts/scene-transition-authority-SHA256SUMS`.
- The final remap fixture passed through both x86-64 and i386 Unix-call tables
  on Intel Iris Xe. Each run verified all 4,096 transported pixels, two WSI
  extents, the source-frame pin and release, a compositor-processed unmap, a
  fresh xdg configure and a successful remap. The first run exposed the missing
  post-unmap initial commit and failed with `STATUS_DEVICE_NOT_READY`; the
  corrected runs are retained as `artifacts/scene-transition-wsi-{x64,i386}.log`
  in the existing Intel task directory.
- The server-authorized x86-64 DComp pipeline passed on the same Intel system
  with three imported slots, six terminal presented frames, no discard or
  failure, and successful `Empty` application after `SetRoot(NULL)` without a
  new producer frame. The fixture now pumps its normal Windows message queue
  while waiting for asynchronous import and frame completion, avoiding a
  self-inflicted configure-versus-frame-credit deadlock. Evidence is retained
  as `artifacts/scene-transition-dcomp-x64.log`. The i386 executable still exits
  before entering the DComp fixture in both available prefixes, so only its
  transport/WSI and server-authority coverage is claimed.
- The authorized Intel WSL endpoint at `testing-laptop:8022` is now reachable.
  Its XFS development volume has reflinks enabled and retained more than
  306 GiB free. Protocol 994 rebuilt there for wineserver, ntdll, the host and
  win32u tests in both PE architectures. WSLg accepted the authenticated
  Wayland endpoint and exposed a seat, but no installed Vulkan ICD met the
  transport contract: the native Intel ICD enumerated no device, gfxstream
  lacked the required Wayland instance extension, and lavapipe lacked a
  required device extension. This is build and fallback-probe coverage, not an
  Intel GPU transport result. Each probe ended with zero task-prefix Wine
  processes.
- Protocol 993 adds a server-owned native-window lease with acknowledged
  `Local`, `PreparingHost`, `TransferringToHost`, `Hosted`, `ReturningLocal`
  and failure states. The guest Wayland driver now suppresses its local role,
  detaches the client surface and waits for an asynchronous compositor sync
  before the host may map. The reverse path drains and unmaps the host root
  before the server authorizes the guest to recreate its role. The x86-64
  authority regression passed 628 checks with no failures. On Intel Iris Xe,
  the real DComp pipeline imported two successive three-slot pools, transferred
  the same HWND to the host twice and restored local ownership once when a
  second target layer forced whole-window fallback. It then re-entered hosted
  mode and applied `Empty` without leaving a Wine process running. Evidence is
  retained as `artifacts/native-lease-authority-x64.log` on the Radeon task
  environment and `artifacts/native-lease-roundtrip-dcomp-pipeline-x64.log`
  in the Intel task directory. The i386 authority and affected production
  binaries compile; the existing i386 DComp prefix still exits before fixture
  setup, so no i386 end-to-end DComp result is claimed.
- Re-entering hosted mode after contributor revocation now treats stale
  server-side pool identities as terminal and destroys the old producer-local
  Vulkan pool before creating one for the new binding. The round-trip fixture
  caught the previous permanent `imports=3` stall and now reports
  `imports=6`, `host_activations=2` and `local_activations=1`. A separate
  transformed local-fallback probe exposed the older Vulkan CPU-composition
  path creating and publicly mapping staging resources on its command-stream
  thread. Vulkan swapchains now prepare and retain two size-matched CPU staging
  textures on the caller thread before queuing Present; the command-stream path
  only maps those prepared resources through their internal operations. The
  Intel fixture now completes Hosted -> transformed LocalFallback -> Hosted,
  with `imports=9`, `presented=6`, `host_activations=3`,
  `local_activations=2`, and no Wine process left afterward. Evidence is in
  `artifacts/composition-thread-transform-rehost-x64.log`.
- DComp now starts `winewayland-host.exe --launch` on the first eligible
  committed composition when no host is registered. The launcher uses the
  server startup permit to select one resident child, while concurrent
  launchers wait for that child and reuse its epoch. The resident registers as
  a Wine system process and waits for the server shutdown event, so it stays
  available while user applications run without keeping the prefix alive
  afterward. It is detached from the console; an earlier console child kept a
  `conhost.exe` user process alive and formed a shutdown cycle, which the Intel
  lifecycle fixture caught. The server also rejects DComp contributors when a
  fallback-only host lacks Vulkan transport. The x86-64 authority regression
  now passes 634 checks with no failures. On Intel Iris Xe, the automatic DComp
  fixture launched one host, submitted seven presents and exited with no Wine
  processes left. Two concurrent launchers selected the same PID and epoch,
  and the full manual round-trip fixture still reports `imports=6`,
  `presented=6`, `host_activations=2` and `local_activations=1`. A second run
  against the final staging-resource fix also passed automatic startup with
  seven presents. Evidence is in
  `artifacts/auto-host-{lifecycle-detached,dcomp-pipeline,race-1,race-2,regression-manual-pipeline}-x64.log`
  and `artifacts/auto-host-dcomp-pipeline-final-x64.log`
  in the Intel task directory and `artifacts/auto-host-authority-x64-v4.log` in
  the Radeon task environment. The Intel task still has 62 GiB free and every
  test prefix has zero Wine processes.
- Protocol 994 routes authenticated hosted input. The x86-64 authority fixture
  passed 652 checks with zero failures on the Radeon environment, including
  foreign-host and wrong-root rejection, replay prevention, key down/up,
  pointer motion and button state, and rejection after scene revocation. On
  Intel Iris Xe, the automatic DComp fixture delivered synthetic A down/up
  through the resident host to `WM_KEYDOWN` and `WM_KEYUP` while also
  completing seven Presents. The final queue and wheel-timestamp revision
  repeated that result with exit status zero, no remaining task-prefix Wine
  process and 62 GiB free. Evidence is retained as
  `/workspace/artifacts/host-input-authority-x64-v3.log` and
  `artifacts/host-input-synthetic-v3-x64.log` in the respective task
  environments. The isolated KWin compositor does not advertise the virtual
  keyboard protocol, so this does not yet prove externally injected physical
  compositor input. A follow-up authority fixture changed Windows focus from
  child A after key-down to child B before key-up and passed 654 checks with
  zero failures. Renderer ABI 7 then replaced the synthetic key-up with the
  same reset used for keyboard leave and seat loss; the Intel DComp fixture
  still delivered one key-down and one key-up, completed seven Presents and
  left no task-prefix Wine process. That evidence is retained as
  `artifacts/host-input-seat-reset-x64.log`. Wineserver now also tracks the
  exact hosted key and button downs in bounded per-desktop state. Host exit
  releases only those tracked inputs before revoking its native leases and
  streams. The authority fixture terminated a host with A and the left mouse
  button held, observed both releases, and passed 660 checks with zero
  failures.
- The decorated native-lease fixture proves that a `WS_OVERLAPPEDWINDOW`
  remains `PreparingHost` when its frame snapshot is stale, then begins the
  transfer only after a replacement snapshot is atomically bound to the
  current geometry. The same suite admits a framed identity DComp swapchain.
  The full x86-64 Radeon authority suite passed 700 checks with zero failures.
  Evidence is retained as
  `/workspace/artifacts/decorated-admission-authority-x64.log` and
  `/workspace/artifacts/decorated-admission-authority-SHA256SUMS`.
- The frame-snapshot authority fixture rejects a non-host reader and a stale
  geometry revision, accepts the current owner publication, closes the
  publisher handle, and verifies the current host can still map and read the
  exact pixel. It then atomically binds a replacement publication to the
  server's current geometry, closes that publisher handle and verifies the
  replacement pixel through the host. Repeated revision and completed-cursor
  queries are rejected. The full x86-64 Radeon authority suite passed 684
  checks with zero failures. Evidence is retained as
  `/workspace/artifacts/frame-snapshot-replacement-authority-x64.log` and
  `/workspace/artifacts/frame-snapshot-replacement-SHA256SUMS`.
- Renderer ABI 8 uploaded a synthetic 96x80 frame snapshot, submitted it to
  WSI, unmapped and remapped the xdg root, and repeated the presentation in
  both x86-64 and i386 Unix-call paths on Intel Iris Xe. The existing transport
  pixel verification and source-release checks also remained green. Evidence
  is retained as `artifacts/frame-snapshot-composite-wsi-{x64,i386}.log` in the
  Intel task directory. Both runs ended with zero task-prefix Wine processes;
  the laptop retained 62 GiB free.
- The real decorated x86-64 DComp path passed on Intel Iris Xe with a 320x192
  client inside a native title bar and resize frame. The driver published 18
  software snapshots, the host imported all three Vulkan transport slots,
  composited six frames without a discard or failure, and acknowledged one
  local-to-host ownership transfer. The pre-existing popup lifecycle fixture
  also remained green with nine imports, six Presents, three host activations,
  two local activations and its transformed fallback/rehost sequence. Evidence
  and hashes are retained as `artifacts/decorated-frame-pipeline-x64.log`,
  `artifacts/decorated-admission-popup-regression-x64.log` and
  `artifacts/decorated-admission-SHA256SUMS` in the Intel task directory. Both
  runs ended with zero task-prefix Wine processes and 62 GiB free.
- A visible child created under an already hosted root now advances the server
  scene to `LocalFallback` and revokes the old stream, while hidden child HWNDs
  still receive the focus-changing keyboard authority test. The full x86-64
  Radeon authority suite passed 706 checks with zero failures. Evidence and
  hashes are retained as
  `/workspace/artifacts/window-family-admission-authority-x64.log` and
  `/workspace/artifacts/window-family-admission-SHA256SUMS`.
- The family fixture now also removes the child, forces a complete fallback
  Commit, rehosts with a fresh contributor binding and creates a visible owned
  popup. The popup independently returns the server scene to `LocalFallback`.
  The expanded x86-64 Radeon suite passed 715 checks with zero failures.
  Evidence and hashes are retained as
  `/workspace/artifacts/popup-family-admission-authority-x64.log` and
  `/workspace/artifacts/popup-family-admission-SHA256SUMS`.
- Visibility authority now preserves the same `HostedContent` generation
  across `ShowWindow(SW_HIDE)` and `ShowWindow(SW_SHOW)`, while rejecting
  native input and frame submission during the hidden interval. The full
  x86-64 Radeon suite passed 721 checks with zero failures. On Intel Iris Xe,
  the real decorated DComp path unmapped and remapped its native root, retained
  all three imported slots, and completed a seventh producer frame after
  restoration with no discard or failure. The popup fallback/rehost fixture
  remained green with nine imports and six Presents. The i386 WSI path also
  remapped two extents and copied four transported images; its full DComp
  fixture still exits before setup, so no i386 DComp result is claimed.
  Evidence is retained as
  `/workspace/artifacts/window-visibility-authority-x64-final.log` on the
  Radeon task environment and
  `artifacts/window-hide-restore-{final-x64,popup-regression-x64,wsi-i386}.log`
  in the Intel task directory. Both laptops ended with zero task-owned Wine
  processes; the Intel laptop retained 62 GiB free.
- Multi-root admission removed the former desktop-wide one-root gate. The
  updated x86-64 authority suite accepted a second simultaneous root and
  passed all 721 checks with zero failures on Radeon. On Intel Iris Xe, one
  host created two xdg roots and two root-private Vulkan devices, imported six
  transport slots and completed ten alternating frames without a discard or
  failure. The fixture then delayed the first root's worker by three seconds;
  the second root completed within 1.5 seconds and the first subsequently
  recovered, proving the absence of cross-root queue head-of-line blocking.
  The x86-64 decorated, popup/rehost and WSI regressions and the i386 WSI path
  remained green. Evidence is retained as
  `/workspace/artifacts/multi-root-authority-x64.log` on the Radeon task
  environment and `artifacts/multi-root-blocked-20260911-023723.log` plus
  `artifacts/per-root-device-20260911-023526-*.log` in the Intel task
  directory. Both environments ended with zero task-owned Wine processes;
  the Intel laptop retained 62 GiB free.
- The permanent frame-copy failure fixture injects an asynchronous Vulkan
  queue-submission failure after the producer's ready value becomes visible.
  The host signaled source reuse, destroyed the unusable local record and
  delivered exactly one failed terminal result. A following frame on the same
  stream then presented successfully, proving credit and slot recovery. The
  complete x86-64 decorated, fallback/rehost and multi-root fixtures plus both
  x86-64 and i386 WSI paths remained green. Evidence is retained as
  `artifacts/frame-copy-failure-20260911-024755.log` and
  `artifacts/per-root-device-20260911-024805-*.log` in the Intel task
  directory. The run ended with zero task-owned Wine processes and 62 GiB
  free.
- The event-driven host loop passed the full Intel Iris Xe matrix after the
  20 ms coalescing correction: the decorated frame fixture recorded 160 event
  wakeups and 50 scans, popup fallback/rehost recorded 108 and 47, multi-root
  fairness recorded 200 and 83, and permanent frame-copy failure recovery
  recorded 66 and 19. The x86-64 and i386 WSI fixtures remained green. The
  Radeon authority regression rejects a file-mapping handle in the event slot,
  observes the initial Ready signal, closes the client handle while the server
  registration remains live, and passes all 723 checks with zero failures.
  Evidence is retained as
  `/workspace/artifacts/work-event-authority-x64-v2.log` and
  `/workspace/artifacts/work-event-authority-v2-SHA256SUMS` on the Radeon task
  environment, and `artifacts/per-root-device-20260911-030100-*.log` in the
  Intel task directory. Both systems ended with zero task-owned Wine processes;
  the personal Intel laptop retained 62 GiB free.
- Protocol 998 direct-surface authority passed 875 checks with zero failures
  and zero skips on Radeon, Intel Iris Xe and WSLg. The suite rejects a foreign
  registrar and invalid state, exercises all 64 count slots plus overflow and
  underflow, proves immediate fallback and rehosting, creates a real WGL
  context on an already hosted root, and creates a real `VkWin32SurfaceKHR`
  whose root is then rejected for contributor admission. Both PE
  architectures and the coupled wineserver, ntdll, Wayland driver and host
  binaries rebuilt in the canonical build and on `testing-laptop` WSL. The
  Intel x86-64/i386 WSI fixtures and the decorated, fallback, multi-root and
  asynchronous-copy-failure DComp fixtures also remained green. The standalone
  i386 authority executable still cannot enter the test in the available
  pure-win64 prefix because its `syswow64/kernel32.dll` is absent; its PE code
  and shared WoW64 Unix-call path compile, and the i386 WSI run passes.
  Evidence and hashes are retained as
  `/workspace/artifacts/direct-surface-final-{authority-x64.log,SHA256SUMS}` on
  the Radeon task environment, `artifacts/direct-surface-final-SHA256SUMS` in
  the personal Intel task directory, and
  `artifacts/direct-surface-final-SHA256SUMS` under the WSL build agent. WSL
  required the 56 KiB `libxkbregistry0` runtime package before the Wayland
  driver could create a window. Every task-prefix Wine process was stopped;
  the personal laptop retained 62 GiB free and WSL retained 304 GiB free.
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
