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

- [ ] Enumerate allowlisted user processes, modules, and readable regions.
- [ ] Capture bounded snapshots without write capability.
- [ ] Reconcile ASLR through verified module-relative addressing.
- [ ] Add lifecycle tests for app exit, relaunch, and module churn.

## 5. On-device search UI

- [x] Define and host-test bounded, generation-bound gameplay-thread pause
      ownership with reverse rollback, retryable cleanup, and forced expiry.
- [ ] Search, refine, cancel, sort, and inspect candidates on Vita.
- [ ] Add a QuickMenuReborn `SceShell` add-on with an **Open VitaCheat** button,
      foreground-title status, and symmetric widget/texture cleanup.
- [ ] Send only a short-lived, generation-bound open request from the Quick Menu;
      never grant it read, pause, write, or freeze authority.
- [ ] Let the matching injected game plugin claim the request after the system
      overlay closes, then render the menu only after compatibility checks pass.
- [ ] In the kernel adapter, build an explicit gameplay-thread allowlist that
      excludes the injected menu, input, renderer, watchdog, and cleanup paths.
- [ ] Couple menu open/close to transactional pause/resume and refuse to open
      when suspension or rollback is incomplete.
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
- [ ] Define a versioned, bounded request ABI between the user plugin and kernel
      service plus a launch-only SceShell role, with caller, process-generation,
      capability, request-ID, expiry, and size checks.
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
