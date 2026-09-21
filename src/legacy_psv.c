#include "vitacheat/legacy_psv.h"

#include <stdbool.h>
#include <string.h>

#if !defined(UINTPTR_MAX)
#error "VitaCheat legacy parsing requires an implementation with uintptr_t"
#endif

typedef char vc_psv_uintptr_must_cover_size_t[
    UINTPTR_MAX >= SIZE_MAX ? 1 : -1];

#define VC_PSV_LEGACY_MAX_CHEATS ((size_t)50)
#define VC_PSV_LEGACY_MAX_CODES_PER_CHEAT ((size_t)200)
#define VC_PSV_LEGACY_MAX_DESCRIPTION_BYTES ((size_t)65)

static bool ascii_space(char value)
{
    return value == ' ' || value == '\t';
}

static bool multiply_size(size_t left, size_t right, size_t *result)
{
    if (left != 0 && right > SIZE_MAX / left) {
        return false;
    }
    *result = left * right;
    return true;
}

static bool ranges_overlap(const void *left,
                           size_t left_size,
                           const void *right,
                           size_t right_size)
{
    uintptr_t left_start;
    uintptr_t right_start;

    if (left == NULL || right == NULL || left_size == 0 || right_size == 0) {
        return false;
    }
    left_start = (uintptr_t)left;
    right_start = (uintptr_t)right;
    if (left_start > UINTPTR_MAX - left_size ||
        right_start > UINTPTR_MAX - right_size) {
        return true;
    }
    return left_start < right_start + right_size &&
           right_start < left_start + left_size;
}

static int hex_value(char value)
{
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    if (value >= 'A' && value <= 'F') {
        return value - 'A' + 10;
    }
    return -1;
}

static bool parse_hex_exact(const char *source, size_t offset, size_t digits, uint32_t *value)
{
    uint32_t parsed = 0;
    size_t index;

    for (index = 0; index < digits; ++index) {
        const int digit = hex_value(source[offset + index]);
        if (digit < 0) {
            return false;
        }
        parsed = (parsed << 4u) | (uint32_t)digit;
    }

    *value = parsed;
    return true;
}

static vc_psv_operation_kind operation_kind(uint16_t code, uint32_t *flags)
{
    switch (code) {
    case UINT16_C(0x0000):
        return VC_PSV_OPERATION_WRITE_U8;
    case UINT16_C(0x0100):
        return VC_PSV_OPERATION_WRITE_U16;
    case UINT16_C(0x0200):
        return VC_PSV_OPERATION_WRITE_U32;
    case UINT16_C(0x5000):
    case UINT16_C(0x5100):
    case UINT16_C(0x5200):
        return VC_PSV_OPERATION_MOVE;
    case UINT16_C(0x4001):
    case UINT16_C(0x4101):
    case UINT16_C(0x4201):
        return VC_PSV_OPERATION_REPEAT;
    case UINT16_C(0x4000):
    case UINT16_C(0x4002):
    case UINT16_C(0x4100):
    case UINT16_C(0x4200):
    case UINT16_C(0x4202):
    case UINT16_C(0x4210):
        *flags |= VC_PSV_OPERATION_FLAG_NONCANONICAL_REPEAT;
        return VC_PSV_OPERATION_REPEAT;
    case UINT16_C(0xa000):
        *flags |= VC_PSV_OPERATION_FLAG_NONCANONICAL_PATCH_U8;
        return VC_PSV_OPERATION_PATCH;
    case UINT16_C(0xa100):
    case UINT16_C(0xa200):
        return VC_PSV_OPERATION_PATCH;
    default:
        break;
    }

    if ((code & UINT16_C(0xff00)) == UINT16_C(0xb200)) {
        return VC_PSV_OPERATION_SELECT_MODULE_BASE;
    }
    if ((code & UINT16_C(0xff00)) == UINT16_C(0xc200)) {
        return VC_PSV_OPERATION_BUTTON_GATE;
    }
    if ((code & UINT16_C(0xf000)) == UINT16_C(0xd000) &&
        ((code >> 8u) & UINT16_C(0x000f)) <= UINT16_C(0x000b)) {
        return VC_PSV_OPERATION_CONDITION_GATE;
    }
    return VC_PSV_OPERATION_OPAQUE;
}

static bool operation_has_address(vc_psv_operation_kind kind)
{
    return kind == VC_PSV_OPERATION_WRITE_U8 ||
           kind == VC_PSV_OPERATION_WRITE_U16 ||
           kind == VC_PSV_OPERATION_WRITE_U32 ||
           kind == VC_PSV_OPERATION_MOVE ||
           kind == VC_PSV_OPERATION_REPEAT ||
           kind == VC_PSV_OPERATION_PATCH ||
           kind == VC_PSV_OPERATION_CONDITION_GATE;
}

static void trim_span(const char *source, size_t *begin, size_t *end)
{
    while (*begin < *end && ascii_space(source[*begin])) {
        ++*begin;
    }
    while (*end > *begin && ascii_space(source[*end - 1])) {
        --*end;
    }
}

static bool parse_code(const char *source,
                       size_t begin,
                       size_t end,
                       uint16_t *code,
                       uint32_t *address,
                       uint32_t *value,
                       uint32_t *flags)
{
    uint32_t parsed_code;

    if (end - begin < 1u + 4u + 1u + 8u + 1u + 8u || source[begin] != '$') {
        return false;
    }
    ++begin;

    if (end - begin < 4u || !parse_hex_exact(source, begin, 4, &parsed_code)) {
        return false;
    }
    begin += 4u;
    if (begin == end || !ascii_space(source[begin])) {
        return false;
    }
    while (begin < end && ascii_space(source[begin])) {
        ++begin;
    }

    if (end - begin < 8u || !parse_hex_exact(source, begin, 8, address)) {
        return false;
    }
    begin += 8u;
    if (begin == end || !ascii_space(source[begin])) {
        return false;
    }
    while (begin < end && ascii_space(source[begin])) {
        ++begin;
    }

    if (end - begin < 8u || !parse_hex_exact(source, begin, 8, value)) {
        return false;
    }
    begin += 8u;
    if (begin < end && !ascii_space(source[begin])) {
        return false;
    }
    while (begin < end && ascii_space(source[begin])) {
        ++begin;
    }
    if (begin < end && source[begin] == '#') {
        *flags |= VC_PSV_OPERATION_FLAG_INLINE_COMMENT;
        begin = end;
    }
    if (begin != end) {
        return false;
    }

    *code = (uint16_t)parsed_code;
    return true;
}

static void store_line(vc_psv_line *lines,
                       size_t capacity,
                       size_t index,
                       size_t raw_begin,
                       size_t raw_end,
                       size_t content_begin,
                       size_t content_end)
{
    if (index >= capacity) {
        return;
    }

    lines[index].raw.offset = (uint32_t)raw_begin;
    lines[index].raw.length = (uint32_t)(raw_end - raw_begin);
    lines[index].content.offset = (uint32_t)content_begin;
    lines[index].content.length = (uint32_t)(content_end - content_begin);
    lines[index].kind = VC_PSV_LINE_OPAQUE;
    lines[index].cheat_index = VC_PSV_NO_INDEX;
    lines[index].operation_index = VC_PSV_NO_INDEX;
}

static void taint_active_cheat(vc_psv_cheat *cheats,
                               size_t cheat_capacity,
                               size_t active_cheat,
                               vc_psv_translation_state state,
                               bool *active_cheat_tainted,
                               vc_psv_report *report)
{
    if (active_cheat == VC_PSV_NO_INDEX) {
        return;
    }
    if (!*active_cheat_tainted) {
        *active_cheat_tainted = true;
        ++report->non_direct_cheats;
    }
    if (active_cheat < cheat_capacity &&
        (int)state > (int)cheats[active_cheat].translation_state) {
        cheats[active_cheat].translation_state = state;
    }
}

static void finalize_active_cheat(vc_psv_cheat *cheats,
                                  size_t cheat_capacity,
                                  size_t active_cheat,
                                  bool repeat_continuation_pending,
                                  bool *active_cheat_tainted,
                                  vc_psv_report *report)
{
    if (active_cheat != VC_PSV_NO_INDEX &&
        repeat_continuation_pending) {
        ++report->invalid_operation_sequences;
        taint_active_cheat(cheats, cheat_capacity, active_cheat,
                           VC_PSV_TRANSLATION_MALFORMED,
                           active_cheat_tainted, report);
    }
}

static void discard_partial_outputs(vc_psv_line *lines,
                                    vc_psv_cheat *cheats,
                                    vc_psv_operation *operations,
                                    vc_psv_report *report)
{
    if (report->stored_lines != 0) {
        memset(lines, 0, report->stored_lines * sizeof(*lines));
    }
    if (report->stored_cheats != 0) {
        memset(cheats, 0, report->stored_cheats * sizeof(*cheats));
    }
    if (report->stored_operations != 0) {
        memset(operations, 0, report->stored_operations * sizeof(*operations));
    }
    report->stored_lines = 0;
    report->stored_cheats = 0;
    report->stored_operations = 0;
}

vc_psv_status vc_psv_parse(const char *source,
                           size_t source_size,
                           vc_psv_line *lines,
                           size_t line_capacity,
                           vc_psv_cheat *cheats,
                           size_t cheat_capacity,
                           vc_psv_operation *operations,
                           size_t operation_capacity,
                           vc_psv_report *report)
{
    size_t position = 0;
    size_t active_cheat = VC_PSV_NO_INDEX;
    size_t active_cheat_operation_count = 0;
    size_t line_bytes;
    size_t cheat_bytes;
    size_t operation_bytes;
    bool active_cheat_tainted = false;
    bool relative_base_active = false;
    bool repeat_continuation_pending = false;
    uint8_t relative_module_serial = 0;
    uint8_t relative_segment_index = 0;
    vc_psv_report result;

    if (report == NULL || (source == NULL && source_size != 0) ||
        (lines == NULL && line_capacity != 0) ||
        (cheats == NULL && cheat_capacity != 0) ||
        (operations == NULL && operation_capacity != 0)) {
        return VC_PSV_STATUS_INVALID_ARGUMENT;
    }
    if (source_size > (size_t)VC_PSV_MAX_SOURCE_BYTES) {
        return VC_PSV_STATUS_SOURCE_TOO_LARGE;
    }
    if (!multiply_size(line_capacity, sizeof(*lines), &line_bytes) ||
        !multiply_size(cheat_capacity, sizeof(*cheats), &cheat_bytes) ||
        !multiply_size(operation_capacity, sizeof(*operations), &operation_bytes)) {
        return VC_PSV_STATUS_INVALID_ARGUMENT;
    }
    if (ranges_overlap(source, source_size, lines, line_bytes) ||
        ranges_overlap(source, source_size, cheats, cheat_bytes) ||
        ranges_overlap(source, source_size, operations, operation_bytes) ||
        ranges_overlap(source, source_size, report, sizeof(*report)) ||
        ranges_overlap(lines, line_bytes, cheats, cheat_bytes) ||
        ranges_overlap(lines, line_bytes, operations, operation_bytes) ||
        ranges_overlap(lines, line_bytes, report, sizeof(*report)) ||
        ranges_overlap(cheats, cheat_bytes, operations, operation_bytes) ||
        ranges_overlap(cheats, cheat_bytes, report, sizeof(*report)) ||
        ranges_overlap(operations, operation_bytes, report, sizeof(*report))) {
        return VC_PSV_STATUS_INVALID_ARGUMENT;
    }

    memset(&result, 0, sizeof(result));
    result.schema_version = VC_PSV_IMPORT_SCHEMA_VERSION;
    result.source_size = source_size;

    while (position < source_size) {
        const size_t raw_begin = position;
        size_t content_begin;
        size_t content_end;
        size_t raw_end;
        size_t trimmed_begin;
        size_t trimmed_end;
        const size_t line_index = result.total_lines;
        vc_psv_line *stored_line = NULL;

        while (position < source_size && source[position] != '\r' && source[position] != '\n') {
            ++position;
        }
        content_begin = raw_begin;
        content_end = position;
        if (position < source_size && source[position] == '\r') {
            ++position;
            if (position < source_size && source[position] == '\n') {
                ++position;
            }
        } else if (position < source_size && source[position] == '\n') {
            ++position;
        }
        raw_end = position;

        if (line_index == 0 && content_end - content_begin >= 3u &&
            (unsigned char)source[content_begin] == 0xefu &&
            (unsigned char)source[content_begin + 1u] == 0xbbu &&
            (unsigned char)source[content_begin + 2u] == 0xbfu) {
            content_begin += 3u;
        }

        store_line(lines, line_capacity, line_index, raw_begin, raw_end,
                   content_begin, content_end);
        if (line_index < line_capacity) {
            stored_line = &lines[line_index];
            ++result.stored_lines;
        }
        ++result.total_lines;

        trimmed_begin = content_begin;
        trimmed_end = content_end;
        trim_span(source, &trimmed_begin, &trimmed_end);

        if (trimmed_begin == trimmed_end) {
            if (stored_line != NULL) {
                stored_line->kind = VC_PSV_LINE_BLANK;
            }
            continue;
        }
        if (source[trimmed_begin] == '#') {
            if (stored_line != NULL) {
                stored_line->kind = VC_PSV_LINE_COMMENT;
            }
            continue;
        }

        if (trimmed_end - trimmed_begin >= 3u && source[trimmed_begin] == '_' &&
            source[trimmed_begin + 1u] == 'V') {
            const char activation = source[trimmed_begin + 2u];
            size_t description_begin = trimmed_begin + 3u;
            const size_t cheat_index = result.total_cheats;

            finalize_active_cheat(cheats, cheat_capacity, active_cheat,
                                  repeat_continuation_pending,
                                  &active_cheat_tainted, &result);
            if ((activation != '0' && activation != '1') ||
                (description_begin < trimmed_end && !ascii_space(source[description_begin]))) {
                if (stored_line != NULL) {
                    stored_line->kind = VC_PSV_LINE_MALFORMED;
                }
                ++result.malformed_lines;
                taint_active_cheat(cheats, cheat_capacity, active_cheat,
                                   VC_PSV_TRANSLATION_MALFORMED,
                                   &active_cheat_tainted, &result);
                active_cheat = VC_PSV_NO_INDEX;
                active_cheat_operation_count = 0;
                active_cheat_tainted = false;
                relative_base_active = false;
                repeat_continuation_pending = false;
                relative_module_serial = 0;
                relative_segment_index = 0;
                continue;
            }
            while (description_begin < trimmed_end && ascii_space(source[description_begin])) {
                ++description_begin;
            }

            if (cheat_index < cheat_capacity) {
                cheats[cheat_index].activation = activation == '1'
                                                      ? VC_PSV_ACTIVATION_AUTOSTART
                                                      : VC_PSV_ACTIVATION_MANUAL;
                cheats[cheat_index].translation_state = VC_PSV_TRANSLATION_DIRECT_ONLY;
                cheats[cheat_index].description.offset = (uint32_t)description_begin;
                cheats[cheat_index].description.length =
                    (uint32_t)(trimmed_end - description_begin);
                cheats[cheat_index].header_line_index = line_index;
                cheats[cheat_index].first_operation_index = result.total_operations;
                cheats[cheat_index].operation_count = 0;
                ++result.stored_cheats;
            }
            if (stored_line != NULL) {
                stored_line->kind = VC_PSV_LINE_CHEAT_HEADER;
                stored_line->cheat_index = cheat_index;
            }
            if (trimmed_end - description_begin > VC_PSV_LEGACY_MAX_DESCRIPTION_BYTES) {
                ++result.legacy_limit_violations;
            }
            ++result.total_cheats;
            if (result.total_cheats == VC_PSV_LEGACY_MAX_CHEATS + 1u) {
                ++result.legacy_limit_violations;
            }
            active_cheat = cheat_index;
            active_cheat_operation_count = 0;
            active_cheat_tainted = false;
            relative_base_active = false;
            repeat_continuation_pending = false;
            relative_module_serial = 0;
            relative_segment_index = 0;
            continue;
        }

        if (source[trimmed_begin] == '$') {
            uint16_t code = 0;
            uint32_t address = 0;
            uint32_t value = 0;
            uint32_t flags = VC_PSV_OPERATION_FLAG_NONE;
            vc_psv_operation_kind kind;
            vc_psv_address_mode address_mode = VC_PSV_ADDRESS_NOT_APPLICABLE;
            const size_t operation_index = result.total_operations;

            if (active_cheat == VC_PSV_NO_INDEX ||
                !parse_code(source, trimmed_begin, trimmed_end, &code, &address,
                            &value, &flags)) {
                if (repeat_continuation_pending) {
                    ++result.invalid_operation_sequences;
                    repeat_continuation_pending = false;
                }
                if (stored_line != NULL) {
                    stored_line->kind = VC_PSV_LINE_MALFORMED;
                }
                ++result.malformed_lines;
                taint_active_cheat(cheats, cheat_capacity, active_cheat,
                                   VC_PSV_TRANSLATION_MALFORMED,
                                   &active_cheat_tainted, &result);
                continue;
            }

            if (repeat_continuation_pending) {
                kind = VC_PSV_OPERATION_REPEAT_CONTINUATION;
                repeat_continuation_pending = false;
            } else {
                kind = operation_kind(code, &flags);
            }
            if (kind == VC_PSV_OPERATION_SELECT_MODULE_BASE) {
                if (address > UINT32_C(1) || value != UINT32_C(0)) {
                    ++result.invalid_operation_sequences;
                    taint_active_cheat(cheats, cheat_capacity, active_cheat,
                                       VC_PSV_TRANSLATION_MALFORMED,
                                       &active_cheat_tainted, &result);
                } else {
                    relative_base_active = true;
                    relative_module_serial = (uint8_t)(code & UINT16_C(0x00ff));
                    relative_segment_index = (uint8_t)address;
                    taint_active_cheat(cheats, cheat_capacity, active_cheat,
                                       VC_PSV_TRANSLATION_MODULE_RELATIVE,
                                       &active_cheat_tainted, &result);
                }
            } else if (operation_has_address(kind)) {
                address_mode = relative_base_active
                                   ? VC_PSV_ADDRESS_SELECTED_MODULE_RELATIVE
                                   : VC_PSV_ADDRESS_ABSOLUTE;
            }
            if (kind == VC_PSV_OPERATION_REPEAT) {
                repeat_continuation_pending = true;
            }
            if (flags != VC_PSV_OPERATION_FLAG_NONE) {
                ++result.compatibility_operations;
                taint_active_cheat(cheats, cheat_capacity, active_cheat,
                                   VC_PSV_TRANSLATION_REQUIRES_COMPATIBILITY,
                                   &active_cheat_tainted, &result);
            }

            if (operation_index < operation_capacity) {
                operations[operation_index].kind = kind;
                operations[operation_index].address_mode = address_mode;
                operations[operation_index].legacy_code = code;
                operations[operation_index].address = address;
                operations[operation_index].value = value;
                operations[operation_index].flags = flags;
                operations[operation_index].module_serial =
                    address_mode == VC_PSV_ADDRESS_SELECTED_MODULE_RELATIVE
                        ? relative_module_serial
                        : 0;
                operations[operation_index].segment_index =
                    address_mode == VC_PSV_ADDRESS_SELECTED_MODULE_RELATIVE
                        ? relative_segment_index
                        : 0;
                operations[operation_index].cheat_index = active_cheat;
                operations[operation_index].line_index = line_index;
                ++result.stored_operations;
            }
            if (stored_line != NULL) {
                stored_line->kind = VC_PSV_LINE_CODE;
                stored_line->cheat_index = active_cheat;
                stored_line->operation_index = operation_index;
            }
            if (kind == VC_PSV_OPERATION_OPAQUE) {
                ++result.unsupported_operations;
                taint_active_cheat(cheats, cheat_capacity, active_cheat,
                                   VC_PSV_TRANSLATION_REQUIRES_UNSUPPORTED,
                                   &active_cheat_tainted, &result);
            }
            ++result.total_operations;
            ++active_cheat_operation_count;
            if (active_cheat < cheat_capacity) {
                ++cheats[active_cheat].operation_count;
            }
            if (active_cheat_operation_count ==
                VC_PSV_LEGACY_MAX_CODES_PER_CHEAT + 1u) {
                ++result.legacy_limit_violations;
            }
            continue;
        }

        if (stored_line != NULL) {
            stored_line->kind = VC_PSV_LINE_OPAQUE;
            stored_line->cheat_index = active_cheat;
        }
        taint_active_cheat(cheats, cheat_capacity, active_cheat,
                           VC_PSV_TRANSLATION_REQUIRES_UNSUPPORTED,
                           &active_cheat_tainted, &result);
    }

    finalize_active_cheat(cheats, cheat_capacity, active_cheat,
                          repeat_continuation_pending,
                          &active_cheat_tainted, &result);
    if (result.stored_lines != result.total_lines ||
        result.stored_cheats != result.total_cheats ||
        result.stored_operations != result.total_operations) {
        discard_partial_outputs(lines, cheats, operations, &result);
        *report = result;
        return VC_PSV_STATUS_TRUNCATED;
    }
    *report = result;
    return VC_PSV_STATUS_OK;
}
