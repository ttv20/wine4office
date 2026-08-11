# Wine4Office Custom Changes Review

## Review result

**Verdict: not release-ready.** The branch contains meaningful root-cause fixes, but also many Office-specific workarounds, permissive silent-success stubs, and several release-blocking security, data-loss, memory-safety, and API-contract defects.

All runtime impacts below are **inferences from inspected source paths** unless noted otherwise.

## Scope

- Main custom delta: `wine-11.14..46ca4e96958a91bd5d3f9958c6ea135926c1c91a`
  - 496 files
  - 65,711 insertions / 6,884 deletions
- Late custom commit: `46ca4e9..1e40a933a78c05897bc6a3b26eb56bd48edf1ec5`
  - 10 files
  - 94 insertions / 5 deletions
- Final HEAD verified: `1e40a933a78c05897bc6a3b26eb56bd48edf1ec5`
- Pure upstream Wine 11.14 changes were excluded as requested.
- The main delta was partitioned into 53 exhaustive slices, plus 2 late-delta slices, all reviewed by Luna xhigh agents. Domain synthesis and direct source rechecks followed.

## Release blockers

### 1. MSIX staging trusts unverified and path-manipulating packages

- `dlls/appxdeploymentclient/msix.c:442-512`
  - Validation checks only that ZIP entries named `AppxManifest.xml`, `AppxBlockMap.xml`, and `AppxSignature.p7x` exist.
  - No signature-chain, PKCS#7, block-map, or payload-hash verification occurs.
  - A tampered archive containing dummy metadata can be extracted and registered.
- `dlls/appxdeploymentclient/msix.c:520-585,735-760`
  - Manifest `Name`, `Version`, and `ProcessorArchitecture` are parsed as unrestricted strings and inserted into the destination path.
  - Dot/path components can escape the intended WindowsApps directory.
- `msix.c:760-777`
  - Existing destinations are treated as success, and registry failure after the move leaves orphaned package content.

**Fix:** real XML parsing and identity grammar; canonical containment checks; AppX signature and block-map verification; transactional staging and rollback.

### 2. Destructive manager operations proceed after Wine shutdown failure

- `tools/wine4office-manager/wine4office_manager.py:494-503`
  - Runner replacement continues after broad `FileNotFoundError`/`OSError` from `stop_wine()`.
- `wine4office_manager.py:914-923`
  - Prefix deletion continues after the same shutdown failures.
- `tools/wine4office-manager/uninstall-user.sh:50-55,85-86`
  - `--remove-prefix` does not require a manager-owned Wine prefix layout.
  - An ordinary user directory can be recursively deleted.
- `uninstall-user.sh:57`
  - Shutdown errors are explicitly swallowed before deletion.

**Fix:** fail closed unless all prefix processes are demonstrably stopped; require manager ownership and valid Wine layout; perform race-safe deletion.

### 3. WAM/OAuth cache can return credentials for the wrong account

- `programs/wine4officeauth/wine4officeauth.cpp:1395-1429`
  - An interactive/account-bound request uses any existing global refresh token before considering its login hint.
  - Account B can receive account A's cached token.
- `wine4officeauth.cpp:637-653,330-355`
  - Account/token fields are separate fixed-name writes with a shared `.tmp`, no interprocess lock, bundle transaction, or rollback.
  - Concurrent sign-ins or partial failure can mix access, ID, refresh, expiry, and account metadata.
- `wine4officeauth.cpp:1246-1324`
  - Several browser setup failures return without re-enabling the disabled owner HWND.

**Fix:** account-scoped caches; identity validation; named interprocess locking; versioned atomic cache bundles; one cleanup path for the OAuth window.

### 4. Incident reporting exposes credentials and can orphan Office

- `tools/wine4office-manager/wine4office_incident.py:56-61,378-386`
  - Only the initial OpenObserve URL is constrained to HTTPS.
  - Default redirect handling can forward `Authorization` to another host or HTTP endpoint.
- `wine4office_incident.py:73-84,149-159`
  - `OPENOBSERVE_TOKEN` remains in the inherited environment of Wine/Office and helper processes.
- `wine4office_incident.py:714-761`
  - Malformed monitor settings, probe exceptions, or cancellation leave the detached child running without supervision.
- `wine4office_incident.py:117-120`
  - JSON-style credentials such as `{"token":"secret"}` and underscore names such as `access_token` bypass redaction.

**Fix:** reject cross-origin/downgrade redirects; keep reporting credentials out of process environment; terminate and wait on all supervisor failure paths; use structured credential redaction.

### 5. Kernel and server memory-safety/resource-exhaustion defects

- `dlls/kernelbase/memory.c:139-150`
  - `FlushInstructionCache()` dereferences the caller's address for debug logging before calling the real API.
  - Unmapped or page-crossing addresses can fault.
- `dlls/nsi/nsi.c:187-197,210-226`
  - Four caller-controlled sizes are summed in 32-bit `SIZE_T`; wrapping can produce a tiny allocation followed by huge copies.
- `server/registry.c:2593-2611`
  - One staged query can retain more than 4 GiB of names in wineserver memory.

**Fix:** remove behavioral debug reads; checked wide arithmetic and received-buffer validation; practical per-query/per-client server limits.

### 6. Delivery Optimization has integrity, isolation, startup, and bounds failures

- `dlls/qmgr/qmgr.c:27-31,115-119`
  - DO and BITS use separate manager identities, but `EnumJobs()` returns the unfiltered global job list.
- `dlls/qmgr/service.c:94-101,131-153`
  - COM classes are registered before `jobEvent` and the transfer worker exist.
  - Early `Resume()` can signal a null event or strand work.
- `dlls/qmgr/file.c:383-396`
  - Mandatory integrity information is stored but never verified.
- `dlls/qmgr/file.c:484-525; dlls/qmgr/job.c:919-947`
  - `RangeCount * sizeof(range)` can overflow on 32-bit, followed by `memcpy`; overlap validation is unbounded $O(n^2)$.

**Fix:** namespace job enumeration; initialize the service before registration; enforce or reject mandatory integrity; bound counts and use checked multiplication/sorted validation.

### 7. Graphics changes can silently return stale data or break shared resources

- `dlls/wined3d/surface.c:1575-1650`
  - Partial GPU-to-CPU downloads are enabled generically.
  - Vulkan and GL backends still reject several boxes, offsets, source types, and conversions through void/early-return paths.
  - The caller nevertheless marks destination data valid.
- `dlls/wined3d/wined3d_vk.h:43; adapter_vk.c:2086-2098`
  - Core `vkGetPhysicalDeviceImageFormatProperties2` is loaded as mandatory before the KHR fallback, breaking Vulkan 1.0 extension-only devices.
- `dlls/d3d11/device.c:4712-4724`
  - Importing a valid shared handle still requires separately named mapping/event objects whose lifetime ends with the exporter.
- `dlls/d3d11/texture.c:1808-1824`
  - Valid legacy `D3D11_RESOURCE_MISC_SHARED` combinations are rejected.
- `dlls/d2d1/device.c:1322,3736`
  - Internal public `Flush()` calls present a batched WIC target early, consume queued CPU glyphs, and allow a later present to overwrite them.

**Fix:** backend capability/result contracts; only mark data valid after success; correct Vulkan optional loading; tie shared metadata lifetime to the exported object; preserve valid legacy sharing modes; separate internal GPU synchronization from WIC presentation.

### 8. DXGI desktop duplication violates output, lifetime, and timeout contracts

- `dlls/dxgi/portal.c:133-135,507-515`
  - First capture initialization can wait indefinitely for portal consent despite a finite `AcquireNextFrame()` timeout.
- `portal.c:237; unixlib.h:5-14`
  - Portal capture has no requested-output identity; secondary-output duplication can return another monitor.
- `portal.c:574-579`
  - Capture state is process-global and unrefcounted; releasing one duplication tears down another's session.
- `dlls/dxgi/output.c:146-164`
  - The GDI path returns immediate frames without new-frame/timeout semantics.

### 9. WinRT/COM objects publish unsafe or false contracts

Representative high findings:

- `dlls/windows.ui.notifications/main.c:98-108`
  - Activated `IXmlDocument` has NULL vtable slots; normal method calls jump through NULL.
- `windows.ui.notifications/main.c:329-360,521-531`
  - Activation handler replacement/removal races dispatch and can invoke a released handler.
- `windows.ui.notifications/unixlib.c:108-165`
  - D-Bus signals are not validated by sender/path; another session client can forge notification events.
- `windows.security.authentication.onlineid/authenticator.c:1334-1348,1626-1637,2328-2335`
  - Generic objects return success for unrelated parameterized IIDs with incompatible vtables.
- `dlls/windows.web/json_array.c:386-563`
  - An agile mutable vector has no synchronization; concurrent mutation/read can access freed storage.
- `dlls/windows.web/protocol_filter.c:183-228`
  - Several getters return `S_OK` with NULL objects.
- `dlls/windows.ui/compositor.c:585-596,1259-1271,1473-1579`
  - Visual collections store only a count, desktop targets never render, and drawing surfaces lack a backing target while returning success.

### 10. Licensing and policy APIs grant success without validation

- `dlls/sppc/sppc.c:1343-1367`
  - Any nonempty "license" returns `S_OK`, zero ID, and is not stored.
- `sppc.c:1427-1480`
  - An unrelated blob can map to an installed UL ID based only on grace-file existence.
- `sppc.c:1040-1048`
  - Grace validity is only a file-existence test with fixed five-day status.
- `dlls/windows.applicationmodel/limited_access.c:154-159,218-238`
  - Arbitrary token/attestation returns `Available`.
- `dlls/combase/roapi.c:114-192`
  - Protection-policy calls return permissive successful answers without policy state.

These are Office-enablement workarounds, not real licensing or enterprise-policy implementations.

### 11. Build and runtime ABI blockers

- `tools/wine4office-manager/packaging/linux-uapi/linux/ntsync.h:11,43-57`
  - Uses `_IOW/_IOR/_IOWR` without including `linux/ioctl.h`; the standalone configure header check cannot define them.
  - NTSYNC detection fails and the release workflow aborts or omits NTSYNC.
- `dlls/msvcp90/exception.c:1245-1284`
  - `_Throw_Cpp_error` unconditionally accesses helpers/layout only present in later MSVCP versions while this parent source builds msvcp90/100 too.
- `dlls/riched20/txtsrv.c:590-618`
  - `TxDrawD2D` logs rendering failures but always returns `S_OK`, preventing fallback while producing blank output.
- `dlls/shcore/main.c:44-70`
  - "Global" counters use a process-private static array, violating their cross-process contract.

## Important medium findings

- Global Office policy in `loader/wine.inf.in:516,565-566` affects every prefix and overwrites X11 preferences.
- The manager preload worker stops unowned ClickToRun components: `wine4office_backend.py:4519-4530,4647-4653`.
- `KillMode=process` plus a 20-second stop timeout can strand Wine/App-V children.
- Qt environment creation omits cancellation/process callbacks and performs policy/incident work on the UI thread.
- Font generation loses Hebrew glyphs in maintainer mode; merged Tahoma OS/2/hhea metrics also changed globally.
- Wayland/X11 popup reveal relies on product/class hardcodes and pixel-variance heuristics; successful client-surface presentation can remain hidden.
- Wayland systray DBus callbacks race `NIM_MODIFY`, permitting freed icon/tip access.
- MSI Click-to-Run parsing confuses PackageGUID with ProductCode and cannot parse top-level `PublishComponentList` records.
- `wine4officeclose` sends `WM_CLOSE` to every visible top-level window in the prefix, including unrelated apps with unsaved work.
- Shell SVG-to-ICO maintainer regeneration fails because imported Breeze SVGs lack Wine render directives.
- Package desktop icons use only executable basenames, causing collisions and stale entries.
- The release tar transformation does not transform hard-link targets. This was experimentally reproduced; extraction fails. `flags=rhS` fixed the disposable case.
- Shell `users.svg/users.ico` are byte-identical to the Home icon.
- RichEdit TOM2 setters return successful no-ops; `TxGetNaturalSize2` reports ascent zero.
- MSXML `ms:string-compare` ignores its language and most comparison options.

## Classification: real fixes vs workarounds

### Genuine root-cause fixes / substantive implementations

Examples that are directionally sound:

- PE section backing and VA mapping: `server/mapping.c`, `dlls/ntdll/unix/virtual.c`.
- Exception-safe `RtlWaitOnAddress` cleanup: `dlls/ntdll/sync.c`.
- Deferred RPC interface unregister/deadlock fix: `dlls/combase/stubmanager.c`.
- WinTrust delete-sharing fix and regression test.
- `GetAdaptersInfo(NULL, &size)` and overflow handling.
- XmlLite incremental UTF-8 buffer preservation.
- MSXML UTF-16 schema parsing and C++ exception rethrow cleanup.
- WIC Fant resampling.
- Dynamic optional winspool loading in `wbemprox`.
- Win32U OpenGL drawable reservation/lifetime fix.
- Late `server/queue.c` change correctly fixes `SMTO_ABORTIFHUNG` for an empty but untouched queue; its user32 test exercises the new contract.

### Compatibility workarounds / partial implementations

- Office/Teams class-name and registry hardcodes.
- Process-global environment variables used as runtime synchronization.
- Global Wine INF font/X11 defaults.
- Fake DComposition surface handles and storage-only composition objects.
- Process-global DXGI portal capture.
- SPPC licensing and permissive policy results.
- WAM file-based global token cache and hard-coded identities.
- Synthetic Wayland/X11 helper HWNDs, popup visibility heuristics, and direct-input properties.
- Delivery Optimization mapped onto the BITS service and custom vtable-slot duplication.
- Sensor activity APIs that return success but never monitor.
- Deprecated APIs accepted as successful no-ops.

## Late commit `1e40a933`

The late commit is mixed:

- **Real fix:** `server/queue.c:1250-1256` correctly removes the erroneous "queue must already contain a message" condition for `SMTO_ABORTIFHUNG`.
- **Good test:** `dlls/user32/tests/msg.c` covers the empty-queue, untouched-thread case.
- **Workaround:** synthetic helper HWNDs are removed from `HWND_BROADCAST`.
- **New defect:** `dlls/win32u/message.c:4161` also skips the default IME HWND after DXGI/wined3d marks it. That IME HWND is shared per thread and the marker is not reference-counted or removed when the synthetic helper dies.
- **Lower-risk race:** helper/IME properties are applied only after `CreateWindow` returns, leaving a small interval where a broadcast can still block on the helper.

## Recommended order

1. Block publication until MSIX, destructive manager operations, WAM cache identity, incident credentials, kernel/NSI, QMgr bounds/integrity, and NTSYNC packaging are fixed.
2. Fix graphics data-validity/shared-resource defects before enabling the new Vulkan/D3D/DXGI paths by default.
3. Replace security/licensing/policy `S_OK` stubs with real behavior or honest unsupported errors.
4. Move Office-specific defaults and class heuristics out of generic Wine paths into manager-owned, per-prefix policy.
5. Add focused regressions for each repaired contract; do not expand the existing tests that currently lock in unsafe behavior such as stopping unowned ClickToRun.

## Verification

Performed:

- Exhaustive static diff review.
- Source-context tracing.
- Final HEAD and scope verification.
- Reviewer synthesis.
- Disposable tar hard-link experiment.

Not performed:

- Project build.
- Wine/Office runtime session.
- Project test suite.

This was a read-only code review.
