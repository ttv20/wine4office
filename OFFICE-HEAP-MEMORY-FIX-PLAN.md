# Wine Office heap-memory investigation and fix plan

Status: planning and proof phase only

Decision: **REVISE; Gate 0 is approved, production implementation is not yet approved**

Last updated: 2026-08-10

Source tree reviewed: Wine4Office `main` at `962bc91db63` (three local commits ahead of `origin/main`)

Office runtime measured: Wine4Office 0.1.11-rc.2, commit `5e763e7f23f`, based on Wine 11.14

## 1. Objective

Determine why Office Click-to-Run and Word retain more private memory under Wine than under Windows, then implement a generic Wine heap fix only if native behavior and allocator evidence justify it.

The result must:

- follow Windows heap and virtual-memory semantics;
- fix generic Wine behavior rather than detect Office;
- include focused conformance and regression tests;
- show that lower RSS/PSS corresponds to genuinely lower committed heap memory;
- remain correct under allocation failure and concurrency.

This is not permission to change the allocator yet. Gate 0 must first prove that safely reclaimable free heap pages explain a material part of the observed difference.

## 2. Explicit non-goals

The following approaches are rejected:

- Office executable-name, version, window-class, or installation-path detection;
- an environment variable or registry knob used only to trim Office;
- a periodic heap-trimming thread or timer;
- forced working-set eviction to make RSS look smaller;
- arbitrarily lowering Wine's heap growth limit;
- disabling LFH or heap checking globally to hit a memory target;
- treating `madvise(MADV_FREE)` or `madvise(MADV_DONTNEED)` as equivalent to Windows `MEM_DECOMMIT` without a separately proven contract;
- changing shared-library loading merely because summed RSS double-counts shared pages.

## 3. Measurement baseline

### 3.1 Windows reference supplied by the user

| State | Process | Private bytes | Working set |
|---|---:|---:|---:|
| C2R idle | Office Click-to-Run | about 50 MiB | about 50 MiB |
| Word open | Office Click-to-Run | about 50 MiB | about 70 MiB |
| Word stable | WINWORD | about 200 MiB | about 250 MiB |

These values are reference observations, not yet a controlled native oracle. The Windows build, Office build, architecture, heap backend, startup age, document state, and cold/warm cache state still need to be recorded.

### 3.2 Wine measurements on the isolated remote environment

| State | Process | RSS | PSS | Private resident |
|---|---:|---:|---:|---:|
| C2R idle | Office Click-to-Run service | 67 MiB | 46 MiB | 42 MiB |
| Word open | Office Click-to-Run service | 127 MiB | 103 MiB | 96 MiB |
| Word stable | WINWORD | 391 MiB | 324 MiB | 300 MiB |
| Word startup peak | WINWORD | 431-450 MiB | not captured consistently | not captured consistently |

When the apparent Click-to-Run *process group* was summed, RSS was about 352 MiB, while PSS was about 161 MiB and private resident memory about 131 MiB. Therefore, the original “C2R uses about 300 MiB” observation is largely an accounting artifact if it is calculated by summing RSS across Wine processes. It is not the footprint of the service process alone.

The remaining differences are still real enough to investigate:

- C2R after Word opens: Wine private resident about 96 MiB versus Windows private about 50 MiB.
- WINWORD stable: Wine private resident about 300 MiB versus Windows private about 200 MiB.

### 3.3 Metrics and interpretation

- **RSS / working set:** resident pages mapped by a process. It includes shared pages and therefore cannot safely be summed across processes.
- **PSS:** resident shared pages divided among mapping processes. Use it for a fairer process-group total on Linux.
- **Private resident:** resident pages unique to the process. This is the closest `/proc` comparison to the Windows private/working-set observations, but it is not identical to Windows private bytes.
- **Committed heap bytes:** the allocator's committed virtual memory. This is the causal metric the heap patch must reduce.
- **Reserved bytes:** address space reserved for future use. Reserved-but-uncommitted pages are not equivalent to resident or committed memory.

A successful allocator change must reduce committed heap bytes and private/PSS together. A reduction in RSS alone is not proof of a fix.

## 4. Findings so far

### 4.1 Confirmed findings

1. **Summed Wine RSS overstates physical memory.** Shared PE/DLL mappings appear in each process RSS. Wine's server maintains shared PE backing mappings in `server/mapping.c`, so code and read-only image pages can be shared by Office applications in the same Wine session/prefix. PSS apportions these pages and is the correct process-group metric.

2. **The C2R service itself was not using 300 MiB while idle.** Its measured idle private resident footprint was 42 MiB, close to the supplied Windows private value of about 50 MiB.

3. **Both C2R-after-Word and WINWORD retain more private memory under Wine.** The controlled Wine snapshots show approximately 46 MiB extra private resident memory for C2R after Word opens and approximately 100 MiB extra for stable WINWORD relative to the supplied Windows observations.

4. **Wine already coalesces adjacent standard free blocks and releases a completely empty non-main subheap.** This happens in `heap_free_block()` in `dlls/ntdll/heap.c`.

5. **Wine currently decommits only the free tail of a subheap.** `subheap_decommit()` moves a single commit boundary represented by `subheap->data_size`. It does not describe or decommit holes between live allocations.

6. **`RtlCompactHeap()` is currently a stub.** It returns zero and does not compact or decommit heap storage.

7. **Wine has LFH and delayed-free paths that cannot be treated like ordinary locked free blocks.** LFH uses interlocked state and lock-free SLIST operations; delayed-free blocks can be marked dead before becoming ordinary reclaimable free blocks.

### 4.2 Partially confirmed findings

The C2R process contained resident anonymous mappings of approximately 8,192 KiB, 15,104 KiB, and several 16,192 KiB regions. The 16,192 KiB value equals Wine's `HEAP_MAX_GROW_SIZE` (`0xfd0000`) in `dlls/ntdll/heap.c`.

This confirms that Wine heap growth-sized mappings exist in the affected process. It does **not** prove that the mappings are empty, fragmented, owned by the standard heap path, or safe to decommit. The pages may contain live allocations, LFH groups, pending-free blocks, or allocator metadata.

### 4.3 Not yet proven

- Interior fragmentation is the cause of the remaining Office private-memory difference.
- Native Windows decommits equivalent interior pages for the same allocation/free sequence.
- The Windows Office processes being compared use the NT heap rather than Segment Heap.
- `RtlCompactHeap()` is called by C2R or Word in the relevant lifecycle.
- A generic interior-decommit implementation would reclaim enough pages to explain the difference.

### 4.4 Current allocator constraints

Wine's current standard subheap model assumes one contiguous committed prefix:

- `subheap_commit_end()` derives the end from `subheap->data_size`;
- `last_block()` and `next_block()` assume block headers can be traversed through that committed prefix;
- `check_subheap()` and `heap_validate()` make the same contiguous-memory assumption;
- `RtlWalkHeap()` exposes one committed portion and one trailing uncommitted portion;
- heap performance counters calculate committed bytes from the same single boundary.

Interior uncommitted ranges therefore require a real data-model change. Adding a lone `MEM_DECOMMIT` call inside `heap_free_block()` would make traversal and validation touch inaccessible pages and would be incorrect.

## 5. Windows behavior that must be established

The fix must be based on measured native behavior and these API contracts:

- [`HeapAlloc`](https://learn.microsoft.com/en-us/windows/win32/api/heapapi/nf-heapapi-heapalloc): allocation flags, zero-initialization, failure behavior, and serialization rules.
- [`HeapFree`](https://learn.microsoft.com/en-us/windows/win32/api/heapapi/nf-heapapi-heapfree): ownership, validity, serialization, and post-free semantics.
- [`HeapCompact`](https://learn.microsoft.com/en-us/windows/win32/api/heapapi/nf-heapapi-heapcompact): compaction behavior and return value.
- [`HeapQueryInformation`](https://learn.microsoft.com/en-us/windows/win32/api/heapapi/nf-heapapi-heapqueryinformation) and [`HEAP_INFORMATION_CLASS`](https://learn.microsoft.com/en-us/windows/win32/api/winnt/ne-winnt-heap_information_class): frontend/backend and heap information behavior.
- [`PROCESS_HEAP_ENTRY`](https://learn.microsoft.com/en-us/windows/win32/api/minwinbase/ns-minwinbase-process_heap_entry): committed, busy, region, and uncommitted walk entries.
- [`RtlCreateHeap`](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntifs/nf-ntifs-rtlcreateheap): reserve/commit sizes, growable heaps, flags, and caller-provided locking.
- [`VirtualAlloc`](https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualalloc) and [`VirtualFree`](https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualfree): `MEM_COMMIT`, `MEM_DECOMMIT`, reservation retention, and zero-filled recommit behavior.
- [`VirtualQuery`](https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualquery): observable committed versus reserved page state.

Required semantic invariants:

- Never decommit a page containing a live allocation or required allocator metadata.
- Preserve the reservation when pages are decommitted.
- Recommit every required page before reading or writing block headers, split metadata, back-pointers, or user data on that page.
- Preserve `HEAP_ZERO_MEMORY`: every returned user byte that requires zero initialization must be zero, whether it came from a newly committed page or an already committed free block.
- Preserve failure atomicity: if decommit or recommit fails, the heap remains internally consistent and reports an honest allocation failure where applicable.
- Respect `HEAP_NO_SERIALIZE`: the caller owns external serialization, but Wine's internal sequence must still have no out-of-lock access to a decommitted range.
- Do not decommit LFH storage while lock-free allocation/free paths may access it.
- Do not decommit delayed/pending-free storage before it becomes an ordinary free block.

Exact `HeapWalk` entry layouts may differ by Windows version and heap backend. The native oracle should test stable semantic invariants rather than byte-for-byte reproducing one Windows build.

## 6. Claude review

Claude independently reviewed the proposed plan and `dlls/ntdll/heap.c`. The verdict was **REVISE**.

### 6.1 Required corrections

1. Treat the 16 MiB mappings only as evidence of Wine's maximum subheap growth size, not proof of fragmentation.
2. Add Gate 0 to quantify page-aligned, provably unused, non-LFH interior space before designing a production patch.
3. Identify the Windows heap backend used by the target Office processes and compare equivalent backends and allocation patterns.
4. Scope the first implementation to ordinary growable subheaps; exclude LFH, debug/fill heaps, pending-free blocks, metadata, and the main heap's minimum committed floor.
5. Add an explicit uncommitted-range model. Preserve tail-boundary semantics and represent interior holes separately.
6. Recommit before any split, header, back-pointer, or payload write. Keep the entire transition within the correct heap-locking domain.
7. Remove the proposed `madvise()` alternative from version 1. Use Windows-style `MEM_DECOMMIT` through Wine's virtual-memory implementation.
8. Update walking, validation, growth, and statistics with the allocator; otherwise the heap representation becomes self-inconsistent.
9. Keep LFH out of version 1 because its lock-free accesses need a separate safety design.
10. Implement `RtlCompactHeap()` minimally and separately. Do not use it as a periodic trimming workaround.
11. Replace predeclared memory targets with evidence-based outcome criteria. Correctness is the gate; memory reduction is a measured outcome.
12. Plan for multiple weeks, not a few days, because this is an allocator representation and concurrency change.

### 6.2 Claude's GO criteria

- Every decommitted page is wholly inside a coalesced ordinary free block.
- No decommitted page contains metadata, an LFH group, a pending-free block, or bytes protected by the main heap's minimum commit size.
- Allocation, free, zeroing, walk, validation, and statistics remain self-consistent on 32-bit, 64-bit, and WoW64/native-comparison cases.
- Recommit occurs before the first access and concurrency stress is clean.
- LFH remains untouched in version 1.
- Common allocation paths and performance remain acceptable.
- The implementation is generic and contains no Office-specific policy.
- Lower PSS/private memory agrees with lower heap committed bytes.

### 6.3 Claude's NO-GO criteria

- Gate 0 finds that the large regions are predominantly live or LFH-owned.
- RSS falls but committed heap bytes do not.
- A desired memory number can be reached only by disabling checks/LFH or lowering a size cap without native evidence.
- Any implementation accesses a potentially decommitted page outside its locking/lifetime rules.
- Tests attempt to copy one Windows version's exact walk layout instead of testing stable contracts.

## 7. Revised implementation and validation sequence

### Gate 0 — Native oracle and reclaimability proof

Purpose: establish cause and native behavior before modifying production allocation behavior.

#### Gate 0A — Build a native differential heap oracle

Add focused tests to the existing heap test area, primarily `dlls/kernel32/tests/heap.c`, with any lower-level assertions placed in the owning ntdll test area if needed.

The test matrix must cover:

- process heap and heaps created with controlled reserve/commit sizes;
- growable and fixed-size heaps;
- standard heap and LFH where native Windows permits selecting/querying it;
- serialized and `HEAP_NO_SERIALIZE` heaps, with correct caller serialization for the latter;
- normal flags, free checking, tail checking, and zero-memory allocations;
- 32-bit and 64-bit binaries, plus WoW64 when a native host is available.

For each case, run deterministic allocation patterns:

1. Create a heap and record reserve, commit, walk output, compatibility information, and process memory counters.
2. Allocate blocks that force multiple growth regions.
3. Free middle allocations while retaining allocations on both sides.
4. Query `VirtualQuery`, `HeapWalk`, heap information, and process counters before and after the free.
5. Call `HeapCompact` where valid, then repeat the queries.
6. Reallocate into freed address ranges, verify returned-pointer validity, preserved live data, and zeroing rules.
7. Destroy the heap and verify cleanup.

The native run must record the Windows version, Office version, process architecture, heap compatibility/backend, reserve and commit sizes, page size, allocation sequence, returned addresses, `VirtualQuery` state, walk entries, compact result, and memory counters.

#### Gate 0B — Identify Office's Windows heap backend

Capture C2R and WINWORD on the Windows reference machine with architecture-aware tools. Record whether each relevant heap is legacy NT heap/LFH or Segment Heap. Use `HeapQueryInformation` where sufficient and architecture-matched debugger/heap tooling when it is not.

If Windows Office uses Segment Heap while Wine implements the NT heap model, keep two comparisons separate:

- API-level behavior for equivalent synthetic NT heaps;
- end-to-end Office memory as a product outcome, not proof that Wine must clone Segment Heap internals.

#### Gate 0C — Prove reclaimable pages inside Wine heaps

Create a diagnostic-only branch/instrumented build. The instrumentation must not become the product fix.

Under the heap lock, record for every standard subheap:

- reservation and committed ranges;
- every block's address, size, type, and coalesced state;
- LFH ownership and group boundaries;
- delayed/pending-free ownership;
- allocator metadata and back-pointer locations;
- page-aligned spans lying wholly inside ordinary coalesced free blocks;
- which eligible spans are interior versus at the current tail;
- theoretical reclaimable committed bytes by heap and process.

Correlate these allocator records with `/proc/<pid>/smaps`, `smaps_rollup`, PSS, private resident memory, and Wine heap performance counters at these states:

- C2R idle after stabilization;
- C2R with Word open after stabilization;
- Word startup peak;
- Word stable with a blank document;
- Word after closing the document/window;
- Word reopen cycle.

Use short targeted logging and monitor trace size. Do not enable broad Wine tracing.

#### Gate 0 decision

**GO** only if all of the following are true:

- a material quantity of committed, page-aligned interior space is provably inside ordinary coalesced free blocks;
- the pages are not LFH, pending-free, metadata, or protected minimum-commit storage;
- native equivalent heaps expose compatible decommit/recommit behavior or a clear API-semantic basis exists;
- theoretical reclaimable committed bytes correlate with the measured private/PSS excess.

**NO-GO / pivot** if the regions are mostly live or LFH-owned, or if reclaimable committed bytes are too small to explain the memory difference. The next investigation would then examine live-object retention, LFH group retention, Office heap-backend differences, and non-heap anonymous allocations. Do not implement interior decommit merely to continue the original hypothesis.

### Patch 1 — Represent interior uncommitted ranges, behavior unchanged

Purpose: make the heap model capable of describing holes without decommitting anything yet.

Design requirements:

- Add an explicit per-subheap UCR (uncommitted range) representation whose metadata is never stored inside a range that can become inaccessible.
- Keep `subheap->data_size` and `subheap_commit_end()` as the trailing commit-boundary model initially; represent interior holes separately.
- Define sorted/non-overlapping range invariants, ownership, lifetime, merge/split behavior, and allocation-failure rollback.
- Avoid allocator recursion when creating UCR metadata.
- Keep all mutation under the heap's required serialization domain.

Update or prepare these consumers so they can reason about UCRs without changing externally observable behavior:

- block/range lookup and subheap membership checks;
- `next_block()`, `last_block()`, and any block iteration helpers;
- `check_subheap()` and `heap_validate()`;
- allocation growth and free-list selection;
- heap destruction;
- debug dump facilities.

Patch 1 acceptance:

- no pages are newly decommitted;
- existing heap tests pass on 32-bit and 64-bit builds;
- fault-injection tests prove UCR metadata allocation failure leaves the original heap state unchanged;
- debug validation reports no new corruption;
- performance difference is within measurement noise.

### Patch 2 — Safe interior decommit and recommit for ordinary heaps

Purpose: reclaim proven-unused committed pages while preserving Windows semantics.

Version 1 eligibility rules:

- the subheap is growable and uses the ordinary non-LFH block path;
- the candidate is a fully coalesced free block;
- only complete allocation-granularity/page-aligned interior pages are selected according to the native oracle;
- block headers, free-list entries, adjacent-block headers, back-pointers, sentinels, and UCR metadata stay committed;
- main-subheap pages below `heap->min_size` stay committed;
- the block is not delayed/pending free;
- heaps using free-fill, tail-check, full-validation, or equivalent patterns are excluded until a native-compatible design preserves those diagnostics;
- the reclaim threshold/hysteresis comes from native measurements and performance evidence, not an Office-specific number.

Decommit sequence:

1. Hold the heap's required lock/serialization domain.
2. Coalesce adjacent ordinary free blocks.
3. Calculate an eligible aligned range and revalidate all exclusions.
4. Call `NtFreeVirtualMemory(..., MEM_DECOMMIT)` while retaining the reservation.
5. Publish the UCR only after successful decommit.
6. On failure, leave the original committed free-block state intact.

Recommit sequence:

1. Select an ordinary free block without touching any inaccessible bytes.
2. Determine which UCR portions the allocation and required metadata will occupy.
3. Recommit through `NtAllocateVirtualMemory(..., MEM_COMMIT, ...)` before any header, split, back-pointer, fill-pattern, or payload access.
4. Update/split/remove the UCR only after successful recommit.
5. Complete normal block splitting and allocation.
6. On recommit failure, preserve the old free block/UCR state and return the correct allocation failure.

Zeroing requirements:

- newly committed Windows pages should be zero-filled by virtual-memory semantics;
- already committed reused bytes must still follow Wine's normal `HEAP_ZERO_MEMORY` path;
- tests must cover allocations spanning both recommitted and already committed portions;
- allocator metadata writes must never leak stale nonzero bytes into the returned zeroed user range.

Concurrency requirements:

- no decommit/recommit or UCR mutation in an LFH lock-free window;
- no reader may traverse a UCR as if it contained accessible block headers;
- ordinary serialized heaps keep the full transition under the heap lock;
- `HEAP_NO_SERIALIZE` tests provide caller serialization and verify that Wine introduces no hidden out-of-lock access;
- heap destruction, validation, walking, compacting, and thread-detach paths obey the same lifetime rules.

Patch 2 acceptance:

- targeted unit tests prove `VirtualQuery` observes reserved/uncommitted pages and later committed pages;
- live neighbors retain their exact data across free/decommit/recommit cycles;
- zero-memory and allocation-failure tests pass;
- Wine heap validation remains clean;
- LFH memory is demonstrably never decommitted by version 1;
- committed-byte counters fall by the size of recorded UCRs.

### Patch 3 — Walking, validation, statistics, and diagnostics

Purpose: make every public/internal observer agree with the new heap state.

Required changes:

- `RtlWalkHeap()` / `HeapWalk()` report interior uncommitted ranges without reading them;
- region committed and uncommitted totals include all UCRs, not only the tail;
- heap performance `commit_size` subtracts interior UCR bytes;
- `heap_validate()` checks UCR sort order, overlap, alignment, reservation containment, and adjacency to valid committed metadata;
- pointer validation rejects pointers inside UCRs cleanly;
- debug dumps identify UCRs without dereferencing them;
- destroy paths release UCR metadata exactly once.

Tests should assert stable invariants:

- total committed plus uncommitted bytes reconcile with the reservation;
- busy blocks are never reported inside UCRs;
- every UCR is reported once and is inaccessible until recommitted;
- iteration terminates and returns correct status on malformed or stale entries;
- Windows-version-specific walk ordering is tolerated where the API does not guarantee it.

Patch 3 acceptance:

- kernel32 heap-walk tests pass natively and under Wine with appropriate `todo_wine` transitions;
- heap performance counters agree with virtual-memory state;
- validation handles first, middle, last, adjacent, and multiple UCRs;
- 32-bit structure packing and 64-bit behavior are both tested.

### Patch 4 — Minimal conformant `RtlCompactHeap`

Purpose: replace the current harmless stub with measured Windows-compatible behavior.

Implementation rules:

- derive invalid-handle, flags, serialization, return-value, and last-error/status behavior from the native oracle;
- reuse normal coalescing and the proven safe decommit helper;
- process only ranges that meet the same eligibility rules as normal free-time decommit;
- return the native-defined result, including largest-free-block semantics where confirmed;
- remain a direct API action, not a background timer or Office hook;
- do not make `RtlCompactHeap()` the only way ordinary free pages can be reclaimed unless native measurements require that behavior.

Patch 4 acceptance:

- native differential tests cover empty, fragmented, fixed, growable, LFH, debug, invalid, and `HEAP_NO_SERIALIZE` heaps;
- repeat compaction is idempotent where native behavior is idempotent;
- compaction never changes live allocations;
- return values and observable committed-state changes match stable native semantics.

### Patch 5 — Stress, performance, and broad regression

Correctness stress:

- randomized allocate/free/reallocate sequences with a deterministic seed;
- multiple heaps and multiple threads;
- allocation sizes around page, region, LFH, and large-block boundaries;
- repeated UCR split, partial recommit, full recommit, and merge cycles;
- heap create/destroy races within valid API ownership rules;
- low-memory and virtual-memory failure injection;
- debug/checking and Valgrind configurations, confirming excluded paths stay excluded rather than silently losing diagnostics.

Build/test ladder:

1. Build the changed ntdll heap object and link ntdll.
2. Build `dlls/kernel32/tests/heap.c` and any added ntdll regression target.
3. Run the focused heap tests with the just-built Wine in disposable task-owned prefixes.
4. Run 32-bit and 64-bit tests, then WoW64 where available.
5. Run broader ntdll/kernel32 tests and `git diff --check`.

Performance gates:

- benchmark common small allocation/free operations with LFH enabled and disabled;
- benchmark fragmented standard heaps that do and do not cross the decommit threshold;
- measure syscall count and heap-lock hold time;
- prevent rapid decommit/recommit thrashing through evidence-based hysteresis;
- reject a patch with a material common-path regression unless the design is revised.

Sanitizer/diagnostic validation should use Wine's supported tools and Valgrind annotations where practical. A harness failure must be reported separately from a product failure.

## 8. Office end-to-end validation

Office testing begins only after the focused allocator series is correct.

### 8.1 Environment

- Build from the intended latest source commit in a task-owned out-of-tree build directory.
- Verify the build directory's `srcdir` points to that worktree.
- Use the canonical remote environment under `/home/ttv20/wine365vm-agents` on `ttv20@keremreim-bardugonet`.
- Create a disposable agent-owned Office prefix/session with `tools/office-test-env/create-agent-env.sh`.
- Verify `vnc_server=krfb-virtualmonitor` and `desktop_backend=kwin-virtual` before launch.
- Check CPU, memory, ZFS ARC, disk space, and active test-container ownership before starting.
- Apply reasonable CPU and memory limits; increase concurrency only after checking resources.

### 8.2 Scenario matrix

Run identical cold and warm scenarios on the baseline build and patched build:

| Scenario | Stabilization point | Required observations |
|---|---|---|
| C2R idle | service startup complete | per-process RSS, PSS, private, heap commit/reserve, UCR bytes |
| Launch Word | splash and startup | peak memory, startup time, crashes/hangs |
| Word blank document | UI idle | stable memory and allocator counters |
| Edit/save document | save complete | correctness, latency, stable memory |
| Close Word | process exit/service idle | C2R retained memory and released mappings |
| Reopen Word | second idle | reuse/recommit correctness and warm-start time |
| Word + Excel + PowerPoint | all idle | per-process and group PSS, shared mapping attribution |
| Repeated open/close | fixed cycle count | leak/retention slope and UCR churn |

Use the same Office build, document, prefix snapshot, architecture, wait intervals, and desktop environment for paired runs. Collect at least enough repetitions to distinguish a stable change from startup noise; record median and spread rather than one screenshot value.

### 8.3 Functional validation

- Word launches, edits, saves, closes, and reopens successfully.
- C2R remains healthy and Office activation/update services do not crash.
- Excel and PowerPoint launch in the same prefix.
- Shared PE/DLL pages remain shared between Office apps; validate this with PSS/mapping identity rather than summed RSS.
- No new heap corruption, access violation, deadlock, startup regression, or visual failure appears.

### 8.4 Memory success criteria

Memory numbers are outcomes, not hard-coded correctness requirements.

The patch is successful only when:

- Wine heap committed bytes fall by an amount consistent with tracked UCRs;
- process private resident memory and PSS fall in the same states;
- the difference survives repeated paired runs;
- the reduction is not caused by evicting shared code pages, disabling features/checks, or delaying required work;
- functional and performance gates pass.

The supplied Windows values remain comparison targets—C2R about 50 MiB private and WINWORD about 200 MiB private—but the implementation must not be distorted merely to reach those exact values. Remaining differences must be classified by heap, LFH, live objects, images, stacks, graphics, or other anonymous mappings.

## 9. Evidence and artifacts to preserve

For each gate/patch, keep a task progress record containing:

- source and build commit IDs;
- native Windows version, Office version, architecture, and heap backend;
- exact allocation oracle input and raw output;
- Wine heap diagnostics and `/proc` memory snapshots;
- focused build logs and test results;
- remote environment name, prefix ownership, and resource checks;
- paired Office memory tables, screenshots when useful, and failure classification;
- rejected hypotheses and why they were rejected.

Never present an unchanged test binary, a harness startup failure, or an RSS-only reduction as validation of the patch.

## 10. Delivery structure

The intended upstreamable series is:

1. **Tests/oracle:** native behavior tests and diagnostic evidence, without product behavior changes.
2. **UCR model:** representation and invariant plumbing, behavior unchanged.
3. **Standard-heap reclaim:** safe interior decommit/recommit for the narrowly eligible non-LFH path.
4. **Observers:** walk, validation, statistics, and diagnostics support.
5. **Compaction:** minimal conformant `RtlCompactHeap()` implementation.
6. **Stress/performance:** regression coverage and evidence-based threshold tuning.

If upstream review benefits from smaller patches, split tests and individual observers further. Do not combine Office policy with the generic allocator series.

## 11. Estimated schedule

This estimate begins after access to an architecture-matched Windows test system is ready:

- Gate 0 oracle, Office backend identification, and Wine liveness proof: 3-7 working days.
- UCR representation and invariant plumbing: 4-8 working days.
- Safe standard-heap decommit/recommit: 5-10 working days.
- Walk/validation/statistics and compact behavior: 4-8 working days.
- Stress, performance, Office paired validation, and revision: 5-10 working days.

Expected total: roughly 3-6 weeks. Gate 0 can terminate or redirect the implementation earlier if the hypothesis is disproven.

## 12. Current next action

Implement **Gate 0 only**: create the native differential oracle and the diagnostic Wine measurement design. Do not edit production heap behavior until Gate 0 produces a GO decision backed by committed-byte and block-liveness evidence.
