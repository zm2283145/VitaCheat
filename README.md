# VitaCheat

VitaCheat is an early research and development project for a modern community
cheat-authoring tool on user-owned PlayStation Vita systems. The intended end
result combines an on-device workflow with an optional PC companion and an
online community database.

This repository is an early scaffold. The current code is a portable,
allocation-free memory snapshot search/refinement core, a lossless legacy
VitaCheat `.psv` importer, and a deterministic menu-activation state machine,
plus transactional gameplay-thread pause ownership, a versioned,
allocation-free Quick Menu launch-request broker, a caller-attesting,
copy-bounded portable launch service front door, a host-testable Quick Menu
launcher and game claimant, and a portable authorization-to-pause menu
coordinator, plus a portable trusted-target attestation catalog and checked
range resolver, and a separate allocation-free read-only memory-service
contract, all with host tests. A separate ordinary user-mode Vita self-test now
exercises the scanner and five-second Select menu against memory owned by that
test app. It is not a plugin and does not attach to a process, suspend another
application's threads, read or write another application's memory, freeze
values, connect over a network, or download cheats.

## Why this project exists

The Vita community has relied on aging closed tooling for many years. Modern
homebrew development work on VitaDebugger has produced useful design patterns
for bounded memory access, module/build identity, safe cleanup, network
protocols, debugging, profiling, and remote deployment. VitaCheat will apply
those lessons to a separate tool focused on finding, creating, organizing, and
using cheats.

The intended scope is user-owned devices, offline and single-player use,
homebrew testing, accessibility, and personal modding. The project will not
target online multiplayer bypasses and will not include copyrighted game data
or proprietary SDK files.

## Current milestone

The first milestone provides `vitacheat_core`, a C11 scanner for the supported
Windows/Vita targets (which provide `uintptr_t`) that:

- reads only caller-provided immutable byte snapshots;
- performs explicit little-endian U8/S8/U16/S16/U32/S32 comparisons;
- supports exact/not-equal initial searches;
- refines candidates by exact value, changed, unchanged, increased, or
  decreased state;
- keeps deterministic ascending, region-relative offsets;
- supports exact in-place result compaction;
- uses caller-owned fixed-capacity buffers and reports truncation;
- rejects malformed queries and candidate lists before writing results.

It deliberately has no VitaSDK, process, kernel, file, or network dependency.

The same portable milestone now also:

- indexes legacy `.psv` comments, `_V0`/`_V1` entries, and code records while
  retaining byte-exact source spans, including original line endings;
- recognizes only the documented `$0000`, `$0100`, and `$0200` direct-write
  forms as typed 8/16/32-bit operations;
- recognizes a strictly bounded `$B200` module/segment selector only when it is
  the first operation in a cheat and scopes following typed writes as relative;
- marks duplicate, late, out-of-range, or dangling `$B200` sequences malformed;
- preserves every other syntactically valid code as opaque data instead of
  guessing its meaning;
- reports malformed records, truncation, and legacy format-limit violations;
- exposes parsed records only when all output buffers fit, preventing dangling
  cross-indexes in a truncated import;
- emits one menu-open event after Select remains held for five seconds, then
  waits for a release before it can fire again;
- coordinates a bounded, generation-bound pause transaction for an adapter-
  supplied allowlist of gameplay threads;
- rolls back partial suspension in reverse order and retains only failed cleanup
  ownership for an explicit retry;
- rejects stale process generations and forces cleanup after a bounded menu
  deadline or monotonic-clock rollback;
- encodes and decodes a fixed little-endian v1 Quick Menu ABI without packed
  structs or host-endian assumptions;
- holds at most one short-lived launch request for the current foreground
  process generation, with deterministic nonrepeating request IDs;
- limits `SceShell` to idempotent launch submission and bounded status while
  only the exactly matching game plugin can claim or cancel;
- records overlay readiness explicitly and invalidates requests on expiry,
  clock rollback, title/process changes, plugin unload, cancel, or reset.
- places the launch broker behind an allocation-free service boundary that
  copies exact fixed-size messages through injected user-copy callbacks;
- derives caller role and process/module generations through an attestation
  callback and rejects wire identities that disagree with trusted caller or
  foreground metadata;
- owns sequenced foreground snapshots, including bounded title identity, and
  invalidates requests on PID reuse, title switch, process exit, plugin unload,
  reset, or stop;
- serializes calls with a nonblocking C11 atomic transaction gate and journals
  an exact response across copy-out failure so a caller can retry without
  repeating or hiding an authority change;
- models the launch-only `SceShell` add-on lifecycle through injected UI,
  worker, trusted-foreground, clock, and transport adapters;
- keeps the UI callback bounded to one foreground-bound queue slot and one
  worker signal, with service transport deferred to a one-operation worker
  step;
- emits only broker v1 `SUBMIT` and `STATUS` requests for the
  `SceShell` role, revalidates the exact snapshot sequence, PID, generation,
  and bounded title identity across every asynchronous boundary, and exposes
  only non-sensitive bounded status text; and
- registers and unwinds texture, label, widget, callback, and worker tokens
  transactionally, retaining failed cleanup tokens for an explicit retry while
  remaining stopped;
- models the injected game-plugin claimant through a trusted, coherent identity,
  overlay, presentation, clock, transport, and unload adapter contract;
- discovers only its own pending request with attested game-role `STATUS`, then
  emits only exact game-role `STATUS`, `CLAIM`, and `CANCEL` v1 envelopes;
- requires a second sequenced closed-overlay observation and exact renderer
  readiness for the same PID, process generation, module instance, title, and
  controller lifecycle before and after claim transport;
- converts one successful claim into a short-lived local menu-open
  authorization that can be consumed and acknowledged once, and revokes it on
  observation change, expiry, rollback, reset, stop, or unload; and
- consumes that authorization only after validating an explicit sorted,
  generation-bound gameplay-thread allowlist and explicit protected control,
  input, presentation, watchdog, cleanup, and calling-thread identities;
- acquires the existing pause transaction with exact observation checks around
  every suspend boundary, exposes a finite opaque provisional/open lease, and
  acknowledges claimant open only after renderer/menu readiness is confirmed;
- revokes local menu authority before reverse resume on close, timeout, clock
  rollback, foreground/overlay/presentation change, reset, unload, or stop,
  retaining failed resume ownership for bounded retry; and
- transactionally collects a trusted foreground target into fixed owned
  storage through injected begin/module/thread/end callbacks, detects mutation
  with a nonzero collection token, and publishes only completely validated
  immutable revisions;
- binds each snapshot to exact PID/process generation, foreground sequence,
  canonical title ID, optional version label, opaque adapter-supplied
  fingerprint algorithm/bytes, module/load generations, checked segments, and
  exact thread ownership;
- rejects malformed/duplicate/over-limit identities, module or thread facts,
  zero/wrapping/overlapping segments, unknown permissions, sequence rollback,
  stale revisions, and partial or repeatedly mutating collections;
- exact-matches title/version/fingerprint/module policy facts, resolves bounded
  `$B200` module serial and segment indices, and validates permission-scoped
  32-bit ranges without dereferencing memory or returning a kernel pointer;
- validates only caller-supplied sorted gameplay and protected thread sets,
  never selecting threads from names, roles, priority, order, or hard-coded
  IDs, and exposes an optional coordinator pre-pause attestation bridge; and
- invalidates and scrubs snapshots on foreground loss/change, process exit,
  module churn, plugin unload, reset, stop, or sequence rollback while keeping
  revisions nonzero and non-reusable within one instance; and
- exposes an independent v1 read-only ABI with exact 80-byte requests,
  64-byte response headers, explicit little-endian fields, and at most 256
  payload bytes;
- permits only an exactly attested game-plugin caller with `READ_TARGET`,
  an internally activated nonrepeating session, the active acknowledged
  menu/pause lineage, and the current foreground target/attestation revision;
- resolves only module/segment/offset requests through target attestation,
  rejects unreadable, cross-segment, stale, wrapping, zero, or over-limit
  ranges, and derives the absolute target address only for one injected read
  callback;
- enforces finite byte, operation, and time budgets without extension, charges
  every fully authenticated request against the operation budget, revalidates
  target lineage after each read, rejects short or mutated reads, and scrubs
  every failed payload;
- journals one exact read/status response across copy-out failure so retries
  never repeat a successful read, while lifecycle invalidation scrubs the
  session and journal; and
- keeps menu lifecycle and portable reading separate from rendering, input,
  native discovery/transport, patch rollback, write, search UI, freeze, and
  hardware operations.

The pause transaction, menu coordinator, launch broker, launch service,
launcher controller, game claimant, target attestation catalog, and read-only
memory service have no platform authority by themselves; process identity,
foreground, overlay, presentation, module/segment/thread enumeration, thread
ownership/control, target read, copy/transport, UI, worker, clock, and cleanup
operations are injected by future native adapters. Host tests read only
synthetic arrays. No imported operation is executed and no Vita
foreign-process memory is read in this milestone.
See
[docs/legacy-psv-compatibility.md](docs/legacy-psv-compatibility.md).

An isolated opt-in
[`experimental/hardware-gate`](docs/hardware-gate.md) now builds a disposable
3.65-only SKPRX and source-owned `VCHG00001` VPK. It validates only a
same-process, main-module-segment, 64-byte VitaSDK copy path. It is not linked
to the production library or existing self-test, is disabled from normal
builds, performs no automatic installation, and grants no foreign-title,
write, injection, hook, pause, search, or network capability.
The corrected guarded retail 3.65 run passed all 23 checks, including exact
1/63/64-byte reads from the source-owned sentinel and fail-closed range,
identity, session, pointer, replay, and timeout cases. The
[scoped hardware record](experimental/hardware-gate/hardware-result-retail-3.65.md)
and exact raw JSON establish only this same-process primitive; no foreign
process or game was read.

A second isolated opt-in
[`experimental/foreign-target-gate`](docs/foreign-target-generation-gate.md)
now host-tests and cross-builds the next disposable source-owned layer. Its
event-driven kernel registry binds exact `VCFT00001` create/start/exit/kill
events to a monotonic process generation, dual module-UID namespaces,
fingerprint, segments, and lifecycle revision. Only exact controller
`VCFC00001` can request an opaque two-second handle and one symbolic
segment-relative read of at most 64 bytes. The Vita kit produces two VPKs and
one SKPRX with build-failing import/export allowlists and no automatic
deployment. A first guarded hardware attempt stopped before input or any
foreign read when no target artifact became observable; restoration completed
byte-exact. A diagnostic rerun then reached a valid source-owned prompt-ready
record, but its immediate wrong-caller syscall still saw the target registry as
unavailable. The current correction bounds that target-only readiness race and
gives controller results an explicit current-run identity. It has not run on
hardware. Process-event ordering, two-application suspend/resume residency,
foreign copying, and unload remain unproven, and no userspace marker or
retail/production authority is claimed.

The first Vita-facing build is the deliberately unprivileged and now
hardware-tested
[`VCHT00001` self-test](vita-self-test/README.md). It provides a real on-device
five-second Select menu and runs the bounded scanner only over its own fixed
test buffer. Its recorded retail 3.65 run passed six checks with zero failures;
the [scoped hardware record](vita-self-test/hardware-result-retail-3.65.md)
contains exact artifact hashes and the retained result screenshot.

## Intended end goals

- Search and refine values directly on the Vita, including unknown initial
  values and common integer/float modes.
- Edit and freeze values with explicit user arming and reliable restoration.
- Create and manage declarative cheats bound to a title, module, and exact
  executable build identity.
- Download compatible cheats directly from a versioned online community
  database.
- Offer an optional PC companion for faster searches, filtering, pointer
  analysis, and cheat authoring over a paired local-network connection.
- Send completed cheats from the PC companion back to the Vita while retaining
  a complete on-device workflow.
- Fail closed when a title, module, build, memory region, protocol version, or
  authorization state does not match.

## Architecture direction

The selected device design is hybrid. A capability-limited kernel service owns
only target lifecycle, cooperative thread-pause operations, and bounded
cross-process memory access. A small QuickMenuReborn add-on in `SceShell`
provides the native **Open VitaCheat** launcher and status. An injected user-mode
game plugin owns menu navigation, display hooks, rendering, search state, and
user approval:

```text
SceShell Quick Menu add-on             optional PC companion
             |                                  |
             +---- bounded, typed request ABI --+
                                |
                 capability-checked kernel service
                                |
          foreground title generation + request broker
                                |
                    injected game user plugin
                                |
             pause ownership + portable core
```

Pressing the Quick Menu button creates only a short-lived request bound to the
current foreground title generation. It does not pause or write game memory
from `SceShell`. After the system overlay closes, the matching injected game
plugin claims the request, validates compatibility, and opens VitaCheat. Only
then may it request suspension of an explicit gameplay-thread allowlist; the
plugin, input, rendering, watchdog, and cleanup paths remain runnable.

The existing five-second Select state machine remains a tested self-test and
recovery component, but is no longer the planned production launcher. A generic
cross-title overlay is still unproven and will not be claimed until renderer
hooks and cleanup pass hardware gates. The portable broker, byte ABI, service boundary, add-on-side controller, and
injected-game claimant/open-authorization lifecycle, and portable
authorization-to-pause/menu-lease coordinator, trusted target snapshot, policy
matcher, module/segment catalog, exact thread ownership validator, and symbolic
range resolver, plus the portable bounded read-only memory service are
implemented. Verified native target/foreground/module/segment/thread/read
adapters, an attested kernel/user transport, a native QuickMenuReborn module,
injection, actual measured title manifests, renderer/input/menu/search UI, and
write/rollback hardware gates are not.

The online database will supply bounded declarative records, never executable
scripts. The kernel service must expose a narrow versioned ABI and pass a
separate review and hardware gate for every capability. Eventual PSP/PS1 work
inside the Vita's PSP emulator is a distinct research track and may also need
an emulator-side component; a Vita kernel service alone is not assumed to
provide complete or stable PSP memory semantics. See
[docs/architecture.md](docs/architecture.md) and
[docs/security-model.md](docs/security-model.md).

## Build and run the host tests

With a C11 compiler and Make:

```sh
make test
```

Or with CMake:

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

The CMake build can also run the complete host suite under AddressSanitizer and
UndefinedBehaviorSanitizer:

```sh
CC=clang cmake -S . -B build-sanitize \
  -DVITACHEAT_ENABLE_SANITIZERS=ON
cmake --build build-sanitize
ctest --test-dir build-sanitize --output-on-failure
```

For bounded libFuzzer coverage, add `-DVITACHEAT_BUILD_FUZZERS=ON` and run
`build-sanitize/vitacheat_search_fuzzer -runs=10000 -max_len=520` plus
`build-sanitize/vitacheat_launch_broker_fuzzer -runs=10000 -max_len=512` and
`build-sanitize/vitacheat_launch_service_fuzzer -runs=10000 -max_len=512` and
`build-sanitize/vitacheat_quick_menu_launcher_fuzzer -runs=10000 -max_len=512`
and
`build-sanitize/vitacheat_launch_claimant_fuzzer -runs=10000 -max_len=512` and
`build-sanitize/vitacheat_menu_coordinator_fuzzer -runs=10000 -max_len=512`
and
`build-sanitize/vitacheat_target_attestation_fuzzer -runs=10000 -max_len=640`
and
`build-sanitize/vitacheat_memory_service_fuzzer -runs=10000 -max_len=640`.
The opt-in hardware-gate fuzzer is
`build-sanitize/experimental/hardware-gate/vitacheat_hardware_gate_fuzzer
-runs=10000 -max_len=160` when
`-DVITACHEAT_ENABLE_HARDWARE_GATE=ON`.
The foreign-target fuzzer is
`build-sanitize/experimental/foreign-target-gate/vitacheat_foreign_target_gate_fuzzer
-runs=10000 -max_len=192` when
`-DVITACHEAT_ENABLE_FOREIGN_TARGET_GATE=ON`.
Dependency-free deterministic smoke targets are also available as
`make launch-service-fuzz-smoke` and
`make quick-menu-launcher-fuzz-smoke` and
`make launch-claimant-fuzz-smoke` and
`make menu-coordinator-fuzz-smoke` and
`make target-attestation-fuzz-smoke` and
`make memory-service-fuzz-smoke`. The isolated targets are
`make hardware-gate-test hardware-gate-fuzz-smoke hardware-gate-analyze`.
The foreign-target equivalents are
`make foreign-target-gate-test foreign-target-gate-fuzz-smoke
foreign-target-gate-analyze`.
GCC's bounded static-analyzer pass is
available as `make analyze` or with
`-DVITACHEAT_ENABLE_ANALYZER=ON -DBUILD_TESTING=OFF`.

VitaSDK is not required to build and run the host tests. Building the Vita
self-test VPK or either separately enabled hardware gate does require VitaSDK.
The exact native build, inspection, manual install/removal, recovery, and
result-capture protocols are in [docs/hardware-gate.md](docs/hardware-gate.md)
and
[docs/foreign-target-generation-gate.md](docs/foreign-target-generation-gate.md).
The diagnostic 3.65 run proved that its prior `OpenSelf` failure was an invalid
kernel-UID/process-UID equality assumption; the isolated gate now binds both
UIDs internally. The corrected run device-validated the bounded source-owned
same-process path. The separate source-owned foreign-target generation gate is
host- and cross-build validated; two guarded attempts stopped before
input/read. The second reached a valid prompt-ready diagnostic record while
the event registry was still unavailable at the target's immediate probe. A
bounded readiness retry and fresh controller-result identity now await a
separately authorized rerun. This remains neither a retail-game nor a
production reader.

## Relationship to VitaDebugger

VitaCheat is a separate project. It may reuse stable public interfaces and
lessons from [VitaDebugger](https://github.com/zm2283145/VitaDebugger), such as
module-relative addressing, build-identity checks, bounded messages, explicit
ownership, watchdog cleanup, logging, profiling, and deployment workflows. It
will not begin as a hard copy of the debugger or as a general arbitrary-kernel-
access service.

## Legacy implementation references

Static clean-room research confirmed that original VitaCheat z06 shipped both a
kernel `vitacheat.skprx` and an injected user `vitacheat.suprx`; the user module
handled input, display hooking, and menu behavior. The earlier rinCheat was a
user plugin and used hard-coded thread IDs plus scheduler starvation to appear
paused. VitaCheat instead blocked a display call while its menu was open; that
did not suspend every gameplay worker.

Those projects are behavioral references only. rinCheat is GPLv3, while the
VitaCheat binary archive has no detected source license. This repository does
not copy their implementations, assets, fonts, or binaries. See the
[architecture notes](docs/architecture.md#legacy-plugin-findings).

Quick Menu integration will target the MIT-licensed
[QuickMenuReborn](https://github.com/Ibrahim778/QuickMenuReborn) public widget
API pinned for research at
[`a3e067e630722bab6f6e245e587243390519a052`](https://github.com/Ibrahim778/QuickMenuReborn/tree/a3e067e630722bab6f6e245e587243390519a052).
[FTP for Vita](https://github.com/M-Essa11/FTP-for-Vita/tree/20080107e116a524605f023f5b72898c9b59e57b)
at `20080107e116a524605f023f5b72898c9b59e57b` demonstrates a clean
register/callback/unregister lifecycle for an add-on. GPLv3
[QuickMenuPlus](https://github.com/PsArchive/QuickMenuPlus) is used only as a
behavioral reference; VitaCheat will not copy its firmware-specific SceShell
patches.

The pinned QuickMenuReborn NID table publicly defines the needed widget,
label, event, and unregister calls, but exposes no runtime interface-version
query. Its own documentation also provides no kernel bridge. Because this
repository cannot verify both exact runtime compatibility and an attested
launch-service transport without inventing glue, it deliberately does not
produce a native `.suprx` yet. No third-party header, stub, source tree, or
artwork is vendored.

## Compatibility

No cross-process Vita compatibility is claimed yet. The ordinary user-mode
self-test has passed on one retail Vita running system software 3.65. Vita TV,
firmware 3.60, and other releases remain untested for this project and will be
listed only after their own gates pass.

## Repository layout

- `include/vitacheat/search.h` — public bounded search/refinement API.
- `include/vitacheat/legacy_psv.h` — bounded lossless legacy importer API.
- `include/vitacheat/menu_activation.h` — portable Select-hold state machine.
- `include/vitacheat/pause.h` — bounded pause ownership and cleanup contract.
- `include/vitacheat/launch_broker.h` — v1 byte ABI and launch broker contract.
- `include/vitacheat/launch_service.h` — trusted adapter callbacks, foreground
  lifecycle, service statuses, and fixed-buffer front door.
- `include/vitacheat/quick_menu_launcher.h` — allocation-free add-on lifecycle,
  UI/worker/transport adapter contract, and bounded public status model.
- `include/vitacheat/launch_claimant.h` — allocation-free injected-game
  identity/overlay/presentation claimant and one-shot open authorization.
- `include/vitacheat/menu_coordinator.h` — exact authorization handoff,
  protected-thread allowlist, pause ownership, and bounded menu lease.
- `include/vitacheat/target_attestation.h` — trusted target catalog,
  transactional adapter contract, exact policy/thread checks, lifecycle
  invalidation, and symbolic range resolution.
- `include/vitacheat/memory_read.h` — fixed little-endian read/status ABI.
- `include/vitacheat/memory_service.h` — trusted authorization, quotas,
  target-read adapter, lifecycle, and result-journal boundary.
- `src/search.c` — portable little-endian implementation.
- `src/legacy_psv.c` — syntax indexing and conservative operation mapping.
- `src/menu_activation.c` — five-second one-shot activation logic.
- `src/pause.c` — transactional suspend, rollback, resume, and expiry logic.
- `src/launch_broker.c` — little-endian codec and launch-request state machine.
- `src/launch_service.c` — caller attestation, exact copy boundary,
  serialization, lifecycle synchronization, and copy-out result journal.
- `src/quick_menu_launcher.c` — transactional resource lifecycle, one-slot
  callback handoff, exact submit/status dispatch, and snapshot revalidation.
- `src/launch_claimant.c` — trusted observation gates, discovery/claim/cancel
  worker, one-time authorization, and unload/stop cleanup.
- `src/menu_coordinator.c` — authorization-to-pause transaction, lease,
  watchdog, lifecycle revalidation, and retryable reverse cleanup.
- `src/target_attestation.c` — immutable snapshot publication, identity and
  catalog validation, policy matching, ownership checks, and range resolution.
- `src/memory_read.c` — exact v1 read/status wire validation and codecs.
- `src/memory_service.c` — menu-bound read authorization, symbolic resolution,
  bounded target reads, post-read race checks, quotas, and copy-out recovery.
- `tests/host/test_search.c` — native behavioral and boundary tests.
- `tests/host/test_legacy_psv.c` — mixed-format, truncation, and fail-closed
  importer tests.
- `tests/host/test_menu_activation.c` — hold, release, and clock-reset tests.
- `tests/host/test_pause.c` — pause ownership, rollback, retry, and expiry tests.
- `tests/host/test_launch_broker.c` — ABI vectors, authority, lifecycle, and
  bounded state-sequence tests.
- `tests/host/test_launch_service.c` — trusted-metadata, copy-fault, lifecycle,
  retry, reentrancy, and bounded service-model tests.
- `tests/host/test_quick_menu_launcher.c` — resource rollback, callback
  idempotence, snapshot races, response validation, status, and reentrancy
  tests.
- `tests/host/test_launch_claimant.c` — overlay/presentation ordering, identity
  churn, exact transport, authorization, cleanup, and bounded state tests.
- `tests/host/test_menu_coordinator.c` — allowlist, authorization, pause,
  lease, lifecycle, reentrancy, expiry, and cleanup tests.
- `tests/host/test_target_attestation.c` — hostile adapter collection,
  identity/catalog/policy/range/thread boundaries, lifecycle, and secrecy
  tests, including test-only `PCSA00133`/`1.00` placeholder facts.
- `tests/host/test_memory_read.c` — exact ABI vectors, round trips, limits, and
  malformed request/response tests.
- `tests/host/test_memory_service.c` — trusted activation, symbolic read,
  caller/race/range/quota/copy/lifecycle, journal, and state-model tests using
  explicitly synthetic `PCSA00133` memory.
- `tests/fuzz/fuzz_launch_broker.c` — bounded codec/dispatch/lifecycle fuzzer.
- `tests/fuzz/fuzz_launch_service.c` — bounded untrusted-byte, metadata, copy,
  and service-lifecycle fuzzer.
- `tests/fuzz/fuzz_quick_menu_launcher.c` — bounded launcher lifecycle,
  foreground, response, and adapter-failure fuzzer.
- `tests/fuzz/fuzz_launch_claimant.c` — bounded claimant lifecycle,
  observation, transport, authorization, and time fuzzer.
- `tests/fuzz/fuzz_menu_coordinator.c` — bounded coordinator allowlist,
  authorization, callback, lease, lifecycle, and cleanup fuzzer.
- `tests/fuzz/fuzz_target_attestation.c` — bounded hostile catalog,
  transaction, lifecycle, and range-query fuzzer.
- `tests/fuzz/fuzz_memory_service.c` — bounded wire, identity, snapshot,
  callback, quota, time, journal, and lifecycle fuzzer.
- `experimental/hardware-gate/` — opt-in portable gate model, host
  tests/fuzzer, VitaSDK SKPRX adapter, generated syscall-stub build, and
  disposable `VCHG00001` validation client.
- `experimental/foreign-target-gate/` — separate opt-in lifecycle-generation
  model, host tests/fuzzer, strict VitaSDK SKPRX, and source-owned
  `VCFT00001`/`VCFC00001` fixture pair.
- `vita-self-test/` — ordinary user-mode on-device menu and owned-buffer probe.
- `docs/architecture.md` — component boundaries and data flow.
- `docs/hardware-gate.md` — experimental ABI, SDK evidence, build, and manual
  hardware protocol.
- `docs/foreign-target-generation-gate.md` — foreign lifecycle API audit,
  opaque-handle ABI, build inventories, and guarded manual protocol.
- `docs/legacy-psv-compatibility.md` — compatibility guarantees and limits.
- `docs/security-model.md` — authority, transport, database, and cleanup rules.
- `ROADMAP.md` — staged work from the host core to hardware validation.

## Project status

This is experimental work, not a ready-to-install replacement for an existing
Vita cheat plugin. Current and planned capabilities are kept separate in this
README and the roadmap so a future feature is not mistaken for a tested one.
