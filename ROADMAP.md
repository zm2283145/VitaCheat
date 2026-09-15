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
- [ ] Add sanitizer, fuzz, and property-based CI coverage.
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

- [ ] Search, refine, cancel, sort, and inspect candidates on Vita.
- [ ] Feed Vita controller samples into the tested five-second Select trigger
      and render the menu only after its one-shot event.
- [ ] Preserve progress within fixed memory and time budgets.
- [ ] Build accessible controls and clear current-operation feedback.

## 6. Controlled write and freeze

- [ ] Require explicit, expiring on-device write authorization.
- [ ] Keep an original-value ledger and restore on disable or exit when safe.
- [ ] Coordinate target stop/resume without leaving threads suspended.
- [ ] Add watchdog, disconnect, title-exit, and partial-failure cleanup gates.

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

## 9. Narrow system integration and later PSPemu research

- [ ] Add a kernel companion only for documented cross-process needs that user
      mode cannot satisfy; keep scanning, UI, networking, and database logic in
      user mode.
- [ ] Keep any kernel interface narrow, versioned, caller-scoped, and free of
      general arbitrary kernel read/write operations.
- [ ] Research the Vita-side and emulator-side pieces required for PSP and PS1
      environments as a separate later target, not promised compatibility.

## 10. Release hardening

- [ ] Long hardware soaks and forced-disconnect testing.
- [ ] Parser fuzzing and protocol state-machine review.
- [ ] Pairing, transport, update, and database security review.
- [ ] Firmware and plugin compatibility matrix.
- [ ] Reproducible packaging and release documentation.
