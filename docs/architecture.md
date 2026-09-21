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

The portable core also owns two authority-free helpers:

- a legacy `.psv` syntax indexer that holds views into caller-owned source,
  plus a physical-record-safe compiler for documented scalar and pointer
  families;
- a callback-driven evaluator that stages concrete little-endian actions,
  followed by a separate bounded writer and reverse patch-ledger rollback;
- a clock-driven Select-hold state machine that emits one event after five
  continuous seconds and rearms only after release;
- a bounded pause-ownership transaction that operates only through adapter
  callbacks, rolls back partial suspension in reverse order, binds cleanup to
  one process generation, and retains failed resumes for retry.

Pointer plans store immutable starting-base expressions and an explicit bounded
sequence of U32 read dependencies. A context-sensitive cursor owns complete
multi-record spans before opcode dispatch, so terminal markers, pointer halves,
and repeat-count records cannot become standalone operations. Repeats stay
symbolic until bounded action materialization.

None of these helpers reads controller hardware, enumerates or suspends threads,
opens files, renders UI, or owns process memory. The evaluator can invoke only
callbacks supplied by an adapter; no Vita or real-process adapter exists in the
portable layer.

The portable target contract is C11 plus an integer pointer type (`uintptr_t`)
wide enough to represent object ranges. That holds for the supported Windows
host and ARM Vita targets and lets the parser reject overlapping source and
output buffers without relying on undefined relational pointer comparisons.

## Future kernel service

The selected privileged component is a narrow kernel service. It exposes
explicit capabilities to the injected user plugin rather than one general
memory service:

1. discover a title and verified modules;
2. list bounded, allowlisted user-memory regions;
3. copy a readable region into a snapshot;
4. enumerate and classify target threads for cooperative menu pause;
5. arm a short-lived write capability after on-device approval;
6. apply typed writes or freezes from declarative records;
7. restore state and release target ownership on every exit path.

The current adapter remains a buffer-only Vita self-test. Cross-process access,
kernel hooks, user-plugin injection, and overlay rendering are later milestones
with their own threat models and hardware evidence.

## Future Quick Menu launcher and injected in-game menu

A small user module loaded by QuickMenuReborn in `SceShell` registers a native
**Open VitaCheat** button and a bounded status label. Its callback may only ask
the kernel service to create one pending launch request for the current
foreground title generation. The request has a unique ID, short expiry, and no
read, pause, write, or freeze capability. Duplicate requests are idempotent;
foreground-title changes and expiry discard them.

The QuickMenuReborn public widget API is preferred over direct SceShell offset
patching. The add-on uses weak imports, registers all widgets and textures
during start, and unregisters each resource before stop. If the pinned
QuickMenuReborn interface is unavailable, the launcher reports unavailable and
does not attempt an undocumented fallback.

The injected game plugin owns display hooks, menu rendering/navigation, search
state, configuration, and on-device approval. It can claim a pending request
only when its caller process and generation match. It waits until the system UI
overlay has closed and a supported presentation path has resumed before
opening the menu. The existing five-second Select helper remains available for
the standalone self-test and recovery experiments, not as the planned
production launcher.

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
not claim that the menu works in every game. QuickMenuReborn is an additional
system-wide dependency with its own firmware and coexistence matrix; absence or
failure leaves VitaCheat inactive rather than falling back to private ShellUI
hooks.

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

The service must keep raw kernel operations behind a small versioned request
ABI, derive and validate the user-plugin caller and target ownership, expose no
arbitrary kernel-memory primitive, clean up after title exits and unload, and
pass independent review plus a disposable hardware gate. Parsing, search
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
