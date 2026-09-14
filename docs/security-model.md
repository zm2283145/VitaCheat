# Security model

VitaCheat will eventually inspect and modify another user process. That makes
authorization, bounds, lifecycle cleanup, and update integrity part of the core
design rather than release-time extras.

## Current milestone

The current library cannot access a process, file, socket, or kernel API. It
only searches byte arrays supplied by its caller. Output storage is also
caller-owned and capacity bounded.

## Authority model

- Read discovery, snapshot capture, writes, and persistent freezes are separate
  capabilities.
- The Vita UI is the authority for choosing a target and approving writes.
- Write and freeze capabilities expire and are revoked on disconnect, title
  change, process exit, sleep/restart, protocol error, or explicit cancellation.
- A PC companion never gains implicit write access merely because it is paired.
- Every operation is scoped to one process generation and verified module
  identity.

## Memory rules

- Regions must be enumerated and allowlisted before access.
- Address, length, alignment, and overflow checks happen before any access.
- Cheats use typed, module-relative operations rather than unbounded scripts.
- Build-identity mismatches fail before reads are interpreted or writes are
  offered.
- Active writes retain enough original state for bounded cleanup where
  restoration remains valid.

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

