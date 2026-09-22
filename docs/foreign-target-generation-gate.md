# Experimental foreign-target generation gate

## Scope and evidence status

`experimental/foreign-target-gate/` is a separate, disposable VitaSDK
experiment. It asks one narrow hardware question: can the exact source-owned
controller `VCFC00001` obtain an opaque kernel handle to the separately running
source-owned fixture `VCFT00001`, bind that handle to process-event generation
and exact main-module identity, and read at most 64 bytes from a symbolic
fixture segment through `ksceKernelCopyFromUserProc`?

The host model and Vita cross-build are complete. The first guarded retail
3.65 attempt reached the controller's successful target-launch request, then
stopped fail closed because the target produced no observable source-owned
artifact. A diagnostic revision added a source-owned startup-stage record.
Its guarded rerun reached a valid stage-6 `prompt-ready` record with successful
sentinel lookup and layout/fixture writes, but the immediate target-side
wrong-caller probe returned `TARGET_UNAVAILABLE` (`-13`) instead of
`CALLER_TITLE_MISMATCH` (`-12`). No input, handle, foreign read, retry, or
relaunch occurred. Both runs restored the original device configuration
byte-exact.

`OpenExactFixture` checks the event registry for `TARGET_STARTED` before it
derives or validates the caller title. The rerun therefore proves only that
the registry had not reached `TARGET_STARTED` at that target instant. It does
not prove that the start callback never arrives: the installed VitaSDK header
does not define callback timing relative to the first user instruction in
`main`. The host runner also saw the intentionally preserved prior result
before the corrected controller truncated it and incorrectly treated its old
test order as a failure. The current revision corrects both races without
changing kernel authority: the fixture performs a bounded target-only
readiness probe, and controller results carry a fresh run identity after
verified truncation. It has not been run on hardware.

The layer is based exactly on remote `native-read-hardware-gate` commit
`1233c08656a1c219d8de0fd7dde1257986ed3a97`. Its device-tested parent gate is
commit `8d74fad289b050e122c2656c0953da43aadef533`; the retained same-process
result JSON SHA-256 is
`04563275cb1fc45398f1100f4ea9c1aa4874844c7f73db0d7cf7faf0f4b635bc`.
This layer does not change that ABI, source, evidence, or five exported NIDs.

This experiment cannot select a PID, title, module UID, or absolute address
from user mode. It contains no write, injection, hook, pause, search, network,
retail-title, `PCSA00133`, production-installation, private-NID, or
firmware-offset path.

## Source-owned fixtures

| Role | Title ID | Main module | Behavior |
|---|---|---|---|
| target | `VCFT00001` | `VitaCheatFtTarget` | publishes one 64-byte aligned sentinel's segment index/offset, verifies it cannot open the controller capability, and remains visibly alive until X launches the controller |
| controller | `VCFC00001` | `VitaCheatFtController` | drives status/open/read/exit/relaunch/stale-handle tests; it never sends an authoritative PID, title, module UID, fingerprint, generation, or address |

The target writes a 32-byte source-owned descriptor containing a magic,
version, segment index, segment-relative offset, sentinel length, and the
bounded wrong-caller result. It also writes the fixed diagnostic startup
record described below. Before each target launch, the controller truncates
and reads back zero bytes from the descriptor, startup record, and fixture
JSON. Any open, truncate, close, or readback failure stops before launch. The
descriptor chooses bytes within the already exact-title/exact-module kernel
record; neither file authorizes a target or contains an address.

The controller uses documented `sceAppMgrLaunchAppByUri` to launch the fixture.
The fixture uses the same API to return to the controller. The controller uses
documented `sceAppMgrDestroyAppByName` only for the exact compile-time
`VCFT00001` fixture. This is intended to exercise the Vita's documented
suspend/resume application lifecycle without injecting code or adding a
background service. If the device closes rather than suspends either app, the
gate stops and records failure; process survival is not assumed.

## Diagnostic startup-stage record

`ux0:data/vitacheat-foreign-target-startup.bin` is an 80-byte canonical
little-endian record. Its magic is `0x53544731`, current version is `2`,
target is `VCFT00001`, and the 16-byte source identity is
`564346472d53544147452d5632000000` (`VCFG-STAGE-V2` plus zero padding).
Bytes 76-79 contain little-endian FNV-1a-32 over bytes 0-75. The integrity
field detects partial or malformed in-place observations; it is not a
signature or authority. The decoder retains exact version-1 support for the
preserved 80-byte diagnostic evidence; new targets emit only version 2.

The record contains only the stage, a completed-result mask, and bounded
return/status codes. It deliberately contains no PID, process generation,
lifecycle revision, module UID, fingerprint, raw address, segment base, key
material, or unrelated device fact.

| Stage | Name | Last completed boundary |
|---:|---|---|
| 1 | `main-entered` | `main` published before module introspection or any gate syscall |
| 2 | `sentinel-lookup-complete` | self-module/sentinel lookup returned |
| 3 | `wrong-caller-open-complete` | bounded target-side `OpenExactFixture` readiness probe completed |
| 4 | `layout-write-complete` | descriptor write returned or was explicitly skipped |
| 5 | `fixture-write-complete` | initial fixture JSON write returned |
| 6 | `prompt-ready` | all pre-input work completed and input sampling was configured |
| 7 | `cross-observed` | one X was observed; controller launch had not returned |
| 8 | `controller-launch-complete` | the controller-resume launch call returned |

Version 2 uses the former reserved bytes without enlarging the record:
bytes 68-69 contain the readiness attempt count, bytes 70-71 the rounded-up
elapsed milliseconds, and bytes 72-75 the readiness-probe result. The raw last
syscall code remains at bytes 52-55. Version 1 requires all eight bytes to be
zero.

The target samples documented `sceKernelGetSystemTimeWide`, calls
`OpenExactFixture` immediately, and retries only `TARGET_UNAVAILABLE` every
20 milliseconds. It stops after at most 101 attempts or two seconds. A
nonpositive clock, rollback, delay failure, unexpected syscall success or
other result, or nonzero returned handle fails closed. The only successful
terminal tuple is
last result `CALLER_TITLE_MISMATCH` (`-12`), readiness result zero, and a zero
handle. This polling neither creates nor advances registry state and cannot
return a controller handle to the target title.

Results use raw non-sensitive API/I/O return values where available. Local
codes are `-4601` for deliberately skipped, `-4602` for sentinel not found,
`-4603` for a zero-length incomplete write, `-4604` for bounded serialization
failure, `-4605` for readiness timeout, `-4606` for clock unavailable,
`-4607` for clock rollback, `-4608` for a leaked handle, and `-4609` for delay
failure. `-4610` classifies an unexpected syscall result while preserving that
raw result separately. A valid prompt-ready record has sentinel `0`,
wrong-caller last result `-12`, readiness result `0`, a nonzero bounded
attempt count and bounded elapsed time, layout `0`, and fixture `0`. A failed readiness probe
publishes stage 3 and exits before layout, prompt, or input.

The target rewrites the record in place and closes the file after every
complete fixed payload. A read-only operator must preserve each distinct
wrong-sized or integrity-invalid observation as a partial and continue polling
without input until a complete record or timeout. Only stage 6 with the exact
expected results, plus the independent 32-byte layout and fixture JSON, permits
one bounded X. Even then, the record proves only target user-mode progress.
The resumed controller still calls `vcfgGetStatus` and requires the
kernel-owned `VC_FTG_TARGET_STARTED` state before `OpenExactFixture`; no stage
record can create, advance, or replace kernel lifecycle authority.

## Controller-result freshness

Before any test, the controller truncates
`ux0:data/vitacheat-foreign-target-result.json` and verifies a zero-byte
readback. Every subsequent non-atomic JSON rewrite uses schema
`vitacheat.foreign-target-gate.result.v2`, build ID `VCFG-RESULT-V2`, and one
nonzero 16-hex-digit run ID sampled from documented system time. Its first two
ordered tests are `clear-controller-result` and
`controller-run-identity`; a successful run now has exactly 32 tests.

The operator preserves the prior result before launch. Its exact bytes remain
stale waiting evidence and must not be compared to the new test order. Polling
continues until either an empty observation or a complete current-identity
document appears. Partial current writes are preserved and retried. A complete
document is current only if schema, build ID, titles, firmware, scope, and
nonzero run ID match, the run ID differs from a preserved version-2 result,
and `clear-controller-result` passed. A complete current record may be
accepted without observing the transient empty file; this covers an atomic or
too-fast clear/rewrite. Structurally complete malformed or identity-mismatched
current records fail closed.

## Documented API audit

The installed SDK is `C:\vitasdk`. It has no Git metadata, so the reproducible
identity is the file hash, declaration, NID database, linked archive, and tool
version. The public source comparison is
[`vitasdk/vita-headers@5e1e7d38d766e4c1634a77f6e5249caab8c8f9cb`](https://github.com/vitasdk/vita-headers/tree/5e1e7d38d766e4c1634a77f6e5249caab8c8f9cb).
VitaSDK's database is incremental: process-event/sysmem/sysroot imports retain
their 3.60 entries, while module-manager imports use the changed 3.63 library.
There is no separate `db/365` snapshot. Runtime firmware is therefore checked
for exactly `0x03650000`, and every missing/changed import or callback anomaly
fails closed.

| Facility | Installed declaration / database | Library and NID |
|---|---|---|
| process-event registration | `arm-vita-eabi/include/psp2kern/kernel/proc_event.h`; `share/vita-headers/db/360/SceSysmem.yml` | `SceProcEventForDriver` `0x887F19D0`; register `0x2A43912D`; unregister `0x3DED57CC`; invoke `0x414CC813` |
| exact PID-to-title lookup | `arm-vita-eabi/include/psp2kern/kernel/sysroot.h`; `db/360/SceSysmem.yml` | `SceSysrootForKernel` `0x3691DA45`; `ksceKernelSysrootGetProcessTitleId` `0xEC3124A3` |
| main module and segments | `arm-vita-eabi/include/psp2kern/kernel/modulemgr.h`; `arm-vita-eabi/include/psp2common/kernel/modulemgr.h`; `db/363/SceKernelModulemgr.yml` | `SceModulemgrForKernel` `0x92C9FFC2`; main module `0x679F5144`; module info `0xDAA90093` |
| module fingerprint | same module-manager files | `SceModulemgrForKernel` `0x92C9FFC2`; `ksceKernelGetModuleFingerprint` `0x337A3908` |
| target-to-kernel copy | `arm-vita-eabi/include/psp2kern/kernel/sysmem/data_transfers.h`; `db/360/SceSysmem.yml` | `SceSysmemForDriver` `0x6F25E18A`; `ksceKernelCopyFromUserProc` `0x605275F8` |
| kernel-to-caller copy | same files | `SceSysmemForDriver` `0x6F25E18A`; `ksceKernelCopyToUserProc` `0x6B825479` |
| caller PID / monotonic time | `arm-vita-eabi/include/psp2kern/kernel/threadmgr/misc.h`; `db/360/SceKernelThreadMgr.yml` | `SceThreadmgrForDriver` `0xE2C40624`; PID `0x9DCB4B7A`; time `0xF4EE4FA9` |
| firmware check | `arm-vita-eabi/include/psp2kern/kernel/modulemgr.h`; `db/360/SceKernelModulemgr.yml` | `SceModulemgrForDriver` `0xD4A60A52`; `ksceKernelGetSystemSwVersion` `0x5182E212` |
| source-owned app transition | `arm-vita-eabi/include/psp2/appmgr.h`; `db/360/SceDriverUser.yml` | `SceAppMgrUser` `0xA6605D6F`; launch `0x003C634F`; destroy `0x4570DC15` |

Installed header/database SHA-256 values:

| Path under `C:\vitasdk` | SHA-256 |
|---|---|
| `arm-vita-eabi/include/psp2kern/kernel/proc_event.h` | `48d54781f993280666f15624cb8ca90bc183d555b7c73013a216771916501de8` |
| `arm-vita-eabi/include/psp2kern/kernel/sysroot.h` | `679549424fd5bb10c6d9e25edbb7998e158d8e723c72060ec1732f7763aad583` |
| `arm-vita-eabi/include/psp2kern/kernel/modulemgr.h` | `d931371954b5d798fedf874a4d55f4f3ba5c5317d1b41c934bce0a1d7c394b52` |
| `arm-vita-eabi/include/psp2common/kernel/modulemgr.h` | `0a3c21ffeb7373062bb1ea1c9a8fcb80368a0d82c117c4e2c88a3db09cdeb68a` |
| `arm-vita-eabi/include/psp2kern/kernel/threadmgr/misc.h` | `3af5f061752252ef068afafe655f8c59400ef5558c3779a8b282b7548f112f44` |
| `arm-vita-eabi/include/psp2kern/kernel/sysmem/data_transfers.h` | `5d475f58214318c36feb851b9e973178c761521f8646d33c5ae9963e31055093` |
| `arm-vita-eabi/include/psp2/appmgr.h` | `84b4603423eff82844bfe19561c3f1cc4418623850381d8f5628eb10ee845d00` |
| `share/vita-headers/db/360/SceSysmem.yml` | `0b69b8d22692bd8af370026b9278e9cf93a8eeb26910b2fe095a1c47022b2258` |
| `share/vita-headers/db/360/SceKernelThreadMgr.yml` | `2b8864ef8b8718bd051f4fc3da4cf42b0a14187ec6c1ef0f4a66c8dcf7d1eb76` |
| `share/vita-headers/db/360/SceKernelModulemgr.yml` | `3bbd6976d0f3d48fc29cf4c2ba87100fed9ea59178f81a478243b732f7272ac6` |
| `share/vita-headers/db/360/SceDriverUser.yml` | `2607941f3aa1d770cf92677f41f8cd845a825fc24f17c7552fcd38f72e114621` |
| `share/vita-headers/db/363/SceKernelModulemgr.yml` | `173127745d50817950beea139591547fcc5341e253b791632706c211653c9ac1` |

Exact linked archive SHA-256 values:

| Archive | SHA-256 |
|---|---|
| `libSceProcEventForDriver_stub.a` | `b10a0e6a765f5cbea5f37de9b640b055693acdbe7532c1cdab5dc416972a0221` |
| `libSceSysmemForDriver_stub.a` | `d28f8443bb194949c3d4d186eecd759fb45c8b61585e118b92a78cb403e86fac` |
| `libSceModulemgrForDriver_stub.a` | `7d7b5cde2e03cac9eafbc7b14bd1266bd1ff13b3accbe6364073a8f502b2e9c3` |
| `libSceModulemgrForKernel_363_stub.a` | `69efa0fde542d34eee2c60d98071d41d696b94ac3e7596563a3138a1232c6da5` |
| `libSceThreadmgrForDriver_stub.a` | `a5bba383ce62769f7bd5b6299147abfe907d7973657a4cf242910304427e7a41` |
| `libSceSysrootForKernel_stub.a` | `640c8fc59e1f9c9c12070e834c2f33f3d074d09e06b74c170591a27f5dbd56c4` |
| `libSceAppMgr_stub.a` | `38fa01b7ae1d981db2d4b9af3099a30d4eefba677d63dfdad3741b9f63f3468f` |
| `libSceCtrl_stub.a` | `1349ca5bcbfe4ba5554ae94ba42726978a320d3c7269d0ec06631af4e6e92582` |
| `libSceIofilemgr_stub.a` | `35123ee1268db3ee9aed160ea0bf1c56e50f666a904eee5dc05f21437c9be4de` |
| `libSceKernelModulemgr_stub.a` | `b6f1fe8c37e618be90223f7c2ea45b761b73c3d5af43209b120a54d37dce011e` |
| `libSceKernelThreadMgr_stub.a` | `81e3b5511ccb6431d032af51075956b6065515cf3303f18bc7b4e78a8b050832` |
| `libSceLibKernel_stub.a` | `3b53befed2c14717d9eb11c85938b9c2b6f8469c054d66f56c6f1732341bd92b` |

The compiler is `arm-vita-eabi-gcc 15.2.0`; binutils is `2.46.1`.
The pinned header fixes `SceProcEventHandler` at `0x1c` bytes and exposes
`create`, `exit`, `kill`, `stop`, `start`, and `switch_process` callbacks.
It labels exit as current-process exit and kill as initiated by SceShell, but
does not specify create/start ordering, suspend/resume delivery, concurrent
dispatch, callback timing relative to target `main`, or quiescence after
unregister. This gate registers only create, start, exit, and kill; stop and
switch callbacks are null. The bounded fixture poll is therefore an
observation accommodation, not a claim about undocumented callback order.
`SceProcEventInvokeParam1`/`Param2` unknown fields and callback `event_type`
are never interpreted as identity, ordering, or generation.

Third-party projects were used only to corroborate behavior. VitaDebugger,
kuBridge, and original z06 are unlicensed for this purpose; no implementation
or undocumented NID was copied. Public process-event users frequently omit
unregister/drain handling, so they are not evidence for this gate's cleanup.

## Registry and trust proof

The allocation-free kernel service owns one target record and one controller
session:

1. A registered `create` callback exact-queries the event PID's title. Only
   `VCFT00001` can create a candidate. A lifecycle revision is assigned in
   kernel state; callback parameter fields are ignored.
2. The first matching `start` must follow that exact create. It assigns the
   nonzero monotonic generation and binds the PID, lifecycle revision, kernel
   module-object UID, process-visible module UID returned by the successful
   module-info query, fingerprint, normalized `VitaCheatFtTarget` name, and at
   most four checked segments.
3. Duplicate, missed, out-of-order, malformed, concurrent, or exhausted
   lifecycle transitions trip one permanent fail-closed latch. A reset/reboot
   is then required. A process that predates registration has no create
   generation, cannot be opened, and a later start event fails closed.
   A callback observed between native registration and committing the
   registered state also trips the latch instead of being silently dropped.
4. Matching exit or kill invalidates the registry and every handle before a
   later PID/module-UID reuse can authorize a read. Relaunch receives the next
   process generation even if all other numeric identity fields are reused.
5. `OpenExactFixture` takes no PID, title, module, address, or selector. It
   derives the syscall caller PID, exact-matches `VCFC00001`, and independently
   revalidates the target title and complete main-module snapshot before
   returning one opaque two-second handle.
6. `ReadFixtureSegment` accepts only handle, segment index, offset, and a
   nonzero length at most 64. It verifies caller, time and rollback, generation,
   lifecycle revision, both module UID namespaces, fingerprint, module name,
   segments, read permission, and checked range before and after one
   target-to-fixed-kernel-bounce copy. Only an exact zero return succeeds.
7. The initialized response is copied from kernel to the derived controller
   PID. If a lifecycle callback races any unlocked adapter operation, the
   callback atomically trips fail-closed; the operation rejects before
   copy-out or overwrites an already copied source-owned response with zeros
   before returning failure. Bounce, snapshots, requests, and responses are
   scrubbed.

There is no polling-derived generation. Controller status polling only waits
for the event-owned state machine; it cannot create or advance authority.

Process-event callbacks and syscalls use fixed nonblocking guards. Callback
contention fails closed rather than waiting. Module stop first requires the
service to be idle, marks it stopped, and unregisters the stored handler UID.
Because VitaSDK does not document callback-quiescence semantics after
unregister, a module that ever registered the handler always refuses runtime
unload even after a successful unregister. Reboot is the only removal
boundary.

## Fixed experimental ABI

The VitaCheat-owned export module/library are `VitaCheatForeignGate`. Module
NID is `0x46544731`; library NID is `0x56434647`.

| Export | NID | Request | Response |
|---|---:|---:|---:|
| `vcfgGetStatus` | `0x18D84F21` | 16 bytes | 48 bytes |
| `vcfgOpenExactFixture` | `0xA8B1466C` | 16 bytes | 32 bytes |
| `vcfgReadFixtureSegment` | `0x70F14E77` | 32 bytes | 80 bytes |
| `vcfgClose` | `0x2CB3D522` | 24 bytes | 16 bytes |

All layouts have compile-time size assertions and exact version, size,
capability, reserved-zero, handle, and range validation. Status exposes only a
bounded runtime state, capability mask, maximum read, timeout, ABI flags,
target state, last result, and diagnostic stage. PID, generation, revision,
module UIDs, fingerprint, segment bases, and addresses remain kernel-only.

`symbol-inventory.txt` is build-failing allowlist enforcement for all kernel
imports, four exports, four generated syscall stubs, and the linked target and
controller symbols. `-ffunction-sections`, `-fdata-sections`, and
`--gc-sections` remove unused client runtime imports; network symbols are not
present.

## Host validation

The portable model covers exact create/start/exit/kill ordering, unrelated
events, duplicate and out-of-order events, PID and both module-UID reuse,
generation/revision exhaustion, wrong caller, module churn, clock rollback,
timeout, concurrent exit during target read or copy-out, stale callbacks,
malformed ABI, bad pointers, exact 1/63/64 reads, 0/65 rejection, permissions,
checked ranges, close/replay, and stale handles after exit/relaunch. The
startup tests cover exact stage ordering, canonical encode/decode, every
truncated length, single-byte corruption, malformed identity/reserved fields,
version-1 compatibility, delayed `-13/-13/-12` readiness, permanent
unavailability, delayed-wake deadline overshoot, unexpected results, handle
leaks, clock rollback, stale prelaunch cleanup, prompt timeout without input,
diagnostic non-authority, controller-result stale/partial/atomic freshness,
and unchanged target wrong-caller rejection. The bounded fuzzer mutates event
sequences, PIDs, modules, time, copy behavior, requests, concurrent exits, raw
startup records, and result-freshness observations, then round-trips every
valid diagnostic record.

Module-UID/fingerprint/segment mismatch is exercised only in the host model.
The manual Vita sequence does not mutate or reload a live module merely to
induce that diagnostic.

Host targets remain opt-in:

```sh
make foreign-target-gate-test foreign-target-gate-fuzz-smoke \
  foreign-target-gate-analyze

cmake -S . -B build-foreign-gate \
  -DVITACHEAT_ENABLE_FOREIGN_TARGET_GATE=ON
cmake --build build-foreign-gate
ctest --test-dir build-foreign-gate --output-on-failure
```

The final validation used strict GCC 16.2.0, Clang 22.1.8, and MSVC
19.51.36256.0 builds with warnings as errors; the opt-in suites passed on all
three compilers and the gate-disabled default suite remained unchanged. The
deterministic standalone fuzzer completed 10,000 inputs and GCC `-fanalyzer`
reported no findings. On this Windows host, the Clang sanitizer configuration
still cannot link because the installed toolchain lacks
`libclang_rt.asan_dynamic.dll.a` and
`libclang_rt.asan_dynamic_runtime_thunk.a`; that is an environment limitation,
not a passing sanitizer result.

## Vita build and artifact inspection

All five safety selections are mandatory:

```sh
cmake -S experimental/foreign-target-gate/vita \
  -B build-vita-foreign-target-gate \
  -DCMAKE_TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake" \
  -DVITACHEAT_FOREIGN_TARGET_GATE=ON \
  -DVITACHEAT_FOREIGN_GATE_TARGET_TITLE_ID=VCFT00001 \
  -DVITACHEAT_FOREIGN_GATE_CONTROLLER_TITLE_ID=VCFC00001 \
  -DVITACHEAT_FOREIGN_GATE_FIRMWARE=3.65 \
  -DVITACHEAT_FOREIGN_GATE_LIFECYCLE=SUSPENDED_SOURCE_FIXTURE
cmake --build build-vita-foreign-target-gate --parallel
```

The build produces:

- `vitacheat-foreign-target-gate.skprx`;
- `vitacheat-foreign-target-fixture.vpk`;
- `vitacheat-foreign-target-controller.vpk`;
- `libVitaCheatForeignGate_stub.a`;
- `symbol-inventory.txt`; and
- `artifact-inventory.txt`.

The final clean cross-build inventory is:

| Artifact | Bytes | SHA-256 |
|---|---:|---|
| `vitacheat-foreign-target-gate.skprx` | 11,299 | `f2b9c59f296eb05485523f294164a6b173c8dce2ddb5900b02ee19976cdc96a7` |
| `vitacheat-foreign-target-fixture.vpk` | 7,643 | `b4e64cdaeb25f1c3c9df1d6da7acb2505e9a29ccd368b527f3af2e3258459a16` |
| `vitacheat-foreign-target-controller.vpk` | 7,514 | `9cf05f5abe2ae05b876abda6588192e720e9091cbadcaa42c2bde02aabaef883` |
| `libVitaCheatForeignGate_stub.a` | 3,728 | `63199aafac1662bd6724d6ae1140db3768a822606bf68b157a80c7b0cff6df10` |
| Target `eboot.bin` | 10,659 | `404b482bb49ea2a086c1e59c5b7ac3d7ba42ea83d22784ee268035541cebfc87` |
| Controller `eboot.bin` | 10,516 | `0bbb33f81fde761be7a99ef2e5023463532c9f6b80dbe53bc0cf2f8637762371` |

Each VPK contains only `sce_sys/param.sfo` and `eboot.bin`, both stamped
`1980-01-01T00:00:00`. The target SFO is 912 bytes with SHA-256
`e8c70ff5c23109678c48e285016ae879f8d92a0c1febf79d77cef3d50bb88b7f`
and contains title ID `VCFT00001`; the controller SFO is 912 bytes with
SHA-256
`d7f8f25c7a1e1141593bed441989010e9b640191ccf338dfd13c4f6627d4a2f6`
and contains title ID `VCFC00001`. Each packaged `eboot.bin` is byte-exact
to the corresponding standalone build output listed above.

The exact kernel import allowlist is
`ksceKernelCopyFromUserProc`, `ksceKernelCopyToUserProc`,
`ksceKernelGetModuleFingerprint`, `ksceKernelGetModuleIdByPid`,
`ksceKernelGetModuleInfo`, `ksceKernelGetProcessId`,
`ksceKernelGetSystemSwVersion`, `ksceKernelGetSystemTimeWide`,
`ksceKernelRegisterProcEventHandler`, `ksceKernelSysrootGetProcessTitleId`,
and `ksceKernelUnregisterProcEventHandler`. The four exports are exactly the
four calls in the ABI table. Client inventories contain only their generated
gate stubs plus AppMgr, control, I/O, thread/process, and C runtime symbols.
Version 2 adds documented `sceKernelGetSystemTimeWide` to both client
allowlists for the bounded target deadline and non-authoritative controller
run identity; the 11 kernel imports and four exports are unchanged.
The linked Vita startup runtime imports allocator primitives even though the
gate, target, and controller source performs no dynamic allocation.
The generated archive uses deterministic GNU `ar`/`ranlib` mode, and the VPK
ZIP metadata uses the fixed lower-bound `SOURCE_DATE_EPOCH`. Two clean builds
must reproduce every deployable, package payload, and inventory hash before
publication.

The repository never transfers, installs, starts, stops, or edits device
configuration.

## Manual retail 3.65 gate

Use only a user-owned test Vita on firmware 3.65. Preserve the original
`ux0:tai/config.txt` and retain `artifact-inventory.txt` before transfer.

1. Transfer only `vitacheat-foreign-target-gate.skprx` to
   `ux0:tai/vitacheat-foreign-target-gate.skprx`.
2. Add exactly
   `ux0:tai/vitacheat-foreign-target-gate.skprx` under `*KERNEL`, then reboot.
   Do not hot-load the module; the event registry must predate both fixtures.
3. Install the fixture and controller VPKs. Verify the bubbles are exactly
   `VCFT00001` and `VCFC00001`.
4. Preserve the old controller result, then launch the controller first. Ignore
   those exact stale bytes while waiting for a complete current schema/build/run
   identity with passing `clear-controller-result`. Preserve partial current
   writes. The controller must report that open is unavailable, then truncate
   and verify empty the exact layout, startup-stage, and fixture-result paths
   before launching the target.
5. Poll the startup path read-only. Preserve distinct partial/invalid bytes and
   send no input while the path is empty, malformed, or below stage 6. Require
   one complete version-2 stage-6 record with the exact identity, integrity,
   result tuple `0/-12/0/0`, readiness result zero, and bounded attempts/time;
   independently require the exact 32-byte layout and complete fixture JSON.
   Then send exactly
   `press cross; wait 100ms; release cross` once. The marker is not evidence of
   a process event.
6. The controller opens the event-registered target and performs exact
   1/63/64-byte reads plus bounded rejection tests only after its independent
   kernel `TARGET_STARTED` check. It then destroys only `VCFT00001`, verifies
   the live handle is stale, clears all three target evidence paths, and
   relaunches the fixture.
7. Repeat the exact stage-6/layout/fixture validation and one bounded X in the
   relaunched fixture. The original controller must resume, reject the old
   handle again, obtain a distinct new handle, perform one exact 64-byte read,
   and close it.
8. Retrieve
   `ux0:data/vitacheat-foreign-target-result.json`,
   `ux0:data/vitacheat-foreign-target-fixture.json`,
   `ux0:data/vitacheat-foreign-target-startup.bin`, both layouts, every
   preserved partial record, and serial lines beginning `VCFG`. Stop on the
   first failure, hang, reboot, missing/truncated record, fail-closed status,
   unexpected identity/integrity/stage, or unexpected syscall code.
9. Remove both VPKs, remove only the exact config line above, delete only the
   named SKPRX, and reboot twice. Confirm both normal boots and that the plugin
   is absent.

If boot is disrupted, hold **L** while booting to bypass taiHEN plugin loading,
remove the exact config line and SKPRX, then reboot normally. Do not attempt
runtime unload as recovery; unregister callback drain is one of the behaviors
under test.

The second-run diagnostic stop matrix is strict:

| Observation | Proven boundary | Required action |
|---|---|---|
| absent/empty through timeout | no successful `main-entered` publication; target entry and process events both remain unproven | no input; preserve and stop |
| partial/corrupt/wrong-sized | an in-place update or write fault, but no valid stage | preserve every distinct value; poll read-only; no input |
| valid stage 1-5 | exactly the named boundary completed | stop at timeout before input |
| stage 6 with unexpected result | target reached prompt but a prerequisite failed | no input; stop |
| stage 3 with last `-13` and readiness `-4605` | target entered, but registry readiness did not become observable within two seconds; callback delivery remains unproven | no input; preserve and stop |
| stage 3 with another failure, clock rollback, or handle leak | bounded readiness contract failed | no input; preserve and stop |
| stage 6 v2 with `0/-12/0/0`, readiness result zero, and valid layout/fixture | target user mode is ready for one X; process-event generation remains unproven | send one bounded X |
| stage 7 persists | X was observed but controller launch did not return | never repeat input; stop at timeout |
| stage 8 | controller launch returned with the recorded code | rely only on resumed controller kernel status for generation proof |

The machine-readable build/run checklist is
[`experimental/foreign-target-gate/manual-run-kit.json`](../experimental/foreign-target-gate/manual-run-kit.json).

## Non-claims after a pass

Even a complete device pass proves only one disposable source-owned
controller/fixture pair on one retail 3.65 device. It does not establish
arbitrary-title discovery, retail-game support, foreground attestation,
production process generations, writes, hooks, injection, pause, search,
freeze, network control, or safe runtime installation/unload. The existing
portable target-attestation and read-service policies remain separate and gain
no native authority from this experiment.
