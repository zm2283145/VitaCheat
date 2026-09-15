# Retail 3.65 Vita self-test gate — 2026-09-15

This record covers only the ordinary user-mode `VCHT00001` self-test. It proves
that the portable menu trigger and bounded scanner run on one retail PS Vita on
system software 3.65. It does not prove cross-process access, a resident plugin,
cheat execution, or compatibility with other firmware versions.

## Source and toolchain identity

- VitaCheat source commit:
  `4c036d6f8031bf058fae308ab033b3f229789da6`.
- Host-test compiler: MSYS2 MinGW GCC 16.2.0, revision 3.
- Compiler: `arm-vita-eabi-gcc` 15.2.0.
- VitaSDK build timestamp: `2026-08-25 12:49:54`.
- VitaSDK component revisions:
  - newlib: `64aa7aa33d4f380451a1f100d19589226cdad334`;
  - pthread-embedded: `11d2e5722d98c86f33c908fc47b2cf6e55205db5`;
  - samples: `fe8fbef570f3280586c0c20157146e3faefb2181`;
  - vita-headers: `ebc8f4f7ac8313fe4f74ca95724116b224bad184`;
  - vita-toolchain: `c527abce028df33f5281e9ed4994c25fcef53c7d`.

The build compiles VitaSDK's mutable `debugScreen` sample directly. The exact
sample inputs were:

- `debugScreen.c`:
  `A69AB5B35F825858630B704402C09BEFE9855186B7E17E9F1E9B7B8B32C99D46`;
- `debugScreen.h`:
  `B9B790BAD51444CB474CC16ACF8E331FFE3ACA082840B067781AA81C49B96592`;
- `debugScreen_custom.h`:
  `1380E5926689806B4B8D4DF85CBC626EE03A5282FA2F40E4447C0E0B29B6DC0E`;
- `debugScreenFont.c`:
  `2BFFE4BD8CAC67101D063C1D64336DFA936EC7411570176CE57BACEB302C6BCA`.

All three native test suites and the Vita artifact were force-rebuilt with the
recorded VitaSDK/MSYS tool directories first on `PATH`:

```text
make -B CC=C:/msys64/mingw64/bin/gcc.exe test vita-self-test
```

The artifact was force-rebuilt once more from the pinned commit with
`make -B vita-self-test`; its hashes are recorded below.

## Artifact and deployment identity

- VPK SHA-256:
  `07CABCC313C04ED7EAF79DDC98349B7ADEC11E5465A13877264825D053F420CF`.
- Unstripped ELF SHA-256:
  `DDF9DB77880E1F9CE6B02AAECCC3C8B8B93C2B646E729242364573B833CDB0F3`.
- `eboot.bin` SHA-256:
  `D956ECFAC0D4A46AF15A289CF22EF0C547972FD145DDC70295F53B21ABAD802E`.
- Signed deployment job: `12dcd5934129a0d295c7ab9bd39c125f`.
- Signed deployment manifest SHA-256:
  `a7ab9f596339fc0ca9c8c45d75e86ec3e608d22cf086b9306746aa671a73a267`.
- Ed25519 signature SHA-256:
  `9D283C9F5CA3FAEE66CB557F8AD2F23EE4991C99FEE32A1766CC044AC981EF9E`.
- Trusted Ed25519 SubjectPublicKeyInfo DER SHA-256:
  `0F1B05F8E26E1F2BADCAE2D6214603D6384E17E6146DB4E6D3CF8BB08926B5C8`.
- VPK inspection: title ID `VCHT00001`, two files, 153,534 uncompressed
  bytes, and 72,220 packaged bytes.
- Signed deployment payload: three files and 154,606 total bytes.

The retained non-secret [request](deployment-request.v1),
[manifest](deployment-manifest.v1), [signature](deployment-signature.bin),
[public key](deployment-public-key.der), and
[completion result](deployment-result.v1) make the recorded signed job
independently inspectable. The signing payload is the VitaDevDeploy v1 domain
`VITADEVDEPLOY-SIGNED-JOB-1` plus its NUL terminator, followed by the exact
request and manifest bytes.

The VitaSDK link used only ordinary user-mode Display, Controller,
KernelThreadMgr, Processmgr, and LibKernel stubs. No taiHEN, KuBridge, or
privileged kernel-module access was linked.

## Procedure and result

1. Install and launch the VPK through a one-use, signed VitaDevDeploy job.
2. Hold Select continuously for at least five seconds and release it.
3. Confirm that the menu opens once, then press X.
4. Observe the owned-buffer scanner result and return to LiveArea.

The run passed all six checks with zero failures:

- initial exact matches: 3;
- changed refinements: 2;
- increased refinements: 1;
- decreased refinements: 1;
- unchanged refinements: 1; and
- bounded output truncation: `TRUNCATED`, 2 offsets written, 3 total matches.

The aggregate pass and the five displayed result counts are visible in the
retained [hardware screenshot](hardware-result-retail-3.65.jpg), whose SHA-256 is
`9588A5FF80D40D547D8ADF7A917907A6E1E5C806F9016AB19A04D93B936F622D`.
The bounded-truncation assertion contributes the sixth successful check but is
not printed separately; its exact predicate is source-backed at the pinned
commit.
After the local copy was opened and verified, the exact source file
`/ux0:/picture/SCREENSHOT/cd/2026-09-15-124645.jpg` was deleted from the Vita.

Start is deliberately unbound so the PS+Start system screenshot shortcut does
not also request application exit. Triangle exits from the menu or result
screen, while Circle navigates back.

## Remaining gates

Memory pressure, cancellation during a real chunked scan, suspend/resume, and
longer lifecycle cleanup tests remain open. All foreign-process discovery,
reads, writes, freezes, and kernel work remain separately gated future work.
