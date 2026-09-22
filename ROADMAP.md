# VitaCheat roadmap

Only completed, tested behavior belongs in the current-status section of the
README. Each Vita-facing milestone will receive a reproducible hardware record.

## 0. Portable foundation — in progress

- [x] Allocation-free C11 exact snapshot search.
- [x] Bounded candidate refinement.
- [x] Integer little-endian decoding without unaligned pointer casts.
- [x] Deterministic host tests for ordinary and malformed inputs.
- [x] Lossless, allocation-free legacy `.psv` syntax importer.
- [x] Conservative typed mapping for documented direct 8/16/32-bit writes.
- [x] Preserve unsupported legacy codes as opaque records without executing
      guessed behavior.
- [x] Portable, debounced five-second Select-hold activation state machine.
- [x] Versioned little-endian Quick Menu launch ABI and allocation-free,
      generation-bound single-request broker.
- [x] Allocation-free launch service front door with trusted caller
      attestation, sequenced foreground snapshots, exact copy boundaries,
      fail-closed copy-out retry, lifecycle cleanup, and nonblocking
      serialization.
- [x] Allocation-free Quick Menu launcher controller with a one-slot deferred
      callback handoff, exact submit/status-only requests, trusted snapshot
      revalidation, bounded status, and transactional resource cleanup.
- [x] Allocation-free injected-game launch claimant with trusted identity,
      stable overlay-close and presentation gates, exact status/claim/cancel
      transport, and one-time expiring local open authorization.
- [x] Allocation-free menu/pause coordinator with exact claimant handoff,
      explicit protected-thread exclusion, bounded leases, readiness
      acknowledgement, watchdog expiry, and retryable reverse cleanup.
- [x] Allocation-free trusted-target attestation catalog with transactional
      bounded acquisition, immutable nonrepeating revisions, exact
      title/version/fingerprint/module policy matching, module/segment lookup,
      explicit thread ownership checks, and symbolic permission-scoped range
      validation.
- [x] Allocation-free read-only memory-service contract with a separate v1
      ABI, trusted active-menu lineage, exact caller/target/snapshot binding,
      symbolic module/segment/offset reads, finite quotas, post-read race
      checks, and idempotent copy-out recovery.
- [x] Add sanitizer, fuzz, and property-based CI coverage.
- [ ] Add snapshot/session file formats with strict version and size limits.

## 1. Search engine coverage

- [ ] Unknown-initial-value capture.
- [ ] Additional integer widths, pointers, and explicit float comparison rules.
- [ ] Range, delta, increased-by, decreased-by, and percentage refinements.
- [ ] Region-aware candidate sets and bounded chunk streaming.
- [ ] Candidate compression suitable for Vita memory limits.

## 2. Offline authoring workflow

- [ ] File-backed snapshot capture and refinement on a PC.
- [ ] Save and resume search sessions.
- [ ] Finalize the versioned declarative cheat schema beyond the current
      versioned import representation.
- [ ] Implement and test each remaining documented legacy VitaCheat code family
      independently; keep unknown codes opaque indefinitely.
- [x] Import and independently validate the `$B200` module-base selection
      family, including chained-operation scope and malformed-sequence handling.
      Runtime module resolution and write execution remain separate gates.
- [ ] Add a conversion/export tool that proves byte-exact `.psv` round trips.
- [ ] Bind every record to title ID, module identity, and module-relative
      offsets.
- [ ] Reject mismatched builds before exposing a write action.

## 3. Vita self-test application

- [x] Build a VitaSDK app that scans only its own explicit test buffer.
- [x] Feed the portable five-second Select trigger into a bounded on-device
      menu without adding privileged kernel-module or foreign-process access.
- [ ] Validate memory pressure, cancellation, suspend/resume, and cleanup.
- [x] Record the first scoped retail Vita 3.65 self-test hardware gate.
- [ ] Repeat the scoped self-test gate on Vita TV 3.65.

## 4. Read-only target discovery

- [x] Define and host-test the portable trusted snapshot, module/segment/thread
      catalog, exact build policy, lifecycle invalidation, and checked range
      query foundation without adding a memory primitive.
- [ ] Enumerate allowlisted user processes, modules, and readable regions.
- [ ] Implement a documented native adapter that supplies authoritative
      process/module/thread generations and measured build fingerprints.
- [x] Implement and host-test the portable bounded read-only memory service
      against synthetic target memory without adding a native syscall.
- [x] Pin and compile/link an isolated experimental native transport that
      derives its caller PID and reads only the source-owned test process
      through a 64-byte `ksceKernelCopyFromUserProc` bounce.
- [ ] Run and retain the `VCHG00001` same-process native gate on owned 3.65
      hardware; passing it still does not authorize foreign-process access.
- [ ] Design, review, and hardware-gate strong foreign-process attestation and
      a production target-read adapter before capturing any foreign bytes.
- [x] Reconcile trusted catalog addresses into verified module/segment-relative
      symbolic offsets without dereferencing them.
- [x] Add portable lifecycle tests for app exit, relaunch, PID reuse,
      foreground sequence rollback, and module churn.

## 5. On-device search UI

- [x] Define and host-test bounded, generation-bound gameplay-thread pause
      ownership with reverse rollback, retryable cleanup, and forced expiry.
- [ ] Search, refine, cancel, sort, and inspect candidates on Vita.
- [ ] Add a QuickMenuReborn `SceShell` add-on with an **Open VitaCheat** button,
      foreground-title status, and symmetric widget/texture cleanup.
- [x] Host-test the launch-only SceShell role, exact game-plugin claim binding,
      overlay-readiness gate, expiry, lifecycle invalidation, and nonrepeating
      request IDs without adding native adapters.
- [x] Host-test the launch-only kernel-service policy boundary, including
      caller/process/module attestation inputs, PID-generation reuse, title
      switches, copy faults, response retries, stop/reset, and reentrancy.
- [x] Host-test sending only a short-lived, generation-bound open request from
      the Quick Menu controller; never grant it read, pause, write, or freeze
      authority.
- [x] Host-test the matching injected game-plugin claimant after a stable
      system-overlay close and exact presentation compatibility check, emitting
      one local open authorization without rendering or pausing.
- [ ] Implement the native injected game plugin and renderer/menu owner around
      the portable authorization-to-pause coordinator.
- [ ] In the kernel adapter, build an explicit gameplay-thread allowlist that
      excludes the injected menu, input, renderer, watchdog, and cleanup paths.
- [x] Host-test coupling menu open/close to transactional pause/resume and
      refuse to open when suspension or rollback is incomplete.
- [x] Add an optional immutable target-snapshot revision gate before and around
      pause acquisition while preserving existing coordinator behavior when no
      attestation adapter is configured.
- [ ] Prove supported display hooks per rendering path before claiming
      cross-title overlay compatibility.
- [ ] Preserve progress within fixed memory and time budgets.
- [ ] Build accessible controls and clear current-operation feedback.

## 6. Controlled write and freeze

- [ ] Require explicit, expiring on-device write authorization.
- [ ] Keep an original-value ledger and restore on disable or exit when safe.
- [ ] Coordinate target stop/resume without leaving threads suspended.
- [ ] Add watchdog, disconnect, title-exit, and partial-failure cleanup gates.
- [ ] Run the first reversible hardware write gate against the user's US Ratchet
      & Clank Collection only after all preceding read and cleanup gates pass:
      - start from the `PCSA00133` US v1.00/NoNpDrm database entry, then require
        the exact installed title ID, region, version, module identity, and
        process generation observed on the test device;
      - import the matching upstream `.psv` entry instead of embedding its
        address or value in code;
      - accept only the first game's `max.Schrauben` bolt operation after the
        preceding `$B200` module-base selector and chained `$0100` typed write
        are both supported, validated, and resolved for the exact build;
      - prove the resolved address is inside an allowlisted writable user
        region for that exact build;
      - display and ledger the original bolt count before explicit on-device
        one-shot authorization;
      - restore the original value on menu close, timeout, title exit, unload,
        or any partial failure, and do not enable freeze or save the game during
        this initial gate;
      - retain artifact hashes, logs, observed before/after/restored values, and
        a scoped hardware record.

## 7. Paired PC companion

- [ ] Use an on-device, session-scoped pairing flow.
- [ ] Add bounded chunked snapshot and refinement transport.
- [ ] Provide faster filtering, pointer analysis, and cheat authoring.
- [ ] Keep remote writes separately armed and short lived.

## 8. Online community database

- [ ] Define signed, versioned manifests and bounded declarative records.
- [ ] Download and cache compatible records atomically.
- [ ] Provide rollback for interrupted or invalid catalog updates.
- [ ] Add an opt-in review and publishing workflow after read-only download is
      mature.

## 9. Narrow hybrid integration and later PSPemu research

- [ ] Implement a narrow kernel service for target lifecycle, cooperative
      thread control, and bounded cross-process memory access without exposing
      general arbitrary kernel read/write operations.
- [ ] Implement a QuickMenuReborn add-on in `SceShell` for native launch/status
      widgets, with a weak/optional dependency and a pinned compatibility gate.
- [ ] Implement an injected game user plugin for request claiming, display
      hooks, menu rendering/navigation, search state, and user approvals.
- [x] Define and host-test a versioned, bounded request ABI for the future user
      plugin/kernel-service boundary plus a launch-only SceShell role, with
      caller, process-generation, capability, request-ID, expiry, and size
      checks.
- [x] Implement the portable launch-only service core and precise platform
      adapter callback contract. Native Vita syscall/export glue, caller
      identity derivation, and foreground discovery remain unimplemented.
- [x] Implement the portable add-on-side launcher controller and host-test
      resource registration/rollback, deferred work, status refresh, stale
      callback rejection, and malformed transport responses.
- [x] Implement the portable game-side claimant/controller and host-test
      attested discovery, stable overlay/presentation gating, one-shot
      authorization, stale completion rejection, and stop/unload cleanup.
- [x] Implement the portable menu owner that consumes one claimant
      authorization, validates a caller-supplied allowlist and protected set,
      owns pause/resume, and enforces a finite nonextending menu lease.
- [x] Implement the separate portable read-only service/ABI, bound to exact
      open-menu lineage and immutable attestation revisions, without widening
      launch-only v1 or adding writes.
- [ ] Resolve the missing public QuickMenuReborn runtime-version probe and
      implement an attested SceShell-to-kernel transport before adding the
      optional native `.suprx` target.
- [ ] Pin and compile verified native user-plugin injection, authoritative
      foreground/overlay observation, and per-renderer readiness adapters
      before adding a claimant `.suprx`; public taiHEN lifecycle/hook APIs alone
      do not provide those complete end-to-end facts.
- [ ] Define and verify a title-specific native gameplay-thread allowlist
      acquisition policy and attested suspend/resume transport without
      hard-coded IDs, name/priority heuristics, or scheduler starvation.
- [ ] Keep parsing, rendering, protocol handling, and database logic out of
      kernel context.
- [ ] Validate process-generation binding, unload, title exit, suspend/resume,
      crash, and partial-hook rollback before enabling memory writes.
- [ ] Research the Vita-side and emulator-side pieces required for PSP and PS1
      environments as a separate later target, not promised compatibility.

## 10. Release hardening

- [ ] Long hardware soaks and forced-disconnect testing.
- [ ] Parser fuzzing and protocol state-machine review.
- [ ] Pairing, transport, update, and database security review.
- [ ] Firmware and plugin compatibility matrix.
- [ ] Reproducible packaging and release documentation.
