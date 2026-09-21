# VitaCheat

VitaCheat is an early research and development project for a modern community
cheat-authoring tool on user-owned PlayStation Vita systems. The intended end
result combines an on-device workflow with an optional PC companion and an
online community database.

This repository is an early scaffold. The current code is a portable,
allocation-free memory snapshot search/refinement core, a lossless legacy
VitaCheat `.psv` importer, and a deterministic menu-activation state machine,
plus a transactional gameplay-thread pause coordinator, all with host tests. A
separate ordinary user-mode Vita self-test now exercises the scanner and
five-second Select menu against memory owned by that test app. It is not a
plugin and does not attach to a process, suspend another application's threads,
read or write another application's memory, freeze values, connect over a
network, or download cheats.

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
  deadline or monotonic-clock rollback.

The pause coordinator has no thread authority by itself; no imported operation
is executed in this milestone. See
[docs/legacy-psv-compatibility.md](docs/legacy-psv-compatibility.md).

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
hooks and cleanup pass hardware gates.

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

For bounded libFuzzer coverage of arbitrary search inputs and refinement
sequences, add `-DVITACHEAT_BUILD_FUZZERS=ON` and run
`build-sanitize/vitacheat_search_fuzzer -runs=10000 -max_len=520`.

VitaSDK is not required to build and run the host tests. Building the Vita
self-test VPK does require VitaSDK.

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
API. [FTP for Vita](https://github.com/M-Essa11/FTP-for-Vita) demonstrates a
clean register/callback/unregister lifecycle for an add-on. GPLv3
[QuickMenuPlus](https://github.com/PsArchive/QuickMenuPlus) is used only as a
behavioral reference; VitaCheat will not copy its firmware-specific SceShell
patches.

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
- `src/search.c` — portable little-endian implementation.
- `src/legacy_psv.c` — syntax indexing and conservative operation mapping.
- `src/menu_activation.c` — five-second one-shot activation logic.
- `src/pause.c` — transactional suspend, rollback, resume, and expiry logic.
- `tests/host/test_search.c` — native behavioral and boundary tests.
- `tests/host/test_legacy_psv.c` — mixed-format, truncation, and fail-closed
  importer tests.
- `tests/host/test_menu_activation.c` — hold, release, and clock-reset tests.
- `tests/host/test_pause.c` — pause ownership, rollback, retry, and expiry tests.
- `vita-self-test/` — ordinary user-mode on-device menu and owned-buffer probe.
- `docs/architecture.md` — component boundaries and data flow.
- `docs/legacy-psv-compatibility.md` — compatibility guarantees and limits.
- `docs/security-model.md` — authority, transport, database, and cleanup rules.
- `ROADMAP.md` — staged work from the host core to hardware validation.

## Project status

This is experimental work, not a ready-to-install replacement for an existing
Vita cheat plugin. Current and planned capabilities are kept separate in this
README and the roadmap so a future feature is not mistaken for a tested one.
