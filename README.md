# VitaCheat

VitaCheat is an early research and development project for a modern community
cheat-authoring tool on user-owned PlayStation Vita systems. The intended end
result combines an on-device workflow with an optional PC companion and an
online community database.

This repository is an early scaffold. The current code is a portable,
allocation-free memory snapshot search/refinement core, a lossless legacy
VitaCheat `.psv` importer, and a deterministic menu-activation state machine,
all with host tests. It does not yet include a Vita application or plugin,
attach to a process, read or write Vita memory, freeze values, connect over a
network, render a menu, or download cheats.

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
- preserves every other syntactically valid code as opaque data instead of
  guessing its meaning;
- reports malformed records, truncation, and legacy format-limit violations;
- exposes parsed records only when all output buffers fit, preventing dangling
  cross-indexes in a truncated import;
- emits one menu-open event after Select remains held for five seconds, then
  waits for a release before it can fire again.

No imported operation is executed in this milestone. See
[docs/legacy-psv-compatibility.md](docs/legacy-psv-compatibility.md).

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

The planned device design is hybrid. Most logic stays in user mode, while a
small capability-limited kernel companion supplies only the cross-process
operations that cannot be implemented safely from the Vita application. The
search engine remains separate from both layers:

```text
on-device UI                       optional PC companion
      |                                      |
      +------- paired, bounded protocol -----+
                         |
             capability-checked Vita adapter
                         |
             narrow kernel companion
                         |
                portable vitacheat_core
```

The online database will supply bounded declarative records, never executable
scripts. The kernel companion must expose a narrow versioned interface and pass
a separate review and hardware gate for every capability. Eventual PSP/PS1
work inside the Vita's PSP emulator is a distinct research track and may also
need an emulator-side component; a Vita kernel plugin alone is not assumed to
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

No VitaSDK installation is required for the current host-only milestone.

## Relationship to VitaDebugger

VitaCheat is a separate project. It may reuse stable public interfaces and
lessons from [VitaDebugger](https://github.com/zm2283145/VitaDebugger), such as
module-relative addressing, build-identity checks, bounded messages, explicit
ownership, watchdog cleanup, logging, profiling, and deployment workflows. It
will not begin as a hard copy of the debugger or as a general arbitrary-kernel-
access service.

## Compatibility

No Vita compatibility is claimed yet because no Vita-side component exists.
Retail Vita and Vita TV hardware on system software 3.65 will be the first
hardware-validation target. Firmware 3.60 and other releases will be listed
only after their own tests pass.

## Repository layout

- `include/vitacheat/search.h` — public bounded search/refinement API.
- `include/vitacheat/legacy_psv.h` — bounded lossless legacy importer API.
- `include/vitacheat/menu_activation.h` — portable Select-hold state machine.
- `src/search.c` — portable little-endian implementation.
- `src/legacy_psv.c` — syntax indexing and conservative operation mapping.
- `src/menu_activation.c` — five-second one-shot activation logic.
- `tests/host/test_search.c` — native behavioral and boundary tests.
- `tests/host/test_legacy_psv.c` — mixed-format, truncation, and fail-closed
  importer tests.
- `tests/host/test_menu_activation.c` — hold, release, and clock-reset tests.
- `docs/architecture.md` — component boundaries and data flow.
- `docs/legacy-psv-compatibility.md` — compatibility guarantees and limits.
- `docs/security-model.md` — authority, transport, database, and cleanup rules.
- `ROADMAP.md` — staged work from the host core to hardware validation.

## Project status

This is experimental work, not a ready-to-install replacement for an existing
Vita cheat plugin. Current and planned capabilities are kept separate in this
README and the roadmap so a future feature is not mistaken for a tested one.
