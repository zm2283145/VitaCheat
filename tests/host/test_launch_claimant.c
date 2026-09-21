#include "vitacheat/launch_claimant.h"

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
#define MODULE_GENERATION UINT64_C(0x1112131415161718)
#define REQUEST_ID UINT64_C(0x2122232425262728)

typedef enum response_fault {
    RESPONSE_VALID = 0,
    RESPONSE_WRONG_OPERATION,
    RESPONSE_WRONG_ID,
    RESPONSE_WRONG_TARGET,
    RESPONSE_BAD_VERSION,
    RESPONSE_TRUNCATED,
    RESPONSE_OVERSIZED,
    RESPONSE_WRONG_STATE,
    RESPONSE_REPLAYED_CLAIM
} response_fault;

typedef enum transport_mutation {
    TRANSPORT_MUTATION_NONE = 0,
    TRANSPORT_MUTATION_OVERLAY_OPEN,
    TRANSPORT_MUTATION_PRESENTATION_LOST,
    TRANSPORT_MUTATION_PROCESS_GENERATION,
    TRANSPORT_MUTATION_MODULE_GENERATION,
    TRANSPORT_MUTATION_TITLE,
    TRANSPORT_MUTATION_FOREGROUND_ABSENT,
    TRANSPORT_MUTATION_IDENTITY_SEQUENCE,
    TRANSPORT_MUTATION_TIME_ADVANCE
} transport_mutation;

typedef struct fake_platform {
    vc_launch_claimant *claimant;
    vc_launch_claimant_observation observation;
    uint8_t last_request[VC_LAUNCH_REQUEST_WIRE_SIZE];
    uint8_t retry_request[VC_LAUNCH_REQUEST_WIRE_SIZE];
    uint64_t now_ms;
    vc_launch_claimant_transport_result transport_result;
    response_fault response_fault;
    transport_mutation mutation;
    int32_t discovery_status;
    bool observation_ok;
    bool time_ok;
    bool unload_ok;
    bool reenter;
    unsigned int observation_calls;
    unsigned int time_calls;
    unsigned int transport_calls;
    unsigned int unload_calls;
    unsigned int busy_reentries;
} fake_platform;

static void check_adapter_entry(fake_platform *platform)
{
    vc_launch_overlay_snapshot overlay =
        platform->observation.overlay;

    CHECK(atomic_load_explicit(
              &platform->claimant->transaction_busy,
              memory_order_acquire) == 0u);
    CHECK(atomic_load_explicit(
              &platform->claimant->adapter_active,
              memory_order_acquire) == 1u);
    if (platform->reenter) {
        CHECK(vc_launch_claimant_worker_step(
                  platform->claimant) ==
              VC_LAUNCH_CLAIMANT_RESULT_BUSY);
        CHECK(vc_launch_claimant_notify_overlay(
                  platform->claimant, &overlay) ==
              VC_LAUNCH_CLAIMANT_RESULT_BUSY);
        ++platform->busy_reentries;
    }
}

static bool fake_get_observation(
    void *context,
    vc_launch_claimant_observation *observation)
{
    fake_platform *platform = (fake_platform *)context;

    check_adapter_entry(platform);
    ++platform->observation_calls;
    if (platform->observation_ok) {
        *observation = platform->observation;
    }
    return platform->observation_ok;
}

static bool fake_get_time(void *context, uint64_t *now_ms)
{
    fake_platform *platform = (fake_platform *)context;

    check_adapter_entry(platform);
    ++platform->time_calls;
    if (platform->time_ok) {
        *now_ms = platform->now_ms;
    }
    return platform->time_ok;
}

static int32_t response_status_for_state(uint32_t state)
{
    switch ((vc_launch_state)state) {
    case VC_LAUNCH_STATE_PENDING:
        return VC_LAUNCH_STATUS_PENDING;
    case VC_LAUNCH_STATE_CLAIMED:
        return VC_LAUNCH_STATUS_CLAIMED;
    case VC_LAUNCH_STATE_EXPIRED:
        return VC_LAUNCH_STATUS_EXPIRED;
    case VC_LAUNCH_STATE_CANCELLED:
        return VC_LAUNCH_STATUS_CANCELLED;
    case VC_LAUNCH_STATE_STALE_TARGET:
        return VC_LAUNCH_STATUS_STALE_TARGET;
    case VC_LAUNCH_STATE_CLOCK_ROLLBACK:
        return VC_LAUNCH_STATUS_CLOCK_ROLLBACK;
    case VC_LAUNCH_STATE_ABSENT:
    default:
        return VC_LAUNCH_STATUS_ABSENT;
    }
}

static void mutate_during_transport(fake_platform *platform)
{
    switch (platform->mutation) {
    case TRANSPORT_MUTATION_OVERLAY_OPEN:
        ++platform->observation.overlay.sequence;
        platform->observation.overlay.state =
            VC_LAUNCH_OVERLAY_OPEN;
        break;
    case TRANSPORT_MUTATION_PRESENTATION_LOST:
        ++platform->observation.presentation.sequence;
        platform->observation.presentation.state =
            VC_LAUNCH_PRESENTATION_LOST;
        break;
    case TRANSPORT_MUTATION_PROCESS_GENERATION:
        ++platform->observation.identity.sequence;
        ++platform->observation.identity.caller.process_generation;
        ++platform->observation.identity.foreground.sequence;
        ++platform->observation.identity.foreground.target_generation;
        ++platform->observation.presentation.sequence;
        ++platform->observation.presentation.process_generation;
        break;
    case TRANSPORT_MUTATION_MODULE_GENERATION:
        ++platform->observation.identity.sequence;
        ++platform->observation.identity.caller.module_generation;
        ++platform->observation.presentation.sequence;
        ++platform->observation.presentation.module_generation;
        break;
    case TRANSPORT_MUTATION_TITLE:
        ++platform->observation.identity.sequence;
        ++platform->observation.identity.foreground.sequence;
        platform->observation.identity.title_id[8] ^= 1u;
        platform->observation.identity.foreground.title_id[8] ^= 1u;
        break;
    case TRANSPORT_MUTATION_FOREGROUND_ABSENT:
        ++platform->observation.identity.sequence;
        memset(&platform->observation.identity.foreground, 0,
               sizeof(platform->observation.identity.foreground));
        platform->observation.identity.foreground.sequence = 2;
        break;
    case TRANSPORT_MUTATION_IDENTITY_SEQUENCE:
        ++platform->observation.identity.sequence;
        break;
    case TRANSPORT_MUTATION_TIME_ADVANCE:
        platform->now_ms += 500;
        break;
    case TRANSPORT_MUTATION_NONE:
    default:
        break;
    }
}

static vc_launch_claimant_transport_result fake_transport(
    void *context,
    const uint8_t *request_wire,
    size_t request_size,
    uint8_t *response_wire,
    size_t response_capacity,
    size_t *response_size)
{
    fake_platform *platform = (fake_platform *)context;
    vc_launch_request request;
    vc_launch_response response;
    uint32_t state;
    size_t encoded_size = 0;

    check_adapter_entry(platform);
    ++platform->transport_calls;
    CHECK(request_size == VC_LAUNCH_REQUEST_WIRE_SIZE);
    CHECK(response_capacity == VC_LAUNCH_RESPONSE_WIRE_SIZE);
    if (platform->transport_calls == 1) {
        memcpy(platform->last_request, request_wire, request_size);
    } else {
        memcpy(platform->retry_request, request_wire, request_size);
    }
    *response_size = 0;
    mutate_during_transport(platform);
    if (platform->transport_result !=
        VC_LAUNCH_CLAIMANT_TRANSPORT_OK) {
        return platform->transport_result;
    }
    CHECK(vc_launch_request_decode(
              request_wire, request_size, &request) ==
          VC_LAUNCH_STATUS_OK);

    if (request.operation == VC_LAUNCH_OPERATION_STATUS) {
        state = platform->discovery_status ==
                        VC_LAUNCH_STATUS_ABSENT
                    ? VC_LAUNCH_STATE_ABSENT
                    : VC_LAUNCH_STATE_PENDING;
    } else if (request.operation == VC_LAUNCH_OPERATION_CLAIM) {
        state = platform->response_fault ==
                        RESPONSE_REPLAYED_CLAIM
                    ? VC_LAUNCH_STATE_CLAIMED
                    : VC_LAUNCH_STATE_CLAIMED;
    } else {
        state = VC_LAUNCH_STATE_CANCELLED;
    }

    memset(&response, 0, sizeof(response));
    response.version = VC_LAUNCH_ABI_VERSION;
    response.struct_size = VC_LAUNCH_RESPONSE_WIRE_SIZE;
    response.operation = request.operation;
    response.status = response_status_for_state(state);
    response.launch_state = state;
    response.capabilities =
        state == VC_LAUNCH_STATE_ABSENT
            ? 0
            : platform->response_fault == RESPONSE_REPLAYED_CLAIM
                  ? 0
                  : request.capabilities;
    if (state != VC_LAUNCH_STATE_ABSENT) {
        response.target_process_id = request.target_process_id;
        response.target_generation = request.target_generation;
        response.request_id =
            request.operation == VC_LAUNCH_OPERATION_STATUS
                ? REQUEST_ID
                : request.request_id;
        response.created_ms = request.now_ms;
        response.deadline_ms = request.now_ms + 500;
    }
    response.observed_ms = request.now_ms;

    switch (platform->response_fault) {
    case RESPONSE_WRONG_OPERATION:
        response.operation = VC_LAUNCH_OPERATION_SUBMIT;
        response.capabilities = VC_LAUNCH_CAPABILITY_LAUNCH;
        break;
    case RESPONSE_WRONG_ID:
        ++response.request_id;
        break;
    case RESPONSE_WRONG_TARGET:
        ++response.target_process_id;
        break;
    case RESPONSE_VALID:
    case RESPONSE_BAD_VERSION:
    case RESPONSE_TRUNCATED:
    case RESPONSE_OVERSIZED:
    case RESPONSE_WRONG_STATE:
    case RESPONSE_REPLAYED_CLAIM:
    default:
        break;
    }
    if (platform->response_fault == RESPONSE_WRONG_STATE) {
        response.status = VC_LAUNCH_STATUS_EXPIRED;
    }
    CHECK(vc_launch_response_encode(
              &response, response_wire, response_capacity,
              &encoded_size) == VC_LAUNCH_STATUS_OK);
    if (platform->response_fault == RESPONSE_BAD_VERSION) {
        response_wire[0] = 2;
    }
    if (platform->response_fault == RESPONSE_TRUNCATED) {
        *response_size = VC_LAUNCH_RESPONSE_WIRE_SIZE - 1u;
    } else if (platform->response_fault == RESPONSE_OVERSIZED) {
        *response_size = VC_LAUNCH_RESPONSE_WIRE_SIZE + 1u;
    } else {
        *response_size = encoded_size;
    }
    return VC_LAUNCH_CLAIMANT_TRANSPORT_OK;
}

static bool fake_plugin_unload(
    void *context,
    const vc_launch_claimant_identity_snapshot *identity,
    uint64_t now_ms)
{
    fake_platform *platform = (fake_platform *)context;

    check_adapter_entry(platform);
    CHECK(identity->caller.process_id == TARGET_PID);
    CHECK(identity->caller.process_generation == TARGET_GENERATION);
    CHECK(identity->caller.module_generation == MODULE_GENERATION);
    CHECK(now_ms == platform->now_ms);
    ++platform->unload_calls;
    return platform->unload_ok;
}

static void set_identity(fake_platform *platform)
{
    static const uint8_t title[] = {
        'P', 'C', 'S', 'A', '0', '0', '0', '0', '1'
    };
    vc_launch_claimant_identity_snapshot *identity =
        &platform->observation.identity;

    memset(&platform->observation, 0,
           sizeof(platform->observation));
    identity->sequence = 1;
    identity->caller.role = VC_LAUNCH_CALLER_GAME_PLUGIN;
    identity->caller.process_id = TARGET_PID;
    identity->caller.process_generation = TARGET_GENERATION;
    identity->caller.module_generation = MODULE_GENERATION;
    identity->foreground.sequence = 1;
    identity->foreground.present = 1;
    identity->foreground.target_process_id = TARGET_PID;
    identity->foreground.target_generation = TARGET_GENERATION;
    identity->foreground.title_id_size = sizeof(title);
    memcpy(identity->foreground.title_id, title, sizeof(title));
    identity->title_id_size = sizeof(title);
    memcpy(identity->title_id, title, sizeof(title));

    platform->observation.overlay.sequence = 1;
    platform->observation.overlay.generation = 1;
    platform->observation.overlay.state =
        VC_LAUNCH_OVERLAY_UNKNOWN;

    platform->observation.presentation.sequence = 1;
    platform->observation.presentation.process_id = TARGET_PID;
    platform->observation.presentation.process_generation =
        TARGET_GENERATION;
    platform->observation.presentation.module_generation =
        MODULE_GENERATION;
    platform->observation.presentation.state =
        VC_LAUNCH_PRESENTATION_UNAVAILABLE;
}

static vc_launch_claimant_dependencies dependencies_for(
    fake_platform *platform)
{
    vc_launch_claimant_dependencies dependencies;

    memset(&dependencies, 0, sizeof(dependencies));
    dependencies.get_observation = fake_get_observation;
    dependencies.get_time = fake_get_time;
    dependencies.transport = fake_transport;
    dependencies.plugin_unload = fake_plugin_unload;
    dependencies.context = platform;
    return dependencies;
}

static void initialize(vc_launch_claimant *claimant,
                       fake_platform *platform)
{
    vc_launch_claimant_dependencies dependencies;

    memset(claimant, 0, sizeof(*claimant));
    memset(platform, 0, sizeof(*platform));
    platform->claimant = claimant;
    platform->observation_ok = true;
    platform->time_ok = true;
    platform->unload_ok = true;
    platform->now_ms = 100;
    platform->transport_result =
        VC_LAUNCH_CLAIMANT_TRANSPORT_OK;
    platform->discovery_status = VC_LAUNCH_STATUS_PENDING;
    set_identity(platform);
    dependencies = dependencies_for(platform);
    CHECK(vc_launch_claimant_init(
              claimant, &dependencies, 10) ==
          VC_LAUNCH_CLAIMANT_RESULT_OK);
    CHECK(vc_launch_claimant_start(claimant) ==
          VC_LAUNCH_CLAIMANT_RESULT_OK);
    CHECK(vc_launch_claimant_bind_identity(claimant) ==
          VC_LAUNCH_CLAIMANT_RESULT_OK);
}

static void notify_overlay(vc_launch_claimant *claimant,
                           fake_platform *platform,
                           vc_launch_overlay_state state)
{
    ++platform->observation.overlay.sequence;
    platform->observation.overlay.state = (uint32_t)state;
    CHECK(vc_launch_claimant_notify_overlay(
              claimant, &platform->observation.overlay) ==
          VC_LAUNCH_CLAIMANT_RESULT_OK);
}

static void notify_presentation(
    vc_launch_claimant *claimant,
    fake_platform *platform,
    vc_launch_presentation_state state)
{
    ++platform->observation.presentation.sequence;
    platform->observation.presentation.state = (uint32_t)state;
    CHECK(vc_launch_claimant_notify_presentation(
              claimant, &platform->observation.presentation) ==
          VC_LAUNCH_CLAIMANT_RESULT_OK);
}

static void make_ready(vc_launch_claimant *claimant,
                       fake_platform *platform)
{
    notify_overlay(claimant, platform, VC_LAUNCH_OVERLAY_CLOSED);
    notify_overlay(claimant, platform, VC_LAUNCH_OVERLAY_CLOSED);
    notify_presentation(
        claimant, platform, VC_LAUNCH_PRESENTATION_READY);
}

static void discover_and_claim(vc_launch_claimant *claimant,
                               fake_platform *platform)
{
    unsigned int transport_calls = platform->transport_calls;

    CHECK(vc_launch_claimant_request_claim(claimant) ==
          VC_LAUNCH_CLAIMANT_RESULT_OK);
    CHECK(vc_launch_claimant_worker_step(claimant) ==
          VC_LAUNCH_CLAIMANT_RESULT_OK);
    CHECK(vc_launch_claimant_worker_step(claimant) ==
          VC_LAUNCH_CLAIMANT_RESULT_OK);
    CHECK(platform->transport_calls == transport_calls + 2u);
    CHECK(vc_launch_claimant_get_status(claimant) ==
          VC_LAUNCH_CLAIMANT_STATUS_OPEN_AUTHORIZED);
}

static void test_happy_path_and_gates(void)
{
    static const uint8_t expected_status[VC_LAUNCH_REQUEST_WIRE_SIZE] = {
        0x01, 0x00, 0x40, 0x00, 0x04, 0x00, 0x02, 0x00,
        0x08, 0x00, 0x00, 0x00, 0x78, 0x56, 0x34, 0x12,
        0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x64, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    vc_launch_claimant claimant = VC_LAUNCH_CLAIMANT_INITIALIZER;
    vc_launch_open_authorization authorization;
    vc_launch_open_authorization_snapshot authorization_snapshot;
    fake_platform platform;
    vc_launch_request request;

    initialize(&claimant, &platform);
    CHECK(vc_launch_claimant_request_claim(&claimant) ==
          VC_LAUNCH_CLAIMANT_RESULT_OK);
    CHECK(vc_launch_claimant_worker_step(&claimant) ==
          VC_LAUNCH_CLAIMANT_RESULT_NO_ACTION);
    CHECK(platform.transport_calls == 0);
    CHECK(vc_launch_claimant_get_status(&claimant) ==
          VC_LAUNCH_CLAIMANT_STATUS_WAITING_OVERLAY_CLOSE);

    notify_overlay(&claimant, &platform, VC_LAUNCH_OVERLAY_OPEN);
    notify_overlay(&claimant, &platform, VC_LAUNCH_OVERLAY_CLOSING);
    notify_overlay(&claimant, &platform, VC_LAUNCH_OVERLAY_CLOSED);
    notify_overlay(&claimant, &platform, VC_LAUNCH_OVERLAY_OPEN);
    CHECK(vc_launch_claimant_worker_step(&claimant) ==
          VC_LAUNCH_CLAIMANT_RESULT_NO_ACTION);
    CHECK(platform.transport_calls == 0);

    notify_overlay(&claimant, &platform, VC_LAUNCH_OVERLAY_CLOSED);
    CHECK(vc_launch_claimant_worker_step(&claimant) ==
          VC_LAUNCH_CLAIMANT_RESULT_NO_ACTION);
    notify_overlay(&claimant, &platform, VC_LAUNCH_OVERLAY_CLOSED);
    CHECK(vc_launch_claimant_worker_step(&claimant) ==
          VC_LAUNCH_CLAIMANT_RESULT_NO_ACTION);
    CHECK(vc_launch_claimant_get_status(&claimant) ==
          VC_LAUNCH_CLAIMANT_STATUS_WAITING_PRESENTATION);

    notify_presentation(
        &claimant, &platform, VC_LAUNCH_PRESENTATION_PROBING);
    CHECK(vc_launch_claimant_worker_step(&claimant) ==
          VC_LAUNCH_CLAIMANT_RESULT_NO_ACTION);
    notify_presentation(
        &claimant, &platform, VC_LAUNCH_PRESENTATION_READY);
    CHECK(vc_launch_claimant_worker_step(&claimant) ==
          VC_LAUNCH_CLAIMANT_RESULT_OK);
    CHECK(memcmp(platform.last_request, expected_status,
                 sizeof(expected_status)) == 0);
    CHECK(vc_launch_request_decode(
              platform.last_request, sizeof(platform.last_request),
              &request) == VC_LAUNCH_STATUS_OK);
    CHECK(request.operation == VC_LAUNCH_OPERATION_STATUS);
    CHECK(request.caller_role == VC_LAUNCH_CALLER_GAME_PLUGIN);
    CHECK(request.capabilities == VC_LAUNCH_CAPABILITY_STATUS);
    CHECK(request.request_id == 0);

    CHECK(vc_launch_claimant_worker_step(&claimant) ==
          VC_LAUNCH_CLAIMANT_RESULT_OK);
    CHECK(vc_launch_request_decode(
              platform.retry_request, sizeof(platform.retry_request),
              &request) == VC_LAUNCH_STATUS_OK);
    CHECK(request.operation == VC_LAUNCH_OPERATION_CLAIM);
    CHECK(request.caller_role == VC_LAUNCH_CALLER_GAME_PLUGIN);
    CHECK(request.capabilities == VC_LAUNCH_CAPABILITY_CLAIM);
    CHECK(request.request_id == REQUEST_ID);
    CHECK(request.presentation_ready == 1);
    CHECK(vc_launch_claimant_get_status(&claimant) ==
          VC_LAUNCH_CLAIMANT_STATUS_OPEN_AUTHORIZED);

    CHECK(vc_launch_claimant_inspect_open_authorization(
              &claimant, &authorization_snapshot) ==
          VC_LAUNCH_CLAIMANT_RESULT_OK);
    CHECK(authorization_snapshot.authorization.value != 0);
    CHECK(authorization_snapshot.authorization.lifecycle_generation ==
          claimant.lifecycle_generation);
    CHECK(authorization_snapshot.observation.identity.caller.process_id ==
          TARGET_PID);
    CHECK(vc_launch_claimant_get_status(&claimant) ==
          VC_LAUNCH_CLAIMANT_STATUS_OPEN_AUTHORIZED);

    CHECK(vc_launch_claimant_consume_open_authorization(
              &claimant, &authorization) ==
          VC_LAUNCH_CLAIMANT_RESULT_OK);
    CHECK(authorization.value ==
          authorization_snapshot.authorization.value);
    CHECK(authorization.value != 0);
    CHECK(authorization.value != REQUEST_ID);
    CHECK(vc_launch_claimant_consume_open_authorization(
              &claimant, &authorization) ==
          VC_LAUNCH_CLAIMANT_RESULT_NO_AUTHORIZATION);
    CHECK(vc_launch_claimant_inspect_open_authorization(
              &claimant, &authorization_snapshot) ==
          VC_LAUNCH_CLAIMANT_RESULT_NO_AUTHORIZATION);
    CHECK(vc_launch_claimant_validate_open_authorization(
              &claimant, &authorization,
              VC_LAUNCH_CLAIMANT_STATUS_AUTHORIZATION_CONSUMED) ==
          VC_LAUNCH_CLAIMANT_RESULT_OK);
    CHECK(vc_launch_claimant_validate_open_authorization(
              &claimant, &authorization,
              VC_LAUNCH_CLAIMANT_STATUS_OPEN) ==
          VC_LAUNCH_CLAIMANT_RESULT_NO_AUTHORIZATION);
    ++authorization_snapshot.authorization.value;
    CHECK(vc_launch_claimant_validate_open_authorization(
              &claimant,
              &authorization_snapshot.authorization,
              VC_LAUNCH_CLAIMANT_STATUS_AUTHORIZATION_CONSUMED) ==
          VC_LAUNCH_CLAIMANT_RESULT_NO_AUTHORIZATION);
    CHECK(vc_launch_claimant_acknowledge_open(
              &claimant, &authorization) ==
          VC_LAUNCH_CLAIMANT_RESULT_OK);
    CHECK(vc_launch_claimant_get_status(&claimant) ==
          VC_LAUNCH_CLAIMANT_STATUS_OPEN);
    CHECK(vc_launch_claimant_validate_open_authorization(
              &claimant, &authorization,
              VC_LAUNCH_CLAIMANT_STATUS_OPEN) ==
          VC_LAUNCH_CLAIMANT_RESULT_OK);
    CHECK(vc_launch_claimant_validate_open_authorization(
              &claimant, &authorization,
              VC_LAUNCH_CLAIMANT_STATUS_AUTHORIZATION_CONSUMED) ==
          VC_LAUNCH_CLAIMANT_RESULT_NO_AUTHORIZATION);
    CHECK(vc_launch_claimant_acknowledge_open(
              &claimant, &authorization) ==
          VC_LAUNCH_CLAIMANT_RESULT_NO_AUTHORIZATION);
    CHECK(vc_launch_claimant_worker_step(&claimant) ==
          VC_LAUNCH_CLAIMANT_RESULT_NO_ACTION);
    CHECK(platform.transport_calls == 2);
}

static void test_gate_and_identity_changes(void)
{
    const transport_mutation mutations[] = {
        TRANSPORT_MUTATION_OVERLAY_OPEN,
        TRANSPORT_MUTATION_PRESENTATION_LOST,
        TRANSPORT_MUTATION_PROCESS_GENERATION,
        TRANSPORT_MUTATION_MODULE_GENERATION,
        TRANSPORT_MUTATION_TITLE,
        TRANSPORT_MUTATION_FOREGROUND_ABSENT,
        TRANSPORT_MUTATION_IDENTITY_SEQUENCE
    };
    size_t index;

    for (index = 0;
         index < sizeof(mutations) / sizeof(mutations[0]);
         ++index) {
        vc_launch_claimant claimant =
            VC_LAUNCH_CLAIMANT_INITIALIZER;
        fake_platform platform;

        initialize(&claimant, &platform);
        make_ready(&claimant, &platform);
        CHECK(vc_launch_claimant_request_claim(&claimant) ==
              VC_LAUNCH_CLAIMANT_RESULT_OK);
        platform.mutation = mutations[index];
        CHECK(vc_launch_claimant_worker_step(&claimant) ==
              VC_LAUNCH_CLAIMANT_RESULT_STALE_IDENTITY);
        CHECK(!claimant.authorization_available);
        CHECK(claimant.request_id == 0);
    }

    {
        vc_launch_claimant claimant =
            VC_LAUNCH_CLAIMANT_INITIALIZER;
        fake_platform platform;

        initialize(&claimant, &platform);
        make_ready(&claimant, &platform);
        discover_and_claim(&claimant, &platform);
        notify_overlay(
            &claimant, &platform, VC_LAUNCH_OVERLAY_OPEN);
        CHECK(!claimant.authorization_available);
        CHECK(vc_launch_claimant_get_status(&claimant) ==
              VC_LAUNCH_CLAIMANT_STATUS_CANCELLED);
    }

    {
        vc_launch_claimant claimant =
            VC_LAUNCH_CLAIMANT_INITIALIZER;
        fake_platform platform;

        initialize(&claimant, &platform);
        make_ready(&claimant, &platform);
        discover_and_claim(&claimant, &platform);
        notify_presentation(
            &claimant, &platform,
            VC_LAUNCH_PRESENTATION_LOST);
        CHECK(!claimant.authorization_available);
        CHECK(vc_launch_claimant_get_status(&claimant) ==
              VC_LAUNCH_CLAIMANT_STATUS_CANCELLED);
    }
}

static void test_observation_gate_matrix(void)
{
    uint32_t overlay_state;
    uint32_t presentation_state;

    for (overlay_state = VC_LAUNCH_OVERLAY_UNKNOWN;
         overlay_state <= VC_LAUNCH_OVERLAY_CLOSED;
         ++overlay_state) {
        for (presentation_state =
                 VC_LAUNCH_PRESENTATION_UNAVAILABLE;
             presentation_state <= VC_LAUNCH_PRESENTATION_LOST;
             ++presentation_state) {
            vc_launch_claimant claimant =
                VC_LAUNCH_CLAIMANT_INITIALIZER;
            fake_platform platform;
            vc_launch_claimant_result result;

            initialize(&claimant, &platform);
            notify_overlay(
                &claimant, &platform,
                (vc_launch_overlay_state)overlay_state);
            if (overlay_state == VC_LAUNCH_OVERLAY_CLOSED) {
                notify_overlay(
                    &claimant, &platform,
                    VC_LAUNCH_OVERLAY_CLOSED);
            }
            notify_presentation(
                &claimant, &platform,
                (vc_launch_presentation_state)
                    presentation_state);
            CHECK(vc_launch_claimant_request_claim(&claimant) ==
                  VC_LAUNCH_CLAIMANT_RESULT_OK);
            result = vc_launch_claimant_worker_step(&claimant);
            if (overlay_state == VC_LAUNCH_OVERLAY_CLOSED &&
                presentation_state ==
                    VC_LAUNCH_PRESENTATION_READY) {
                CHECK(result == VC_LAUNCH_CLAIMANT_RESULT_OK);
                CHECK(platform.transport_calls == 1);
            } else {
                CHECK(result ==
                      VC_LAUNCH_CLAIMANT_RESULT_NO_ACTION);
                CHECK(platform.transport_calls == 0);
                CHECK(vc_launch_claimant_get_status(&claimant) ==
                      (overlay_state ==
                               VC_LAUNCH_OVERLAY_CLOSED
                           ? VC_LAUNCH_CLAIMANT_STATUS_WAITING_PRESENTATION
                           : VC_LAUNCH_CLAIMANT_STATUS_WAITING_OVERLAY_CLOSE));
            }
        }
    }
}

static void test_consumed_open_and_sequence_changes(void)
{
    vc_launch_claimant claimant =
        VC_LAUNCH_CLAIMANT_INITIALIZER;
    fake_platform platform;
    vc_launch_open_authorization authorization;

    initialize(&claimant, &platform);
    make_ready(&claimant, &platform);
    discover_and_claim(&claimant, &platform);
    CHECK(vc_launch_claimant_consume_open_authorization(
              &claimant, &authorization) ==
          VC_LAUNCH_CLAIMANT_RESULT_OK);
    notify_presentation(
        &claimant, &platform, VC_LAUNCH_PRESENTATION_LOST);
    CHECK(vc_launch_claimant_acknowledge_open(
              &claimant, &authorization) ==
          VC_LAUNCH_CLAIMANT_RESULT_NO_AUTHORIZATION);
    CHECK(vc_launch_claimant_get_status(&claimant) ==
          VC_LAUNCH_CLAIMANT_STATUS_CANCELLED);

    CHECK(vc_launch_claimant_reset(&claimant) ==
          VC_LAUNCH_CLAIMANT_RESULT_OK);
    ++platform.now_ms;
    set_identity(&platform);
    CHECK(vc_launch_claimant_bind_identity(&claimant) ==
          VC_LAUNCH_CLAIMANT_RESULT_OK);
    make_ready(&claimant, &platform);
    discover_and_claim(&claimant, &platform);
    CHECK(vc_launch_claimant_consume_open_authorization(
              &claimant, &authorization) ==
          VC_LAUNCH_CLAIMANT_RESULT_OK);
    CHECK(vc_launch_claimant_acknowledge_open(
              &claimant, &authorization) ==
          VC_LAUNCH_CLAIMANT_RESULT_OK);
    ++platform.now_ms;
    CHECK(vc_launch_claimant_tick(&claimant) ==
          VC_LAUNCH_CLAIMANT_RESULT_OK);
    CHECK(vc_launch_claimant_get_status(&claimant) ==
          VC_LAUNCH_CLAIMANT_STATUS_OPEN);
    notify_overlay(
        &claimant, &platform, VC_LAUNCH_OVERLAY_OPEN);
    CHECK(vc_launch_claimant_get_status(&claimant) ==
          VC_LAUNCH_CLAIMANT_STATUS_CANCELLED);

    CHECK(vc_launch_claimant_reset(&claimant) ==
          VC_LAUNCH_CLAIMANT_RESULT_OK);
    ++platform.now_ms;
    set_identity(&platform);
    platform.observation.identity.sequence = 2;
    CHECK(vc_launch_claimant_bind_identity(&claimant) ==
          VC_LAUNCH_CLAIMANT_RESULT_OK);
    platform.observation.identity.sequence = 1;
    CHECK(vc_launch_claimant_observe(&claimant) ==
          VC_LAUNCH_CLAIMANT_RESULT_STALE_IDENTITY);
    CHECK(vc_launch_claimant_get_status(&claimant) ==
          VC_LAUNCH_CLAIMANT_STATUS_STALE);
}

static void test_transport_validation_and_retry(void)
{
    const response_fault faults[] = {
        RESPONSE_WRONG_OPERATION,
        RESPONSE_WRONG_ID,
        RESPONSE_WRONG_TARGET,
        RESPONSE_BAD_VERSION,
        RESPONSE_TRUNCATED,
        RESPONSE_OVERSIZED,
        RESPONSE_WRONG_STATE
    };
    size_t index;

    for (index = 0;
         index < sizeof(faults) / sizeof(faults[0]);
         ++index) {
        vc_launch_claimant claimant =
            VC_LAUNCH_CLAIMANT_INITIALIZER;
        fake_platform platform;

        initialize(&claimant, &platform);
        make_ready(&claimant, &platform);
        CHECK(vc_launch_claimant_request_claim(&claimant) ==
              VC_LAUNCH_CLAIMANT_RESULT_OK);
        if (faults[index] == RESPONSE_WRONG_ID) {
            CHECK(vc_launch_claimant_worker_step(&claimant) ==
                  VC_LAUNCH_CLAIMANT_RESULT_OK);
        }
        platform.response_fault = faults[index];
        CHECK(vc_launch_claimant_worker_step(&claimant) ==
              VC_LAUNCH_CLAIMANT_RESULT_PROTOCOL_FAILURE);
        CHECK(!claimant.authorization_available);
        CHECK(claimant.request_id == 0);
        CHECK(vc_launch_claimant_get_status(&claimant) ==
              VC_LAUNCH_CLAIMANT_STATUS_ERROR);
    }

    {
        vc_launch_claimant claimant =
            VC_LAUNCH_CLAIMANT_INITIALIZER;
        fake_platform platform;

        initialize(&claimant, &platform);
        make_ready(&claimant, &platform);
        CHECK(vc_launch_claimant_request_claim(&claimant) ==
              VC_LAUNCH_CLAIMANT_RESULT_OK);
        platform.transport_result =
            VC_LAUNCH_CLAIMANT_TRANSPORT_RETRY;
        CHECK(vc_launch_claimant_worker_step(&claimant) ==
              VC_LAUNCH_CLAIMANT_RESULT_RETRY);
        CHECK(claimant.request_wire_valid);
        platform.transport_result =
            VC_LAUNCH_CLAIMANT_TRANSPORT_OK;
        CHECK(vc_launch_claimant_worker_step(&claimant) ==
              VC_LAUNCH_CLAIMANT_RESULT_OK);
        CHECK(memcmp(platform.last_request,
                     platform.retry_request,
                     sizeof(platform.last_request)) == 0);
    }

    {
        vc_launch_claimant claimant =
            VC_LAUNCH_CLAIMANT_INITIALIZER;
        fake_platform platform;

        initialize(&claimant, &platform);
        make_ready(&claimant, &platform);
        CHECK(vc_launch_claimant_request_claim(&claimant) ==
              VC_LAUNCH_CLAIMANT_RESULT_OK);
        platform.transport_result =
            VC_LAUNCH_CLAIMANT_TRANSPORT_BUSY;
        CHECK(vc_launch_claimant_worker_step(&claimant) ==
              VC_LAUNCH_CLAIMANT_RESULT_RETRY);
        CHECK(!claimant.request_wire_valid);
        ++platform.now_ms;
        platform.transport_result =
            VC_LAUNCH_CLAIMANT_TRANSPORT_OK;
        CHECK(vc_launch_claimant_worker_step(&claimant) ==
              VC_LAUNCH_CLAIMANT_RESULT_OK);
        CHECK(memcmp(platform.last_request,
                     platform.retry_request,
                     sizeof(platform.last_request)) != 0);
    }

    {
        vc_launch_claimant claimant =
            VC_LAUNCH_CLAIMANT_INITIALIZER;
        fake_platform platform;

        initialize(&claimant, &platform);
        make_ready(&claimant, &platform);
        CHECK(vc_launch_claimant_request_claim(&claimant) ==
              VC_LAUNCH_CLAIMANT_RESULT_OK);
        CHECK(vc_launch_claimant_worker_step(&claimant) ==
              VC_LAUNCH_CLAIMANT_RESULT_OK);
        platform.response_fault = RESPONSE_REPLAYED_CLAIM;
        CHECK(vc_launch_claimant_worker_step(&claimant) ==
              VC_LAUNCH_CLAIMANT_RESULT_PROTOCOL_FAILURE);
        CHECK(!claimant.authorization_available);
    }

    {
        vc_launch_claimant claimant =
            VC_LAUNCH_CLAIMANT_INITIALIZER;
        fake_platform platform;

        initialize(&claimant, &platform);
        make_ready(&claimant, &platform);
        CHECK(vc_launch_claimant_request_claim(&claimant) ==
              VC_LAUNCH_CLAIMANT_RESULT_OK);
        CHECK(vc_launch_claimant_worker_step(&claimant) ==
              VC_LAUNCH_CLAIMANT_RESULT_OK);
        platform.mutation = TRANSPORT_MUTATION_TIME_ADVANCE;
        CHECK(vc_launch_claimant_worker_step(&claimant) ==
              VC_LAUNCH_CLAIMANT_RESULT_PROTOCOL_FAILURE);
        CHECK(!claimant.authorization_available);
        CHECK(claimant.request_id == 0);
    }
}

static void test_deadlines_cancel_and_lifecycle(void)
{
    vc_launch_claimant claimant =
        VC_LAUNCH_CLAIMANT_INITIALIZER;
    fake_platform platform;
    vc_launch_open_authorization authorization;
    vc_launch_request request;
    uint64_t old_lifecycle;

    initialize(&claimant, &platform);
    make_ready(&claimant, &platform);
    discover_and_claim(&claimant, &platform);
    platform.now_ms = 109;
    CHECK(vc_launch_claimant_tick(&claimant) ==
          VC_LAUNCH_CLAIMANT_RESULT_OK);
    platform.now_ms = 110;
    CHECK(vc_launch_claimant_consume_open_authorization(
              &claimant, &authorization) ==
          VC_LAUNCH_CLAIMANT_RESULT_AUTHORIZATION_EXPIRED);
    CHECK(vc_launch_claimant_get_status(&claimant) ==
          VC_LAUNCH_CLAIMANT_STATUS_EXPIRED);

    CHECK(vc_launch_claimant_reset(&claimant) ==
          VC_LAUNCH_CLAIMANT_RESULT_OK);
    platform.now_ms = 200;
    set_identity(&platform);
    CHECK(vc_launch_claimant_bind_identity(&claimant) ==
          VC_LAUNCH_CLAIMANT_RESULT_OK);
    make_ready(&claimant, &platform);
    CHECK(vc_launch_claimant_request_claim(&claimant) ==
          VC_LAUNCH_CLAIMANT_RESULT_OK);
    CHECK(vc_launch_claimant_worker_step(&claimant) ==
          VC_LAUNCH_CLAIMANT_RESULT_OK);
    CHECK(claimant.request_id == REQUEST_ID);
    CHECK(vc_launch_claimant_cancel(&claimant) ==
          VC_LAUNCH_CLAIMANT_RESULT_OK);
    CHECK(vc_launch_claimant_worker_step(&claimant) ==
          VC_LAUNCH_CLAIMANT_RESULT_OK);
    CHECK(vc_launch_request_decode(
              platform.retry_request,
              sizeof(platform.retry_request), &request) ==
          VC_LAUNCH_STATUS_OK);
    CHECK(request.operation == VC_LAUNCH_OPERATION_CANCEL);
    CHECK(request.caller_role == VC_LAUNCH_CALLER_GAME_PLUGIN);
    CHECK(request.capabilities == VC_LAUNCH_CAPABILITY_CANCEL);
    CHECK(request.request_id == REQUEST_ID);
    CHECK(claimant.request_id == 0);

    CHECK(vc_launch_claimant_reset(&claimant) ==
          VC_LAUNCH_CLAIMANT_RESULT_OK);
    platform.now_ms = 400;
    set_identity(&platform);
    CHECK(vc_launch_claimant_bind_identity(&claimant) ==
          VC_LAUNCH_CLAIMANT_RESULT_OK);
    make_ready(&claimant, &platform);
    CHECK(vc_launch_claimant_request_claim(&claimant) ==
          VC_LAUNCH_CLAIMANT_RESULT_OK);
    CHECK(vc_launch_claimant_worker_step(&claimant) ==
          VC_LAUNCH_CLAIMANT_RESULT_OK);
    platform.now_ms = claimant.request_deadline_ms;
    CHECK(vc_launch_claimant_tick(&claimant) ==
          VC_LAUNCH_CLAIMANT_RESULT_AUTHORIZATION_EXPIRED);
    CHECK(claimant.request_id == 0);
    CHECK(vc_launch_claimant_get_status(&claimant) ==
          VC_LAUNCH_CLAIMANT_STATUS_EXPIRED);

    old_lifecycle = claimant.lifecycle_generation;
    CHECK(vc_launch_claimant_stop(&claimant) ==
          VC_LAUNCH_CLAIMANT_RESULT_OK);
    CHECK(claimant.lifecycle_generation > old_lifecycle);
    CHECK(claimant.request_id == 0);
    CHECK(claimant.authorization_id == 0);
    CHECK(vc_launch_claimant_notify_overlay(
              &claimant, &platform.observation.overlay) ==
          VC_LAUNCH_CLAIMANT_RESULT_NOT_RUNNING);
    CHECK(vc_launch_claimant_stop(&claimant) ==
          VC_LAUNCH_CLAIMANT_RESULT_OK);

    CHECK(vc_launch_claimant_start(&claimant) ==
          VC_LAUNCH_CLAIMANT_RESULT_OK);
    CHECK(vc_launch_claimant_bind_identity(&claimant) ==
          VC_LAUNCH_CLAIMANT_RESULT_OK);
    platform.unload_ok = false;
    CHECK(vc_launch_claimant_plugin_unload(&claimant) ==
          VC_LAUNCH_CLAIMANT_RESULT_CLEANUP_FAILED);
    CHECK(claimant.phase == VC_LAUNCH_CLAIMANT_PHASE_STOPPED);
    CHECK(platform.unload_calls == 1);
    platform.unload_ok = true;
    CHECK(vc_launch_claimant_plugin_unload(&claimant) ==
          VC_LAUNCH_CLAIMANT_RESULT_OK);
    CHECK(platform.unload_calls == 2);
    CHECK(vc_launch_claimant_plugin_unload(&claimant) ==
          VC_LAUNCH_CLAIMANT_RESULT_OK);
    CHECK(platform.unload_calls == 2);
}

static void test_stop_cancels_pending_request(void)
{
    vc_launch_claimant claimant =
        VC_LAUNCH_CLAIMANT_INITIALIZER;
    fake_platform platform;
    vc_launch_request request;

    initialize(&claimant, &platform);
    make_ready(&claimant, &platform);
    CHECK(vc_launch_claimant_request_claim(&claimant) ==
          VC_LAUNCH_CLAIMANT_RESULT_OK);
    CHECK(vc_launch_claimant_worker_step(&claimant) ==
          VC_LAUNCH_CLAIMANT_RESULT_OK);
    CHECK(claimant.request_id == REQUEST_ID);
    CHECK(platform.transport_calls == 1);
    CHECK(vc_launch_claimant_stop(&claimant) ==
          VC_LAUNCH_CLAIMANT_RESULT_OK);
    CHECK(platform.transport_calls == 2);
    CHECK(vc_launch_request_decode(
              platform.retry_request,
              sizeof(platform.retry_request), &request) ==
          VC_LAUNCH_STATUS_OK);
    CHECK(request.operation == VC_LAUNCH_OPERATION_CANCEL);
    CHECK(request.capabilities == VC_LAUNCH_CAPABILITY_CANCEL);
    CHECK(request.request_id == REQUEST_ID);
    CHECK(claimant.phase == VC_LAUNCH_CLAIMANT_PHASE_STOPPED);
    CHECK(claimant.request_id == 0);
    CHECK(claimant.authorization_id == 0);

    initialize(&claimant, &platform);
    make_ready(&claimant, &platform);
    CHECK(vc_launch_claimant_request_claim(&claimant) ==
          VC_LAUNCH_CLAIMANT_RESULT_OK);
    CHECK(vc_launch_claimant_worker_step(&claimant) ==
          VC_LAUNCH_CLAIMANT_RESULT_OK);
    platform.transport_result =
        VC_LAUNCH_CLAIMANT_TRANSPORT_FAILED;
    CHECK(vc_launch_claimant_stop(&claimant) ==
          VC_LAUNCH_CLAIMANT_RESULT_CLEANUP_FAILED);
    CHECK(claimant.phase == VC_LAUNCH_CLAIMANT_PHASE_STOPPED);
    CHECK(claimant.request_id == 0);
    CHECK(!claimant.authorization_available);
}

static void test_stop_recovers_service_journal(void)
{
    vc_launch_claimant claimant =
        VC_LAUNCH_CLAIMANT_INITIALIZER;
    fake_platform platform;

    initialize(&claimant, &platform);
    make_ready(&claimant, &platform);
    CHECK(vc_launch_claimant_request_claim(&claimant) ==
          VC_LAUNCH_CLAIMANT_RESULT_OK);
    platform.transport_result =
        VC_LAUNCH_CLAIMANT_TRANSPORT_RETRY;
    CHECK(vc_launch_claimant_worker_step(&claimant) ==
          VC_LAUNCH_CLAIMANT_RESULT_RETRY);
    CHECK(claimant.request_id == 0);
    CHECK(claimant.request_wire_valid);
    platform.transport_result =
        VC_LAUNCH_CLAIMANT_TRANSPORT_OK;
    CHECK(vc_launch_claimant_stop(&claimant) ==
          VC_LAUNCH_CLAIMANT_RESULT_OK);
    CHECK(platform.transport_calls == 2);
    CHECK(memcmp(platform.last_request,
                 platform.retry_request,
                 sizeof(platform.last_request)) == 0);
    CHECK(claimant.phase == VC_LAUNCH_CLAIMANT_PHASE_STOPPED);
    CHECK(claimant.request_id == 0);
    CHECK(!claimant.authorization_available);
}

static void test_invalid_notifications_fail_closed(void)
{
    vc_launch_claimant claimant =
        VC_LAUNCH_CLAIMANT_INITIALIZER;
    fake_platform platform;
    vc_launch_overlay_snapshot overlay;

    initialize(&claimant, &platform);
    make_ready(&claimant, &platform);
    discover_and_claim(&claimant, &platform);
    overlay = platform.observation.overlay;
    --overlay.sequence;
    CHECK(vc_launch_claimant_notify_overlay(
              &claimant, &overlay) ==
          VC_LAUNCH_CLAIMANT_RESULT_INVALID_OBSERVATION);
    CHECK(!claimant.authorization_available);
    CHECK(!claimant.identity_bound);
    CHECK(vc_launch_claimant_get_status(&claimant) ==
          VC_LAUNCH_CLAIMANT_STATUS_STALE);
}

static void test_clock_reentrancy_and_formatter(void)
{
    vc_launch_claimant claimant =
        VC_LAUNCH_CLAIMANT_INITIALIZER;
    fake_platform platform;
    char one[1] = {'x'};
    char short_buffer[8];
    char exact[35];
    char large[128];
    size_t length;

    initialize(&claimant, &platform);
    platform.reenter = true;
    CHECK(vc_launch_claimant_observe(&claimant) ==
          VC_LAUNCH_CLAIMANT_RESULT_OK);
    CHECK(platform.busy_reentries >= 2);
    platform.reenter = false;

    --platform.now_ms;
    CHECK(vc_launch_claimant_tick(&claimant) ==
          VC_LAUNCH_CLAIMANT_RESULT_CLOCK_ROLLBACK);
    CHECK(claimant.request_id == 0);
    CHECK(!claimant.authorization_available);

    length = vc_launch_claimant_format_status(
        &claimant, NULL, 0);
    CHECK(length == strlen("Launch failed"));
    CHECK(vc_launch_claimant_format_status(
              &claimant, one, sizeof(one)) == length);
    CHECK(one[0] == '\0');
    memset(short_buffer, 'x', sizeof(short_buffer));
    CHECK(vc_launch_claimant_format_status(
              &claimant, short_buffer,
              sizeof(short_buffer)) == length);
    CHECK(short_buffer[sizeof(short_buffer) - 1u] == '\0');

    CHECK(vc_launch_claimant_reset(&claimant) ==
          VC_LAUNCH_CLAIMANT_RESULT_OK);
    platform.now_ms = 300;
    set_identity(&platform);
    CHECK(vc_launch_claimant_bind_identity(&claimant) ==
          VC_LAUNCH_CLAIMANT_RESULT_OK);
    make_ready(&claimant, &platform);
    CHECK(vc_launch_claimant_format_status(
              &claimant, exact, sizeof(exact)) ==
          strlen("Waiting for launch request"));
    CHECK(strcmp(exact, "Waiting for launch request") == 0);
    discover_and_claim(&claimant, &platform);
    CHECK(vc_launch_claimant_format_status(
              &claimant, large, sizeof(large)) ==
          strlen("Menu open authorized"));
    CHECK(strcmp(large, "Menu open authorized") == 0);
    CHECK(strstr(large, "PCSA") == NULL);
    CHECK(strstr(large, "12345678") == NULL);
    CHECK(strstr(large, "212223") == NULL);
    CHECK(strstr(large, "010203") == NULL);
}

int main(void)
{
    test_happy_path_and_gates();
    test_gate_and_identity_changes();
    test_observation_gate_matrix();
    test_consumed_open_and_sequence_changes();
    test_transport_validation_and_retry();
    test_deadlines_cancel_and_lifecycle();
    test_stop_cancels_pending_request();
    test_stop_recovers_service_journal();
    test_invalid_notifications_fail_closed();
    test_clock_reentrancy_and_formatter();

    if (failures != 0) {
        fprintf(stderr, "%d launch claimant test(s) failed\n",
                failures);
        return 1;
    }
    puts("all launch claimant tests passed");
    return 0;
}
