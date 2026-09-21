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

- a legacy `.psv` syntax indexer that holds views into caller-owned source and
  conservatively maps only independently documented operations; and
- a clock-driven Select-hold state machine that emits one event after five
  continuous seconds and rearms only after release;
- a bounded pause-ownership transaction that operates only through adapter
  callbacks, rolls back partial suspension in reverse order, binds cleanup to
  one process generation, and retains failed resumes for retry.

None of these helpers reads controller hardware, enumerates or suspends threads,
opens files, renders UI, or writes memory. Those responsibilities remain in
adapters with separately testable authority.

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

## Future injected in-game menu

The injected user plugin owns controller polling, display hooks, menu rendering,
search state, configuration, and on-device approval. The five-second Select
hold remains the menu trigger. Before the menu becomes interactive, it requests
that the kernel service construct an explicit allowlist of gameplay threads for
the current process generation and suspend them through the portable pause
transaction. The injected plugin, input hook, display hook, menu renderer,
watchdog, and cleanup execution paths must never be included in that list.

Menu close, timeout, title exit, plugin stop, and recoverable errors all request
reverse-order resume. A failed resume remains owned and retryable; a process
generation change prevents operations on recycled thread IDs. Ownership may be
abandoned only after the adapter independently proves that the old process
generation no longer exists.

A cross-title overlay is not assumed to be universal. Games can use different
display paths and timing behavior, so each supported user-mode hook path needs a
fail-closed compatibility gate. Until those gates exist, the repository must
not claim that the menu works in every game.

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
