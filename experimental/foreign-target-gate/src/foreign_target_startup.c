#include "vitacheat/foreign_target_startup.h"

static const uint8_t g_startup_build_id[
    VC_FTG_STARTUP_BUILD_ID_SIZE] =
        VC_FTG_STARTUP_BUILD_ID_BYTES;
static const uint8_t g_startup_title_id[
    VC_FTG_STARTUP_TITLE_ID_SIZE] =
        VC_FTG_STARTUP_TARGET_TITLE_BYTES;

static void startup_zero(void *value, size_t size)
{
    uint8_t *bytes = (uint8_t *)value;

    while (size != 0u) {
        *bytes++ = 0u;
        --size;
    }
}

static void startup_copy(
    uint8_t *destination,
    const uint8_t *source,
    size_t size)
{
    while (size != 0u) {
        *destination++ = *source++;
        --size;
    }
}

static bool startup_equal(
    const uint8_t *left,
    const uint8_t *right,
    size_t size)
{
    while (size != 0u) {
        if (*left++ != *right++) {
            return false;
        }
        --size;
    }
    return true;
}

static void startup_store_u32(
    uint8_t *output,
    uint32_t value)
{
    output[0] = (uint8_t)value;
    output[1] = (uint8_t)(value >> 8u);
    output[2] = (uint8_t)(value >> 16u);
    output[3] = (uint8_t)(value >> 24u);
}

static uint32_t startup_load_u32(const uint8_t *input)
{
    return (uint32_t)input[0] |
           ((uint32_t)input[1] << 8u) |
           ((uint32_t)input[2] << 16u) |
           ((uint32_t)input[3] << 24u);
}

static int32_t startup_load_i32(const uint8_t *input)
{
    const uint32_t value = startup_load_u32(input);

    if (value <= INT32_MAX) {
        return (int32_t)value;
    }
    return -INT32_C(1) -
           (int32_t)(UINT32_MAX - value);
}

static uint32_t startup_integrity(
    const uint8_t *bytes,
    size_t size)
{
    uint32_t value = UINT32_C(2166136261);

    while (size != 0u) {
        value ^= *bytes++;
        value *= UINT32_C(16777619);
        --size;
    }
    return value;
}

static uint32_t startup_expected_results(uint32_t stage)
{
    switch ((vc_ftg_startup_stage)stage) {
    case VC_FTG_STARTUP_STAGE_MAIN_ENTERED:
        return 0u;
    case VC_FTG_STARTUP_STAGE_SENTINEL_LOOKUP_COMPLETE:
        return VC_FTG_STARTUP_RESULT_SENTINEL_LOOKUP;
    case VC_FTG_STARTUP_STAGE_WRONG_CALLER_OPEN_COMPLETE:
        return VC_FTG_STARTUP_RESULT_SENTINEL_LOOKUP |
               VC_FTG_STARTUP_RESULT_WRONG_CALLER_OPEN;
    case VC_FTG_STARTUP_STAGE_LAYOUT_WRITE_COMPLETE:
        return VC_FTG_STARTUP_RESULT_SENTINEL_LOOKUP |
               VC_FTG_STARTUP_RESULT_WRONG_CALLER_OPEN |
               VC_FTG_STARTUP_RESULT_LAYOUT_WRITE;
    case VC_FTG_STARTUP_STAGE_FIXTURE_WRITE_COMPLETE:
    case VC_FTG_STARTUP_STAGE_PROMPT_READY:
    case VC_FTG_STARTUP_STAGE_CROSS_OBSERVED:
        return VC_FTG_STARTUP_RESULT_SENTINEL_LOOKUP |
               VC_FTG_STARTUP_RESULT_WRONG_CALLER_OPEN |
               VC_FTG_STARTUP_RESULT_LAYOUT_WRITE |
               VC_FTG_STARTUP_RESULT_FIXTURE_WRITE;
    case VC_FTG_STARTUP_STAGE_CONTROLLER_LAUNCH_COMPLETE:
        return VC_FTG_STARTUP_RESULT_SENTINEL_LOOKUP |
               VC_FTG_STARTUP_RESULT_WRONG_CALLER_OPEN |
               VC_FTG_STARTUP_RESULT_LAYOUT_WRITE |
               VC_FTG_STARTUP_RESULT_FIXTURE_WRITE |
               VC_FTG_STARTUP_RESULT_CONTROLLER_LAUNCH;
    default:
        return UINT32_MAX;
    }
}

static bool startup_fields_valid(
    const vc_ftg_startup_record *record)
{
    uint32_t index;
    const uint32_t expected =
        startup_expected_results(record->stage);

    if (record->magic != VC_FTG_STARTUP_MAGIC ||
        record->version != VC_FTG_STARTUP_VERSION ||
        record->struct_size != VC_FTG_STARTUP_RECORD_SIZE ||
        expected == UINT32_MAX ||
        record->completed_results != expected ||
        !startup_equal(
            record->build_id,
            g_startup_build_id,
            sizeof(record->build_id)) ||
        !startup_equal(
            record->title_id,
            g_startup_title_id,
            sizeof(record->title_id))) {
        return false;
    }
    for (index = 0u;
         index < sizeof(record->reserved) /
                     sizeof(record->reserved[0]);
         ++index) {
        if (record->reserved[index] != 0u) {
            return false;
        }
    }
    return true;
}

static void startup_encode_fields(
    const vc_ftg_startup_record *record,
    uint8_t output[VC_FTG_STARTUP_RECORD_SIZE])
{
    startup_zero(output, VC_FTG_STARTUP_RECORD_SIZE);
    startup_store_u32(output + 0u, record->magic);
    startup_store_u32(output + 4u, record->version);
    startup_store_u32(output + 8u, record->struct_size);
    startup_store_u32(output + 12u, record->stage);
    startup_copy(
        output + 16u,
        record->build_id,
        sizeof(record->build_id));
    startup_copy(
        output + 32u,
        record->title_id,
        sizeof(record->title_id));
    startup_store_u32(
        output + 44u, record->completed_results);
    startup_store_u32(
        output + 48u,
        (uint32_t)record->sentinel_lookup_result);
    startup_store_u32(
        output + 52u,
        (uint32_t)record->wrong_caller_open_result);
    startup_store_u32(
        output + 56u,
        (uint32_t)record->layout_write_result);
    startup_store_u32(
        output + 60u,
        (uint32_t)record->fixture_write_result);
    startup_store_u32(
        output + 64u,
        (uint32_t)record->controller_launch_result);
    startup_store_u32(output + 68u, record->reserved[0]);
    startup_store_u32(output + 72u, record->reserved[1]);
}

void vc_ftg_startup_record_init(
    vc_ftg_startup_record *record)
{
    if (record == NULL) {
        return;
    }
    startup_zero(record, sizeof(*record));
    record->magic = VC_FTG_STARTUP_MAGIC;
    record->version = VC_FTG_STARTUP_VERSION;
    record->struct_size = VC_FTG_STARTUP_RECORD_SIZE;
    record->stage = VC_FTG_STARTUP_STAGE_MAIN_ENTERED;
    startup_copy(
        record->build_id,
        g_startup_build_id,
        sizeof(record->build_id));
    startup_copy(
        record->title_id,
        g_startup_title_id,
        sizeof(record->title_id));
}

bool vc_ftg_startup_record_complete(
    vc_ftg_startup_record *record,
    vc_ftg_startup_stage stage,
    int32_t result)
{
    uint32_t result_flag;
    int32_t *result_field;

    if (record == NULL ||
        !startup_fields_valid(record) ||
        (uint32_t)stage != record->stage + 1u) {
        return false;
    }
    switch (stage) {
    case VC_FTG_STARTUP_STAGE_SENTINEL_LOOKUP_COMPLETE:
        result_flag = VC_FTG_STARTUP_RESULT_SENTINEL_LOOKUP;
        result_field = &record->sentinel_lookup_result;
        break;
    case VC_FTG_STARTUP_STAGE_WRONG_CALLER_OPEN_COMPLETE:
        result_flag = VC_FTG_STARTUP_RESULT_WRONG_CALLER_OPEN;
        result_field = &record->wrong_caller_open_result;
        break;
    case VC_FTG_STARTUP_STAGE_LAYOUT_WRITE_COMPLETE:
        result_flag = VC_FTG_STARTUP_RESULT_LAYOUT_WRITE;
        result_field = &record->layout_write_result;
        break;
    case VC_FTG_STARTUP_STAGE_FIXTURE_WRITE_COMPLETE:
        result_flag = VC_FTG_STARTUP_RESULT_FIXTURE_WRITE;
        result_field = &record->fixture_write_result;
        break;
    case VC_FTG_STARTUP_STAGE_CONTROLLER_LAUNCH_COMPLETE:
        result_flag =
            VC_FTG_STARTUP_RESULT_CONTROLLER_LAUNCH;
        result_field = &record->controller_launch_result;
        break;
    default:
        return false;
    }
    *result_field = result;
    record->completed_results |= result_flag;
    record->stage = (uint32_t)stage;
    record->integrity = 0u;
    return startup_fields_valid(record);
}

bool vc_ftg_startup_record_mark(
    vc_ftg_startup_record *record,
    vc_ftg_startup_stage stage)
{
    if (record == NULL ||
        !startup_fields_valid(record) ||
        (uint32_t)stage != record->stage + 1u ||
        (stage != VC_FTG_STARTUP_STAGE_PROMPT_READY &&
         stage != VC_FTG_STARTUP_STAGE_CROSS_OBSERVED)) {
        return false;
    }
    record->stage = (uint32_t)stage;
    record->integrity = 0u;
    return startup_fields_valid(record);
}

bool vc_ftg_startup_record_encode(
    vc_ftg_startup_record *record,
    uint8_t output[VC_FTG_STARTUP_RECORD_SIZE])
{
    if (record == NULL || output == NULL ||
        !startup_fields_valid(record)) {
        return false;
    }
    startup_encode_fields(record, output);
    record->integrity = startup_integrity(output, 76u);
    startup_store_u32(output + 76u, record->integrity);
    return true;
}

bool vc_ftg_startup_record_decode(
    const uint8_t *input,
    size_t input_size,
    vc_ftg_startup_record *record)
{
    vc_ftg_startup_record decoded;
    uint32_t expected_integrity;

    if (input == NULL || record == NULL ||
        input_size != VC_FTG_STARTUP_RECORD_SIZE) {
        return false;
    }
    startup_zero(&decoded, sizeof(decoded));
    decoded.magic = startup_load_u32(input + 0u);
    decoded.version = startup_load_u32(input + 4u);
    decoded.struct_size = startup_load_u32(input + 8u);
    decoded.stage = startup_load_u32(input + 12u);
    startup_copy(
        decoded.build_id,
        input + 16u,
        sizeof(decoded.build_id));
    startup_copy(
        decoded.title_id,
        input + 32u,
        sizeof(decoded.title_id));
    decoded.completed_results =
        startup_load_u32(input + 44u);
    decoded.sentinel_lookup_result =
        startup_load_i32(input + 48u);
    decoded.wrong_caller_open_result =
        startup_load_i32(input + 52u);
    decoded.layout_write_result =
        startup_load_i32(input + 56u);
    decoded.fixture_write_result =
        startup_load_i32(input + 60u);
    decoded.controller_launch_result =
        startup_load_i32(input + 64u);
    decoded.reserved[0] = startup_load_u32(input + 68u);
    decoded.reserved[1] = startup_load_u32(input + 72u);
    decoded.integrity = startup_load_u32(input + 76u);
    expected_integrity = startup_integrity(input, 76u);
    if (decoded.integrity != expected_integrity ||
        !startup_fields_valid(&decoded)) {
        startup_zero(&decoded, sizeof(decoded));
        return false;
    }
    *record = decoded;
    startup_zero(&decoded, sizeof(decoded));
    return true;
}

bool vc_ftg_startup_record_validate(
    const vc_ftg_startup_record *record)
{
    vc_ftg_startup_record encoded_record;
    uint8_t encoded[VC_FTG_STARTUP_RECORD_SIZE];
    const uint32_t integrity =
        record == NULL ? 0u : record->integrity;
    bool valid;

    if (record == NULL) {
        return false;
    }
    encoded_record = *record;
    valid = vc_ftg_startup_record_encode(
        &encoded_record, encoded) &&
        encoded_record.integrity == integrity;
    startup_zero(encoded, sizeof(encoded));
    startup_zero(&encoded_record, sizeof(encoded_record));
    return valid;
}

const char *vc_ftg_startup_stage_name(
    vc_ftg_startup_stage stage)
{
    switch (stage) {
    case VC_FTG_STARTUP_STAGE_MAIN_ENTERED:
        return "main-entered";
    case VC_FTG_STARTUP_STAGE_SENTINEL_LOOKUP_COMPLETE:
        return "sentinel-lookup-complete";
    case VC_FTG_STARTUP_STAGE_WRONG_CALLER_OPEN_COMPLETE:
        return "wrong-caller-open-complete";
    case VC_FTG_STARTUP_STAGE_LAYOUT_WRITE_COMPLETE:
        return "layout-write-complete";
    case VC_FTG_STARTUP_STAGE_FIXTURE_WRITE_COMPLETE:
        return "fixture-write-complete";
    case VC_FTG_STARTUP_STAGE_PROMPT_READY:
        return "prompt-ready";
    case VC_FTG_STARTUP_STAGE_CROSS_OBSERVED:
        return "cross-observed";
    case VC_FTG_STARTUP_STAGE_CONTROLLER_LAUNCH_COMPLETE:
        return "controller-launch-complete";
    default:
        return "invalid";
    }
}
