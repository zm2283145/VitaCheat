# Experimental native read hardware gate

## Scope

`experimental/hardware-gate/` is a disposable, opt-in VitaSDK experiment. It
validates one question on owned hardware: can a source-owned `VCHG00001`
application read at most 64 bytes from one of its own main-module segments
through a generated syscall stub, a boot-loaded SKPRX, and
`ksceKernelCopyFromUserProc`?

This is **not** a VitaCheat plugin, production memory service, foreign-process
reader, installer, or compatibility claim. It cannot target another PID,
accept an absolute address, write memory, inject a module, hook code, pause a
thread, search memory, expose network control, or edit `tai/config.txt`.
`PCSA00133`, every other foreign title, menu integration, writes, pause, and
injection remain blocked.

The branch is based on `read-only-memory-service` commit
`3cb074e7aaeeb5179ba2d49a69c57f053b597e20`, whose parent is
target-attestation-foundation commit
`6b6e6a34edf56a56d34d5485b9d28be0a274498a`.

## Clean-room evidence and rejected designs

Only behavior was used from these pinned research references:

- original z06 at
  [`r0ah/vitacheat@bb8158a1c696914a8ea2299889d42ab9a57a3ab2`](https://github.com/r0ah/vitacheat/tree/bb8158a1c696914a8ea2299889d42ab9a57a3ab2)
  demonstrated a broad unsafe 36-syscall global bridge and direct in-process
  writes; that authority is expressly rejected;
- [VitaDebugger at
  `c01a38690897e71b673429e7f872aad7dbd31411`](https://github.com/zm2283145/VitaDebugger/tree/c01a38690897e71b673429e7f872aad7dbd31411)
  provided device evidence for a boot-loaded SKPRX, generated stubs,
  kernel-derived caller PID, and bounded exact-copy/cleanup behavior, but is
  unlicensed project-wide and has no production-proven foreign reader; and
- [kuBridge at
  `417ddde9a744eba98d769382c1c1372b8e119139`](https://github.com/TheOfficialFloW/kuBridge/tree/417ddde9a744eba98d769382c1c1372b8e119139)
  was inspected only as evidence. Its runtime API is not a dependency and its
  unlicensed source is not copied, linked, or wrapped.

The gate does not expose unrestricted memcpy, allocation, mprotect, cache,
exception, or RWX operations. Its implementation is independently written
against installed VitaSDK declarations and generated-stub tooling.

## Fixed ABI

The VitaCheat-owned export module and library are both
`VitaCheatHardwareGate`. The export manifest pins module NID `0x67589133` and
library NID `0x56434847`. The five append-only v1
exports are:

| Export | NID | Request | Response |
|---|---:|---:|---:|
| `vchgGetStatus` | `0xD62A0D75` | 16 bytes | 32-byte v1 or 48-byte diagnostic v2 |
| `vchgOpenSelf` | `0xF4B684B1` | 16 bytes | 40 bytes |
| `vchgGetSelfMainModule` | `0x2E3A51C7` | 24 bytes | 128 bytes |
| `vchgReadSelfSegment` | `0x8C97E420` | 32 bytes | 80 bytes |
| `vchgClose` | `0x4A6FD239` | 24 bytes | 16 bytes |

Every request and response has a compile-time size assertion. Requests require
the exact version, size, capability set, nonzero handle where applicable, and
zero reserved fields. `GetStatus` remains callable when the capability is
disabled, stopped, or compiled status-only because of an API mismatch.
`GetStatus` alone also accepts append-only diagnostic version 2. Its first 32
bytes retain the v1 layout and its final 16 bytes report only a diagnostic
stage, the exact signed return from the failed VitaSDK call when one exists,
and two zero reserved words. All other calls remain v1-only. No PID, module ID,
address, segment base, fingerprint, or user data is added to diagnostics.

`OpenSelf` derives the ambient syscall caller PID in the kernel, exact-matches
the compile-time `VCHG00001` title ID, obtains that PID's main module, and
snapshots its module ID, normalized exact name `VitaCheatHgClient`, fingerprint,
and at most four segments. It returns one kernel-owned, nonzero, monotonically
nonrepeating handle with a fixed two-second lifetime. The user never supplies
an authoritative PID, module ID, address, or deadline.

`GetSelfMainModule` reports only module name, fingerprint, segment indices,
sizes, and permission values. It deliberately omits segment bases.
`ReadSelfSegment` accepts only handle, segment index, offset, and a nonzero
length of at most 64. It re-derives caller PID and the complete module snapshot,
checks handle/caller/deadline/module identity, read permission, index, both
`offset + length` and `base + offset`, then performs exactly this chain:

1. fixed request from the caller into a kernel local with
   `ksceKernelCopyFromUserProc(caller_pid, ...)`;
2. the validated caller-process segment into one zeroed 64-byte kernel bounce
   with `ksceKernelCopyFromUserProc(caller_pid, ...)`;
3. one fully initialized 80-byte response back to that caller with
   `ksceKernelCopyToUserProc(caller_pid, ...)`.

Only return value zero is success. There is no short-success path. The bounce
and local responses are scrubbed on entry and every exit. A post-read module
snapshot and clock check reject module churn or expiry before copy-out.

The service has one session and nonblocking C11 atomic serialization. Adapter
calls execute without the transaction guard while an adapter-active exclusion
makes reentry return `BUSY`; no user pointer is retained. Stop invalidates and
scrubs the session. If a syscall is active, module stop returns
`SCE_KERNEL_STOP_CANCEL`, so reliable cleanup is a reboot rather than a
pretended successful unload.

This is source-owned same-process identity checking, not strong
foreign-process attestation and not a production generation model.

### First 3.65 run and diagnostic retest

The first guarded retail 3.65 run used commit
`f97c31f31bb1a0be063f1371964d8fa0a290c836` and SKPRX SHA-256
`2b561802df82529dec9ffa7f4ef3bc10625d294582585ba00fab3b1c9f935b09`.
`status-ready` passed, then `OpenSelf` returned signed `-1073741837`
(`0xBFFFFFF3`); no read ran. The raw result JSON SHA-256 was
`b92423707f36736e546daba859675098f0efc83708d941e70f288fe7b565dca2`.
The config and SKPRX were removed and two subsequent boots were normal.

The internal `OpenSelf` path can return `MODULE_UNAVAILABLE` (`-13`) only after
caller PID, request copy, clock, and exact title validation have succeeded and
the main-module adapter has returned false. The observed `0xBFFFFFF3` is the
same private result with bit 30 cleared. The client now restores that bit only
when the result decodes into the gate's exact `-24..-1` range; unrelated SCE
errors are preserved.

The original adapter collapsed all module-query failures and discarded their
raw results, so the first run cannot prove whether module ID, module info,
fingerprint, name normalization, or segment validation failed. Installed
VitaSDK declarations, argument order, `SceKernelModuleInfo` size `0x1B8`, and
the linked 3.63+ import NIDs all match the implementation; there is no proven
safe API correction to make yet. Diagnostic status v2 therefore records these
non-sensitive stage IDs:

| ID | Stage | Raw field |
|---:|---|---|
| 1 | caller PID | API return |
| 2 | request copy | API return |
| 3 | system time | API return |
| 4-5 | title query / normalization | API return, or zero for local validation |
| 6-8 | module ID / info / ID consistency | API return, or zero for consistency |
| 9-10 | fingerprint call / zero fingerprint | API return |
| 11-12 | module name / segment validation | zero for local validation |
| 13 | response copy | API return |

A retest must use both the newly built SKPRX and VPK. Run once and stop on the
same first anomaly rules. If `OpenSelf` fails, retain the additive JSON
`diagnostic` object and the `VCHG diagnostic` serial line. This diagnostic
change has been compile/link and host validated only; it has not been deployed,
launched, or otherwise validated on a Vita.

## Pinned SDK and firmware evidence

The first build is intentionally limited to retail firmware **3.65**. Module
queries link the VitaSDK `3.63+` module-manager import set; all other imports
use the named default driver/kernel libraries. Any runtime lookup failure is
reported and fails closed. No private NID, firmware offset, or fallback copy is
present.

The inspected Windows VitaSDK installation was `C:\vitasdk`. Its relevant
declarations and SHA-256 values were:

| Header and declarations | SHA-256 |
|---|---|
| `arm-vita-eabi/include/psp2kern/kernel/sysmem.h` — `SceKernelMemoryRefPerm` read and known permission bits | `c338c9e7bd8041c2c4b960b296723f92cea4b719e99d9914770b238aa645470a` |
| `arm-vita-eabi/include/psp2kern/kernel/sysmem/data_transfers.h` — `ksceKernelCopyFromUserProc`, `ksceKernelCopyToUserProc` | `5d475f58214318c36feb851b9e973178c761521f8646d33c5ae9963e31055093` |
| `arm-vita-eabi/include/psp2kern/kernel/modulemgr.h` — `ksceKernelGetSystemSwVersion`, `ksceKernelGetModuleIdByPid`, `ksceKernelGetModuleInfo`, `ksceKernelGetModuleFingerprint` | `d931371954b5d798fedf874a4d55f4f3ba5c5317d1b41c934bce0a1d7c394b52` |
| `arm-vita-eabi/include/psp2kern/kernel/threadmgr/misc.h` — `ksceKernelGetProcessId`, `ksceKernelGetSystemTimeWide` | `3af5f061752252ef068afafe655f8c59400ef5558c3779a8b282b7548f112f44` |
| `arm-vita-eabi/include/psp2kern/kernel/sysroot.h` — `ksceKernelSysrootGetProcessTitleId` | `679549424fd5bb10c6d9e25edbb7998e158d8e723c72060ec1732f7763aad583` |
| `arm-vita-eabi/include/psp2common/kernel/modulemgr.h` — `SceKernelModuleInfo` and four segment records | `0a3c21ffeb7373062bb1ea1c9a8fcb80368a0d82c117c4e2c88a3db09cdeb68a` |

The exact linked stub archives were:

| Archive | SHA-256 |
|---|---|
| `libSceSysmemForDriver_stub.a` | `d28f8443bb194949c3d4d186eecd759fb45c8b61585e118b92a78cb403e86fac` |
| `libSceModulemgrForDriver_stub.a` | `7d7b5cde2e03cac9eafbc7b14bd1266bd1ff13b3accbe6364073a8f502b2e9c3` |
| `libSceModulemgrForKernel_363_stub.a` | `69efa0fde542d34eee2c60d98071d41d696b94ac3e7596563a3138a1232c6da5` |
| `libSceThreadmgrForDriver_stub.a` | `a5bba383ce62769f7bd5b6299147abfe907d7973657a4cf242910304427e7a41` |
| `libSceSysrootForKernel_stub.a` | `640c8fc59e1f9c9c12070e834c2f33f3d074d09e06b74c170591a27f5dbd56c4` |

The compiler was `arm-vita-eabi-gcc 15.2.0`; binutils (`nm`) was `2.46.1`.
`vita-libs-gen` identified itself as `vita-libs-gen by xerpi`. The installation
had no Git metadata, so these hashes, declarations, linked symbol inspection,
and tool versions are the reproducible SDK identity.

## Build and inspect

Host tests are opt-in and do not require VitaSDK:

```sh
make hardware-gate-test hardware-gate-fuzz-smoke

cmake -S . -B build-hardware-gate \
  -DVITACHEAT_ENABLE_HARDWARE_GATE=ON
cmake --build build-hardware-gate
ctest --test-dir build-hardware-gate --output-on-failure
```

The normal `make test` and default CMake build do not build this directory.
For the Vita artifacts, set `VITASDK`, then explicitly supply the only accepted
title and firmware:

```sh
cmake -S experimental/hardware-gate/vita \
  -B build-vita-hardware-gate \
  -DCMAKE_TOOLCHAIN_FILE="$VITASDK/share/vita.toolchain.cmake" \
  -DVITACHEAT_HARDWARE_GATE=ON \
  -DVITACHEAT_HARDWARE_GATE_TITLE_ID=VCHG00001 \
  -DVITACHEAT_HARDWARE_GATE_FIRMWARE=3.65
cmake --build build-vita-hardware-gate --parallel
```

The build invokes `vita-elf-export` and `vita-libs-gen`, assembles the generated
user syscall stubs, packages the VPK, and writes:

- `build-vita-hardware-gate/vitacheat-hardware-gate.skprx`
- `build-vita-hardware-gate/vitacheat-hardware-gate-test.vpk`
- `build-vita-hardware-gate/libVitaCheatHardwareGate_stub.a`
- `build-vita-hardware-gate/artifact-inventory.txt`
- `build-vita-hardware-gate/symbol-inventory.txt`

Inspect before transfer:

```sh
arm-vita-eabi-nm -g build-vita-hardware-gate/VitaCheatHardwareGateKernel
arm-vita-eabi-nm -g build-vita-hardware-gate/VitaCheatHgClient
arm-vita-eabi-ar t \
  build-vita-hardware-gate/libVitaCheatHardwareGate_stub.a
```

Stop if the kernel imports any unrestricted copy/allocation/protection/cache/
exception API, exports anything beyond the five named calls, or the generated
archive has anything beyond those five stubs.

## Manual install, run, removal, and recovery

Nothing in this repository deploys to a Vita. On a user-owned 3.65 test device:

1. Verify `artifact-inventory.txt` locally and retain it with the test record.
2. Transfer `vitacheat-hardware-gate.skprx` to
   `ux0:tai/vitacheat-hardware-gate.skprx`.
3. Manually add exactly this line under `*KERNEL` in `ux0:tai/config.txt`:

   ```text
   ux0:tai/vitacheat-hardware-gate.skprx
   ```

4. Reboot. Install `vitacheat-hardware-gate-test.vpk` with VitaShell and
   confirm its bubble title ID is exactly `VCHG00001`.
5. Launch it once. It stops on the first anomaly and writes
   `ux0:data/vitacheat-hardware-gate-result.json`; capture the complete serial
   lines beginning `VCHG` and retrieve the JSON without editing it.
6. Do not continue if status is not `READY`, any test fails, the device hangs
   or reboots, a module/title/fingerprint differs, a copy returns an unexpected
   code, or the log is missing/truncated.
7. Remove the VPK, remove only the exact config line above, delete only the
   named SKPRX, and reboot. Verify the line and file are absent.

If boot is disrupted, hold **L** while booting to bypass taiHEN plugin loading,
then remove the exact config line and SKPRX before rebooting normally. Because
unload is not claimed safe during an active syscall, reboot is the required
cleanup boundary.

The client validates 0/1/63/64/65-byte behavior, exact sentinel bytes, safe
segment start/end and past-end rejection, offset/length overflow, invalid
segment and handle, bad output pointer, single-session enforcement,
close/replay, nonrepeating reopen, timeout, and stale module/session binding.
It never probes an unmapped source address.

Expected result template:

```json
{
  "schema": "vitacheat.hardware-gate.result.v1",
  "title_id": "VCHG00001",
  "target_firmware": "3.65",
  "scope": "same-process-source-owned-sentinel",
  "tests": [
    {"name": "status-ready", "result": "pass", "code": 0}
  ],
  "diagnostic": {
    "queried": false,
    "stage": 0,
    "stage_name": "none",
    "raw_api_code": 0,
    "raw_api_hex": "0x00000000",
    "query_code": 0,
    "syscall_code": 0,
    "syscall_hex": "0x00000000"
  },
  "summary": {"passed": 23, "failed": 0, "result": "pass"}
}
```

Do not treat a passing result as evidence for foreign-title reads, production
installation, `PCSA00133`, menu integration, writes, pause, injection,
arbitrary memory access, or any firmware other than the exact tested device.
