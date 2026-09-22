#ifndef VITACHEAT_FOREIGN_TARGET_STARTUP_H
#define VITACHEAT_FOREIGN_TARGET_STARTUP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define VC_FTG_STARTUP_MAGIC UINT32_C(0x53544731)
#define VC_FTG_STARTUP_VERSION UINT32_C(1)
#define VC_FTG_STARTUP_RECORD_SIZE 80u
#define VC_FTG_STARTUP_BUILD_ID_SIZE 16u
#define VC_FTG_STARTUP_TITLE_ID_SIZE 12u

#define VC_FTG_STARTUP_BUILD_ID_BYTES                         \
    {                                                         \
        0x56, 0x43, 0x46, 0x47, 0x2d, 0x53, 0x54, 0x41,       \
        0x47, 0x45, 0x2d, 0x56, 0x31, 0x00, 0x00, 0x00        \
    }
#define VC_FTG_STARTUP_TARGET_TITLE_BYTES                     \
    {                                                         \
        0x56, 0x43, 0x46, 0x54, 0x30, 0x30, 0x30, 0x30,       \
        0x31, 0x00, 0x00, 0x00                                \
    }

#define VC_FTG_STARTUP_RESULT_SENTINEL_LOOKUP UINT32_C(0x01)
#define VC_FTG_STARTUP_RESULT_WRONG_CALLER_OPEN UINT32_C(0x02)
#define VC_FTG_STARTUP_RESULT_LAYOUT_WRITE UINT32_C(0x04)
#define VC_FTG_STARTUP_RESULT_FIXTURE_WRITE UINT32_C(0x08)
#define VC_FTG_STARTUP_RESULT_CONTROLLER_LAUNCH UINT32_C(0x10)

#define VC_FTG_STARTUP_STATUS_SKIPPED INT32_C(-4601)
#define VC_FTG_STARTUP_STATUS_SENTINEL_NOT_FOUND INT32_C(-4602)
#define VC_FTG_STARTUP_STATUS_ZERO_WRITE INT32_C(-4603)
#define VC_FTG_STARTUP_STATUS_FORMAT_FAILED INT32_C(-4604)

typedef enum vc_ftg_startup_stage {
    VC_FTG_STARTUP_STAGE_INVALID = 0,
    VC_FTG_STARTUP_STAGE_MAIN_ENTERED = 1,
    VC_FTG_STARTUP_STAGE_SENTINEL_LOOKUP_COMPLETE = 2,
    VC_FTG_STARTUP_STAGE_WRONG_CALLER_OPEN_COMPLETE = 3,
    VC_FTG_STARTUP_STAGE_LAYOUT_WRITE_COMPLETE = 4,
    VC_FTG_STARTUP_STAGE_FIXTURE_WRITE_COMPLETE = 5,
    VC_FTG_STARTUP_STAGE_PROMPT_READY = 6,
    VC_FTG_STARTUP_STAGE_CROSS_OBSERVED = 7,
    VC_FTG_STARTUP_STAGE_CONTROLLER_LAUNCH_COMPLETE = 8
} vc_ftg_startup_stage;

typedef struct vc_ftg_startup_record {
    uint32_t magic;
    uint32_t version;
    uint32_t struct_size;
    uint32_t stage;
    uint8_t build_id[VC_FTG_STARTUP_BUILD_ID_SIZE];
    uint8_t title_id[VC_FTG_STARTUP_TITLE_ID_SIZE];
    uint32_t completed_results;
    int32_t sentinel_lookup_result;
    int32_t wrong_caller_open_result;
    int32_t layout_write_result;
    int32_t fixture_write_result;
    int32_t controller_launch_result;
    uint32_t reserved[2];
    uint32_t integrity;
} vc_ftg_startup_record;

_Static_assert(
    sizeof(vc_ftg_startup_record) == VC_FTG_STARTUP_RECORD_SIZE,
    "foreign-target startup record ABI changed");

void vc_ftg_startup_record_init(
    vc_ftg_startup_record *record);

bool vc_ftg_startup_record_complete(
    vc_ftg_startup_record *record,
    vc_ftg_startup_stage stage,
    int32_t result);

bool vc_ftg_startup_record_mark(
    vc_ftg_startup_record *record,
    vc_ftg_startup_stage stage);

bool vc_ftg_startup_record_encode(
    vc_ftg_startup_record *record,
    uint8_t output[VC_FTG_STARTUP_RECORD_SIZE]);

bool vc_ftg_startup_record_decode(
    const uint8_t *input,
    size_t input_size,
    vc_ftg_startup_record *record);

bool vc_ftg_startup_record_validate(
    const vc_ftg_startup_record *record);

const char *vc_ftg_startup_stage_name(
    vc_ftg_startup_stage stage);

#endif
