# Architecture

VitaCheat is divided by authority. Searching bytes is a different capability
from reading a process, and reading is different from writing or freezing.
Keeping those operations separate makes each layer easier to test and limits
what a parsing or UI error can do.

## `vitacheat_core`

The current component is a deterministic, allocation-free C library. It accepts
immutable byte snapshots and caller-owned result buffers. It knows nothing
about processes, absolute addresses, files, sockets, VitaSDK, or the kernel.

Offsets are 32-bit and relative to a region selected by a future adapter. The
adapter, not the search engine, will map a result to a verified process, module,
build identity, region, and ASLR placement.

## Future access adapter

The device adapter will expose explicit capabilities rather than one general
memory service:

1. discover a title and verified modules;
2. list bounded, allowlisted user-memory regions;
3. copy a readable region into a snapshot;
4. arm a short-lived write capability after on-device approval;
5. apply typed writes or freezes from declarative records;
6. restore state and release target ownership on every exit path.

The first adapter will be a buffer-only Vita self-test. Cross-process access is
a later milestone with its own threat model and hardware evidence.

## Future on-device application

The Vita UI will own target selection, user approval, operation progress,
cancellation, write arming, active-freeze visibility, and cleanup. A remote PC
cannot silently grant itself write authority.

## Future PC companion

The PC companion will accelerate scans, filtering, pointer work, and authoring.
Its protocol will be bounded and versioned. Pairing is initiated and confirmed
on the Vita, and remote writes require a second short-lived capability.

## Future online database

Database entries will be declarative typed operations tied to exact title and
module build identities. Catalog data will never be treated as executable
code. Downloads, cache updates, and rollbacks will be bounded and atomic.

## Narrow kernel companion

The intended Vita architecture includes a small kernel companion for narrowly
defined cross-process discovery, snapshot, write, and lifecycle operations that
cannot be implemented from the user application. The search engine, UI,
database client, pairing, protocol parsing, and cheat authoring do not belong in
kernel space.

The companion must use a small versioned ABI, derive and validate caller
ownership, expose no arbitrary kernel-memory primitive, clean up after
disconnects, and pass independent review plus a disposable hardware gate.

PSP and PS1 titles running inside the Vita's PSP emulator are a separate later
research target. A Vita kernel plugin may be required to locate or cooperate
with the emulator, but it does not automatically understand the emulated PSP
address space or load PSP-side hooks. That support will not be claimed until an
explicit Vita-side/emulator-side design is proven on hardware.

## Relationship to VitaDebugger

The projects remain separate. VitaCheat can reuse stable interface ideas and
host-side utilities from VitaDebugger, especially build identity,
module-relative addresses, bounded framing, stop ownership, watchdog cleanup,
logging, profiling, and VitaDevDeploy integration. Sharing a reviewed component
later is preferable to copying experimental debugger internals now.
