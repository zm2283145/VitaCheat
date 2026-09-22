# Retail 3.65 native same-process read gate — 2026-09-22

This record covers only the disposable `VCHG00001` source-owned application
and experimental hardware-gate SKPRX on one retail PS Vita running system
software 3.65. It device-validates the bounded same-process read primitive
described in `docs/hardware-gate.md`; it is not a VitaCheat plugin or production
memory service.

## Source and artifact identity

- VitaCheat source commit:
  `8d74fad289b050e122c2656c0953da43aadef533`.
- SKPRX SHA-256:
  `997cd346172f32fe71ca0041fbefa2e5a4621cb4d1ee08312c88f791b5590090`.
- Test VPK SHA-256:
  `387c7a8f4a6934e3f4a45e6bb5aad4fe60021aa9bb521d01fcd168eb14001d19`.
- Generated stub archive SHA-256:
  `6ec2031cb0296852577540a8bccc0a6ba900ad31a084338a939bbc74fe0b2f6d`.
- Build artifact inventory SHA-256:
  `4423e86a3644ce3b59f4e5aea1b4145915fd3417c5e73c54470f2bd5da6e36c3`.
- Symbol inventory SHA-256:
  `0c9bb4eb2b4517fb921349ef85808c73da4e7e32b74956e720849fe7980e50b2`.

The symbol inventory contains exactly the approved nine VitaSDK imports, five
VitaCheat hardware-gate exports, and five generated syscall stubs.

## Procedure and result

The guarded run used the documented stop-on-first-anomaly procedure:

1. Load the experimental SKPRX under `*KERNEL`, reboot, install the exact test
   VPK, and launch `VCHG00001` once.
2. Require `READY`, open a kernel-owned self session, snapshot the caller's
   exact main module, and locate the source-owned sentinel in that module.
3. Require rejection of lengths 0 and 65 and exact bytes for lengths 1, 63,
   and 64.
4. Validate segment start and end, reject past-end and offset/length overflow,
   and reject invalid segment, handle, and output pointer inputs.
5. Validate single-session enforcement, close/replay rejection, nonrepeating
   reopen, stale-session rejection, and timeout.
6. Restore the original plugin configuration, remove the gate SKPRX, and
   complete two normal reboots.

All 23 checks passed. The retained
[raw result](hardware-result-retail-3.65.json) is unchanged from the device
client output and has SHA-256
`04563275cb1fc45398f1100f4ea9c1aa4874844c7f73db0d7cf7faf0f4b635bc`.
Its schema is `vitacheat.hardware-gate.result.v1`, scope is
`same-process-source-owned-sentinel`, diagnostic query is false, and summary is
23 passed, 0 failed. Serial logs were not captured; this record relies on the
byte-exact client JSON and the sanitized restoration facts below.

## Restoration

The original plugin configuration was restored byte-for-byte with SHA-256
`f3b109b6fe6d89855fbe04f842180dab6e2657e92c7d2c8763780a4d18e576d3`.
Post-restoration verification found one legacy VitaCheat plugin entry and its
SKPRX active, zero hardware-gate entries, and no hardware-gate SKPRX. Two
controlled reboots completed normally and reproduced that state. `VCHG00001`
remains installed but inert without the gate plugin.

The complete operator evidence bundle was intentionally not committed because
it also contains transport, configuration, and unrelated device-operational
data. Only the exact client result and the minimum sanitized facts required to
verify its source, artifacts, scope, outcome, and restoration are retained.

## Scope and next gate

This pass proves only an exact, bounded read from a readable segment of the
calling source-owned process through the generated stub, boot-loaded SKPRX,
64-byte kernel bounce, and safe per-PID copy primitive. It does **not** prove or
enable a foreign-process read, retail-game read, write, injection, hook, pause,
search, production installation, or compatibility with another firmware or
device.

The next native read milestone must be a separate disposable
**source-owned foreign-target generation gate**. A second source-owned test
process must expose a known read-only sentinel while the kernel independently
derives and binds its PID, process generation, main-module load generation,
fingerprint, and bounded readable segment. Only after lifecycle invalidation
and exact-copy behavior pass that gate may any retail-game target or production
adapter be considered. The next gate must still expose no raw PID or absolute
address and must add no write, injection, hook, or pause authority.
