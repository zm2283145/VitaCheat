#ifndef VITACHEAT_FOREIGN_TARGET_STARTUP_H
#define VITACHEAT_FOREIGN_TARGET_STARTUP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define VC_FTG_STARTUP_MAGIC UINT32_C(0x53544731)
#define VC_FTG_STARTUP_VERSION_V1 UINT32_C(1)
#define VC_FTG_STARTUP_VERSION_V2 UINT32_C(2)
#define VC_FTG_STARTUP_VERSION VC_FTG_STARTUP_VERSION_V2
#define VC_FTG_STARTUP_RECORD_SIZE 80u
#define VC_FTG_STARTUP_BUILD_ID_SIZE 16u
#define VC_FTG_STARTUP_TITLE_ID_SIZE 12u

#define VC_FTG_STARTUP_BUILD_ID_V1_BYTES                      \
    {                                                         \
        0x56, 0x43, 0x46, 0x47, 0x2d, 0x53, 0x54, 0x41,       \
        0x47, 0x45, 0x2d, 0x56, 0x31, 0x00, 0x00, 0x00        \
    }
#define VC_FTG_STARTUP_BUILD_ID_V2_BYTES                      \
    {                                                         \
        0x56, 0x43, 0x46, 0x47, 0x2d, 0x53, 0x54, 0x41,       \
        0x47, 0x45, 0x2d, 0x56, 0x32, 0x00, 0x00, 0x00        \
    }
#define VC_FTG_STARTUP_BUILD_ID_BYTES \
    VC_FTG_STARTUP_BUILD_ID_V2_BYTES
#define VC_FTG_STARTUP_TARGET_TITLE_BYTES                     \
    {                                                         \
        0x56, 0x43, 0x46, 0x54, 0x30, 0x30, 0x30, 0x30,       \
        0x31, 0x00, 0x00, 0x00                                \
    }

#define VC_FTG_READINESS_DELAY_US UINT32_C(20000)
#define VC_FTG_READINESS_DEADLINE_US UINT64_C(2000000)
#define VC_FTG_READINESS_MAX_ATTEMPTS UINT16_C(101)

#define VC_FTG_CONTROLLER_RESULT_SCHEMA \
    "vitacheat.foreign-target-gate.result.v4"
#define VC_FTG_CONTROLLER_RESULT_BUILD_ID "VCFG-RESULT-V4"

#define VC_FTG_STARTUP_RESULT_SENTINEL_LOOKUP UINT32_C(0x01)
#define VC_FTG_STARTUP_RESULT_WRONG_CALLER_OPEN UINT32_C(0x02)
#define VC_FTG_STARTUP_RESULT_LAYOUT_WRITE UINT32_C(0x04)
#define VC_FTG_STARTUP_RESULT_FIXTURE_WRITE UINT32_C(0x08)
#define VC_FTG_STARTUP_RESULT_CONTROLLER_LAUNCH UINT32_C(0x10)

#define VC_FTG_STARTUP_STATUS_SKIPPED INT32_C(-4601)
#define VC_FTG_STARTUP_STATUS_SENTINEL_NOT_FOUND INT32_C(-4602)
#define VC_FTG_STARTUP_STATUS_ZERO_WRITE INT32_C(-4603)
#define VC_FTG_STARTUP_STATUS_FORMAT_FAILED INT32_C(-4604)
#define VC_FTG_STARTUP_STATUS_READINESS_TIMEOUT INT32_C(-4605)
#define VC_FTG_STARTUP_STATUS_CLOCK_UNAVAILABLE INT32_C(-4606)
#define VC_FTG_STARTUP_STATUS_CLOCK_ROLLBACK INT32_C(-4607)
#define VC_FTG_STARTUP_STATUS_HANDLE_LEAK INT32_C(-4608)
#define VC_FTG_STARTUP_STATUS_DELAY_FAILED INT32_C(-4609)
#define VC_FTG_STARTUP_STATUS_UNEXPECTED_RESULT INT32_C(-4610)

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
    uint16_t readiness_attempt_count;
    uint16_t readiness_elapsed_ms;
    int32_t readiness_probe_result;
    uint32_t integrity;
} vc_ftg_startup_record;

_Static_assert(
    sizeof(vc_ftg_startup_record) == VC_FTG_STARTUP_RECORD_SIZE,
    "foreign-target startup record ABI changed");

typedef int32_t (*vc_ftg_readiness_open_fn)(
    void *context,
    uint64_t *handle);
typedef bool (*vc_ftg_readiness_time_fn)(
    void *context,
    uint64_t *now_us);
typedef bool (*vc_ftg_readiness_delay_fn)(
    void *context,
    uint32_t delay_us);

typedef struct vc_ftg_readiness_dependencies {
    void *context;
    vc_ftg_readiness_open_fn open_exact_fixture;
    vc_ftg_readiness_time_fn get_time_us;
    vc_ftg_readiness_delay_fn delay_us;
} vc_ftg_readiness_dependencies;

typedef struct vc_ftg_readiness_observation {
    uint16_t attempt_count;
    uint16_t elapsed_ms;
    int32_t last_result;
    int32_t probe_result;
} vc_ftg_readiness_observation;

typedef enum vc_ftg_result_observation_kind {
    VC_FTG_RESULT_OBSERVATION_STALE_PRIOR = 0,
    VC_FTG_RESULT_OBSERVATION_EMPTY = 1,
    VC_FTG_RESULT_OBSERVATION_CURRENT_PARTIAL = 2,
    VC_FTG_RESULT_OBSERVATION_CURRENT_COMPLETE = 3,
    VC_FTG_RESULT_OBSERVATION_CURRENT_MALFORMED = 4
} vc_ftg_result_observation_kind;

typedef enum vc_ftg_result_freshness_action {
    VC_FTG_RESULT_FRESHNESS_WAIT = 0,
    VC_FTG_RESULT_FRESHNESS_ACCEPT = 1,
    VC_FTG_RESULT_FRESHNESS_REJECT = 2
} vc_ftg_result_freshness_action;

typedef struct vc_ftg_result_freshness {
    uint64_t preserved_run_id;
    uint64_t current_run_id;
    bool saw_empty;
    bool accepted;
} vc_ftg_result_freshness;

void vc_ftg_startup_record_init(
    vc_ftg_startup_record *record);

bool vc_ftg_startup_record_complete(
    vc_ftg_startup_record *record,
    vc_ftg_startup_stage stage,
    int32_t result);

bool vc_ftg_startup_record_complete_readiness(
    vc_ftg_startup_record *record,
    const vc_ftg_readiness_observation *observation);

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

bool vc_ftg_probe_wrong_caller_readiness(
    const vc_ftg_readiness_dependencies *dependencies,
    vc_ftg_readiness_observation *observation);

void vc_ftg_result_freshness_init(
    vc_ftg_result_freshness *freshness,
    uint64_t preserved_run_id);

vc_ftg_result_freshness_action vc_ftg_result_freshness_observe(
    vc_ftg_result_freshness *freshness,
    vc_ftg_result_observation_kind kind,
    bool current_identity_valid,
    bool clear_result_passed,
    uint64_t run_id);

#endif
