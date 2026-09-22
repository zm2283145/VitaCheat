#include "vitacheat/foreign_target_controller.h"
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

typedef struct readiness_fake {
    int32_t results[4];
    uint64_t handles[4];
    size_t result_count;
    size_t result_index;
    int32_t default_result;
    uint64_t now_us;
    bool clock_available;
    bool delay_ok;
    bool rollback_after_open;
    uint64_t open_advance_us;
    uint64_t delay_overshoot_us;
} readiness_fake;

static int32_t fake_open(
    void *context,
    uint64_t *handle)
{
    readiness_fake *fake = (readiness_fake *)context;
    const size_t index = fake->result_index;

    if (index < fake->result_count) {
        *handle = fake->handles[index];
        ++fake->result_index;
        fake->now_us += fake->open_advance_us;
        return fake->results[index];
    }
    *handle = 0u;
    ++fake->result_index;
    fake->now_us += fake->open_advance_us;
    return fake->default_result;
}

static bool fake_time(
    void *context,
    uint64_t *now_us)
{
    readiness_fake *fake = (readiness_fake *)context;

    if (!fake->clock_available) {
        return false;
    }
    if (fake->rollback_after_open &&
        fake->result_index != 0u) {
        *now_us = fake->now_us - 1u;
    } else {
        *now_us = fake->now_us;
    }
    return true;
}

static bool fake_delay(
    void *context,
    uint32_t delay_us)
{
    readiness_fake *fake = (readiness_fake *)context;

    if (!fake->delay_ok) {
        return false;
    }
    fake->now_us +=
        (uint64_t)delay_us + fake->delay_overshoot_us;
    return true;
}

static vc_ftg_readiness_dependencies fake_dependencies(
    readiness_fake *fake)
{
    vc_ftg_readiness_dependencies dependencies;

    memset(&dependencies, 0, sizeof(dependencies));
    dependencies.context = fake;
    dependencies.open_exact_fixture = fake_open;
    dependencies.get_time_us = fake_time;
    dependencies.delay_us = fake_delay;
    return dependencies;
}

static vc_ftg_readiness_observation ready_observation(void)
{
    vc_ftg_readiness_observation observation;

    memset(&observation, 0, sizeof(observation));
    observation.attempt_count = 1u;
    observation.last_result = -12;
    return observation;
}

static bool complete_to_prompt(
    vc_ftg_startup_record *record)
{
    const vc_ftg_readiness_observation observation =
        ready_observation();

    return vc_ftg_startup_record_complete(
               record,
               VC_FTG_STARTUP_STAGE_SENTINEL_LOOKUP_COMPLETE,
               0) &&
           vc_ftg_startup_record_complete_readiness(
               record, &observation) &&
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
    {
        const vc_ftg_readiness_observation observation =
            ready_observation();

        CHECK(vc_ftg_startup_record_complete_readiness(
            &record, &observation));
    }
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
    record.readiness_elapsed_ms = 1u;
    CHECK(!vc_ftg_startup_record_encode(&record, encoded));
    record.readiness_elapsed_ms = 0u;
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
    {
        vc_ftg_readiness_observation observation;

        memset(&observation, 0, sizeof(observation));
        observation.attempt_count = 1u;
        observation.last_result = INT32_MAX;
        observation.probe_result = INT32_MAX;
        CHECK(vc_ftg_startup_record_complete_readiness(
            &record, &observation));
    }
    check_round_trip(&record);
}

static void test_v1_decode_remains_supported(void)
{
    static const uint8_t build_id[
        VC_FTG_STARTUP_BUILD_ID_SIZE] =
            VC_FTG_STARTUP_BUILD_ID_V1_BYTES;
    static const uint8_t title_id[
        VC_FTG_STARTUP_TITLE_ID_SIZE] =
            VC_FTG_STARTUP_TARGET_TITLE_BYTES;
    vc_ftg_startup_record record;
    vc_ftg_startup_record decoded;
    uint8_t encoded[VC_FTG_STARTUP_RECORD_SIZE];

    memset(&record, 0, sizeof(record));
    record.magic = VC_FTG_STARTUP_MAGIC;
    record.version = VC_FTG_STARTUP_VERSION_V1;
    record.struct_size = VC_FTG_STARTUP_RECORD_SIZE;
    record.stage = VC_FTG_STARTUP_STAGE_MAIN_ENTERED;
    memcpy(record.build_id, build_id, sizeof(build_id));
    memcpy(record.title_id, title_id, sizeof(title_id));
    CHECK(vc_ftg_startup_record_complete(
        &record,
        VC_FTG_STARTUP_STAGE_SENTINEL_LOOKUP_COMPLETE,
        0));
    CHECK(vc_ftg_startup_record_complete(
        &record,
        VC_FTG_STARTUP_STAGE_WRONG_CALLER_OPEN_COMPLETE,
        -13));
    CHECK(vc_ftg_startup_record_complete(
        &record,
        VC_FTG_STARTUP_STAGE_LAYOUT_WRITE_COMPLETE,
        0));
    CHECK(vc_ftg_startup_record_complete(
        &record,
        VC_FTG_STARTUP_STAGE_FIXTURE_WRITE_COMPLETE,
        0));
    CHECK(vc_ftg_startup_record_mark(
        &record, VC_FTG_STARTUP_STAGE_PROMPT_READY));
    CHECK(vc_ftg_startup_record_encode(&record, encoded));
    CHECK(vc_ftg_startup_record_decode(
        encoded, sizeof(encoded), &decoded));
    CHECK(decoded.version == VC_FTG_STARTUP_VERSION_V1);
    CHECK(decoded.stage == VC_FTG_STARTUP_STAGE_PROMPT_READY);
    CHECK(decoded.wrong_caller_open_result == -13);
    CHECK(decoded.readiness_attempt_count == 0u);
    CHECK(decoded.readiness_elapsed_ms == 0u);
    CHECK(decoded.readiness_probe_result == 0);
}

static void test_delayed_readiness_transition(void)
{
    readiness_fake fake;
    vc_ftg_readiness_dependencies dependencies;
    vc_ftg_readiness_observation observation;

    memset(&fake, 0, sizeof(fake));
    fake.results[0] = -13;
    fake.results[1] = -13;
    fake.results[2] = -12;
    fake.result_count = 3u;
    fake.default_result = -13;
    fake.now_us = UINT64_C(1000000);
    fake.clock_available = true;
    fake.delay_ok = true;
    dependencies = fake_dependencies(&fake);
    CHECK(vc_ftg_probe_wrong_caller_readiness(
        &dependencies, &observation));
    CHECK(observation.attempt_count == 3u);
    CHECK(observation.elapsed_ms == 40u);
    CHECK(observation.last_result == -12);
    CHECK(observation.probe_result == 0);
}

static void test_permanent_unavailable_times_out(void)
{
    readiness_fake fake;
    vc_ftg_readiness_dependencies dependencies;
    vc_ftg_readiness_observation observation;
    vc_ftg_startup_record record;

    memset(&fake, 0, sizeof(fake));
    fake.default_result = -13;
    fake.now_us = UINT64_C(1000000);
    fake.clock_available = true;
    fake.delay_ok = true;
    dependencies = fake_dependencies(&fake);
    CHECK(!vc_ftg_probe_wrong_caller_readiness(
        &dependencies, &observation));
    CHECK(observation.attempt_count ==
          VC_FTG_READINESS_MAX_ATTEMPTS);
    CHECK(observation.elapsed_ms == 2000u);
    CHECK(observation.last_result == -13);
    CHECK(observation.probe_result ==
          VC_FTG_STARTUP_STATUS_READINESS_TIMEOUT);

    vc_ftg_startup_record_init(&record);
    CHECK(vc_ftg_startup_record_complete(
        &record,
        VC_FTG_STARTUP_STAGE_SENTINEL_LOOKUP_COMPLETE,
        0));
    CHECK(vc_ftg_startup_record_complete_readiness(
        &record, &observation));
    CHECK(!vc_ftg_startup_record_complete(
        &record,
        VC_FTG_STARTUP_STAGE_LAYOUT_WRITE_COMPLETE,
        0));
    CHECK(!vc_ftg_startup_record_mark(
        &record, VC_FTG_STARTUP_STAGE_PROMPT_READY));
}

static void test_delay_overshoot_blocks_late_retry(void)
{
    readiness_fake fake;
    vc_ftg_readiness_dependencies dependencies;
    vc_ftg_readiness_observation observation;

    memset(&fake, 0, sizeof(fake));
    fake.results[0] = -13;
    fake.results[1] = -12;
    fake.result_count = 2u;
    fake.default_result = -13;
    fake.now_us = UINT64_C(1000000);
    fake.clock_available = true;
    fake.delay_ok = true;
    fake.delay_overshoot_us =
        VC_FTG_READINESS_DEADLINE_US;
    dependencies = fake_dependencies(&fake);
    CHECK(!vc_ftg_probe_wrong_caller_readiness(
        &dependencies, &observation));
    CHECK(fake.result_index == 1u);
    CHECK(observation.attempt_count == 1u);
    CHECK(observation.elapsed_ms == 2020u);
    CHECK(observation.last_result == -13);
    CHECK(observation.probe_result ==
          VC_FTG_STARTUP_STATUS_READINESS_TIMEOUT);
}

static void test_unexpected_and_leaked_handle_fail(void)
{
    readiness_fake fake;
    vc_ftg_readiness_dependencies dependencies;
    vc_ftg_readiness_observation observation;

    memset(&fake, 0, sizeof(fake));
    fake.results[0] = -16;
    fake.result_count = 1u;
    fake.default_result = -13;
    fake.now_us = UINT64_C(1000000);
    fake.clock_available = true;
    fake.delay_ok = true;
    dependencies = fake_dependencies(&fake);
    CHECK(!vc_ftg_probe_wrong_caller_readiness(
        &dependencies, &observation));
    CHECK(observation.attempt_count == 1u);
    CHECK(observation.last_result == -16);
    CHECK(observation.probe_result ==
          VC_FTG_STARTUP_STATUS_UNEXPECTED_RESULT);

    memset(&fake, 0, sizeof(fake));
    fake.results[0] = -12;
    fake.handles[0] = UINT64_C(0x1234);
    fake.result_count = 1u;
    fake.default_result = -13;
    fake.now_us = UINT64_C(1000000);
    fake.clock_available = true;
    fake.delay_ok = true;
    dependencies = fake_dependencies(&fake);
    CHECK(!vc_ftg_probe_wrong_caller_readiness(
        &dependencies, &observation));
    CHECK(observation.last_result == -12);
    CHECK(observation.probe_result ==
          VC_FTG_STARTUP_STATUS_HANDLE_LEAK);
}

static void test_readiness_clock_rollback_fails(void)
{
    readiness_fake fake;
    vc_ftg_readiness_dependencies dependencies;
    vc_ftg_readiness_observation observation;

    memset(&fake, 0, sizeof(fake));
    fake.results[0] = -13;
    fake.result_count = 1u;
    fake.default_result = -13;
    fake.now_us = UINT64_C(1000000);
    fake.clock_available = true;
    fake.delay_ok = true;
    fake.rollback_after_open = true;
    dependencies = fake_dependencies(&fake);
    CHECK(!vc_ftg_probe_wrong_caller_readiness(
        &dependencies, &observation));
    CHECK(observation.probe_result ==
          VC_FTG_STARTUP_STATUS_CLOCK_ROLLBACK);

    memset(&fake, 0, sizeof(fake));
    fake.results[0] = -12;
    fake.result_count = 1u;
    fake.default_result = -13;
    fake.now_us = UINT64_C(1000000);
    fake.clock_available = true;
    fake.delay_ok = true;
    fake.open_advance_us =
        VC_FTG_READINESS_DEADLINE_US + UINT64_C(1);
    dependencies = fake_dependencies(&fake);
    CHECK(!vc_ftg_probe_wrong_caller_readiness(
        &dependencies, &observation));
    CHECK(observation.last_result == -12);
    CHECK(observation.probe_result ==
          VC_FTG_STARTUP_STATUS_READINESS_TIMEOUT);
    {
        vc_ftg_startup_record record;

        vc_ftg_startup_record_init(&record);
        CHECK(vc_ftg_startup_record_complete(
            &record,
            VC_FTG_STARTUP_STAGE_SENTINEL_LOOKUP_COMPLETE,
            0));
        CHECK(vc_ftg_startup_record_complete_readiness(
            &record, &observation));
        CHECK(record.stage ==
              VC_FTG_STARTUP_STAGE_WRONG_CALLER_OPEN_COMPLETE);
    }
}

static void test_result_freshness_policy(void)
{
    vc_ftg_result_freshness freshness;

    vc_ftg_result_freshness_init(&freshness, 0u);
    CHECK(vc_ftg_result_freshness_observe(
              &freshness,
              VC_FTG_RESULT_OBSERVATION_STALE_PRIOR,
              false,
              false,
              0u) == VC_FTG_RESULT_FRESHNESS_WAIT);
    CHECK(vc_ftg_result_freshness_observe(
              &freshness,
              VC_FTG_RESULT_OBSERVATION_EMPTY,
              false,
              false,
              0u) == VC_FTG_RESULT_FRESHNESS_WAIT);
    CHECK(vc_ftg_result_freshness_observe(
              &freshness,
              VC_FTG_RESULT_OBSERVATION_CURRENT_PARTIAL,
              true,
              false,
              UINT64_C(0x22)) ==
          VC_FTG_RESULT_FRESHNESS_WAIT);
    CHECK(vc_ftg_result_freshness_observe(
              &freshness,
              VC_FTG_RESULT_OBSERVATION_CURRENT_COMPLETE,
              true,
              true,
              UINT64_C(0x22)) ==
          VC_FTG_RESULT_FRESHNESS_ACCEPT);
    CHECK(freshness.saw_empty);
    CHECK(freshness.current_run_id == UINT64_C(0x22));
    CHECK(vc_ftg_result_freshness_observe(
              &freshness,
              VC_FTG_RESULT_OBSERVATION_CURRENT_PARTIAL,
              true,
              true,
              UINT64_C(0x22)) ==
          VC_FTG_RESULT_FRESHNESS_WAIT);
    CHECK(vc_ftg_result_freshness_observe(
              &freshness,
              VC_FTG_RESULT_OBSERVATION_CURRENT_COMPLETE,
              true,
              true,
              UINT64_C(0x22)) ==
          VC_FTG_RESULT_FRESHNESS_ACCEPT);

    vc_ftg_result_freshness_init(
        &freshness, UINT64_C(0x44));
    CHECK(vc_ftg_result_freshness_observe(
              &freshness,
              VC_FTG_RESULT_OBSERVATION_CURRENT_COMPLETE,
              true,
              true,
              UINT64_C(0x45)) ==
          VC_FTG_RESULT_FRESHNESS_ACCEPT);

    vc_ftg_result_freshness_init(
        &freshness, UINT64_C(0x44));
    CHECK(vc_ftg_result_freshness_observe(
              &freshness,
              VC_FTG_RESULT_OBSERVATION_CURRENT_COMPLETE,
              true,
              true,
              UINT64_C(0x44)) ==
          VC_FTG_RESULT_FRESHNESS_REJECT);
    vc_ftg_result_freshness_init(&freshness, 0u);
    CHECK(vc_ftg_result_freshness_observe(
              &freshness,
              VC_FTG_RESULT_OBSERVATION_CURRENT_COMPLETE,
              true,
              false,
              UINT64_C(0x45)) ==
          VC_FTG_RESULT_FRESHNESS_REJECT);
    vc_ftg_result_freshness_init(&freshness, 0u);
    CHECK(vc_ftg_result_freshness_observe(
              &freshness,
              VC_FTG_RESULT_OBSERVATION_CURRENT_MALFORMED,
              true,
              true,
              UINT64_C(0x45)) ==
          VC_FTG_RESULT_FRESHNESS_REJECT);
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

static vc_ftg_controller_counts controller_baseline(void)
{
    vc_ftg_controller_counts counts;

    memset(&counts, 0, sizeof(counts));
    counts.create_callback_count = 3u;
    counts.start_callback_count = 7u;
    return counts;
}

static vc_ftg_controller_counts controller_generation(
    vc_ftg_controller_counts baseline,
    uint32_t revalidations)
{
    ++baseline.create_callback_count;
    baseline.start_callback_count += revalidations;
    baseline.start_revalidation_count += revalidations;
    ++baseline.target_create_match_count;
    ++baseline.target_create_authorized_count;
    return baseline;
}

static void test_controller_generation_profiles(void)
{
    const vc_ftg_controller_counts baseline =
        controller_baseline();
    vc_ftg_controller_counts current =
        controller_generation(baseline, 0u);

    CHECK(vc_ftg_controller_generation_profile_valid(
        &current, &baseline));
    current = controller_generation(
        baseline, VC_FTG_CONTROLLER_MIN_REVALIDATIONS);
    CHECK(vc_ftg_controller_generation_profile_valid(
        &current, &baseline));
    current = controller_generation(
        baseline,
        VC_FTG_CONTROLLER_MIN_REVALIDATIONS - 1u);
    CHECK(!vc_ftg_controller_generation_profile_valid(
        &current, &baseline));
    current = controller_generation(baseline, 0u);
    --current.create_callback_count;
    CHECK(!vc_ftg_controller_generation_profile_valid(
        &current, &baseline));
    current = controller_generation(baseline, 0u);
    ++current.target_create_match_count;
    CHECK(!vc_ftg_controller_generation_profile_valid(
        &current, &baseline));
    current = controller_generation(baseline, 0u);
    ++current.start_callback_count;
    CHECK(!vc_ftg_controller_generation_profile_valid(
        &current, &baseline));
    current = controller_generation(
        baseline, VC_FTG_CONTROLLER_MIN_REVALIDATIONS);
    --current.start_callback_count;
    CHECK(!vc_ftg_controller_generation_profile_valid(
        &current, &baseline));
}

static void test_controller_checkpoint_round_trip(void)
{
    const vc_ftg_controller_counts baseline =
        controller_baseline();
    const vc_ftg_controller_counts exit_counts =
        controller_generation(baseline, 0u);
    vc_ftg_controller_checkpoint checkpoint;
    vc_ftg_controller_checkpoint decoded;
    uint8_t encoded[VC_FTG_CONTROLLER_CHECKPOINT_SIZE];
    uint8_t corrupted[VC_FTG_CONTROLLER_CHECKPOINT_SIZE];
    uint8_t oversized[VC_FTG_CONTROLLER_CHECKPOINT_SIZE + 1u];
    size_t size;
    size_t index;

    CHECK(vc_ftg_controller_checkpoint_init_generation_1(
        &checkpoint,
        UINT64_C(0x100),
        UINT64_C(0x100),
        &baseline));
    CHECK(checkpoint.phase ==
          VC_FTG_CONTROLLER_PHASE_LAUNCH_GENERATION_1);
    CHECK(vc_ftg_controller_checkpoint_encode(
        &checkpoint, encoded));
    CHECK(vc_ftg_controller_checkpoint_validate(
        &checkpoint));
    CHECK(vc_ftg_controller_checkpoint_decode(
        encoded, sizeof(encoded), &decoded));
    CHECK(memcmp(
        &checkpoint, &decoded, sizeof(decoded)) == 0);
    CHECK(vc_ftg_controller_checkpoint_observe(
              NULL, 0u, &decoded) ==
          VC_FTG_CONTROLLER_CHECKPOINT_EMPTY);
    CHECK(vc_ftg_controller_checkpoint_observe(
              encoded, sizeof(encoded), &decoded) ==
          VC_FTG_CONTROLLER_CHECKPOINT_VALID);
    CHECK(vc_ftg_controller_checkpoint_observe(
              NULL, sizeof(encoded), &decoded) ==
          VC_FTG_CONTROLLER_CHECKPOINT_INVALID);
    memcpy(oversized, encoded, sizeof(encoded));
    oversized[sizeof(encoded)] = 0u;
    CHECK(vc_ftg_controller_checkpoint_observe(
              oversized, sizeof(oversized), &decoded) ==
          VC_FTG_CONTROLLER_CHECKPOINT_INVALID);
    for (size = 0u; size < sizeof(encoded); ++size) {
        CHECK(!vc_ftg_controller_checkpoint_decode(
            encoded, size, &decoded));
        if (size != 0u) {
            CHECK(vc_ftg_controller_checkpoint_observe(
                      encoded, size, &decoded) ==
                  VC_FTG_CONTROLLER_CHECKPOINT_INVALID);
        }
    }
    for (index = 0u; index < sizeof(encoded); ++index) {
        memcpy(corrupted, encoded, sizeof(corrupted));
        corrupted[index] ^= UINT8_C(1);
        CHECK(!vc_ftg_controller_checkpoint_decode(
            corrupted, sizeof(corrupted), &decoded));
        CHECK(vc_ftg_controller_checkpoint_observe(
                  corrupted, sizeof(corrupted), &decoded) ==
              VC_FTG_CONTROLLER_CHECKPOINT_INVALID);
    }
    CHECK(!vc_ftg_controller_checkpoint_advance_generation_2(
        &checkpoint,
        UINT64_C(0x100),
        UINT64_C(0x200),
        &exit_counts));
    CHECK(vc_ftg_controller_checkpoint_commit_launch(
        &checkpoint));
    CHECK(checkpoint.phase ==
          VC_FTG_CONTROLLER_PHASE_WAIT_GENERATION_1);
    CHECK(vc_ftg_controller_checkpoint_encode(
        &checkpoint, encoded));
    CHECK(!vc_ftg_controller_checkpoint_begin_resume(
        &checkpoint, UINT64_C(0x100)));
    CHECK(vc_ftg_controller_checkpoint_begin_resume(
        &checkpoint, UINT64_C(0x101)));
    CHECK(checkpoint.phase ==
          VC_FTG_CONTROLLER_PHASE_RUN_GENERATION_1);
    CHECK(checkpoint.previous_run_id == UINT64_C(0x101));
    CHECK(checkpoint.restart_count == 2u);
    CHECK(vc_ftg_controller_checkpoint_encode(
        &checkpoint, encoded));
    CHECK(!vc_ftg_controller_checkpoint_begin_resume(
        &checkpoint, UINT64_C(0x102)));
    CHECK(!vc_ftg_controller_checkpoint_commit_launch(
        &checkpoint));
    CHECK(!vc_ftg_controller_checkpoint_advance_generation_2(
        &checkpoint,
        UINT64_C(0x102),
        0u,
        &exit_counts));
    CHECK(!vc_ftg_controller_checkpoint_advance_generation_2(
        &checkpoint,
        UINT64_C(0x101),
        0u,
        &exit_counts));
    CHECK(vc_ftg_controller_checkpoint_advance_generation_2(
        &checkpoint,
        UINT64_C(0x101),
        UINT64_C(0x200),
        &exit_counts));
    CHECK(checkpoint.phase ==
          VC_FTG_CONTROLLER_PHASE_LAUNCH_GENERATION_2);
    CHECK(vc_ftg_controller_checkpoint_encode(
        &checkpoint, encoded));
    CHECK(!vc_ftg_controller_checkpoint_begin_resume(
        &checkpoint, UINT64_C(0x102)));
    CHECK(vc_ftg_controller_checkpoint_commit_launch(
        &checkpoint));
    CHECK(vc_ftg_controller_checkpoint_encode(
        &checkpoint, encoded));
    CHECK(vc_ftg_controller_checkpoint_decode(
        encoded, sizeof(encoded), &decoded));
    CHECK(decoded.phase ==
          VC_FTG_CONTROLLER_PHASE_WAIT_GENERATION_2);
    CHECK(decoded.transaction_id == UINT64_C(0x100));
    CHECK(decoded.previous_run_id == UINT64_C(0x101));
    CHECK(decoded.first_handle == UINT64_C(0x200));
    CHECK(decoded.restart_count == 2u);
    CHECK(!vc_ftg_controller_checkpoint_begin_resume(
        &decoded, UINT64_C(0x101)));
    CHECK(!vc_ftg_controller_checkpoint_begin_resume(
        &decoded, UINT64_C(0x100)));
    CHECK(vc_ftg_controller_checkpoint_begin_resume(
        &decoded, UINT64_C(0x102)));
    CHECK(decoded.phase ==
          VC_FTG_CONTROLLER_PHASE_RUN_GENERATION_2);
    CHECK(decoded.previous_run_id == UINT64_C(0x102));
    CHECK(decoded.restart_count == 3u);
    CHECK(vc_ftg_controller_checkpoint_encode(
        &decoded, encoded));
    CHECK(vc_ftg_controller_checkpoint_validate(&decoded));
    CHECK(!vc_ftg_controller_checkpoint_begin_resume(
        &decoded, UINT64_C(0x103)));
    CHECK(!vc_ftg_controller_checkpoint_commit_launch(
        &decoded));
    decoded.phase =
        VC_FTG_CONTROLLER_PHASE_WAIT_GENERATION_1;
    CHECK(!vc_ftg_controller_checkpoint_validate(&decoded));
    decoded = checkpoint;
    decoded.reserved0 = 1u;
    CHECK(!vc_ftg_controller_checkpoint_validate(&decoded));
}

int main(void)
{
    test_ordering_and_round_trip();
    test_out_of_order_and_malformed_rejected();
    test_signed_result_round_trip();
    test_v1_decode_remains_supported();
    test_delayed_readiness_transition();
    test_permanent_unavailable_times_out();
    test_delay_overshoot_blocks_late_retry();
    test_unexpected_and_leaked_handle_fail();
    test_readiness_clock_rollback_fails();
    test_result_freshness_policy();
    test_partial_and_corrupt_rejected();
    test_prelaunch_cleanup_rejects_stale_record();
    test_no_input_timeout_remains_prompt_ready();
    test_stage_names();
    test_controller_generation_profiles();
    test_controller_checkpoint_round_trip();

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
