#include "vitacheat/legacy_plan.h"

#include <stddef.h>
#include <stdint.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    vc_psv_line lines[64];
    vc_psv_cheat cheats[64];
    vc_psv_operation operations[64];
    vc_psv_plan_node nodes[64];
    vc_psv_report report;
    size_t cheat_index;

    if (size > (size_t)VC_PSV_MAX_SOURCE_BYTES) {
        return 0;
    }
    if (vc_psv_parse((const char *)data, size,
                     lines, 64, cheats, 64, operations, 64,
                     &report) != VC_PSV_STATUS_OK) {
        return 0;
    }
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
