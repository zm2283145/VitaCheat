#ifndef VITACHEAT_LEGACY_PSV_H
#define VITACHEAT_LEGACY_PSV_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VC_PSV_IMPORT_SCHEMA_VERSION UINT32_C(3)
#define VC_PSV_MAX_SOURCE_BYTES (UINT32_C(1024) * UINT32_C(1024))
#define VC_PSV_NO_INDEX SIZE_MAX

typedef enum vc_psv_status {
    VC_PSV_STATUS_OK = 0,
    VC_PSV_STATUS_TRUNCATED = 1,
    VC_PSV_STATUS_INVALID_ARGUMENT = -1,
    VC_PSV_STATUS_SOURCE_TOO_LARGE = -2
} vc_psv_status;

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
    VC_PSV_OPERATION_CONDITION_GATE
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
    VC_PSV_OPERATION_FLAG_NONCANONICAL_PATCH_U8 = 1u << 2
} vc_psv_operation_flags;

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

#ifdef __cplusplus
}
#endif

#endif
