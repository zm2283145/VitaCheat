# Legacy VitaCheat compatibility

The compatibility target is the data-oriented VitaCheat `.psv` format used by
the public z05/z06 community database. Binary, game-specific `.suprx` cheat
plugins are executable modules rather than declarative cheat files and are not
converted or loaded by this importer.

Primary references:

- [VitaCheat formatting](https://github.com/r0ah/vitacheat/wiki/Formatting)
- [VitaCheat code types](https://github.com/r0ah/vitacheat/wiki/Code-Types)
- [VitaCheat direct write](https://github.com/r0ah/vitacheat/wiki/Write)
- [VitaCheat B2 relative-base modifier](https://github.com/r0ah/vitacheat/wiki/B2-Code)
- [VitaCheat code header](https://github.com/r0ah/vitacheat/wiki/Code-Header)
- [Ratchet & Clank PCSA00133 database example](https://github.com/r0ah/vitacheat/blob/master/db/PCSA00133.psv)

## Lossless import schema 5

`vc_psv_parse` is allocation-free and accepts at most 1 MiB of caller-owned
source. With adequate output capacity, every line contains a raw span and the
spans cover the input contiguously. Concatenating them reconstructs the exact
input, including comments, unknown extensions, whitespace, a first-line UTF-8
BOM, and CRLF/LF/CR line endings.

The API is all-or-nothing. If any caller-owned output array is too small, it
returns required totals, clears partial records, reports zero stored records,
and returns `VC_PSV_STATUS_TRUNCATED`. Consumers never receive dangling
cross-indexes from an incomplete object graph.

`vc_psv_parse_with_options` validates BOM-marked or unmarked UTF-8 before
tokenization. Invalid UTF-8 fails unless the caller explicitly selects the
Windows-1252 or ISO-8859-1 fallback. The selected source encoding is reported;
fallback never rewrites source bytes. `vc_psv_emit_lossless` verifies that raw
line spans are contiguous and reconstructs the source byte-for-byte.

Schema 5 keeps one imported operation per lexically valid physical `$` record.
A repeat header
owns its immediately following physical record, which is classified as
`VC_PSV_OPERATION_REPEAT_CONTINUATION` by position rather than by interpreting
its first field as a top-level opcode. Pointer roots similarly reserve their
complete physical span, and every following continuation, marker, pointer MOV
half, or repeat-count record in that span is classified as
`VC_PSV_OPERATION_POINTER_POSITIONAL`. A positional `$9000`, `$B2MM`, or count
can therefore never dispatch as a standalone opcode or mutate selector state.
Every imported record retains the raw line span, 16-bit first field, both
32-bit fields, descriptor and line indexes, and any explicit compatibility
flags. A malformed child still occupies its expected physical position, taints
the complete descriptor, and cannot expose a later child as a top-level
operation. Incomplete 4/3/7/8 spans receive an explicit truncated-span
diagnostic. Lexically invalid records remain available through their raw line
span and `Unknown` record role; their fields are never padded or truncated.

Diagnostics separately identify lexical errors, truncated spans, unsupported
opcodes, noncanonical headers/markers/suffixes/levels/widths, invalid B2
module/segment operands, authoring-limit warnings, runtime-dependent
addresses, lowercase hexadecimal, orphan records, and canonical-emission
blocks. Informational runtime and lowercase diagnostics do not authorize an
operation and do not change its semantics.

The schema corrects the earlier B2 field interpretation. z06 encodes a selector
as `$B2MM SSSSSSSS 00000000`: `MM` is the module serial, and `S` is segment 0
or 1. Thus the public PCSA00133 record
`$B200 00000001 00000000` selects module 0, segment 1. B2 state:

- resets at every `_V0` or `_V1` descriptor;
- persists until another valid B2 record overwrites it;
- supports all module serials, including observed `B200`, `B201`, `B20E`, and
  `B229` forms;
- rejects a segment other than 0 or 1 or a nonzero third operand; and
- is snapshotted onto every following address-bearing imported operation.

A B2 record can appear or be overwritten anywhere in a descriptor and is valid
even when no address-bearing record follows it. It does not itself produce a
memory action.

## Typed scalar and pointer planner

`vc_psv_compile_cheat` consumes one complete imported descriptor and emits
caller-owned `vc_psv_plan_node` records. Compilation is atomic and
allocation-free. Any opaque record, malformed sequence, unapproved
compatibility record, action-budget overflow, gate target beyond the
descriptor, or gate target inside a repeat span rejects the complete
descriptor.

The default symbolic action limit is 4,096 and the hard configurable limit is
65,536. Scalar and pointer repeats remain one plan node; compilation sums their
possible action and pointer-read counts without expanding them. A count from
`0x0000` through `0xFFFF` is representable when the caller selects an adequate
limit.

Strict compilation supports these families:

| Legacy form | Typed meaning |
| --- | --- |
| `$0X00 A V` | U8/U16/U32 write of the low-width bits of `V` |
| `$5X00 D S` | Read U8/U16/U32 from `S`, then write those bytes to `D` |
| `$4X01 A V` + `$NNNN G I` | `N` writes at `A + k*G` with value `V + k*I` |
| `$A100 A V`, `$A200 A V` | Restorable little-endian U16/U32 code patch |
| `$C2NN 0000MMMM B` | Exact normalized-button-mask gate over `NN` records |
| `$DXNN A V`, X=0..B | Unsigned typed condition gate over `NN` records |
| `$B2MM S 00000000` | Select module `MM`, segment `S` for later addresses |
| `$3X0L B O1` ... `$3300 00000000 V` | Width-X write through an L-level pointer path |
| `$7X0L B O1` ... `$77SS 00000000 V` + `$NNNN G I` | Re-evaluated pointer path with selected base/offset and value increments |
| `$8X0L DB DO1` ... `$8800` + `$8Y0L SB SO1` ... `$8900` | Snapshot a matching-width source pointer target, then write its bytes to the destination pointer target |

For repeat/compression, `N` includes the first write and zero means no writes.
Address and value stepping are explicit modulo-2^32 arithmetic; each stored
value is then truncated to its selected width. B2 applies to the starting
address only, never to the count, address gap, or value increment. Adding a B2
base to an offset is overflow-checked before a concrete action is offered.

MOV resolves the same active B2 snapshot for both source and destination. The
evaluator stages each read before its corresponding write action. Later MOV,
condition, and patch reads observe earlier staged writes, which gives overlap
and sequential descriptor behavior a deterministic result without touching
real memory during evaluation.

Button gates require exact equality: extra pressed bits make the predicate
false. The mode field is preserved as `0` (undefined), `1` (Vita/default), `2`
(PSTV), `4` (DS3), or `8` (DS4); the core consumes only the normalized mask
returned by the caller. Supported button bits are Select `0x0001`, Start
`0x0008`, directions `0x0010` through `0x0080`, L/R `0x0100`/`0x0200`, and
Triangle/Circle/Cross/Square `0x1000` through `0x8000`.

Conditions map `X=0..2` to equal U8/U16/U32, `3..5` to not-equal, `6..8` to
unsigned greater-than, and `9..B` to unsigned less-than. Values are truncated
to the selected width. An unresolved or unreadable condition address evaluates
false. `X=C..F` remains opaque rather than inheriting an undocumented fallback.

Both gate families count following physical records, excluding the gate
itself. Counts range from 0 through 255 and can nest. The compiler rejects a
skip whose target would expose any continuation, pointer marker, repeat count,
or second MOV half.

## Pointer paths

Pointer paths contain one to eight read dependencies. Strict authoring accepts
levels 1 through 5; levels 6 through 8 require the explicit observed-format
policy. For each level, evaluation reads a little-endian U32 at the current
address and adds that level's U32 offset modulo 2^32. This intentionally gives
signed-looking offsets their legacy two's-complement behavior while storing
the arithmetic safely in unsigned types. A null pointer, unreadable or
out-of-range U32 dependency, null final target, or final target that cannot fit
its selected width fails the complete evaluation and clears staged actions.

For family `3`, the root owns `L-1` `$3X00` continuations and one positional
terminal. Strict mode requires `$3300`; observed `$3302` and `$9000` terminals
are accepted only by the pointer-marker policy and produce diagnostics.

For family `7`, the root owns `L-1` `$7X00` continuations, a `$77SS` marker, and
one positional count/gap/increment record. `SS=0` advances the already resolved
starting base by `G`; `SS=1..L` advances only that pointer offset. `SS>L` is
invalid. `N=0` performs no base resolution or reads. Values and addresses use
modulo-2^32 stepping, followed by width truncation for values. A gap through
`0xFFFF` is strict; a full-U32 gap requires the explicit U32-gap policy and a
diagnostic. The observed `$7402` marker requires the pointer-marker policy.

For family `8`, destination and source paths have the same strict level and
width (`X=0/1/2`, `Y=4/5/6`) and use zero-argument `$8800`/`$8900` markers.
Evaluation resolves both paths and snapshots the source bytes before staging
the destination write, including overlap. An explicit MOV-mismatch policy can
preserve and diagnose observed source width/level or swapped 88/89 marker
mismatches; traversal and copy width then use the destination settings. The
observed source settings remain in the plan and are never silently normalized.

B2 applies only to each pointer starting-base expression. It never applies to
pointer offsets, repeat gaps, counts, selectors, or value increments. Module
base addition is overflow checked; legacy pointer offsets and repeat stepping
remain deliberate U32 wraparound.

## Callback evaluation and restoration

`vc_psv_evaluate_plan` has no write callback. It resolves module bases, reads
up to four bytes, samples normalized buttons, and emits concrete
little-endian `vc_psv_action` records into caller-owned storage. It validates
the complete plan, callback set, and worst-case action capacity before invoking
the first callback. Failed evaluation clears partial actions.

`vc_psv_apply_actions` is the separate authority boundary. It receives only a
bounded byte-write callback. Successful patch actions copy their captured
original bytes into a caller-owned ledger. `vc_psv_rollback_patches` restores
active entries in reverse order and leaves a failed entry plus all earlier
entries active for retry. Ordinary `$0X00`, MOV, and repeat writes are
intentionally not added to the patch ledger.

These interfaces contain no Vita kernel APIs, process lookup, region
enumeration, or real-memory implementation. An adapter must still bind every
callback to a verified title, module build, process generation, and allowlisted
region.

## Strict and explicit compatibility modes

Strict mode is the default. Observed forms can be decoded only when the caller
enables the corresponding compile option, and every accepted deviation is
reported on its plan node and in the plan diagnostic count:

- inline `#` comments after an otherwise exact three-field record;
- repeat suffixes observed as `4000`, `4002`, `4100`, `4200`, `4202`, or
  `4210` instead of canonical `4X01`; and
- runtime-observed `$A000` restorable U8 patch behavior.
- pointer levels 6 through 8;
- pointer-write `$3302`/`$9000` and pointer-repeat `$7402` markers;
- full-U32 pointer-repeat gaps; and
- pointer-MOV source width/level or swapped `$8800`/`$8900` marker mismatches.
- the two observed `_V0---Alternative Codes [...]---` descriptor headers.

Field width remains exact. A 9-digit address or 7/6-digit value is malformed,
not padded or truncated. Top-level `0001`, `01F1`, `B000`, `C001`, `C007`, and
`C101` remain unknown. The importer preserves them, but no executable plan is
available for that descriptor.

Lowercase hexadecimal remains semantically equivalent and is diagnosed so
tools can apply a visible policy. It is the only textual variation that strict,
semantically decoded records may normalize. Compatibility-only forms retain a
diagnostic even when explicitly enabled.

## Canonical re-emission

`vc_psv_emit_canonical` first compiles each descriptor in strict mode. Only a
descriptor that is fully decoded without a blocking or compatibility
diagnostic is eligible. Its physical records are emitted as uppercase,
fixed-width `$CCCC AAAAAAAA VVVVVVVV` fields; descriptor text, comments, blank
lines, and original line endings remain unchanged. The operation is sized
before writing, so insufficient output capacity cannot produce a partial
document.

Any descriptor containing an unknown opcode, malformed field, truncated or
mismatched span, noncanonical repeat suffix/patch width/pointer level/marker,
inline comment, B2 anomaly, or noncanonical header is copied byte-for-byte.
This prevents silent normalization of observed `0001`, `01F1`, `B000`, `C001`,
`C007`, `C101`, `$3302`, `$7402`, `$9000`, repeat suffixes `00/02/10`, levels
6-8, or anomalous B2 operands.

## Pinned corpus regression

The repository does not vendor the third-party database. Run the deterministic
host inventory against a caller-owned checkout:

```sh
git clone --filter=blob:none https://github.com/r0ah/vitacheat.git ../legacy-vc-db
git -C ../legacy-vc-db checkout --detach bb8158a1c696914a8ea2299889d42ab9a57a3ab2
make legacy-corpus-test LEGACY_CORPUS_DIR=../legacy-vc-db
```

The Make target rejects any other checkout revision. The runner checks all 676
`.psv` files and reports each mismatch against these pinned facts: 34,839
physical records; 318 distinct four-hex first fields; 7,867 ordinary `_V0`,
121 `_V1`, and two `_V0---Alternative` headers; CRLF line endings; 1,343 inline
comments; 18 uncommented records with trailing whitespace; the three exact
field-width defects and source locations; 171 continuation-only tokens; 147
opcode/structural candidates; and zero unclassified physical records. It also
runs byte-exact and canonical emission over every file and reports deterministic
strict/compatibility compilation totals. `vitacheat_legacy_corpus --self-test`
is offline and is part of the ordinary host suite.

## Deliberately unsupported

Unknown extensions remain opaque. One unknown physical record makes the
complete descriptor unavailable for planning; a later scalar or pointer record
is never detached from that unsupported context.

The pinned public corpus regression is exhaustive for syntax preservation,
inventory, contextual physical ownership, and diagnostic classification. It
does not claim full-corpus execution compatibility: unknown extensions remain
opaque, compatibility forms require explicit opt-in, and no live-memory
adapter exists. Import, bounded planning, and callback evaluation do not
authorize real execution. A `.suprx` binary is never treated as data, `_V1`
intent never bypasses future arming policy, and no legacy absolute or
pointer-derived address is automatically trusted.
