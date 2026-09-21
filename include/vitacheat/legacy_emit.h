#ifndef VITACHEAT_LEGACY_EMIT_H
#define VITACHEAT_LEGACY_EMIT_H

#include "vitacheat/legacy_plan.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VC_PSV_EMIT_SCHEMA_VERSION UINT32_C(1)

typedef struct vc_psv_canonical_report {
    uint32_t schema_version;
    size_t required_size;
    size_t written_size;
    size_t canonicalized_records;
    size_t preserved_records;
    size_t eligible_cheats;
    size_t blocked_cheats;
} vc_psv_canonical_report;

/*
 * Re-emits strict, semantically decoded descriptors with uppercase fixed-width
 * code fields. A descriptor with any blocking or compatibility diagnostic is
 * copied byte-for-byte instead. Comments, blank lines, descriptor text, opaque
 * lines, and original line endings are always preserved.
 *
 * A NULL output with zero capacity is a sizing query. OUTPUT_TOO_SMALL never
 * writes a partial document.
 */
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
    vc_psv_canonical_report *report);

#ifdef __cplusplus
}
#endif

#endif
