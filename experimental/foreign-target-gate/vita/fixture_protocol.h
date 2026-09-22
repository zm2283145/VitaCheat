#ifndef VITACHEAT_FOREIGN_FIXTURE_PROTOCOL_H
#define VITACHEAT_FOREIGN_FIXTURE_PROTOCOL_H

#include <stdint.h>

#define VC_FTG_LAYOUT_PATH \
    "ux0:data/vitacheat-foreign-target-layout.bin"
#define VC_FTG_TARGET_RESULT_PATH \
    "ux0:data/vitacheat-foreign-target-fixture.json"
#define VC_FTG_CONTROLLER_RESULT_PATH \
    "ux0:data/vitacheat-foreign-target-result.json"
#define VC_FTG_LAYOUT_MAGIC UINT32_C(0x46544731)
#define VC_FTG_LAYOUT_VERSION UINT32_C(1)

#define VC_FTG_SENTINEL_BYTES                                      \
    {                                                              \
        0x56, 0x43, 0x46, 0x47, 0x2d, 0x46, 0x4f, 0x52,            \
        0x45, 0x49, 0x47, 0x4e, 0x2d, 0x52, 0x45, 0x41,            \
        0x44, 0x2d, 0x56, 0x31, 0x3a, 0x00, 0x01, 0x02,            \
        0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a,            \
        0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12,            \
        0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0x1a,            \
        0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20, 0x21, 0x22,            \
        0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a             \
    }

typedef struct vc_ftg_fixture_layout {
    uint32_t magic;
    uint32_t version;
    uint32_t struct_size;
    uint32_t segment_index;
    uint32_t segment_offset;
    uint32_t sentinel_size;
    int32_t wrong_caller_open_result;
    uint32_t reserved1;
} vc_ftg_fixture_layout;

_Static_assert(sizeof(vc_ftg_fixture_layout) == 32,
               "foreign fixture layout ABI changed");

#endif
