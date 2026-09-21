# Security model

VitaCheat will eventually inspect and modify another user process. That makes
authorization, bounds, lifecycle cleanup, and update integrity part of the core
design rather than release-time extras.

## Current milestone

The current library cannot access a process, file, socket, or kernel API. It
only searches byte arrays supplied by its caller. Output storage is also
caller-owned and capacity bounded.

The legacy `.psv` importer likewise only indexes caller-owned bytes. Unknown
operations remain opaque, malformed lines remain visible, and one unsupported
modifier taints the complete cheat entry so a later direct-write record cannot
be executed out of sequence. The five-second Select helper consumes only a
button state and timestamp; it grants no memory capability by itself. The
portable pause coordinator calls only adapter-supplied operations and cannot
enumerate or suspend a real thread on its own. The portable Quick Menu broker
and service similarly validate and serialize launch state. The service consumes
only injected exact-copy, caller-attestation, foreground-snapshot, and cleanup
callbacks; no Vita syscall implementation, process discovery, hook, injection,
or memory authority is present. The portable Quick Menu launcher controller
likewise acts only through injected UI, worker, snapshot, clock, and transport
callbacks and has no native SceShell or kernel dependency.

## Authority model

- Read discovery, snapshot capture, writes, and persistent freezes are separate
  capabilities.
- The Vita UI is the authority for choosing a target and approving writes.
- Write and freeze capabilities expire and are revoked on disconnect, title
  change, process exit, sleep/restart, protocol error, or explicit cancellation.
- A PC companion never gains implicit write access merely because it is paired.
- Every operation is scoped to one process generation and verified module
  identity.

## Quick Menu launcher

- The implemented v1 byte ABI uses exact 64-byte requests and 72-byte responses,
  explicit little-endian fields, fixed version/size checks, zeroed reserved
  fields, and operation-specific payload validation.
- The `SceShell` add-on has a launch-only role. It cannot read game memory,
  suspend threads, claim/cancel as a game, arm writes, apply cheats, or freeze
  values.
- A launch request is bound by the kernel service to the current foreground
  process generation, assigned a deterministic nonzero and nonwrapping unique
  ID, and expires after at most 15 monotonic seconds.
- The injected game plugin may claim a request only when its caller process and
  generation and request ID match. A title switch, process exit, successful
  claim, timeout, cancel, plugin unload, service reset, or monotonic-clock
  rollback invalidates it.
- One pending request is retained with an explicit terminal state. Duplicate
  submissions for its target are idempotent and do not extend the deadline;
  malformed, mismatched, or overprivileged calls cannot retarget it.
- Thread suspension begins only after the system Quick Menu has closed and the
  game plugin has revalidated its presentation path and target generation.
- Missing or incompatible QuickMenuReborn support fails closed; VitaCheat does
  not patch unknown SceShell offsets as a fallback.
- All Quick Menu widgets, event handlers, textures, and worker state have
  symmetric stop cleanup.
- The service never decodes directly from caller memory. It validates lengths,
  copies exactly 64 bytes into a zeroed local request, fully validates and
  attests it, dispatches, encodes into a zeroed 72-byte local response, and
  copies out exactly those 72 bytes.
- Caller role, process generation, and module-load generation come from a
  privileged attestation callback. Foreground PID, process generation, bounded
  title identity, and observation sequence come from a separate trusted
  snapshot callback. Wire values can only match these facts, never define them.
- Copy-in failure occurs before broker mutation. Copy-out failure retains one
  exact result journal; unrelated calls are blocked until exact retry or a
  lifecycle event revokes the journal. Partial copies cannot expose padding or
  uninitialized kernel bytes.
- A nonblocking atomic transaction gate rejects reentry and concurrent service
  calls. Callbacks run without an internal mutex, and lifecycle events that
  encounter `BUSY` must be retried rather than silently dropped.
- Stop/reset scrub the pending record, foreground snapshot, and result journal
  while preserving the nonwrapping request-ID counter. Stop remains revoked
  even when platform cleanup reports failure.
- The v1 operation and capability sets remain launch-only. Unknown future
  memory, pause, search, patch, or freeze operations/bits fail closed.
- The add-on-side controller has one fixed action slot. Its UI callback only
  captures and revalidates trusted foreground metadata and signals one worker;
  one worker tick can issue at most one transport call.
- The controller emits only exact v1 `SceShell` `SUBMIT/LAUNCH` and
  `STATUS/STATUS` envelopes through the existing codec. It exposes no generic
  operation pass-through and has no claim, cancel, memory, pause, write,
  freeze, search, patch, renderer, or hardware interface.
- Snapshot sequence, PID, nonzero generation, and normalized bounded title
  identity must remain byte-identical before queueing, before request
  construction, after clock acquisition, and after transport. A mismatch
  discards local work and presents only a non-sensitive stale-title state.
- Adapter callbacks run outside the controller's nonblocking transaction gate.
  Reentry is rejected as busy. Resource registration is transactional; stop
  invalidates callback generations and local work before reverse cleanup, and
  failed cleanup tokens remain retryable while the controller stays stopped.
- Public launcher status strings disclose no target, title, request, process,
  module, generation, nonce, kernel, or memory identity. A pending request is
  described only as waiting for the game to claim it.
- A zero-ID `STATUS` request is valid only for the game role and discovers only
  a pending record for its exact target. The service must first attest the game
  module and match its PID/generation to the current foreground snapshot.
  `SceShell`, nonmatching game callers, terminal records, and public formatters
  receive no discovered request ID.
- The game claimant binds authoritative self PID, process generation,
  module-load generation, bounded title identity, and matching foreground
  metadata from one coherent trusted adapter observation. Identity-sequence
  changes or rollback, PID reuse, module reload, title mismatch, and missing or
  changed foreground all revoke local authority.
- Overlay closure requires two strictly sequenced closed observations in one
  overlay generation. Unknown, open, closing, reopen, and generation changes
  fail closed. No timer substitutes for this gate and the game claimant never
  attempts to close the system overlay.
- Presentation readiness is an abstract trusted adapter result bound to the
  exact process/module generation. It must remain exact across claim transport.
  Loss or any newer observation revokes unconsumed authorization and closes
  local open state; this portable layer installs no display hook.
- The claimant emits only game-role `STATUS/STATUS`, `CLAIM/CLAIM`, and
  `CANCEL/CANCEL` envelopes through the existing codec. One worker step performs
  at most one transport call; callback-facing observation updates never call
  transport.
- Successful claim creates one short-lived controller-local token rather than
  opening the menu. The token is bound internally to request, identity,
  overlay, presentation, and lifecycle generations, is consumed once, and
  requires a separate open acknowledgement. It expires without extension and
  is scrubbed on rollback, stale observation, reset, stop, or unload.
- Stop/unload revoke local authority before bounded cleanup. Safe known pending
  requests receive only game-role cancel, exact service-journal retries are
  never rebuilt, and the unload adapter invokes the service plugin-unload
  lifecycle for the saved module identity. Failure is explicit but cannot keep
  the controller running or preserve a token.
- The menu coordinator accepts only that exact unconsumed authorization. It
  completes all local allowlist, duration, identity, readiness, and ownership
  checks before consuming the token once immediately before pause acquisition;
  a consumed token is never replaced or replayed after failure.
- The allowlist is caller-supplied, copied, strictly increasing, nonempty, and
  bounded by `VC_PAUSE_MAX_THREADS`. It carries a nonzero trusted source
  revision and must match the exact claimant PID, process generation, and
  module-load generation.
- Plugin control, input hook, renderer/present hook, watchdog, cleanup, and
  current calling thread IDs are explicit positive protected inputs. Any
  protected occurrence, zero/negative ID, duplicate, disorder, unknown
  ownership, revision change, or over-limit count fails before suspension.
- Suspend and resume callbacks are bracketed by trusted target/readiness or
  ownership checks without holding the coordinator transaction gate. Consumed
  and open claimant states must still byte-match the exact authorization
  lineage after external callbacks; resume ownership checks contain only
  threads that remain suspended. Reentrant mutations return `BUSY`; stale
  completion cannot acknowledge or reopen a menu.
- Full suspension creates only a provisional opaque lease. Renderer/menu
  acknowledgement consumes and rotates it before `OPEN`; close consumes the
  final lease. Target identity, claimant lineage, exact copied allowlist,
  revision, pause generation, coordinator lifecycle, and fixed deadlines
  remain bound internally and are never formatted.
- The acknowledgement deadline is at most one second and the menu lease is no
  longer than the existing ten-minute pause maximum. Zero, over-limit, and
  overflowing intervals are rejected rather than clamped, and no operation
  silently extends either deadline.
- Close, timeout, clock rollback, target/foreground loss, overlay reopen,
  presentation loss, claimant reset, exact target exit, unload, and stop revoke
  menu authority before reverse resume. Failed resumes remain explicitly owned
  and block new opens; exact target exit is the only notification that may
  abandon ownership without resume. Stop remains stopped while cleanup is
  retryable.

The portable broker/ABI, launch-only service policy, add-on controller, game
claimant/open-authorization lifecycle, and authorization-to-pause/menu-lease
coordinator are implemented. Native QuickMenuReborn widgets remain blocked
because the pinned public API has no runtime version query, and native service
transport remains blocked because QuickMenuReborn documents no kernel bridge.
Public taiHEN lifecycle/hook APIs do not by themselves provide the required
authoritative foreground, system overlay, universal presentation, and
title-specific gameplay-thread facts. SceShell hooks, Vita service
syscalls/exports, concrete caller identity derivation, native injection,
renderer/input/menu hooks, allowlist acquisition, bounded memory access, and
cleanup hardware gates remain unavailable; no firmware-offset fallback is
permitted.

## Menu pause rules

- Only a bounded, explicit allowlist of gameplay threads may be suspended.
- Injected-plugin, input, display, menu-render, watchdog, cleanup, kernel, and
  unrelated process threads are never members of that allowlist.
- Every successful suspension is recorded as owned before the next operation.
- Partial setup rolls owned suspensions back in reverse order.
- Menu close and forced expiry attempt every owned resume even if one fails;
  failed ownership remains recorded for a later cleanup retry.
- A process-generation mismatch fails closed before touching a thread ID.
- An unpaused watchdog must tick the pause transaction; it forces the cleanup
  path after at most ten minutes or after a monotonic-clock rollback.
- Ownership is discarded without resume only after the adapter independently
  proves that the original process generation is gone.

## Memory rules

- Regions must be enumerated and allowlisted before access.
- Address, length, alignment, and overflow checks happen before any access.
- Cheats use typed, module-relative operations rather than unbounded scripts.
- Build-identity mismatches fail before reads are interpreted or writes are
  offered.
- Active writes retain enough original state for bounded cleanup where
  restoration remains valid.

The first foreign-process write gate is reserved for the user's offline
US Ratchet & Clank Collection (`PCSA00133`) test. Its upstream first-game bolt
entry is a chained `$B200` module-base selector plus `$0100` typed write, not a
standalone direct address. Neither a matching cheat name nor the legacy chain
is sufficient authority: both record semantics and sequence scope must be
implemented independently, and the installed title ID, region, version, module
identity, process generation, resolved writable region, and typed operation
must all match. The gate is a one-shot reversible bolt-count write with an
original-value ledger; freezing and saving the modified value remain disabled
until cleanup behavior is proven.

## Local-network protocol

No LAN control will ship before pairing exists. The eventual protocol requires:

- on-device, session-scoped pairing;
- capability and protocol-version negotiation;
- bounded frames, strings, counts, and transfer windows;
- replay-resistant session messages;
- explicit timeouts, cancellation, and idempotent cleanup;
- a reviewed cryptographic library rather than custom cryptography.

Until those properties are implemented and tested, development transports must
be treated as trusted-private-LAN tools and remote writes remain out of scope.

## Online database

- Catalogs and records are versioned, bounded, and tied to build identity.
- Records contain declarative typed operations, never executable scripts.
- Updates use authenticated transport where available, signed manifests,
  atomic cache replacement, and rollback to the last verified catalog.
- Downloading is read-only first; publishing and account workflows are later
  milestones.

## Intended-use boundary

The project is intended for systems the user owns, offline and single-player
play, homebrew development, accessibility, and personal modding. It will not
provide features intended to bypass online multiplayer protections.
