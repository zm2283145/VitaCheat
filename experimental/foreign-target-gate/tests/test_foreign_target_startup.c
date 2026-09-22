#include "vitacheat/foreign_target_startup.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(expression)                                                        \
    do {                                                                         \
        if (!(expression)) {                                                     \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expression); \
            ++failures;                                                          \
        }                                                                        \
    } while (0)

static bool complete_to_prompt(
    vc_ftg_startup_record *record)
{
    return vc_ftg_startup_record_complete(
               record,
               VC_FTG_STARTUP_STAGE_SENTINEL_LOOKUP_COMPLETE,
               0) &&
           vc_ftg_startup_record_complete(
               record,
               VC_FTG_STARTUP_STAGE_WRONG_CALLER_OPEN_COMPLETE,
               -12) &&
           vc_ftg_startup_record_complete(
               record,
               VC_FTG_STARTUP_STAGE_LAYOUT_WRITE_COMPLETE,
               0) &&
           vc_ftg_startup_record_complete(
               record,
               VC_FTG_STARTUP_STAGE_FIXTURE_WRITE_COMPLETE,
               0) &&
           vc_ftg_startup_record_mark(
               record,
               VC_FTG_STARTUP_STAGE_PROMPT_READY);
}

static void check_round_trip(
    vc_ftg_startup_record *record)
{
    uint8_t encoded[VC_FTG_STARTUP_RECORD_SIZE];
    vc_ftg_startup_record decoded;

    memset(encoded, 0xa5, sizeof(encoded));
    memset(&decoded, 0xa5, sizeof(decoded));
    CHECK(vc_ftg_startup_record_encode(record, encoded));
    CHECK(vc_ftg_startup_record_validate(record));
    CHECK(vc_ftg_startup_record_decode(
        encoded, sizeof(encoded), &decoded));
    CHECK(memcmp(record, &decoded, sizeof(decoded)) == 0);
}

static void test_ordering_and_round_trip(void)
{
    vc_ftg_startup_record record;

    vc_ftg_startup_record_init(&record);
    CHECK(record.stage == VC_FTG_STARTUP_STAGE_MAIN_ENTERED);
    CHECK(record.completed_results == 0u);
    check_round_trip(&record);
    CHECK(vc_ftg_startup_record_complete(
        &record,
        VC_FTG_STARTUP_STAGE_SENTINEL_LOOKUP_COMPLETE,
        0));
    check_round_trip(&record);
    CHECK(vc_ftg_startup_record_complete(
        &record,
        VC_FTG_STARTUP_STAGE_WRONG_CALLER_OPEN_COMPLETE,
        -12));
    check_round_trip(&record);
    CHECK(vc_ftg_startup_record_complete(
        &record,
        VC_FTG_STARTUP_STAGE_LAYOUT_WRITE_COMPLETE,
        0));
    check_round_trip(&record);
    CHECK(vc_ftg_startup_record_complete(
        &record,
        VC_FTG_STARTUP_STAGE_FIXTURE_WRITE_COMPLETE,
        0));
    check_round_trip(&record);
    CHECK(vc_ftg_startup_record_mark(
        &record, VC_FTG_STARTUP_STAGE_PROMPT_READY));
    check_round_trip(&record);
    CHECK(vc_ftg_startup_record_mark(
        &record, VC_FTG_STARTUP_STAGE_CROSS_OBSERVED));
    check_round_trip(&record);
    CHECK(vc_ftg_startup_record_complete(
        &record,
        VC_FTG_STARTUP_STAGE_CONTROLLER_LAUNCH_COMPLETE,
        0));
    CHECK(record.completed_results == UINT32_C(0x1f));
    check_round_trip(&record);
}

static void test_out_of_order_and_malformed_rejected(void)
{
    vc_ftg_startup_record record;
    uint8_t encoded[VC_FTG_STARTUP_RECORD_SIZE];

    vc_ftg_startup_record_init(&record);
    CHECK(!vc_ftg_startup_record_mark(
        &record, VC_FTG_STARTUP_STAGE_PROMPT_READY));
    CHECK(!vc_ftg_startup_record_complete(
        &record,
        VC_FTG_STARTUP_STAGE_WRONG_CALLER_OPEN_COMPLETE,
        -12));
    CHECK(record.stage == VC_FTG_STARTUP_STAGE_MAIN_ENTERED);
    CHECK(vc_ftg_startup_record_encode(&record, encoded));

    record.completed_results = UINT32_C(0x1f);
    CHECK(!vc_ftg_startup_record_encode(&record, encoded));
    record.completed_results = 0u;
    record.reserved[1] = 1u;
    CHECK(!vc_ftg_startup_record_encode(&record, encoded));
    record.reserved[1] = 0u;
    record.title_id[0] ^= UINT8_C(1);
    CHECK(!vc_ftg_startup_record_encode(&record, encoded));
}

static void test_signed_result_round_trip(void)
{
    vc_ftg_startup_record record;

    vc_ftg_startup_record_init(&record);
    CHECK(vc_ftg_startup_record_complete(
        &record,
        VC_FTG_STARTUP_STAGE_SENTINEL_LOOKUP_COMPLETE,
        INT32_MIN));
    check_round_trip(&record);
    CHECK(vc_ftg_startup_record_complete(
        &record,
        VC_FTG_STARTUP_STAGE_WRONG_CALLER_OPEN_COMPLETE,
        INT32_MAX));
    check_round_trip(&record);
}

static void test_partial_and_corrupt_rejected(void)
{
    vc_ftg_startup_record record;
    vc_ftg_startup_record decoded;
    uint8_t encoded[VC_FTG_STARTUP_RECORD_SIZE];
    uint8_t corrupted[VC_FTG_STARTUP_RECORD_SIZE];
    size_t size;
    size_t index;

    vc_ftg_startup_record_init(&record);
    CHECK(complete_to_prompt(&record));
    CHECK(vc_ftg_startup_record_encode(&record, encoded));
    for (size = 0u; size < sizeof(encoded); ++size) {
        CHECK(!vc_ftg_startup_record_decode(
            encoded, size, &decoded));
    }
    for (index = 0u; index < sizeof(encoded); ++index) {
        memcpy(corrupted, encoded, sizeof(corrupted));
        corrupted[index] ^= UINT8_C(0x01);
        CHECK(!vc_ftg_startup_record_decode(
            corrupted, sizeof(corrupted), &decoded));
    }
}

static void test_prelaunch_cleanup_rejects_stale_record(void)
{
    vc_ftg_startup_record stale;
    vc_ftg_startup_record fresh;
    vc_ftg_startup_record decoded;
    uint8_t storage[VC_FTG_STARTUP_RECORD_SIZE];
    size_t stored_size = sizeof(storage);

    vc_ftg_startup_record_init(&stale);
    CHECK(complete_to_prompt(&stale));
    CHECK(vc_ftg_startup_record_encode(&stale, storage));
    CHECK(vc_ftg_startup_record_decode(
        storage, stored_size, &decoded));

    memset(storage, 0, sizeof(storage));
    stored_size = 0u;
    CHECK(!vc_ftg_startup_record_decode(
        storage, stored_size, &decoded));

    vc_ftg_startup_record_init(&fresh);
    CHECK(vc_ftg_startup_record_encode(&fresh, storage));
    stored_size = sizeof(storage);
    CHECK(vc_ftg_startup_record_decode(
        storage, stored_size, &decoded));
    CHECK(decoded.stage == VC_FTG_STARTUP_STAGE_MAIN_ENTERED);
    CHECK(decoded.stage != stale.stage);
}

static void test_no_input_timeout_remains_prompt_ready(void)
{
    vc_ftg_startup_record record;
    vc_ftg_startup_record observed;
    uint8_t encoded[VC_FTG_STARTUP_RECORD_SIZE];
    unsigned int attempt;

    vc_ftg_startup_record_init(&record);
    CHECK(complete_to_prompt(&record));
    CHECK(vc_ftg_startup_record_encode(&record, encoded));
    for (attempt = 0u; attempt < 50u; ++attempt) {
        CHECK(vc_ftg_startup_record_decode(
            encoded, sizeof(encoded), &observed));
        CHECK(observed.stage ==
              VC_FTG_STARTUP_STAGE_PROMPT_READY);
        CHECK((observed.completed_results &
               VC_FTG_STARTUP_RESULT_CONTROLLER_LAUNCH) == 0u);
    }
    CHECK(record.controller_launch_result == 0);
}

static void test_stage_names(void)
{
    CHECK(strcmp(
              vc_ftg_startup_stage_name(
                  VC_FTG_STARTUP_STAGE_MAIN_ENTERED),
              "main-entered") == 0);
    CHECK(strcmp(
              vc_ftg_startup_stage_name(
                  VC_FTG_STARTUP_STAGE_PROMPT_READY),
              "prompt-ready") == 0);
    CHECK(strcmp(
              vc_ftg_startup_stage_name(
                  VC_FTG_STARTUP_STAGE_CONTROLLER_LAUNCH_COMPLETE),
              "controller-launch-complete") == 0);
    CHECK(strcmp(
              vc_ftg_startup_stage_name(
                  VC_FTG_STARTUP_STAGE_INVALID),
              "invalid") == 0);
}

int main(void)
{
    test_ordering_and_round_trip();
    test_out_of_order_and_malformed_rejected();
    test_signed_result_round_trip();
    test_partial_and_corrupt_rejected();
    test_prelaunch_cleanup_rejects_stale_record();
    test_no_input_timeout_remains_prompt_ready();
    test_stage_names();

    if (failures != 0) {
        fprintf(
            stderr,
            "%d foreign-target startup test(s) failed\n",
            failures);
        return 1;
    }
    puts("foreign-target startup tests passed");
    return 0;
}
