#include "vitacheat/legacy_emit.h"

#include <stddef.h>
#include <stdint.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    vc_psv_line lines[64];
    vc_psv_cheat cheats[64];
    vc_psv_operation operations[64];
    vc_psv_plan_node nodes[64];
    vc_psv_report report;
    vc_psv_parse_options parse_options;
    vc_psv_canonical_report canonical;
    char output[4096];
    size_t required_size;
    size_t cheat_index;

    if (size > (size_t)VC_PSV_MAX_SOURCE_BYTES) {
        return 0;
    }
    parse_options.legacy_fallback = VC_PSV_LEGACY_ENCODING_ISO_8859_1;
    if (vc_psv_parse_with_options(
            (const char *)data, size, &parse_options, lines, 64, cheats,
            64, operations, 64, &report) != VC_PSV_STATUS_OK) {
        return 0;
    }
    (void)vc_psv_emit_lossless(
        (const char *)data, size, lines, report.total_lines, output,
        sizeof(output), &required_size);
    (void)vc_psv_emit_canonical(
        (const char *)data, size, lines, report.total_lines, cheats,
        report.total_cheats, operations, report.total_operations,
        output, sizeof(output), &canonical);
    for (cheat_index = 0;
         cheat_index < report.total_cheats;
         ++cheat_index) {
        vc_psv_plan plan;

        (void)vc_psv_compile_cheat(
            &cheats[cheat_index], operations, report.total_operations,
            NULL, nodes, 64, &plan);
    }
    return 0;
}
