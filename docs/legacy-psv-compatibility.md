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

## Lossless import schema 3

`vc_psv_parse` is allocation-free and accepts at most 1 MiB of caller-owned
source. With adequate output capacity, every line contains a raw span and the
spans cover the input contiguously. Concatenating them reconstructs the exact
input, including comments, unknown extensions, whitespace, a first-line UTF-8
BOM, and CRLF/LF/CR line endings.

The API is all-or-nothing. If any caller-owned output array is too small, it
returns required totals, clears partial records, reports zero stored records,
and returns `VC_PSV_STATUS_TRUNCATED`. Consumers never receive dangling
cross-indexes from an incomplete object graph.

Schema 3 keeps one imported operation per physical `$` record. A repeat header
owns its immediately following physical record, which is classified as
`VC_PSV_OPERATION_REPEAT_CONTINUATION` by position rather than by interpreting
its first field as a top-level opcode. Every imported record retains the raw
line span, 16-bit first field, both 32-bit fields, descriptor and line indexes,
and any explicit compatibility flags.

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

## Typed scalar planner

`vc_psv_compile_cheat` consumes one complete imported descriptor and emits
caller-owned `vc_psv_plan_node` records. Compilation is atomic and
allocation-free. Any opaque record, malformed sequence, unapproved
compatibility record, action-budget overflow, gate target beyond the
descriptor, or gate target inside a repeat span rejects the complete
descriptor.

The default symbolic action limit is 4,096 and the hard configurable limit is
65,536. Repeats remain one plan node; compilation sums their possible action
counts without expanding them. A count from `0x0000` through `0xFFFF` is
representable when the caller selects an adequate limit.

Strict compilation supports these non-pointer families:

| Legacy form | Typed meaning |
| --- | --- |
| `$0X00 A V` | U8/U16/U32 write of the low-width bits of `V` |
| `$5X00 D S` | Read U8/U16/U32 from `S`, then write those bytes to `D` |
| `$4X01 A V` + `$NNNN G I` | `N` writes at `A + k*G` with value `V + k*I` |
| `$A100 A V`, `$A200 A V` | Restorable little-endian U16/U32 code patch |
| `$C2NN 0000MMMM B` | Exact normalized-button-mask gate over `NN` records |
| `$DXNN A V`, X=0..B | Unsigned typed condition gate over `NN` records |
| `$B2MM S 00000000` | Select module `MM`, segment `S` for later addresses |

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
skip whose target would expose the continuation half of a repeat.

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

Strict mode is the default. Three observed forms can be decoded only when the
caller enables the corresponding compile option, and every accepted record is
reported as a compatibility diagnostic:

- inline `#` comments after an otherwise exact three-field record;
- repeat suffixes observed as `4000`, `4002`, `4100`, `4200`, `4202`, or
  `4210` instead of canonical `4X01`; and
- runtime-observed `$A000` restorable U8 patch behavior.

Field width remains exact. A 9-digit address or 7/6-digit value is malformed,
not padded or truncated. Top-level `0001`, `01F1`, `B000`, `C001`, `C007`, and
`C101` remain unknown. The importer preserves them, but no executable plan is
available for that descriptor.

## Deliberately unsupported

Pointer write/MOV/compression families 3, 8, and 7 remain opaque. One such
physical record makes the complete descriptor unavailable for execution; a
later scalar write is never detached from its unsupported pointer context.
Pointer traversal needs a separate bounded model, callback contract, and test
layer.

The audited public corpus baseline contains 676 files, 34,839 physical records,
and 318 distinct first-field tokens. This layer does not claim full-corpus
execution compatibility because pointer families and unknown extensions remain
opaque. A `.suprx` binary is never treated as data, `_V1` intent never bypasses
future arming policy, and no legacy absolute address is automatically trusted.
