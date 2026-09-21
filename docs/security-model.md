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
enumerate or suspend a real thread on its own.

## Authority model

- Read discovery, snapshot capture, writes, and persistent freezes are separate
  capabilities.
- The Vita UI is the authority for choosing a target and approving writes.
- Write and freeze capabilities expire and are revoked on disconnect, title
  change, process exit, sleep/restart, protocol error, or explicit cancellation.
- A PC companion never gains implicit write access merely because it is paired.
- Every operation is scoped to one process generation and verified module
  identity.

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
