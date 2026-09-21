#include "vitacheat/target_attestation.h"

#include <limits.h>
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

#define TARGET_PID UINT32_C(0x12345678)
#define TARGET_GENERATION UINT64_C(0x0102030405060708)
#define FOREGROUND_SEQUENCE UINT64_C(11)
#define MODULE_ID UINT32_C(1)
#define MODULE_GENERATION UINT64_C(0x1112131415161718)
#define TEST_FINGERPRINT_ALGORITHM UINT32_C(0x54455354)

typedef enum fake_failure {
    FAKE_FAILURE_NONE = 0,
    FAKE_FAILURE_BEGIN,
    FAKE_FAILURE_MODULE,
    FAKE_FAILURE_THREAD,
    FAKE_FAILURE_END
} fake_failure;

typedef struct fake_adapter {
    vc_target_attestation *attestation;
    vc_target_snapshot source;
    fake_failure failure;
    int failure_index;
    unsigned int mutations_remaining;
    unsigned int begin_calls;
    unsigned int module_calls;
    unsigned int thread_calls;
    unsigned int end_calls;
    unsigned int cleanup_calls;
    unsigned int reentry_calls;
    bool reenter;
    bool cleanup_ok;
} fake_adapter;

typedef struct fixture {
    vc_target_attestation attestation;
    fake_adapter adapter;
} fixture;

static const uint8_t target_title[] = {
    'P', 'C', 'S', 'A', '0', '0', '1', '3', '3'
};
static const uint8_t target_version[] = {
    '1', '.', '0', '0'
};
/*
 * Every fingerprint/module/segment value in this PCSA00133 fixture is
 * synthetic test data, not a measured or asserted retail hardware fact.
 */
static const uint8_t test_fingerprint[] = {
    0xde, 0xad, 0xbe, 0xef, 0x01, 0x23, 0x45, 0x67
};

static void check_callback_boundary(fake_adapter *adapter)
{
    if (adapter->attestation == NULL) {
        return;
    }
    CHECK(atomic_load_explicit(
              &adapter->attestation->adapter_active,
              memory_order_acquire) != 0u);
    CHECK(atomic_load_explicit(
              &adapter->attestation->transaction_busy,
              memory_order_acquire) == 0u);
    if (adapter->reenter) {
        vc_target_state state;

        CHECK(vc_target_attestation_get_state(
                  adapter->attestation, &state) ==
              VC_TARGET_STATUS_BUSY);
        ++adapter->reentry_calls;
    }
}

static bool fake_begin(
    void *context,
    vc_target_identity *identity,
    uint32_t *module_count,
    uint32_t *thread_count,
    uint64_t *mutation_token)
{
    fake_adapter *adapter = (fake_adapter *)context;

    check_callback_boundary(adapter);
    ++adapter->begin_calls;
    if (adapter->failure == FAKE_FAILURE_BEGIN) {
        return false;
    }
    *identity = adapter->source.identity;
    *module_count = adapter->source.module_count;
    *thread_count = adapter->source.thread_count;
    *mutation_token = UINT64_C(0xabc);
    return true;
}

static bool fake_read_module(
    void *context,
    uint64_t mutation_token,
    uint32_t module_index,
    vc_target_module *module)
{
    fake_adapter *adapter = (fake_adapter *)context;

    check_callback_boundary(adapter);
    CHECK(mutation_token == UINT64_C(0xabc));
    ++adapter->module_calls;
    if (adapter->failure == FAKE_FAILURE_MODULE &&
        adapter->failure_index == (int)module_index) {
        return false;
    }
    *module = adapter->source.modules[module_index];
    return true;
}

static bool fake_read_thread(
    void *context,
    uint64_t mutation_token,
    uint32_t thread_index,
    vc_target_thread *thread)
{
    fake_adapter *adapter = (fake_adapter *)context;

    check_callback_boundary(adapter);
    CHECK(mutation_token == UINT64_C(0xabc));
    ++adapter->thread_calls;
    if (adapter->failure == FAKE_FAILURE_THREAD &&
        adapter->failure_index == (int)thread_index) {
        return false;
    }
    *thread = adapter->source.threads[thread_index];
    return true;
}

static bool fake_end(
    void *context,
    uint64_t mutation_token,
    uint64_t *completion_token)
{
    fake_adapter *adapter = (fake_adapter *)context;

    check_callback_boundary(adapter);
    ++adapter->end_calls;
    if (adapter->failure == FAKE_FAILURE_END) {
        return false;
    }
    if (adapter->mutations_remaining != 0) {
        --adapter->mutations_remaining;
        *completion_token = mutation_token + 1u;
    } else {
        *completion_token = mutation_token;
    }
    return true;
}

static bool fake_cleanup(void *context)
{
    fake_adapter *adapter = (fake_adapter *)context;

    check_callback_boundary(adapter);
    ++adapter->cleanup_calls;
    return adapter->cleanup_ok;
}

static vc_target_dependencies fake_dependencies(
    fake_adapter *adapter)
{
    vc_target_dependencies dependencies;

    memset(&dependencies, 0, sizeof(dependencies));
    dependencies.begin_snapshot = fake_begin;
    dependencies.read_module = fake_read_module;
    dependencies.read_thread = fake_read_thread;
    dependencies.end_snapshot = fake_end;
    dependencies.cleanup = fake_cleanup;
    dependencies.context = adapter;
    return dependencies;
}

static void set_valid_source(vc_target_snapshot *snapshot)
{
    vc_target_module *module;

    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->identity.process_id = TARGET_PID;
    snapshot->identity.process_generation =
        TARGET_GENERATION;
    snapshot->identity.foreground_sequence =
        FOREGROUND_SEQUENCE;
    snapshot->identity.title_id_size =
        (uint32_t)sizeof(target_title);
    memcpy(snapshot->identity.title_id, target_title,
           sizeof(target_title));
    snapshot->identity.version_size =
        (uint32_t)sizeof(target_version);
    memcpy(snapshot->identity.version, target_version,
           sizeof(target_version));
    snapshot->identity.fingerprint_algorithm =
        TEST_FINGERPRINT_ALGORITHM;
    snapshot->identity.fingerprint_size =
        (uint32_t)sizeof(test_fingerprint);
    memcpy(snapshot->identity.fingerprint,
           test_fingerprint, sizeof(test_fingerprint));

    snapshot->module_count = 1;
    module = &snapshot->modules[0];
    module->module_id = MODULE_ID;
    module->load_generation = MODULE_GENERATION;
    module->segment_count = 2;
    module->segments[0].segment_index = 0;
    module->segments[0].base = UINT32_C(0x1000);
    module->segments[0].size = UINT32_C(0x100);
    module->segments[0].permissions =
        VC_TARGET_PERMISSION_READ |
        VC_TARGET_PERMISSION_EXECUTE;
    module->segments[1].segment_index = 1;
    module->segments[1].base = UINT32_C(0x2000);
    module->segments[1].size = UINT32_C(0x200);
    module->segments[1].permissions =
        VC_TARGET_PERMISSION_READ |
        VC_TARGET_PERMISSION_WRITE;

    snapshot->thread_count = 4;
    snapshot->threads[0].thread_id = 10;
    snapshot->threads[1].thread_id = 20;
    snapshot->threads[2].thread_id = 30;
    snapshot->threads[3].thread_id = 40;
    snapshot->threads[0].role_flags = 7;
    snapshot->threads[1].role_flags = 1;
    snapshot->threads[2].role_flags = 0;
    snapshot->threads[3].role_flags =
        VC_TARGET_THREAD_ROLE_FLAGS_MAX;
    for (uint32_t index = 0;
         index < snapshot->thread_count; ++index) {
        snapshot->threads[index].process_id = TARGET_PID;
        snapshot->threads[index].process_generation =
            TARGET_GENERATION;
    }
}

static void initialize_fixture(fixture *fixture_value)
{
    vc_target_dependencies dependencies;

    memset(fixture_value, 0, sizeof(*fixture_value));
    set_valid_source(&fixture_value->adapter.source);
    fixture_value->adapter.cleanup_ok = true;
    dependencies =
        fake_dependencies(&fixture_value->adapter);
    CHECK(vc_target_attestation_init(
              &fixture_value->attestation,
              &dependencies) == VC_TARGET_STATUS_OK);
    fixture_value->adapter.attestation =
        &fixture_value->attestation;
    CHECK(vc_target_attestation_start(
              &fixture_value->attestation) ==
          VC_TARGET_STATUS_OK);
}

static vc_target_status source_status(
    const vc_target_snapshot *source)
{
    fixture fixture_value;

    initialize_fixture(&fixture_value);
    fixture_value.adapter.source = *source;
    return vc_target_attestation_refresh(
        &fixture_value.attestation);
}

static vc_target_policy ratchet_policy(void)
{
    vc_target_policy policy;

    memset(&policy, 0, sizeof(policy));
    policy.title_id_size =
        (uint32_t)sizeof(target_title);
    memcpy(policy.title_id, target_title,
           sizeof(target_title));
    policy.version_size =
        (uint32_t)sizeof(target_version);
    memcpy(policy.version, target_version,
           sizeof(target_version));
    policy.fingerprint_algorithm =
        TEST_FINGERPRINT_ALGORITHM;
    policy.fingerprint_size =
        (uint32_t)sizeof(test_fingerprint);
    memcpy(policy.fingerprint, test_fingerprint,
           sizeof(test_fingerprint));
    policy.module_count = 1;
    policy.modules[0].module_id = MODULE_ID;
    policy.modules[0].load_generation =
        MODULE_GENERATION;
    return policy;
}

static void test_acquisition_and_immutable_revisions(void)
{
    fixture fixture_value;
    vc_target_dependencies dependencies;
    vc_target_snapshot first;
    vc_target_snapshot second;
    vc_target_state state;
    vc_target_attestation uninitialized =
        VC_TARGET_INITIALIZER;

    memset(&first, 0xa5, sizeof(first));
    CHECK(vc_target_attestation_start(&uninitialized) ==
          VC_TARGET_STATUS_NOT_INITIALIZED);
    CHECK(vc_target_attestation_init(NULL, NULL) ==
          VC_TARGET_STATUS_INVALID_ARGUMENT);

    initialize_fixture(&fixture_value);
    dependencies =
        fake_dependencies(&fixture_value.adapter);
    CHECK(vc_target_attestation_init(
              &fixture_value.attestation,
              &dependencies) == VC_TARGET_STATUS_OK);
    dependencies.context = &first;
    CHECK(vc_target_attestation_init(
              &fixture_value.attestation,
              &dependencies) ==
          VC_TARGET_STATUS_INVALID_ARGUMENT);
    CHECK(vc_target_attestation_get_snapshot(
              &fixture_value.attestation, NULL) ==
          VC_TARGET_STATUS_INVALID_ARGUMENT);
    CHECK(vc_target_attestation_get_snapshot(
              &fixture_value.attestation, &first) ==
          VC_TARGET_STATUS_UNAVAILABLE);
    CHECK(first.revision == 0);

    fixture_value.adapter.reenter = true;
    CHECK(vc_target_attestation_refresh(
              &fixture_value.attestation) ==
          VC_TARGET_STATUS_OK);
    CHECK(fixture_value.adapter.reentry_calls != 0);
    CHECK(vc_target_attestation_get_snapshot(
              &fixture_value.attestation, &first) ==
          VC_TARGET_STATUS_OK);
    CHECK(first.revision == 1);
    CHECK(first.lifecycle_generation != 0);
    CHECK(memcmp(first.identity.title_id, target_title,
                 sizeof(target_title)) == 0);

    ++fixture_value.adapter.source.identity
          .foreground_sequence;
    fixture_value.adapter.source.modules[0]
        .segments[1].base = UINT32_C(0x3000);
    CHECK(vc_target_attestation_refresh(
              &fixture_value.attestation) ==
          VC_TARGET_STATUS_OK);
    CHECK(vc_target_attestation_get_snapshot(
              &fixture_value.attestation, &second) ==
          VC_TARGET_STATUS_OK);
    CHECK(second.revision == first.revision + 1u);
    CHECK(first.modules[0].segments[1].base ==
          UINT32_C(0x2000));
    CHECK(second.modules[0].segments[1].base ==
          UINT32_C(0x3000));
    CHECK(vc_target_attestation_find_segment(
              &fixture_value.attestation,
              first.revision, MODULE_ID,
              MODULE_GENERATION, 0,
              &second.modules[0].segments[0]) ==
          VC_TARGET_STATUS_STALE_SNAPSHOT);
    CHECK(vc_target_attestation_get_state(
              &fixture_value.attestation, &state) ==
          VC_TARGET_STATUS_OK);
    CHECK(state.snapshot_available);
    CHECK(state.snapshot_revision == second.revision);
    CHECK(state.status == VC_TARGET_RUNTIME_AVAILABLE);
}

static void test_identity_and_count_validation(void)
{
    vc_target_snapshot source;
    uint32_t index;

    set_valid_source(&source);
    source.identity.process_id = 0;
    CHECK(source_status(&source) ==
          VC_TARGET_STATUS_INVALID_IDENTITY);
    set_valid_source(&source);
    source.identity.process_generation = 0;
    CHECK(source_status(&source) ==
          VC_TARGET_STATUS_INVALID_IDENTITY);
    set_valid_source(&source);
    source.identity.foreground_sequence = 0;
    CHECK(source_status(&source) ==
          VC_TARGET_STATUS_INVALID_IDENTITY);
    set_valid_source(&source);
    source.identity.reserved0 = 1;
    CHECK(source_status(&source) ==
          VC_TARGET_STATUS_INVALID_IDENTITY);

    set_valid_source(&source);
    source.identity.title_id_size = 0;
    CHECK(source_status(&source) ==
          VC_TARGET_STATUS_INVALID_TITLE);
    set_valid_source(&source);
    source.identity.title_id_size =
        VC_TARGET_TITLE_ID_MAX + 1u;
    CHECK(source_status(&source) ==
          VC_TARGET_STATUS_INVALID_TITLE);
    set_valid_source(&source);
    source.identity.title_id[0] = (uint8_t)'p';
    CHECK(source_status(&source) ==
          VC_TARGET_STATUS_INVALID_TITLE);
    set_valid_source(&source);
    source.identity.title_id_size =
        VC_TARGET_TITLE_ID_MAX;
    memset(source.identity.title_id, 'A',
           sizeof(source.identity.title_id));
    CHECK(source_status(&source) == VC_TARGET_STATUS_OK);

    set_valid_source(&source);
    source.identity.version_size =
        VC_TARGET_VERSION_MAX + 1u;
    CHECK(source_status(&source) ==
          VC_TARGET_STATUS_INVALID_VERSION);
    set_valid_source(&source);
    source.identity.version[0] = (uint8_t)' ';
    CHECK(source_status(&source) ==
          VC_TARGET_STATUS_INVALID_VERSION);
    set_valid_source(&source);
    source.identity.version_size =
        VC_TARGET_VERSION_MAX;
    memset(source.identity.version, 'v',
           sizeof(source.identity.version));
    CHECK(source_status(&source) == VC_TARGET_STATUS_OK);

    set_valid_source(&source);
    source.identity.fingerprint_algorithm = 0;
    CHECK(source_status(&source) ==
          VC_TARGET_STATUS_INVALID_FINGERPRINT);
    set_valid_source(&source);
    source.identity.fingerprint_size = 0;
    CHECK(source_status(&source) ==
          VC_TARGET_STATUS_INVALID_FINGERPRINT);
    set_valid_source(&source);
    source.identity.fingerprint_size =
        VC_TARGET_FINGERPRINT_MAX + 1u;
    CHECK(source_status(&source) ==
          VC_TARGET_STATUS_INVALID_FINGERPRINT);
    set_valid_source(&source);
    source.identity.fingerprint_size =
        VC_TARGET_FINGERPRINT_MAX;
    memset(source.identity.fingerprint, 0xa5,
           sizeof(source.identity.fingerprint));
    CHECK(source_status(&source) == VC_TARGET_STATUS_OK);
    set_valid_source(&source);
    source.identity.fingerprint_algorithm = 0;
    source.identity.fingerprint_size = 0;
    memset(source.identity.fingerprint, 0,
           sizeof(source.identity.fingerprint));
    CHECK(source_status(&source) == VC_TARGET_STATUS_OK);

    set_valid_source(&source);
    source.module_count = 0;
    CHECK(source_status(&source) ==
          VC_TARGET_STATUS_INVALID_MODULE);
    set_valid_source(&source);
    source.module_count = VC_TARGET_MAX_MODULES + 1u;
    CHECK(source_status(&source) ==
          VC_TARGET_STATUS_LIMIT);
    set_valid_source(&source);
    source.module_count = VC_TARGET_MAX_MODULES;
    for (index = 0; index < source.module_count; ++index) {
        source.modules[index] = source.modules[0];
        source.modules[index].module_id = index + 1u;
        source.modules[index].load_generation =
            UINT64_C(100) + index;
    }
    CHECK(source_status(&source) == VC_TARGET_STATUS_OK);

    set_valid_source(&source);
    source.thread_count = 0;
    CHECK(source_status(&source) ==
          VC_TARGET_STATUS_INVALID_THREAD);
    set_valid_source(&source);
    source.thread_count = VC_TARGET_MAX_THREADS + 1u;
    CHECK(source_status(&source) ==
          VC_TARGET_STATUS_LIMIT);
    set_valid_source(&source);
    source.thread_count = VC_TARGET_MAX_THREADS;
    for (index = 0; index < source.thread_count; ++index) {
        source.threads[index] = source.threads[0];
        source.threads[index].thread_id =
            (int32_t)(index + 1u);
    }
    CHECK(source_status(&source) == VC_TARGET_STATUS_OK);
}

static void test_module_segment_and_thread_validation(void)
{
    vc_target_snapshot source;
    uint32_t index;

    set_valid_source(&source);
    source.modules[0].module_id = 0;
    CHECK(source_status(&source) ==
          VC_TARGET_STATUS_INVALID_MODULE);
    set_valid_source(&source);
    source.modules[0].load_generation = 0;
    CHECK(source_status(&source) ==
          VC_TARGET_STATUS_INVALID_MODULE);
    set_valid_source(&source);
    source.module_count = 2;
    source.modules[1] = source.modules[0];
    source.modules[1].load_generation++;
    CHECK(source_status(&source) ==
          VC_TARGET_STATUS_DUPLICATE);
    set_valid_source(&source);
    source.module_count = 2;
    source.modules[1] = source.modules[0];
    source.modules[1].module_id++;
    CHECK(source_status(&source) == VC_TARGET_STATUS_OK);

    set_valid_source(&source);
    source.modules[0].segment_count = 0;
    CHECK(source_status(&source) ==
          VC_TARGET_STATUS_INVALID_SEGMENT);
    set_valid_source(&source);
    source.modules[0].segment_count =
        VC_TARGET_MAX_SEGMENTS + 1u;
    CHECK(source_status(&source) ==
          VC_TARGET_STATUS_LIMIT);
    set_valid_source(&source);
    source.modules[0].segment_count =
        VC_TARGET_MAX_SEGMENTS;
    for (index = 0;
         index < source.modules[0].segment_count;
         ++index) {
        source.modules[0].segments[index] =
            source.modules[0].segments[0];
        source.modules[0].segments[index].segment_index =
            index;
        source.modules[0].segments[index].base =
            UINT32_C(0x1000) + index * UINT32_C(0x100);
        source.modules[0].segments[index].size =
            UINT32_C(0x80);
    }
    CHECK(source_status(&source) == VC_TARGET_STATUS_OK);

    set_valid_source(&source);
    source.modules[0].segments[0].size = 0;
    CHECK(source_status(&source) ==
          VC_TARGET_STATUS_INVALID_SEGMENT);
    set_valid_source(&source);
    source.modules[0].segments[0].base =
        UINT32_MAX - UINT32_C(3);
    source.modules[0].segments[0].size = 4;
    CHECK(source_status(&source) ==
          VC_TARGET_STATUS_OVERFLOW);
    set_valid_source(&source);
    source.modules[0].segments[0].permissions = 0;
    CHECK(source_status(&source) ==
          VC_TARGET_STATUS_UNKNOWN_PERMISSION);
    set_valid_source(&source);
    source.modules[0].segments[0].permissions =
        UINT32_C(0x08);
    CHECK(source_status(&source) ==
          VC_TARGET_STATUS_UNKNOWN_PERMISSION);
    set_valid_source(&source);
    source.modules[0].segments[0].flags = 1;
    CHECK(source_status(&source) ==
          VC_TARGET_STATUS_INVALID_SEGMENT);
    set_valid_source(&source);
    source.modules[0].segments[1].segment_index = 0;
    CHECK(source_status(&source) ==
          VC_TARGET_STATUS_DUPLICATE);
    set_valid_source(&source);
    source.modules[0].segments[1].base =
        UINT32_C(0x1080);
    CHECK(source_status(&source) ==
          VC_TARGET_STATUS_OVERLAP);

    set_valid_source(&source);
    source.threads[0].thread_id = 0;
    CHECK(source_status(&source) ==
          VC_TARGET_STATUS_INVALID_THREAD);
    set_valid_source(&source);
    source.threads[1].thread_id =
        source.threads[0].thread_id;
    CHECK(source_status(&source) ==
          VC_TARGET_STATUS_DUPLICATE);
    set_valid_source(&source);
    source.threads[0].process_id++;
    CHECK(source_status(&source) ==
          VC_TARGET_STATUS_INVALID_THREAD);
    set_valid_source(&source);
    source.threads[0].process_generation++;
    CHECK(source_status(&source) ==
          VC_TARGET_STATUS_INVALID_THREAD);
    set_valid_source(&source);
    source.threads[0].role_flags =
        VC_TARGET_THREAD_ROLE_FLAGS_MAX + 1u;
    CHECK(source_status(&source) ==
          VC_TARGET_STATUS_INVALID_THREAD);
    set_valid_source(&source);
    source.threads[0].reserved0 = 1;
    CHECK(source_status(&source) ==
          VC_TARGET_STATUS_INVALID_THREAD);
}

static void test_adapter_failures_mutation_and_atomicity(void)
{
    fixture fixture_value;
    vc_target_snapshot retained;
    vc_target_snapshot after_failure;

    initialize_fixture(&fixture_value);
    fixture_value.adapter.failure = FAKE_FAILURE_BEGIN;
    CHECK(vc_target_attestation_refresh(
              &fixture_value.attestation) ==
          VC_TARGET_STATUS_ADAPTER_FAILURE);
    CHECK(fixture_value.adapter.end_calls == 0);

    fixture_value.adapter.failure = FAKE_FAILURE_NONE;
    CHECK(vc_target_attestation_refresh(
              &fixture_value.attestation) ==
          VC_TARGET_STATUS_OK);
    CHECK(vc_target_attestation_get_snapshot(
              &fixture_value.attestation, &retained) ==
          VC_TARGET_STATUS_OK);

    fixture_value.adapter.failure = FAKE_FAILURE_MODULE;
    fixture_value.adapter.failure_index = 0;
    ++fixture_value.adapter.source.identity
          .foreground_sequence;
    fixture_value.adapter.source.modules[0].module_id = 99;
    CHECK(vc_target_attestation_refresh(
              &fixture_value.attestation) ==
          VC_TARGET_STATUS_ADAPTER_FAILURE);
    CHECK(vc_target_attestation_get_snapshot(
              &fixture_value.attestation,
              &after_failure) == VC_TARGET_STATUS_OK);
    CHECK(memcmp(&retained, &after_failure,
                 sizeof(retained)) == 0);

    fixture_value.adapter.failure = FAKE_FAILURE_THREAD;
    fixture_value.adapter.failure_index = 3;
    CHECK(vc_target_attestation_refresh(
              &fixture_value.attestation) ==
          VC_TARGET_STATUS_ADAPTER_FAILURE);
    fixture_value.adapter.failure = FAKE_FAILURE_END;
    CHECK(vc_target_attestation_refresh(
              &fixture_value.attestation) ==
          VC_TARGET_STATUS_ADAPTER_FAILURE);

    fixture_value.adapter.failure = FAKE_FAILURE_NONE;
    fixture_value.adapter.source.modules[0].module_id =
        MODULE_ID;
    fixture_value.adapter.mutations_remaining = 2;
    fixture_value.adapter.begin_calls = 0;
    CHECK(vc_target_attestation_refresh(
              &fixture_value.attestation) ==
          VC_TARGET_STATUS_OK);
    CHECK(fixture_value.adapter.begin_calls == 3);

    ++fixture_value.adapter.source.identity
          .foreground_sequence;
    fixture_value.adapter.mutations_remaining =
        VC_TARGET_REFRESH_ATTEMPTS;
    fixture_value.adapter.begin_calls = 0;
    CHECK(vc_target_attestation_refresh(
              &fixture_value.attestation) ==
          VC_TARGET_STATUS_MUTATED_SNAPSHOT);
    CHECK(fixture_value.adapter.begin_calls ==
          VC_TARGET_REFRESH_ATTEMPTS);
    CHECK(vc_target_attestation_get_snapshot(
              &fixture_value.attestation,
              &after_failure) == VC_TARGET_STATUS_OK);
}

static void test_policy_and_b2_segment_lookup(void)
{
    fixture fixture_value;
    vc_target_snapshot snapshot;
    vc_target_policy policy;
    vc_target_segment segment;

    initialize_fixture(&fixture_value);
    CHECK(vc_target_attestation_refresh(
              &fixture_value.attestation) ==
          VC_TARGET_STATUS_OK);
    CHECK(vc_target_attestation_get_snapshot(
              &fixture_value.attestation, &snapshot) ==
          VC_TARGET_STATUS_OK);

    policy = ratchet_policy();
    CHECK(vc_target_attestation_match_policy(
              &fixture_value.attestation,
              snapshot.revision, &policy) ==
          VC_TARGET_STATUS_OK);
    policy.version_size = 0;
    memset(policy.version, 0, sizeof(policy.version));
    policy.fingerprint_algorithm = 0;
    policy.fingerprint_size = 0;
    memset(policy.fingerprint, 0,
           sizeof(policy.fingerprint));
    CHECK(vc_target_attestation_match_policy(
              &fixture_value.attestation,
              snapshot.revision, &policy) ==
          VC_TARGET_STATUS_OK);

    policy = ratchet_policy();
    policy.title_id[8] = (uint8_t)'4';
    CHECK(vc_target_attestation_match_policy(
              &fixture_value.attestation,
              snapshot.revision, &policy) ==
          VC_TARGET_STATUS_INVALID_TITLE);
    policy = ratchet_policy();
    policy.version[3] = (uint8_t)'1';
    CHECK(vc_target_attestation_match_policy(
              &fixture_value.attestation,
              snapshot.revision, &policy) ==
          VC_TARGET_STATUS_INVALID_VERSION);
    policy = ratchet_policy();
    policy.fingerprint[0] ^= 1u;
    CHECK(vc_target_attestation_match_policy(
              &fixture_value.attestation,
              snapshot.revision, &policy) ==
          VC_TARGET_STATUS_INVALID_FINGERPRINT);
    policy = ratchet_policy();
    policy.modules[0].load_generation++;
    CHECK(vc_target_attestation_match_policy(
              &fixture_value.attestation,
              snapshot.revision, &policy) ==
          VC_TARGET_STATUS_INVALID_MODULE);
    policy = ratchet_policy();
    policy.fingerprint_algorithm = 0;
    CHECK(vc_target_attestation_match_policy(
              &fixture_value.attestation,
              snapshot.revision, &policy) ==
          VC_TARGET_STATUS_INVALID_FINGERPRINT);

    CHECK(vc_target_attestation_find_segment(
              &fixture_value.attestation,
              snapshot.revision, MODULE_ID,
              MODULE_GENERATION, 0, &segment) ==
          VC_TARGET_STATUS_OK);
    CHECK(segment.base == UINT32_C(0x1000));
    CHECK(vc_target_attestation_find_segment(
              &fixture_value.attestation,
              snapshot.revision, MODULE_ID,
              MODULE_GENERATION, 1, &segment) ==
          VC_TARGET_STATUS_OK);
    CHECK(segment.base == UINT32_C(0x2000));
    CHECK(vc_target_attestation_find_segment(
              &fixture_value.attestation,
              snapshot.revision, MODULE_ID,
              MODULE_GENERATION, 2, &segment) ==
          VC_TARGET_STATUS_INVALID_SEGMENT);
    CHECK(vc_target_attestation_find_segment(
              &fixture_value.attestation,
              snapshot.revision, MODULE_ID + 1u,
              MODULE_GENERATION, 0, &segment) ==
          VC_TARGET_STATUS_INVALID_MODULE);

    ++fixture_value.adapter.source.identity
          .foreground_sequence;
    fixture_value.adapter.source.identity
        .fingerprint_algorithm = 0;
    fixture_value.adapter.source.identity
        .fingerprint_size = 0;
    memset(fixture_value.adapter.source.identity.fingerprint,
           0, sizeof(fixture_value.adapter.source.identity
                         .fingerprint));
    CHECK(vc_target_attestation_refresh(
              &fixture_value.attestation) ==
          VC_TARGET_STATUS_OK);
    CHECK(vc_target_attestation_get_snapshot(
              &fixture_value.attestation, &snapshot) ==
          VC_TARGET_STATUS_OK);
    policy = ratchet_policy();
    CHECK(vc_target_attestation_match_policy(
              &fixture_value.attestation,
              snapshot.revision, &policy) ==
          VC_TARGET_STATUS_INVALID_FINGERPRINT);

    ++fixture_value.adapter.source.identity
          .foreground_sequence;
    fixture_value.adapter.source.identity.version_size = 0;
    memset(fixture_value.adapter.source.identity.version,
           0, sizeof(fixture_value.adapter.source.identity
                         .version));
    CHECK(vc_target_attestation_refresh(
              &fixture_value.attestation) ==
          VC_TARGET_STATUS_OK);
    CHECK(vc_target_attestation_get_snapshot(
              &fixture_value.attestation, &snapshot) ==
          VC_TARGET_STATUS_OK);
    policy = ratchet_policy();
    policy.fingerprint_algorithm = 0;
    policy.fingerprint_size = 0;
    memset(policy.fingerprint, 0,
           sizeof(policy.fingerprint));
    CHECK(vc_target_attestation_match_policy(
              &fixture_value.attestation,
              snapshot.revision, &policy) ==
          VC_TARGET_STATUS_INVALID_VERSION);
}

static void test_range_queries(void)
{
    fixture fixture_value;
    vc_target_snapshot snapshot;
    vc_target_range range;

    initialize_fixture(&fixture_value);
    CHECK(vc_target_attestation_refresh(
              &fixture_value.attestation) ==
          VC_TARGET_STATUS_OK);
    CHECK(vc_target_attestation_get_snapshot(
              &fixture_value.attestation, &snapshot) ==
          VC_TARGET_STATUS_OK);

    CHECK(vc_target_attestation_resolve_range(
              &fixture_value.attestation,
              snapshot.revision, MODULE_ID,
              MODULE_GENERATION, UINT32_C(0x1000), 1,
              VC_TARGET_PERMISSION_READ, &range) ==
          VC_TARGET_STATUS_OK);
    CHECK(range.segment_index == 0 &&
          range.segment_offset == 0 && range.length == 1);
    CHECK(vc_target_attestation_resolve_range(
              &fixture_value.attestation,
              snapshot.revision, MODULE_ID,
              MODULE_GENERATION, UINT32_C(0x10ff), 1,
              VC_TARGET_PERMISSION_EXECUTE, &range) ==
          VC_TARGET_STATUS_OK);
    CHECK(range.segment_offset == UINT32_C(0xff));
    CHECK(vc_target_attestation_resolve_range(
              &fixture_value.attestation,
              snapshot.revision, MODULE_ID,
              MODULE_GENERATION, UINT32_C(0x2000),
              UINT32_C(0x200),
              VC_TARGET_PERMISSION_READ |
                  VC_TARGET_PERMISSION_WRITE,
              &range) == VC_TARGET_STATUS_OK);
    CHECK(range.segment_index == 1 &&
          range.segment_offset == 0 &&
          range.length == UINT32_C(0x200));

    CHECK(vc_target_attestation_resolve_range(
              &fixture_value.attestation,
              snapshot.revision, MODULE_ID,
              MODULE_GENERATION, UINT32_C(0x1000), 0,
              VC_TARGET_PERMISSION_READ, &range) ==
          VC_TARGET_STATUS_INVALID_ARGUMENT);
    CHECK(vc_target_attestation_resolve_range(
              &fixture_value.attestation,
              snapshot.revision, MODULE_ID,
              MODULE_GENERATION, UINT32_MAX - 1u, 2,
              VC_TARGET_PERMISSION_READ, &range) ==
          VC_TARGET_STATUS_OVERFLOW);
    CHECK(vc_target_attestation_resolve_range(
              &fixture_value.attestation,
              snapshot.revision, MODULE_ID,
              MODULE_GENERATION, UINT32_C(0x10f0),
              UINT32_C(0x20),
              VC_TARGET_PERMISSION_READ, &range) ==
          VC_TARGET_STATUS_CROSS_SEGMENT_RANGE);
    CHECK(vc_target_attestation_resolve_range(
              &fixture_value.attestation,
              snapshot.revision, MODULE_ID,
              MODULE_GENERATION, UINT32_C(0x1800), 4,
              VC_TARGET_PERMISSION_READ, &range) ==
          VC_TARGET_STATUS_UNMAPPED_RANGE);
    CHECK(vc_target_attestation_resolve_range(
              &fixture_value.attestation,
              snapshot.revision, MODULE_ID,
              MODULE_GENERATION, UINT32_C(0x1000), 4,
              VC_TARGET_PERMISSION_WRITE, &range) ==
          VC_TARGET_STATUS_PERMISSION_DENIED);
    CHECK(vc_target_attestation_resolve_range(
              &fixture_value.attestation,
              snapshot.revision, MODULE_ID,
              MODULE_GENERATION, UINT32_C(0x1000), 4,
              UINT32_C(0x08), &range) ==
          VC_TARGET_STATUS_UNKNOWN_PERMISSION);
    CHECK(vc_target_attestation_resolve_range(
              &fixture_value.attestation,
              snapshot.revision, MODULE_ID,
              MODULE_GENERATION + 1u,
              UINT32_C(0x1000), 4,
              VC_TARGET_PERMISSION_READ, &range) ==
          VC_TARGET_STATUS_INVALID_MODULE);

    ++fixture_value.adapter.source.identity
          .foreground_sequence;
    CHECK(vc_target_attestation_refresh(
              &fixture_value.attestation) ==
          VC_TARGET_STATUS_OK);
    CHECK(vc_target_attestation_resolve_range(
              &fixture_value.attestation,
              snapshot.revision, MODULE_ID,
              MODULE_GENERATION, UINT32_C(0x1000), 4,
              VC_TARGET_PERMISSION_READ, &range) ==
          VC_TARGET_STATUS_STALE_SNAPSHOT);
}

static vc_target_runtime_binding runtime_binding(void)
{
    vc_target_runtime_binding binding;

    memset(&binding, 0, sizeof(binding));
    binding.process_id = TARGET_PID;
    binding.process_generation = TARGET_GENERATION;
    binding.foreground_sequence = FOREGROUND_SEQUENCE;
    binding.title_id_size =
        (uint32_t)sizeof(target_title);
    memcpy(binding.title_id, target_title,
           sizeof(target_title));
    binding.module_id = MODULE_ID;
    binding.module_load_generation =
        MODULE_GENERATION;
    return binding;
}

static void test_thread_allowlists(void)
{
    fixture fixture_value;
    vc_target_snapshot snapshot;
    vc_target_runtime_binding binding;
    int32_t gameplay[] = {10, 20, 30};
    int32_t protected_ids[] = {40, 50, 60};
    int32_t invalid[] = {10, 10};
    int32_t max_threads[VC_TARGET_MAX_THREADS];
    int32_t too_many[VC_TARGET_MAX_THREADS + 1u];
    size_t index;

    initialize_fixture(&fixture_value);
    CHECK(vc_target_attestation_refresh(
              &fixture_value.attestation) ==
          VC_TARGET_STATUS_OK);
    CHECK(vc_target_attestation_get_snapshot(
              &fixture_value.attestation, &snapshot) ==
          VC_TARGET_STATUS_OK);
    CHECK(vc_target_attestation_validate_threads(
              &fixture_value.attestation,
              snapshot.revision, gameplay, 3,
              protected_ids, 3) ==
          VC_TARGET_STATUS_OK);
    CHECK(vc_target_attestation_validate_threads(
              &fixture_value.attestation,
              snapshot.revision, gameplay, 3,
              NULL, 1) ==
          VC_TARGET_STATUS_INVALID_ARGUMENT);
    CHECK(vc_target_attestation_validate_threads(
              &fixture_value.attestation,
              snapshot.revision, NULL, 0,
              protected_ids, 3) ==
          VC_TARGET_STATUS_INVALID_THREAD);

    invalid[0] = 20;
    invalid[1] = 10;
    CHECK(vc_target_attestation_validate_threads(
              &fixture_value.attestation,
              snapshot.revision, invalid, 2,
              protected_ids, 3) ==
          VC_TARGET_STATUS_INVALID_THREAD);
    invalid[0] = 10;
    invalid[1] = 10;
    CHECK(vc_target_attestation_validate_threads(
              &fixture_value.attestation,
              snapshot.revision, invalid, 2,
              protected_ids, 3) ==
          VC_TARGET_STATUS_INVALID_THREAD);
    invalid[0] = 0;
    invalid[1] = 10;
    CHECK(vc_target_attestation_validate_threads(
              &fixture_value.attestation,
              snapshot.revision, invalid, 2,
              protected_ids, 3) ==
          VC_TARGET_STATUS_INVALID_THREAD);
    invalid[0] = 10;
    invalid[1] = 99;
    CHECK(vc_target_attestation_validate_threads(
              &fixture_value.attestation,
              snapshot.revision, invalid, 2,
              protected_ids, 3) ==
          VC_TARGET_STATUS_INVALID_THREAD);

    for (index = 0; index < 3; ++index) {
        int32_t protected_one[1];

        protected_one[0] = gameplay[index];
        CHECK(vc_target_attestation_validate_threads(
                  &fixture_value.attestation,
                  snapshot.revision, gameplay, 3,
                  protected_one, 1) ==
              VC_TARGET_STATUS_PROTECTED_THREAD);
    }
    CHECK(vc_target_attestation_validate_threads(
              &fixture_value.attestation,
              snapshot.revision + 1u, gameplay, 3,
              protected_ids, 3) ==
          VC_TARGET_STATUS_STALE_SNAPSHOT);

    fixture_value.adapter.source.thread_count =
        VC_TARGET_MAX_THREADS;
    for (index = 0; index < VC_TARGET_MAX_THREADS; ++index) {
        fixture_value.adapter.source.threads[index] =
            fixture_value.adapter.source.threads[0];
        fixture_value.adapter.source.threads[index].thread_id =
            (int32_t)(index + 1u);
        max_threads[index] = (int32_t)(index + 1u);
        too_many[index] = (int32_t)(index + 1u);
    }
    too_many[VC_TARGET_MAX_THREADS] =
        (int32_t)(VC_TARGET_MAX_THREADS + 1u);
    ++fixture_value.adapter.source.identity
          .foreground_sequence;
    CHECK(vc_target_attestation_refresh(
              &fixture_value.attestation) ==
          VC_TARGET_STATUS_OK);
    CHECK(vc_target_attestation_get_snapshot(
              &fixture_value.attestation, &snapshot) ==
          VC_TARGET_STATUS_OK);
    CHECK(vc_target_attestation_validate_threads(
              &fixture_value.attestation,
              snapshot.revision, max_threads,
              VC_TARGET_MAX_THREADS, NULL, 0) ==
          VC_TARGET_STATUS_OK);
    CHECK(vc_target_attestation_validate_threads(
              &fixture_value.attestation,
              snapshot.revision, too_many,
              VC_TARGET_MAX_THREADS + 1u, NULL, 0) ==
          VC_TARGET_STATUS_LIMIT);
    CHECK(vc_target_attestation_validate_threads(
              &fixture_value.attestation,
              snapshot.revision, max_threads, 1,
              too_many, VC_TARGET_MAX_THREADS + 1u) ==
          VC_TARGET_STATUS_LIMIT);

    binding = runtime_binding();
    binding.foreground_sequence =
        fixture_value.adapter.source.identity
            .foreground_sequence;
    CHECK(vc_target_attestation_validate_allowlist(
              &fixture_value.attestation,
              snapshot.revision, &binding,
              max_threads, VC_TARGET_MAX_THREADS,
              NULL, 0) == VC_TARGET_STATUS_OK);
    binding.module_load_generation++;
    CHECK(vc_target_attestation_validate_allowlist(
              &fixture_value.attestation,
              snapshot.revision, &binding,
              max_threads, VC_TARGET_MAX_THREADS,
              NULL, 0) ==
          VC_TARGET_STATUS_INVALID_MODULE);
}

static void test_pid_reuse_and_generation_change(void)
{
    fixture fixture_value;
    vc_target_snapshot first;
    vc_target_snapshot second;
    vc_target_policy policy;
    uint32_t index;

    initialize_fixture(&fixture_value);
    CHECK(vc_target_attestation_refresh(
              &fixture_value.attestation) ==
          VC_TARGET_STATUS_OK);
    CHECK(vc_target_attestation_get_snapshot(
              &fixture_value.attestation, &first) ==
          VC_TARGET_STATUS_OK);

    ++fixture_value.adapter.source.identity
          .foreground_sequence;
    ++fixture_value.adapter.source.identity
          .process_generation;
    ++fixture_value.adapter.source.modules[0]
          .load_generation;
    for (index = 0;
         index < fixture_value.adapter.source.thread_count;
         ++index) {
        ++fixture_value.adapter.source.threads[index]
              .process_generation;
    }
    CHECK(vc_target_attestation_refresh(
              &fixture_value.attestation) ==
          VC_TARGET_STATUS_OK);
    CHECK(vc_target_attestation_get_snapshot(
              &fixture_value.attestation, &second) ==
          VC_TARGET_STATUS_OK);
    CHECK(second.identity.process_id ==
          first.identity.process_id);
    CHECK(second.identity.process_generation !=
          first.identity.process_generation);
    CHECK(second.revision > first.revision);
    policy = ratchet_policy();
    CHECK(vc_target_attestation_match_policy(
              &fixture_value.attestation,
              first.revision, &policy) ==
          VC_TARGET_STATUS_STALE_SNAPSHOT);
    CHECK(vc_target_attestation_process_exit(
              &fixture_value.attestation,
              second.identity.process_id,
              second.identity.process_generation) ==
          VC_TARGET_STATUS_OK);
    CHECK(vc_target_attestation_process_exit(
              &fixture_value.attestation,
              second.identity.process_id,
              second.identity.process_generation) ==
          VC_TARGET_STATUS_OK);
}

static void test_lifecycle_and_formatter(void)
{
    fixture fixture_value;
    vc_target_snapshot snapshot;
    vc_target_snapshot restarted;
    char text[128];
    char short_text[5];
    size_t length;

    initialize_fixture(&fixture_value);
    CHECK(vc_target_attestation_refresh(
              &fixture_value.attestation) ==
          VC_TARGET_STATUS_OK);
    CHECK(vc_target_attestation_get_snapshot(
              &fixture_value.attestation, &snapshot) ==
          VC_TARGET_STATUS_OK);

    CHECK(vc_target_attestation_module_changed(
              &fixture_value.attestation, MODULE_ID,
              MODULE_GENERATION + 1u) ==
          VC_TARGET_STATUS_STALE_SNAPSHOT);
    CHECK(vc_target_attestation_get_snapshot(
              &fixture_value.attestation, &restarted) ==
          VC_TARGET_STATUS_UNAVAILABLE);
    ++fixture_value.adapter.source.identity
          .foreground_sequence;
    ++fixture_value.adapter.source.modules[0]
          .load_generation;
    CHECK(vc_target_attestation_refresh(
              &fixture_value.attestation) ==
          VC_TARGET_STATUS_OK);
    CHECK(vc_target_attestation_get_snapshot(
              &fixture_value.attestation, &restarted) ==
          VC_TARGET_STATUS_OK);
    CHECK(restarted.revision > snapshot.revision);
    CHECK(vc_target_attestation_module_changed(
              &fixture_value.attestation, MODULE_ID,
              restarted.modules[0].load_generation) ==
          VC_TARGET_STATUS_OK);

    ++fixture_value.adapter.source.identity
          .foreground_sequence;
    CHECK(vc_target_attestation_refresh(
              &fixture_value.attestation) ==
          VC_TARGET_STATUS_OK);
    CHECK(vc_target_attestation_get_snapshot(
              &fixture_value.attestation, &restarted) ==
          VC_TARGET_STATUS_OK);
    CHECK(vc_target_attestation_process_exit(
              &fixture_value.attestation, TARGET_PID,
              TARGET_GENERATION + 1u) ==
          VC_TARGET_STATUS_STALE_SNAPSHOT);
    CHECK(vc_target_attestation_get_snapshot(
              &fixture_value.attestation, &snapshot) ==
          VC_TARGET_STATUS_UNAVAILABLE);

    CHECK(vc_target_attestation_reset(
              &fixture_value.attestation) ==
          VC_TARGET_STATUS_OK);
    CHECK(vc_target_attestation_plugin_unload(
              &fixture_value.attestation) ==
          VC_TARGET_STATUS_OK);
    CHECK(vc_target_attestation_foreground_changed(
              &fixture_value.attestation, 20) ==
          VC_TARGET_STATUS_OK);
    CHECK(vc_target_attestation_foreground_lost(
              &fixture_value.attestation, 19) ==
          VC_TARGET_STATUS_SEQUENCE_ROLLBACK);

    CHECK(vc_target_attestation_reset(
              &fixture_value.attestation) ==
          VC_TARGET_STATUS_OK);
    set_valid_source(&fixture_value.adapter.source);
    CHECK(vc_target_attestation_refresh(
              &fixture_value.attestation) ==
          VC_TARGET_STATUS_OK);
    fixture_value.adapter.source.identity
        .foreground_sequence =
        FOREGROUND_SEQUENCE - 1u;
    CHECK(vc_target_attestation_refresh(
              &fixture_value.attestation) ==
          VC_TARGET_STATUS_SEQUENCE_ROLLBACK);
    CHECK(vc_target_attestation_get_snapshot(
              &fixture_value.attestation, &snapshot) ==
          VC_TARGET_STATUS_UNAVAILABLE);

    CHECK(vc_target_attestation_reset(
              &fixture_value.attestation) ==
          VC_TARGET_STATUS_OK);
    set_valid_source(&fixture_value.adapter.source);
    CHECK(vc_target_attestation_refresh(
              &fixture_value.attestation) ==
          VC_TARGET_STATUS_OK);
    fixture_value.adapter.source.identity.version[3] =
        (uint8_t)'1';
    CHECK(vc_target_attestation_refresh(
              &fixture_value.attestation) ==
          VC_TARGET_STATUS_MUTATED_SNAPSHOT);
    CHECK(vc_target_attestation_get_snapshot(
              &fixture_value.attestation, &snapshot) ==
          VC_TARGET_STATUS_UNAVAILABLE);

    length = vc_target_attestation_format_status(
        &fixture_value.attestation, text, sizeof(text));
    CHECK(length == strlen(text));
    CHECK(strstr(text, "PCSA00133") == NULL);
    CHECK(strstr(text, "12345678") == NULL);
    CHECK(strstr(text, "deadbeef") == NULL);
    CHECK(vc_target_attestation_format_status(
              &fixture_value.attestation,
              short_text, sizeof(short_text)) == length);
    CHECK(short_text[sizeof(short_text) - 1u] == '\0');

    fixture_value.adapter.cleanup_ok = false;
    CHECK(vc_target_attestation_stop(
              &fixture_value.attestation) ==
          VC_TARGET_STATUS_CLEANUP_FAILED);
    CHECK(vc_target_attestation_get_status(
              &fixture_value.attestation) ==
          VC_TARGET_RUNTIME_STOPPED);
    CHECK(vc_target_attestation_refresh(
              &fixture_value.attestation) ==
          VC_TARGET_STATUS_STOPPED);
    fixture_value.adapter.cleanup_ok = true;
    CHECK(vc_target_attestation_stop(
              &fixture_value.attestation) ==
          VC_TARGET_STATUS_OK);
    CHECK(fixture_value.adapter.cleanup_calls == 2);
    CHECK(vc_target_attestation_start(
              &fixture_value.attestation) ==
          VC_TARGET_STATUS_OK);
    set_valid_source(&fixture_value.adapter.source);
    CHECK(vc_target_attestation_refresh(
              &fixture_value.attestation) ==
          VC_TARGET_STATUS_OK);
    CHECK(vc_target_attestation_get_snapshot(
              &fixture_value.attestation, &restarted) ==
          VC_TARGET_STATUS_OK);
    CHECK(restarted.revision > snapshot.revision);
}

int main(void)
{
    test_acquisition_and_immutable_revisions();
    test_identity_and_count_validation();
    test_module_segment_and_thread_validation();
    test_adapter_failures_mutation_and_atomicity();
    test_policy_and_b2_segment_lookup();
    test_range_queries();
    test_thread_allowlists();
    test_pid_reuse_and_generation_change();
    test_lifecycle_and_formatter();

    if (failures != 0) {
        fprintf(stderr, "%d target attestation test(s) failed\n",
                failures);
        return 1;
    }
    puts("all target attestation tests passed");
    return 0;
}
