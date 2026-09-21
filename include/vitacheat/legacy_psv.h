#ifndef VITACHEAT_LEGACY_PSV_H
#define VITACHEAT_LEGACY_PSV_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VC_PSV_IMPORT_SCHEMA_VERSION UINT32_C(5)
#define VC_PSV_MAX_SOURCE_BYTES (UINT32_C(1024) * UINT32_C(1024))
#define VC_PSV_NO_INDEX SIZE_MAX

typedef enum vc_psv_status {
    VC_PSV_STATUS_OK = 0,
    VC_PSV_STATUS_TRUNCATED = 1,
    VC_PSV_STATUS_INVALID_ARGUMENT = -1,
    VC_PSV_STATUS_SOURCE_TOO_LARGE = -2,
    VC_PSV_STATUS_INVALID_ENCODING = -3
} vc_psv_status;

typedef enum vc_psv_legacy_encoding {
    VC_PSV_LEGACY_ENCODING_NONE = 0,
    VC_PSV_LEGACY_ENCODING_WINDOWS_1252,
    VC_PSV_LEGACY_ENCODING_ISO_8859_1
} vc_psv_legacy_encoding;

typedef enum vc_psv_source_encoding {
    VC_PSV_SOURCE_UTF8 = 0,
    VC_PSV_SOURCE_UTF8_BOM,
    VC_PSV_SOURCE_WINDOWS_1252,
    VC_PSV_SOURCE_ISO_8859_1
} vc_psv_source_encoding;

typedef struct vc_psv_parse_options {
    vc_psv_legacy_encoding legacy_fallback;
} vc_psv_parse_options;

typedef enum vc_psv_line_kind {
    VC_PSV_LINE_BLANK = 0,
    VC_PSV_LINE_COMMENT,
    VC_PSV_LINE_CHEAT_HEADER,
    VC_PSV_LINE_CODE,
    VC_PSV_LINE_OPAQUE,
    VC_PSV_LINE_MALFORMED
} vc_psv_line_kind;

typedef enum vc_psv_activation {
    VC_PSV_ACTIVATION_MANUAL = 0,
    VC_PSV_ACTIVATION_AUTOSTART = 1
} vc_psv_activation;

typedef enum vc_psv_operation_kind {
    VC_PSV_OPERATION_OPAQUE = 0,
    VC_PSV_OPERATION_WRITE_U8,
    VC_PSV_OPERATION_WRITE_U16,
    VC_PSV_OPERATION_WRITE_U32,
    VC_PSV_OPERATION_SELECT_MODULE_BASE,
    VC_PSV_OPERATION_MOVE,
    VC_PSV_OPERATION_REPEAT,
    VC_PSV_OPERATION_REPEAT_CONTINUATION,
    VC_PSV_OPERATION_PATCH,
    VC_PSV_OPERATION_BUTTON_GATE,
    VC_PSV_OPERATION_CONDITION_GATE,
    VC_PSV_OPERATION_POINTER_WRITE,
    VC_PSV_OPERATION_POINTER_REPEAT,
    VC_PSV_OPERATION_POINTER_MOVE,
    VC_PSV_OPERATION_POINTER_POSITIONAL
} vc_psv_operation_kind;

typedef enum vc_psv_translation_state {
    VC_PSV_TRANSLATION_DIRECT_ONLY = 0,
    VC_PSV_TRANSLATION_MODULE_RELATIVE,
    VC_PSV_TRANSLATION_REQUIRES_COMPATIBILITY,
    VC_PSV_TRANSLATION_REQUIRES_UNSUPPORTED,
    VC_PSV_TRANSLATION_MALFORMED
} vc_psv_translation_state;

typedef enum vc_psv_address_mode {
    VC_PSV_ADDRESS_NOT_APPLICABLE = 0,
    VC_PSV_ADDRESS_ABSOLUTE,
    VC_PSV_ADDRESS_SELECTED_MODULE_RELATIVE
} vc_psv_address_mode;

typedef enum vc_psv_operation_flags {
    VC_PSV_OPERATION_FLAG_NONE = 0,
    VC_PSV_OPERATION_FLAG_INLINE_COMMENT = 1u << 0,
    VC_PSV_OPERATION_FLAG_NONCANONICAL_REPEAT = 1u << 1,
    VC_PSV_OPERATION_FLAG_NONCANONICAL_PATCH_U8 = 1u << 2,
    VC_PSV_OPERATION_FLAG_LOWERCASE_HEX = 1u << 3
} vc_psv_operation_flags;

typedef enum vc_psv_record_role {
    VC_PSV_RECORD_NOT_APPLICABLE = 0,
    VC_PSV_RECORD_TOP_LEVEL,
    VC_PSV_RECORD_CONTINUATION,
    VC_PSV_RECORD_UNKNOWN
} vc_psv_record_role;

typedef enum vc_psv_diagnostic {
    VC_PSV_DIAGNOSTIC_NONE = 0,
    VC_PSV_DIAGNOSTIC_LEXICAL_ERROR = 1u << 0,
    VC_PSV_DIAGNOSTIC_TRUNCATED_SPAN = 1u << 1,
    VC_PSV_DIAGNOSTIC_UNSUPPORTED_OPCODE = 1u << 2,
    VC_PSV_DIAGNOSTIC_NONCANONICAL_HEADER = 1u << 3,
    VC_PSV_DIAGNOSTIC_NONCANONICAL_MARKER = 1u << 4,
    VC_PSV_DIAGNOSTIC_NONCANONICAL_SUFFIX = 1u << 5,
    VC_PSV_DIAGNOSTIC_NONCANONICAL_LEVEL = 1u << 6,
    VC_PSV_DIAGNOSTIC_NONCANONICAL_WIDTH = 1u << 7,
    VC_PSV_DIAGNOSTIC_INVALID_MODULE_SELECTOR = 1u << 8,
    VC_PSV_DIAGNOSTIC_AUTHORING_LIMIT = 1u << 9,
    VC_PSV_DIAGNOSTIC_RUNTIME_DEPENDENT_ADDRESS = 1u << 10,
    VC_PSV_DIAGNOSTIC_CANONICAL_EMISSION_BLOCKED = 1u << 11,
    VC_PSV_DIAGNOSTIC_LOWERCASE_HEX = 1u << 12,
    VC_PSV_DIAGNOSTIC_ORPHAN_RECORD = 1u << 13
} vc_psv_diagnostic;

#define VC_PSV_DIAGNOSTIC_BLOCKING_MASK \
    (VC_PSV_DIAGNOSTIC_LEXICAL_ERROR | \
     VC_PSV_DIAGNOSTIC_TRUNCATED_SPAN | \
     VC_PSV_DIAGNOSTIC_UNSUPPORTED_OPCODE | \
     VC_PSV_DIAGNOSTIC_INVALID_MODULE_SELECTOR | \
     VC_PSV_DIAGNOSTIC_CANONICAL_EMISSION_BLOCKED)

typedef struct vc_psv_span {
    uint32_t offset;
    uint32_t length;
} vc_psv_span;

/*
 * The raw span includes the original line ending. The content span excludes
 * it. With enough line capacity, raw spans are contiguous and reconstruct the
 * input byte-for-byte, including mixed CR/LF endings and a UTF-8 BOM.
 */
typedef struct vc_psv_line {
    vc_psv_span raw;
    vc_psv_span content;
    vc_psv_line_kind kind;
    vc_psv_record_role record_role;
    uint32_t diagnostics;
    size_t cheat_index;
    size_t operation_index;
} vc_psv_line;

typedef struct vc_psv_cheat {
    vc_psv_activation activation;
    vc_psv_translation_state translation_state;
    vc_psv_span description;
    size_t header_line_index;
    size_t first_operation_index;
    size_t operation_count;
    uint32_t diagnostics;
} vc_psv_cheat;

typedef struct vc_psv_operation {
    vc_psv_operation_kind kind;
    vc_psv_address_mode address_mode;
    uint16_t legacy_code;
    /*
     * Physical records retain their two parsed 32-bit fields in address/value.
     * For SELECT_MODULE_BASE, the opcode low byte is the module serial,
     * address is the segment (0 or 1), and value must be zero.
     */
    uint32_t address;
    uint32_t value;
    uint32_t flags;
    uint32_t diagnostics;
    vc_psv_record_role record_role;
    /*
     * Address-bearing records snapshot the active B2 selector. These fields
     * are meaningful only for SELECTED_MODULE_RELATIVE address mode.
     */
    uint8_t module_serial;
    uint8_t segment_index;
    size_t cheat_index;
    size_t line_index;
} vc_psv_operation;

typedef struct vc_psv_report {
    uint32_t schema_version;
    vc_psv_source_encoding source_encoding;
    size_t source_size;
    size_t total_lines;
    size_t stored_lines;
    size_t total_cheats;
    size_t stored_cheats;
    size_t total_operations;
    size_t stored_operations;
    size_t unsupported_operations;
    size_t compatibility_operations;
    size_t non_direct_cheats;
    size_t invalid_operation_sequences;
    size_t malformed_lines;
    size_t legacy_limit_violations;
    size_t physical_records;
    size_t top_level_records;
    size_t continuation_records;
    size_t unknown_records;
    size_t inline_comments;
    size_t trailing_whitespace_records;
    size_t noncanonical_headers;
    size_t lexical_errors;
    size_t truncated_spans;
} vc_psv_report;

/*
 * Parses legacy VitaCheat .psv text without allocating or modifying source.
 * The caller owns every output buffer. Unknown code types remain represented
 * as opaque operations and must never be executed as a guessed equivalent.
 * A syntactically bad line is retained as a raw span and counted as malformed.
 * Source, output arrays, and report must not overlap. Report contents are
 * defined only after OK or TRUNCATED is returned.
 */
vc_psv_status vc_psv_parse(const char *source,
                           size_t source_size,
                           vc_psv_line *lines,
                           size_t line_capacity,
                           vc_psv_cheat *cheats,
                           size_t cheat_capacity,
                           vc_psv_operation *operations,
                           size_t operation_capacity,
                           vc_psv_report *report);

/*
 * Validates UTF-8 (after an optional UTF-8 BOM) before tokenization. Invalid
 * UTF-8 is accepted only when the caller explicitly selects a legacy
 * single-byte fallback. Raw source spans always continue to reference the
 * caller's original bytes.
 */
vc_psv_status vc_psv_parse_with_options(
    const char *source,
    size_t source_size,
    const vc_psv_parse_options *options,
    vc_psv_line *lines,
    size_t line_capacity,
    vc_psv_cheat *cheats,
    size_t cheat_capacity,
    vc_psv_operation *operations,
    size_t operation_capacity,
    vc_psv_report *report);

typedef enum vc_psv_emit_status {
    VC_PSV_EMIT_OK = 0,
    VC_PSV_EMIT_INVALID_ARGUMENT = -1,
    VC_PSV_EMIT_OUTPUT_TOO_SMALL = -2,
    VC_PSV_EMIT_INVALID_SPANS = -3
} vc_psv_emit_status;

/*
 * Reconstructs a parsed source from its contiguous raw line spans. This is a
 * checked byte-for-byte round trip; no decoding, newline conversion, or repair
 * is performed.
 */
vc_psv_emit_status vc_psv_emit_lossless(
    const char *source,
    size_t source_size,
    const vc_psv_line *lines,
    size_t line_count,
    char *output,
    size_t output_capacity,
    size_t *required_size);

#ifdef __cplusplus
}
#endif

#endif
