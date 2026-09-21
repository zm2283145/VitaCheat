#include "vitacheat/legacy_emit.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define VC_PSV_CANONICAL_RECORD_BYTES ((size_t)23)

static bool add_size(size_t left, size_t right, size_t *result)
{
    if (right > SIZE_MAX - left) {
        return false;
    }
    *result = left + right;
    return true;
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

static bool strict_cheat_is_eligible(const vc_psv_cheat *cheat,
                                     const vc_psv_operation *operations,
                                     size_t operation_count)
{
    vc_psv_compile_options options;
    vc_psv_plan plan;
    vc_psv_compile_status status;

    options.compatibility = VC_PSV_COMPILE_STRICT;
    options.action_limit = VC_PSV_MAX_ACTION_LIMIT;
    status = vc_psv_compile_cheat(
        cheat, operations, operation_count, &options, NULL, 0, &plan);
    return status == VC_PSV_COMPILE_OK ||
           status == VC_PSV_COMPILE_OUTPUT_TOO_SMALL;
}

static bool valid_line_span(const vc_psv_line *line,
                            size_t source_size,
                            size_t expected_offset)
{
    const size_t raw_offset = (size_t)line->raw.offset;
    const size_t raw_length = (size_t)line->raw.length;
    const size_t content_offset = (size_t)line->content.offset;
    const size_t content_length = (size_t)line->content.length;

    return raw_offset == expected_offset &&
           raw_offset <= source_size &&
           raw_length <= source_size - raw_offset &&
           content_offset >= raw_offset &&
           content_offset <= raw_offset + raw_length &&
           content_length <= raw_offset + raw_length - content_offset;
}

static bool canonical_record_line(const vc_psv_line *line,
                                  const vc_psv_operation *operations,
                                  size_t operation_count,
                                  size_t line_index,
                                  bool active_cheat_eligible)
{
    return active_cheat_eligible &&
           line->kind == VC_PSV_LINE_CODE &&
           line->operation_index < operation_count &&
           operations[line->operation_index].cheat_index ==
               line->cheat_index &&
           operations[line->operation_index].line_index == line_index;
}

static vc_psv_emit_status emit_pass(
    const char *source,
    size_t source_size,
    const vc_psv_line *lines,
    size_t line_count,
    const vc_psv_cheat *cheats,
    size_t cheat_count,
    const vc_psv_operation *operations,
    size_t operation_count,
    char *output,
    bool write_output,
    vc_psv_canonical_report *report)
{
    size_t source_offset = 0;
    size_t output_offset = 0;
    size_t active_cheat = VC_PSV_NO_INDEX;
    bool active_cheat_eligible = false;
    size_t index;

    for (index = 0; index < line_count; ++index) {
        const vc_psv_line *line = &lines[index];
        const size_t raw_offset = (size_t)line->raw.offset;
        const size_t raw_length = (size_t)line->raw.length;
        const size_t content_offset = (size_t)line->content.offset;
        const size_t content_length = (size_t)line->content.length;
        const size_t content_end = content_offset + content_length;
        const size_t raw_end = raw_offset + raw_length;
        bool canonical_record;
        size_t emitted_length;

        if (!valid_line_span(line, source_size, source_offset)) {
            return VC_PSV_EMIT_INVALID_SPANS;
        }
        if (line->kind == VC_PSV_LINE_CHEAT_HEADER) {
            if (line->cheat_index >= cheat_count ||
                cheats[line->cheat_index].header_line_index != index) {
                return VC_PSV_EMIT_INVALID_SPANS;
            }
            active_cheat = line->cheat_index;
            active_cheat_eligible = strict_cheat_is_eligible(
                &cheats[active_cheat], operations, operation_count);
            if (!write_output) {
                if (active_cheat_eligible) {
                    ++report->eligible_cheats;
                } else {
                    ++report->blocked_cheats;
                }
            }
        }
        if (line->kind == VC_PSV_LINE_CODE) {
            if (active_cheat == VC_PSV_NO_INDEX ||
                line->cheat_index != active_cheat ||
                line->operation_index >= operation_count ||
                operations[line->operation_index].cheat_index !=
                    line->cheat_index ||
                operations[line->operation_index].line_index != index) {
                return VC_PSV_EMIT_INVALID_SPANS;
            }
        }
        canonical_record = canonical_record_line(
            line, operations, operation_count, index,
            active_cheat_eligible);
        emitted_length = canonical_record
                             ? VC_PSV_CANONICAL_RECORD_BYTES +
                                   (raw_end - content_end)
                             : raw_length;
        if (!add_size(output_offset, emitted_length, &output_offset)) {
            return VC_PSV_EMIT_INVALID_ARGUMENT;
        }
        if (!write_output) {
            if (line->kind == VC_PSV_LINE_CODE) {
                if (canonical_record) {
                    ++report->canonicalized_records;
                } else {
                    ++report->preserved_records;
                }
            }
        } else if (canonical_record) {
            const vc_psv_operation *operation =
                &operations[line->operation_index];
            char record[VC_PSV_CANONICAL_RECORD_BYTES + 1u];
            const int written = snprintf(
                record, sizeof(record), "$%04X %08X %08X",
                (unsigned int)operation->legacy_code,
                (unsigned int)operation->address,
                (unsigned int)operation->value);
            const size_t destination =
                output_offset - emitted_length;

            if (written != (int)VC_PSV_CANONICAL_RECORD_BYTES) {
                return VC_PSV_EMIT_INVALID_ARGUMENT;
            }
            memcpy(output + destination, record,
                   VC_PSV_CANONICAL_RECORD_BYTES);
            memcpy(output + destination + VC_PSV_CANONICAL_RECORD_BYTES,
                   source + content_end, raw_end - content_end);
        } else if (raw_length != 0) {
            memcpy(output + output_offset - raw_length,
                   source + raw_offset, raw_length);
        }
        source_offset = raw_end;
    }
    if (source_offset != source_size) {
        return VC_PSV_EMIT_INVALID_SPANS;
    }
    report->required_size = output_offset;
    if (write_output) {
        report->written_size = output_offset;
    }
    return VC_PSV_EMIT_OK;
}

vc_psv_emit_status vc_psv_emit_canonical(
    const char *source,
    size_t source_size,
    const vc_psv_line *lines,
    size_t line_count,
    const vc_psv_cheat *cheats,
    size_t cheat_count,
    const vc_psv_operation *operations,
    size_t operation_count,
    char *output,
    size_t output_capacity,
    vc_psv_canonical_report *report)
{
    vc_psv_canonical_report result;
    vc_psv_emit_status status;
    size_t line_bytes;
    size_t cheat_bytes;
    size_t operation_bytes;

    if (report == NULL ||
        (source == NULL && source_size != 0) ||
        (lines == NULL && line_count != 0) ||
        (cheats == NULL && cheat_count != 0) ||
        (operations == NULL && operation_count != 0) ||
        (output == NULL && output_capacity != 0)) {
        return VC_PSV_EMIT_INVALID_ARGUMENT;
    }
    if (!multiply_size(line_count, sizeof(*lines), &line_bytes) ||
        !multiply_size(cheat_count, sizeof(*cheats), &cheat_bytes) ||
        !multiply_size(operation_count, sizeof(*operations),
                       &operation_bytes) ||
        ranges_overlap(output, output_capacity, source, source_size) ||
        ranges_overlap(output, output_capacity, lines, line_bytes) ||
        ranges_overlap(output, output_capacity, cheats, cheat_bytes) ||
        ranges_overlap(output, output_capacity, operations,
                       operation_bytes) ||
        ranges_overlap(output, output_capacity, report,
                       sizeof(*report)) ||
        ranges_overlap(report, sizeof(*report), source, source_size) ||
        ranges_overlap(report, sizeof(*report), lines, line_bytes) ||
        ranges_overlap(report, sizeof(*report), cheats, cheat_bytes) ||
        ranges_overlap(report, sizeof(*report), operations,
                       operation_bytes)) {
        return VC_PSV_EMIT_INVALID_ARGUMENT;
    }
    memset(&result, 0, sizeof(result));
    result.schema_version = VC_PSV_EMIT_SCHEMA_VERSION;
    status = emit_pass(source, source_size, lines, line_count, cheats,
                       cheat_count, operations, operation_count, NULL,
                       false, &result);
    if (status != VC_PSV_EMIT_OK) {
        return status;
    }
    if (output_capacity < result.required_size) {
        *report = result;
        return VC_PSV_EMIT_OUTPUT_TOO_SMALL;
    }
    status = emit_pass(source, source_size, lines, line_count, cheats,
                       cheat_count, operations, operation_count, output,
                       true, &result);
    if (status != VC_PSV_EMIT_OK) {
        return status;
    }
    *report = result;
    return VC_PSV_EMIT_OK;
}
