# Architecture

VitaCheat is divided by authority. Searching bytes is a different capability
from reading a process, and reading is different from writing or freezing.
Keeping those operations separate makes each layer easier to test and limits
what a parsing or UI error can do.

## `vitacheat_core`

The search component is a deterministic, allocation-free C library. It accepts
immutable byte snapshots and caller-owned result buffers. It knows nothing
about processes, absolute addresses, files, sockets, VitaSDK, or the kernel.

Offsets are 32-bit and relative to a region selected by a future adapter. The
adapter, not the search engine, will map a result to a verified process, module,
build identity, region, and ASLR placement.

The portable core also owns authority-free helpers:

- a legacy `.psv` syntax indexer that holds views into caller-owned source and
  conservatively maps only independently documented operations; and
- a clock-driven Select-hold state machine that emits one event after five
  continuous seconds and rearms only after release;
- a bounded pause-ownership transaction that operates only through adapter
  callbacks, rolls back partial suspension in reverse order, binds cleanup to
  one process generation, and retains failed resumes for retry; and
- a bounded Quick Menu launch broker with a fixed little-endian byte ABI,
  explicit caller roles/capabilities, one pending request, and retained terminal
  status; and
- a launch-only service front door that owns trusted caller attestation,
  sequenced foreground snapshots, exact user-copy boundaries, lifecycle
  dispatch, nonblocking serialization, and copy-out recovery; and
- a launch-only Quick Menu controller that owns one bounded callback-to-worker
  handoff, exact foreground revalidation, submit/status transport, bounded
  public status, and transactional UI-resource cleanup; and
- an injected-game claimant/controller that owns coherent trusted identity,
  overlay, and presentation observations, exact status/claim/cancel transport,
  and a one-time local menu-open authorization; and
- a menu/pause coordinator that validates an explicit protected-thread
  allowlist, consumes one exact claimant authorization, owns the pause
  transaction, and issues one bounded local menu lease; and
- a trusted-target attestation catalog that transactionally copies bounded
  process, build, module, segment, and thread facts from an injected adapter,
  publishes immutable revisions, and answers exact policy, ownership, and
  symbolic range queries; and
- a separate read-only memory service that accepts only symbolic
  module/segment/offset requests from the exactly attested game plugin while an
  internally trusted, acknowledged menu/pause lineage remains active.

None of these helpers reads controller hardware, enumerates or suspends threads,
opens files, renders UI, or writes memory. The memory service can call only an
injected bounded target-read adapter; no native implementation is present.

### Portable Quick Menu broker and v1 ABI

`include/vitacheat/launch_broker.h` separates native C commands from a fixed
byte boundary. Requests are exactly 64 bytes and responses are exactly 72 bytes;
the codecs read and write each little-endian integer explicitly and reject
unsupported versions, truncated or oversized envelopes, mismatched size
fields, unknown enum/capability values, nonzero reserved fields, unused
operation fields, invalid identities, and timestamp overflow before dispatch.

The broker owns one request because only the current foreground title can be
launched. It retains that request's terminal state so claimed, expired,
cancelled, stale, and clock-rollback results cannot become pending again. A
caller-supplied nonzero counter seed produces strictly increasing IDs; the
counter never wraps, and exhaustion is explicit. Duplicate submissions for the
same active process generation return the original ID and deadline.

The native lifecycle surface covers foreground-generation changes, process
exit, plugin unload, service reset/unload, and monotonic expiry ticks. The
request surface covers submit, claim, cancel, and status. `SceShell` can submit
with only `LAUNCH` and query with only `STATUS`; it cannot claim or cancel. The
game plugin can claim only with `CLAIM`, after explicitly reporting
presentation readiness, and can cancel only with `CANCEL`. Every request ID,
process ID, and nonzero generation must match.

Game-role `STATUS` has one deliberately narrow discovery form: request ID zero
may return the pending request ID only when the target PID/generation matches.
It returns absent for a nonmatching or terminal record. This does not change
the byte ABI version or size, and `SceShell` still must provide the request ID
it received from submission. Because a wire role is not authentication,
discovery is safe only through the service front door: caller attestation,
exact game PID/generation binding, and the matching current foreground
snapshot all run before broker dispatch.

This is serialization, validation, and pure state only. It does not authenticate
a real Vita caller or provide a syscall, hook, widget, injection path,
cross-process operation, thread control, firmware offset, or hardware access.
Those are adapter milestones.

### Portable launch service boundary

`include/vitacheat/launch_service.h` defines the precise privileged-adapter
contract without importing VitaSDK. The front door first copies exactly 64
untrusted bytes into a zeroed local buffer. It then asks injected callbacks to
attest the caller from process and loader identity and to return an atomic,
sequenced foreground snapshot. The callback-produced role, caller PID and
generation, module-load generation, foreground PID and generation, bounded
title identity, and trusted monotonic time are never taken from caller memory.
Wire fields must agree with them before broker dispatch.

The service rejects unknown callers before broker mutation. `SceShell` can
submit or query only the launch-only v1 capability set. A game plugin can
claim, cancel, or query only for its own exact PID and nonzero generation, while
that same target is foreground. Snapshot sequence rollback or inconsistent
reuse fails closed. PID-generation reuse, title changes, no-foreground
transitions, process exit, plugin unload, reset, and stop route through broker
lifecycle operations.

All public calls share one nonblocking C11 atomic transaction gate. Identity,
foreground, copy, and cleanup callbacks execute without an internal mutex and
must not reenter; reentry and concurrent calls return `BUSY`, and platform
lifecycle events must be retried before acknowledgement. No allocation or
unbounded wait occurs.

Responses are encoded into a zeroed fixed local buffer and exactly 72 bytes are
copied out. If copy-out fails after dispatch, the service retains one exact
response journal keyed to the copied request, attested caller/module instance,
and target identity. Only an exact retry can retrieve it; unrelated dispatch
and tick calls fail with `RESULT_PENDING`. A target change, process exit,
plugin unload, reset, or stop revokes the journal. This keeps submit, claim,
cancel, and status idempotent or explicitly consumed after a partial copy,
without reporting a success-shaped fallback.

The adapter callbacks are the current native boundary. This repository has no
documented VitaSDK/taiHEN kernel syscall/export scaffold from which a real
`.skprx` transport can be compiled without guessing APIs, so this layer does
not add native module glue. The existing ordinary user-mode self-test remains
unchanged.

### Portable Quick Menu launcher controller

`include/vitacheat/quick_menu_launcher.h` models only the unprivileged
`SceShell` add-on role. Initialization injects an exact UI API-version probe,
texture/label/widget/callback/worker registration and cleanup, status update,
worker signal, trusted foreground snapshot, monotonic clock, and launch-service
transport. A text-only adapter may omit the paired texture callbacks. Every
other callback is mandatory.

Start probes the exact adapter API before registering anything, then records
each nonzero resource token. Failure rolls back successful registrations in
reverse order. Stop disables and generation-invalidates callbacks, scrubs all
local request/status authority, attempts every cleanup without an unbounded
wait, remains stopped on failure, and retains only failed tokens for a later
retry. Destroy clears the controller only after cleanup succeeds.

The button callback reads one trusted foreground snapshot, claims one fixed
queue slot, signals one already-created worker, revalidates the snapshot after
that handoff, and returns. It never invokes the service transport. Repeated
presses while queued, in flight, pending, or in a terminal/error state are
no-ops until an explicit local reset.

One worker step performs at most one transport call. It revalidates the exact
nonzero snapshot sequence, PID, process generation, and normalized bounded
title identity before request construction, after clock acquisition, and after
transport. Any change discards the response and local action. Requests are
built only through `vc_launch_request_init` and
`vc_launch_request_encode`: the controller can emit only `SUBMIT` with
`LAUNCH` or `STATUS` with `STATUS`, both as `SceShell`. A retry retains the
exact 64 encoded bytes for the service copy-out journal; it never rebuilds the
request or extends its TTL. A service `BUSY` result is distinct: because no
mutation occurred, the next bounded step discards the old timestamp and rebuilds
only after a fresh trusted clock/snapshot check.

Responses must be exactly 72 bytes, decode under the existing v1 codec, match
the operation, capability, request/target identities and original trusted
timestamp, and carry a state-consistent status. Only then may local state move
to pending claim, consumed, expired, or stale. The status formatter writes
caller-owned bounded storage and never reveals PID, generation, title,
request ID, nonce, module, kernel, or memory information. “Pending” explicitly
means waiting for the matching game plugin to claim after Quick Menu closure;
it never claims that VitaCheat is already open.

The controller uses a nonblocking atomic transaction gate only while accessing
its own state. Before invoking any adapter it marks the adapter boundary and
releases that gate; reentrant public mutation is rejected as `BUSY`.
Registration, foreground, time, transport, status, signal, and cleanup
callbacks therefore never run under the gate. Adapters must not invoke the
supplied button or worker callback inline, and their callback-path foreground
and signal operations must be bounded and nonblocking.

### Portable injected-game claimant controller

`include/vitacheat/launch_claimant.h` models the game-plugin role without
injecting or hooking a title. Its coherent trusted-observation adapter supplies
the authoritative game-plugin PID, nonzero process and module-load
generations, bounded title identity, matching foreground snapshot, sequenced
overlay state, and a presentation compatibility result for that exact module
instance. A separate trusted monotonic clock and fixed-buffer service
transport complete the adapter boundary.

Start and reset generation-invalidate all prior work. Identity binding accepts
only game-role metadata whose self PID/generation/title byte-match the present
foreground target. Any identity-sequence change, PID reuse, process-generation
change, module reload, title mismatch, foreground loss, foreground sequence
rollback, or incoherent same-sequence reuse revokes local authority. The
controller snapshots and revalidates the complete observation before and after
every transport boundary.

Overlay closure is not inferred from time. `UNKNOWN`, `OPEN`, and `CLOSING`
all block transport, and the first `CLOSED` observation only starts a stable
boundary. A second strictly newer `CLOSED` observation in the same overlay
generation is required. Reopen or generation change resets the gate. The
presentation adapter must report `READY` for the exact bound process and module
generation. Both gates must remain byte-identical across claim transport;
loss or a newer observation revokes an unconsumed authorization and closes
local open state. The claimant never tries to close the system Quick Menu and
does not hook a display API.

The short callback-facing notification APIs only normalize and store trusted
overlay/presentation observations. They never call transport. One worker step
performs at most one exact game-role `STATUS`, `CLAIM`, or `CANCEL`; there is
no generic operation passthrough. Discovery uses the attested zero-ID status
form, then a later step claims the returned pending ID only while both gates
hold. Service `BUSY` rebuilds with fresh trusted time; result-journal `RETRY`
preserves the exact 64 request bytes. Exact response size, codec validity,
operation, capability, target, request ID, timestamp, and state/status
consistency are checked before any local authority changes.

A successful claim does not render or pause. It creates one controller-local
opaque token bound internally to the service request, complete trusted
observation, stable overlay sequence, presentation sequence, lifecycle
generation, and a short nonextending monotonic deadline. The future menu owner
must consume it once and separately acknowledge open. Replay, duplicate claim
status, stale completion, deadline boundary, rollback, reset, stop, unload, or
observation change cannot mint or reopen authorization. Public status and text
are bounded and omit every request, PID, title, process/module generation, and
kernel detail.

Stop and unload revoke callback work and scrub local request/token state before
returning, even when cleanup fails. A known unclaimed request receives at most
one safe game-role cancel attempt; an outstanding service-journal request is
retrieved only by its exact bytes and never converted into local authority
during stop. Plugin unload additionally invokes an idempotent adapter that must
map the exact saved module identity to the launch-service plugin-unload
lifecycle. Cleanup failure is explicit while the controller remains stopped.

### Portable menu/pause coordinator

`include/vitacheat/menu_coordinator.h` composes the claimant and pause
transactions without duplicating either state machine. The caller binds one
normalized game-plugin PID, process generation, module-load generation, title,
and foreground identity, then supplies a copied, strictly increasing allowlist
of at most `VC_PAUSE_MAX_THREADS` positive gameplay-thread IDs. Six explicit
protected roles—plugin control, input hook, renderer/present hook, watchdog,
cleanup, and the current calling thread—are mandatory and may not occur
anywhere in that allowlist.

Before consuming authority, the coordinator inspects the claimant's still-
unconsumed `OPEN_AUTHORIZED` lineage, byte-matches its complete identity,
overlay, and presentation snapshot, checks the caller's nonzero allowlist
revision, and asks one trusted adapter to verify ownership of the exact copied
thread set. It revalidates the same observation after every callback boundary.
Only then does it consume the claimant token immediately before
`vc_pause_begin_checked` acquisition. Deterministic suspension order and
reverse rollback remain owned by the pause primitive. After consumption, a
coherent claimant query also byte-matches the exact authorization lineage and
expected consumed/open state after each external callback and before either
lease is minted.

Successful suspension yields a controller-local provisional lease. A separate
owner acknowledgement revalidates the exact target and claimant token, rotates
the provisional value so it cannot be replayed, and only then marks the menu
open. The final lease is bound internally to target and module identity,
claimant lineage, exact copied allowlist and revision, pause generation,
coordinator lifecycle, and fixed monotonic acknowledgement/menu deadlines.
Neither acknowledgement nor tick extends either deadline.

Close, expiry, clock rollback, foreground/target change, overlay reopen,
presentation loss, claimant reset, target exit, plugin unload, service stop,
and coordinator stop revoke the local lease before claimant cleanup and
reverse resume. Resume callbacks are bracketed by trusted ownership checks for
only the threads still owned by that pause transaction; already-resumed or
never-suspended IDs are never revalidated as owned. Failures retain only
still-owned suspensions for explicit bounded retry; new opens remain blocked.
An exact target-exit event may abandon ownership without calling recycled
thread IDs. Stop stays stopped even while cleanup is pending. Public state and
formatter output reveal no PID, title, generation, thread, authorization,
lease, kernel, or memory identity.

This component has no thread enumeration policy or native authority. A future
adapter must provide the exact title-specific allowlist, protected IDs,
coherent identity/readiness observations, ownership verification, and
attested suspend/resume transport. It does not draw a menu, read input, access
memory, restore patches, or execute search/write/freeze operations.

### Portable target attestation and range foundation

`include/vitacheat/target_attestation.h` defines the read-only trust boundary
needed before a pause adapter or the memory service can use a target.
Collection is begin/enumerate/end through injected callbacks. A nonzero
mutation token must survive every module and thread read and the completion
callback; mutation retries are capped at three. Adapter calls run outside the
nonblocking serialization gate, reentry fails `BUSY`, and no partial or stale
completion can publish. The complete candidate is copied into fixed owned
storage and validated only after collection ends.

Each immutable snapshot has a nonzero local revision and lifecycle generation,
exact PID/process generation and foreground sequence, canonical bounded title
ID, optional bounded version label, and an opaque build fingerprint algorithm
and bytes supplied by the adapter. The portable layer implements no hash.
Modules use a stable numeric module ID (including zero) plus nonzero load
generation. Their bounded segments have unique indices, nonzero sizes, checked
32-bit ends, and
explicit read/write/execute permissions; overlap and unknown flags/bits are
rejected. Thread descriptors must prove each positive unique ID belongs to the
same exact PID/process generation. Opaque role flags are retained but never
used to infer gameplay ownership.

Policy matching exact-compares title and any supplied version, fingerprint, or
module-generation constraints. `$B200`-compatible lookup is bounded by module
serial and segment index. Range resolution checks revision, module generation,
32-bit addition, one-segment containment, and requested permissions, then
returns only module/segment/offset/length metadata. It never dereferences an
address. Gameplay-thread validation accepts only a caller-supplied sorted
unique allowlist and a separate protected set; names, priority, enumeration
order, and role flags cannot select threads.

The menu coordinator has an optional callback for the exact immutable target
revision. It runs before claimant authorization is consumed and at checked
suspend boundaries. If absent, existing coordinator behavior is unchanged; if
present, a zero/stale revision or ownership mismatch fails before native pause
operations. Foreground change/loss, process exit, module unload/reload, plugin
unload, reset, stop, and sequence rollback invalidate derived use. Stop scrubs
identity, fingerprint, module, address, and thread data before optional cleanup
and remains stopped on cleanup failure.

### Portable read-only memory service

`include/vitacheat/memory_read.h` is intentionally separate from launch-only
v1. Its v1 request is exactly 80 bytes. Responses have an exact 64-byte header
and at most 256 payload bytes. Every integer is encoded explicitly in little
endian; no packed structure, host pointer, absolute address, write payload, or
generic operation crosses the boundary. The only operations are `READ` and
`STATUS`, the only role is the game plugin, and the exact capability is
`READ_TARGET`.

`include/vitacheat/memory_service.h` owns session authority. The untrusted game
request can present a session ID but cannot create, extend, or reset one.
Trusted activation first inspects the coordinator's acknowledged `OPEN`
lineage, then exact-matches the caller, foreground sequence, PID/process
generation, plugin module generation, attestation revision/lifecycle, and
fixed menu deadline. A new service-local session ID never repeats within the
service lifetime. Every session has finite byte and operation budgets and a
deadline capped at 60 seconds; reads and status do not extend or reset any of
them. Every fully authenticated request consumes one operation, including a
request later denied by symbolic range or permission checks; bytes are charged
only immediately before invoking the one read callback.

Each read copies exactly one 80-byte request to owned storage, authenticates
the caller, revalidates the active lineage and immutable target, and asks
`target_attestation` to resolve exactly one readable
module/load-generation/segment/offset range. Only then is a checked 32-bit
absolute target address derived internally for one bounded adapter callback.
The callback must report exact bytes read and its observed target generation
and revision. A second trusted lineage, foreground, and attestation check after
the callback rejects short reads, process exit, target mutation, or stale
completion and scrubs all payload bytes.

All callbacks execute outside the service's nonblocking transaction gate under
a reentry exclusion. A successful or otherwise committed read/status response
is encoded into initialized fixed storage and journaled before copy-out. An
exact retry returns stable bytes without repeating the read or charging quota
again; a different request is blocked. Close, expiry, rollback, target change,
overlay reopen, presentation loss, process exit, plugin unload, reset, and stop
scrub both session and journal immediately. Public status text is coarse and
contains no address, token, identity, module, revision, or payload data.

The production service remains host-first and has no native adapter. A separate
opt-in experiment in `experimental/hardware-gate/` now compile/link-validates a
five-call SKPRX ABI and a source-owned `VCHG00001` client on the installed
VitaSDK. It is not linked to this service and permits only a same-process,
main-module-segment read of at most 64 bytes. It establishes neither production
attestation nor foreign-process access. See `docs/hardware-gate.md`.
The first 3.65 run proved module load and status but failed closed during
main-module discovery before any read. Its append-only status v2 reports only
the failed stage and raw API result; it never reports PID, module ID, address,
segment base, fingerprint, or payload bytes. The diagnostic retest proved that
module-info succeeded and the failure was a local equality check between the
main module's kernel UID and the process-visible UID returned in
`SceKernelModuleInfo.modid`. The gate now keeps both as an unexposed dual
binding, uses the kernel UID for module-info/fingerprint calls, and invalidates
the session if either changes. The corrected retail 3.65 run passed all 23
bounded same-process checks, including exact 1/63/64-byte sentinel reads. This
is device evidence only for the source-owned caller process and does not add
foreign-target or production authority.

The separate `experimental/foreign-target-gate/` keeps that proven ABI
unchanged and adds an isolated event-generation model. A fixed
`SceProcEventForDriver` registry accepts only exact source-owned `VCFT00001`
events. The unique create callback binds one kernel monotonic generation and
the exact dual-module-UID/fingerprint/segment snapshot; repeated same-PID
starts are idempotent exact revalidation. Exit or kill invalidates the record
and opaque controller handle. Exact `VCFC00001` caller identity is
kernel-derived. Its public read request contains only an opaque handle plus
segment index/offset and a length capped at 64; PID, generation, revision,
module identity, fingerprint, and addresses never cross the ABI. Lifecycle
callbacks that overlap an unlocked adapter operation atomically trip a
permanent fail-closed latch, and a raced response is scrubbed before the
blocked caller returns.

The first Vita attempt stopped before input/read at an early target-start
observability gap. A diagnostic rerun reached prompt-ready while the old
start-bound registry was unavailable. Offline retail-3.65 analysis proved one
create followed by at least 19 synchronous starts before user-thread
activation, so the corrected model authorizes at create and revalidates on
every start. The source-owned target's 80-byte diagnostic record and bounded
two-second wrong-caller probe cannot create lifecycle authority. Controller
status v2 records non-sensitive counter deltas needed to falsify callback
reachability/classification. Unregister quiescence, two-homebrew
suspend/resume residency, and foreign-target copying remain explicit hardware
questions.
A pre-registration target receives no generation and cannot be opened; an
out-of-order later start fails closed. Runtime unload is not a recovery path.
See `docs/foreign-target-generation-gate.md`.

The portable scanner contract is C11 plus an integer pointer type (`uintptr_t`)
wide enough to represent object ranges. That holds for the supported Windows
host and ARM Vita targets and lets the parser reject overlapping source and
output buffers without relying on undefined relational pointer comparisons.

## Future privileged capabilities

The launch service remains launch-only. The read service is a separate
portable capability with no native adapter. A later privileged component may
expose these separately versioned capabilities to the injected user plugin
rather than one general memory service:

1. acquire native facts for the existing verified title/module catalog;
2. list native bounded, allowlisted user-memory regions;
3. implement the reviewed native adapter for the existing bounded read ABI;
4. enumerate explicit target-thread ownership for cooperative menu pause;
5. arm a short-lived write capability after on-device approval;
6. apply typed writes or freezes from declarative records;
7. restore state and release target ownership on every exit path.

None of these capabilities is represented by the production v1 operation or
capability mask; unknown operations and bits fail closed. The ordinary Vita
self-test remains buffer-only. The same-process and foreign-target gates use
separate disposable namespaces. The former has a passing 3.65 record; the
latter is only host/cross-build validated and exists to test event-derived
foreign identity against two source-owned VPKs. Retail-title access, kernel
hooks, user-plugin injection, and overlay rendering remain later milestones
with their own threat models and evidence.

## Future native Quick Menu launcher and injected in-game menu

A small user module loaded by QuickMenuReborn in `SceShell` registers a native
**Open VitaCheat** button and a bounded status label. Its callback may only ask
the kernel service to create one pending launch request for the current
foreground title generation. The request has a unique ID, short expiry, and no
read, pause, write, or freeze capability. Duplicate requests are idempotent;
foreground-title changes and expiry discard them.

The portable broker, v1 ABI, caller-attesting service policy, add-on-side
controller, injected-game claimant/open-authorization state machine, and
authorization-to-pause/menu-lease coordinator implementing these rules are
present. The QuickMenuReborn widget adapter, SceShell transport, Vita
caller/foreground/overlay adapter, verified thread-allowlist source, kernel
export, native game-plugin injection, display/input hooks, and menu renderer
described below are not.

The QuickMenuReborn public widget API is preferred over direct SceShell offset
patching. Clean-room research pins the MIT API to
`Ibrahim778/QuickMenuReborn@a3e067e630722bab6f6e245e587243390519a052`;
its public NID table defines the required widget, label, event, and unregister
symbols, and
`M-Essa11/FTP-for-Vita@20080107e116a524605f023f5b72898c9b59e57b`
confirms their VitaSDK weak-stub lifecycle. QuickMenuReborn exposes no public
runtime version query, however, and its documentation explicitly has no kernel
bridge. Until exact runtime compatibility and an attested transport can be
verified end to end, this repository does not vendor those declarations or
generate a native `.suprx`. It does not fall back to direct firmware-specific
SceShell patches.

The future native injected game plugin owns display hooks, menu
rendering/navigation, search state, configuration, and on-device approval. Its
portable claimant can claim a pending request only when its caller process,
process generation, module instance, title, foreground snapshot, stable overlay
closure, and presentation probe match. The existing five-second Select helper
remains available for the standalone self-test and recovery experiments, not
as the planned production launcher.

Before the menu becomes interactive, the game plugin requests that the kernel
service construct an explicit allowlist of gameplay threads for the current
process generation and suspend them through the portable pause transaction.
The injected plugin, input path, display hook, menu renderer, watchdog, and
cleanup execution paths must never be included in that list.

Menu close, timeout, title exit, plugin stop, and recoverable errors all request
reverse-order resume. A failed resume remains owned and retryable; a process
generation change prevents operations on recycled thread IDs. Ownership may be
abandoned only after the adapter independently proves that the old process
generation no longer exists.

A cross-title overlay is not assumed to be universal. Games can use different
display paths and timing behavior, so each supported user-mode hook path needs a
fail-closed compatibility gate. Until those gates exist, the repository must
not claim that the menu works in every game.

Public taiHEN APIs document module start/stop and user import/export hooking,
but the pinned public interfaces do not provide one authoritative,
end-to-end foreground-title plus Quick Menu overlay-state plus universal
presentation-readiness source. The launch service also still lacks a pinned
native syscall/export bridge. Therefore this layer deliberately adds no native
compilation unit: its exact adapter contract is the blocker boundary, not a
fake transport or guessed firmware-offset implementation.

The full cheat browser does not run inside `SceShell`. Keeping the shell add-on
to one button and status surface limits shell-wide failure impact and avoids
giving the system process direct game-memory authority.

## Future PC companion

The PC companion will accelerate scans, filtering, pointer work, and authoring.
Its protocol will be bounded and versioned. Pairing is initiated and confirmed
on the Vita, and remote writes require a second short-lived capability.

## Future online database

Database entries will be declarative typed operations tied to exact title and
module build identities. Catalog data will never be treated as executable
code. Downloads, cache updates, and rollbacks will be bounded and atomic.

## Kernel service authority boundary

The portable launch service keeps its broker behind the versioned v1 request
ABI, validates adapter-attested caller and target ownership, exposes no memory
primitive, and cleans up on title exit, unload, reset, and stop. A future Vita
adapter must implement the documented exact-copy, strong process/module
attestation, foreground snapshot, and cleanup callbacks with public,
compile-verified APIs before a native target is added. Parsing, search
semantics, menu state, rendering, network framing, and database logic stay in
portable or user-mode components.

## Legacy plugin findings

Static inspection used behavior and interfaces as research inputs, not source
for this implementation:

- Original VitaCheat z06 shipped both `vitacheat.skprx` and
  `vitacheat.suprx`. Its kernel module injected the user module; the user module
  polled controller input, hooked `sceDisplaySetFrameBuf`, and rendered the
  menu. The archive is available at commit
  [`bb8158a`](https://github.com/r0ah/vitacheat/blob/bb8158a1c696914a8ea2299889d42ab9a57a3ab2/plugin/v365-z06beta/FINALCHEAT-PSVITACHEAT-V365-Z06BETA.zip).
- VitaCheat held a mutex used by its display hook while the menu was open. That
  blocked the presenting thread but did not prove that gameplay, audio, or
  worker threads were paused.
- rinCheat was a user plugin. It tried to pause one assumed main thread using
  hard-coded thread IDs, priority changes, and busy-spin threads; see
  [`threads.c`](https://github.com/r0ah/rinCheat/blob/6f40cf688586ab0f77d8117db9dd907ea4234a48/main_module/threads.c#L71-L110).
  That scheduler-starvation approach is intentionally not reused.
- rinCheat is GPLv3. The VitaCheat archive has no detected license, so its
  binaries, font, assets, and implementation remain clean-room references only.
- QuickMenuReborn and FTP for Vita are MIT-licensed references for public widget
  registration, callbacks, worker separation, and symmetric cleanup.
  QuickMenuPlus is GPLv3 and uses firmware-specific SceShell offsets; it is a
  behavioral compatibility reference only, not an implementation source.

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

The native hardware gate uses VitaDebugger commit
`c01a38690897e71b673429e7f872aad7dbd31411` only as behavioral evidence for
boot-loaded SKPRX, generated-stub, caller-PID, bounded-copy, and cleanup
patterns. The unlicensed implementation is not copied. kuBridge commit
`417ddde9a744eba98d769382c1c1372b8e119139` is likewise evidence only: none of
its unrestricted memcpy, allocation, protection, cache, exception, or RWX
interfaces is linked or wrapped. Original z06 commit
`bb8158a1c696914a8ea2299889d42ab9a57a3ab2` demonstrates the unsafe
36-syscall/direct-write design that this gate rejects.
