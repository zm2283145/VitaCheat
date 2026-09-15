# Legacy VitaCheat compatibility

The compatibility target is the data-oriented VitaCheat `.psv` format used by
the public community database. Binary, game-specific `.suprx` cheat plugins are
executable modules rather than declarative cheat files and are not converted or
loaded by this importer.

Primary references:

- [VitaCheat formatting](https://github.com/r0ah/vitacheat/wiki/Formatting)
- [VitaCheat code types](https://github.com/r0ah/vitacheat/wiki/Code-Types)
- [VitaCheat direct write](https://github.com/r0ah/vitacheat/wiki/Write)
- [VitaCheat B2 relative-base modifier](https://github.com/r0ah/vitacheat/wiki/B2-Code)
- [VitaCheat code header](https://github.com/r0ah/vitacheat/wiki/Code-Header)
- [VitaCheat cheat-plugin installation](https://github.com/r0ah/vitacheat/wiki/How-to-Use-Cheat-Plugins)
- [Ratchet & Clank PCSF00484 database example](https://github.com/r0ah/vitacheat/blob/master/db/PCSF00484.psv)

## What the importer guarantees now

`vc_psv_parse` is allocation-free and accepts at most 1 MiB of caller-owned
source. With adequate output capacity, every line contains a raw span and the
spans cover the input contiguously. Concatenating them reconstructs the exact
input, including comments, unknown extensions, whitespace, a first-line UTF-8
BOM, and CRLF/LF/CR line endings.

The API is all-or-nothing. If any caller-owned output array is too small, it
returns the required total counts, clears every partial output it wrote, reports
zero stored records, and returns `VC_PSV_STATUS_TRUNCATED`. Consumers therefore
never receive dangling cross-indexes from an incomplete object graph.

The parser indexes:

- `_V0` as manually activated and `_V1` as automatically activated;
- `$hhhh aaaaaaaa vvvvvvvv` as a syntactically valid legacy code record; and
- comments, blank lines, opaque extension lines, and malformed header/code
  attempts as distinct classes.

Only three code identifiers receive executable meaning in the import model:

| Legacy identifier | Imported meaning |
| --- | --- |
| `$0000` | direct 8-bit write |
| `$0100` | direct 16-bit write |
| `$0200` | direct 32-bit write |

Every other valid identifier is an opaque operation. It retains its exact raw
line and parsed numeric words but is not silently translated into a new action.
One opaque or malformed line taints the complete cheat entry, so a recognized
direct-write line following a base-address, condition, or pointer modifier can
never be detached from that context and executed by itself. Malformed records
are also retained and counted. This makes unsupported input inspectable and
round-trippable without making it dangerous.

The legacy limits documented by the database—65 description characters, 50
descriptions per file, and 200 codes per description—are reported as
compatibility diagnostics. Because the importer deliberately does not decode
legacy ANSI or UTF-8 text yet, it conservatively reports the description limit
in source bytes. The importer does not destroy data solely because a newer
authoring tool can represent more of it.

## What is deliberately not implemented

- No code is executed and no process memory is accessed.
- Pointer, condition, button, compression, MOV, ARM-write, and B2 semantics are
  not yet translated. Each family needs its own primary-source interpretation,
  hostile-input tests, execution bounds, and Vita hardware gate.
- A `.suprx` binary is never treated as a data file.
- Legacy absolute addresses are not automatically considered safe. Before a
  future write is offered, conversion must bind it to a verified title, module,
  executable build identity, and valid region.
- `_V1` is represented as legacy intent; it will not bypass the new tool's
  explicit arming and safety policy.

Ratchet & Clank Collection cheat files will be a real-world compatibility
fixture once a read-only Vita-side loader exists. The first hardware test will
parse and display entries without enabling writes.
