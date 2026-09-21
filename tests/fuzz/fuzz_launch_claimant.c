#include "vitacheat/launch_claimant.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum {
    VC_LAUNCH_CLAIMANT_FUZZ_MAX_STEPS = 256
};

typedef struct fuzz_platform {
    vc_launch_claimant_observation observation;
    uint64_t now_ms;
    vc_launch_claimant_transport_result transport_result;
    bool observation_ok;
    bool time_ok;
    bool unload_ok;
} fuzz_platform;

static void fuzz_check(int condition)
{
    if (!condition) {
        abort();
    }
}

static uint32_t read_u32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}

static uint64_t read_u64(const uint8_t *bytes)
{
    uint64_t value = 0;
    unsigned int index;

    for (index = 0; index < 8; ++index) {
        value |= (uint64_t)bytes[index] << (index * 8);
    }
    return value;
}

static bool fuzz_observation(
    void *context,
    vc_launch_claimant_observation *observation)
{
    fuzz_platform *platform = (fuzz_platform *)context;

    if (platform->observation_ok) {
        *observation = platform->observation;
    }
    return platform->observation_ok;
}

static bool fuzz_time(void *context, uint64_t *now_ms)
{
    fuzz_platform *platform = (fuzz_platform *)context;

    if (platform->time_ok) {
        *now_ms = platform->now_ms;
    }
    return platform->time_ok;
}

static vc_launch_claimant_transport_result fuzz_transport(
    void *context,
    const uint8_t *request_wire,
    size_t request_size,
    uint8_t *response_wire,
    size_t response_capacity,
    size_t *response_size)
{
    fuzz_platform *platform = (fuzz_platform *)context;
    vc_launch_request request;
    vc_launch_response response;
    size_t encoded_size = 0;

    *response_size = 0;
    if (platform->transport_result !=
        VC_LAUNCH_CLAIMANT_TRANSPORT_OK) {
        return platform->transport_result;
    }
    if (vc_launch_request_decode(
            request_wire, request_size, &request) !=
        VC_LAUNCH_STATUS_OK) {
        return VC_LAUNCH_CLAIMANT_TRANSPORT_FAILED;
    }
    fuzz_check(request.operation == VC_LAUNCH_OPERATION_STATUS ||
               request.operation == VC_LAUNCH_OPERATION_CLAIM ||
               request.operation == VC_LAUNCH_OPERATION_CANCEL);
    fuzz_check(request.caller_role ==
               VC_LAUNCH_CALLER_GAME_PLUGIN);

    memset(&response, 0, sizeof(response));
    response.version = VC_LAUNCH_ABI_VERSION;
    response.struct_size = VC_LAUNCH_RESPONSE_WIRE_SIZE;
    response.operation = request.operation;
    response.target_process_id = request.target_process_id;
    response.target_generation = request.target_generation;
    response.request_id =
        request.operation == VC_LAUNCH_OPERATION_STATUS
            ? UINT64_C(1)
            : request.request_id;
    response.created_ms = request.now_ms;
    response.deadline_ms = request.now_ms + 100;
    response.observed_ms = request.now_ms;
    response.capabilities = request.capabilities;
    if (request.operation == VC_LAUNCH_OPERATION_STATUS) {
        response.status = VC_LAUNCH_STATUS_PENDING;
        response.launch_state = VC_LAUNCH_STATE_PENDING;
    } else if (request.operation == VC_LAUNCH_OPERATION_CLAIM) {
        response.status = VC_LAUNCH_STATUS_CLAIMED;
        response.launch_state = VC_LAUNCH_STATE_CLAIMED;
    } else {
        response.status = VC_LAUNCH_STATUS_CANCELLED;
        response.launch_state = VC_LAUNCH_STATE_CANCELLED;
    }
    if (vc_launch_response_encode(
            &response, response_wire, response_capacity,
            &encoded_size) != VC_LAUNCH_STATUS_OK) {
        return VC_LAUNCH_CLAIMANT_TRANSPORT_FAILED;
    }
    *response_size = encoded_size;
    return VC_LAUNCH_CLAIMANT_TRANSPORT_OK;
}

static bool fuzz_unload(
    void *context,
    const vc_launch_claimant_identity_snapshot *identity,
    uint64_t now_ms)
{
    fuzz_platform *platform = (fuzz_platform *)context;

    (void)identity;
    (void)now_ms;
    return platform->unload_ok;
}

static void fuzz_set_observation(fuzz_platform *platform,
                                 uint32_t process_id,
                                 uint64_t process_generation,
                                 uint64_t module_generation)
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
    identity->caller.process_id = process_id;
    identity->caller.process_generation = process_generation;
    identity->caller.module_generation = module_generation;
    identity->foreground.sequence = 1;
    identity->foreground.present = 1;
    identity->foreground.target_process_id = process_id;
    identity->foreground.target_generation = process_generation;
    identity->foreground.title_id_size = sizeof(title);
    memcpy(identity->foreground.title_id, title, sizeof(title));
    identity->title_id_size = sizeof(title);
    memcpy(identity->title_id, title, sizeof(title));

    platform->observation.overlay.sequence = 1;
    platform->observation.overlay.generation = 1;
    platform->observation.overlay.state = VC_LAUNCH_OVERLAY_UNKNOWN;
    platform->observation.presentation.sequence = 1;
    platform->observation.presentation.process_id = process_id;
    platform->observation.presentation.process_generation =
        process_generation;
    platform->observation.presentation.module_generation =
        module_generation;
    platform->observation.presentation.state =
        VC_LAUNCH_PRESENTATION_UNAVAILABLE;
}

static void fuzz_check_invariants(
    const vc_launch_claimant *claimant)
{
    fuzz_check(claimant->phase <=
               VC_LAUNCH_CLAIMANT_PHASE_STOPPED);
    fuzz_check(claimant->action <=
               VC_LAUNCH_CLAIMANT_ACTION_CANCEL);
    fuzz_check(vc_launch_claimant_get_status(claimant) <=
               VC_LAUNCH_CLAIMANT_STATUS_ERROR);
    fuzz_check(!(claimant->authorization_available &&
                 claimant->authorization_consumed));
    fuzz_check(!(claimant->authorization_available &&
                 claimant->menu_open));
    fuzz_check(!(claimant->authorization_consumed &&
                 claimant->menu_open));
    if (claimant->authorization_available ||
        claimant->authorization_consumed) {
        fuzz_check(claimant->authorization_id != 0);
        fuzz_check(claimant->server_claimed);
    }
    if (claimant->phase == VC_LAUNCH_CLAIMANT_PHASE_STOPPED) {
        fuzz_check(!claimant->identity_bound);
        fuzz_check(!claimant->authorization_available);
        fuzz_check(!claimant->authorization_consumed);
        fuzz_check(!claimant->menu_open);
        fuzz_check(claimant->request_id == 0);
    }
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t data_size)
{
    vc_launch_claimant claimant =
        VC_LAUNCH_CLAIMANT_INITIALIZER;
    vc_launch_claimant_dependencies dependencies;
    vc_launch_open_authorization authorization;
    fuzz_platform platform;
    uint32_t process_id;
    uint64_t process_generation;
    uint64_t module_generation;
    size_t cursor;
    size_t step;

    if (data_size < 20) {
        return 0;
    }
    process_id = read_u32(data);
    if (process_id == 0) {
        process_id = 1;
    }
    process_generation = read_u64(data + 4);
    if (process_generation == 0) {
        process_generation = 1;
    }
    module_generation = read_u64(data + 12);
    if (module_generation == 0) {
        module_generation = 1;
    }

    memset(&platform, 0, sizeof(platform));
    platform.observation_ok = true;
    platform.time_ok = true;
    platform.unload_ok = true;
    fuzz_set_observation(&platform, process_id,
                         process_generation, module_generation);
    memset(&dependencies, 0, sizeof(dependencies));
    dependencies.get_observation = fuzz_observation;
    dependencies.get_time = fuzz_time;
    dependencies.transport = fuzz_transport;
    dependencies.plugin_unload = fuzz_unload;
    dependencies.context = &platform;
    fuzz_check(vc_launch_claimant_init(
                   &claimant, &dependencies,
                   VC_LAUNCH_CLAIMANT_DEFAULT_AUTHORIZATION_TTL_MS) ==
               VC_LAUNCH_CLAIMANT_RESULT_OK);

    memset(&authorization, 0, sizeof(authorization));
    cursor = 20;
    for (step = 0;
         cursor < data_size &&
         step < VC_LAUNCH_CLAIMANT_FUZZ_MAX_STEPS;
         ++step) {
        const uint8_t command = data[cursor++];
        const uint8_t action = command & 15u;

        platform.observation_ok = (command & 0x20u) == 0;
        platform.time_ok = (command & 0x40u) == 0;
        platform.unload_ok = (command & 0x80u) == 0;
        platform.transport_result =
            (vc_launch_claimant_transport_result)(
                (command >> 4) %
                (VC_LAUNCH_CLAIMANT_TRANSPORT_FAILED + 1u));
        if ((command & 1u) != 0 && platform.now_ms != 0) {
            --platform.now_ms;
        } else if (platform.now_ms != UINT64_MAX) {
            ++platform.now_ms;
        }

        switch (action) {
        case 0:
            (void)vc_launch_claimant_start(&claimant);
            break;
        case 1:
            (void)vc_launch_claimant_bind_identity(&claimant);
            break;
        case 2:
            ++platform.observation.overlay.sequence;
            platform.observation.overlay.state =
                command % (VC_LAUNCH_OVERLAY_CLOSED + 1u);
            (void)vc_launch_claimant_notify_overlay(
                &claimant, &platform.observation.overlay);
            break;
        case 3:
            ++platform.observation.presentation.sequence;
            platform.observation.presentation.state =
                command % (VC_LAUNCH_PRESENTATION_LOST + 1u);
            (void)vc_launch_claimant_notify_presentation(
                &claimant, &platform.observation.presentation);
            break;
        case 4:
            (void)vc_launch_claimant_request_claim(&claimant);
            break;
        case 5:
            (void)vc_launch_claimant_worker_step(&claimant);
            break;
        case 6:
            (void)vc_launch_claimant_tick(&claimant);
            break;
        case 7:
            (void)vc_launch_claimant_consume_open_authorization(
                &claimant, &authorization);
            break;
        case 8:
            (void)vc_launch_claimant_acknowledge_open(
                &claimant, &authorization);
            break;
        case 9:
            (void)vc_launch_claimant_cancel(&claimant);
            break;
        case 10:
            (void)vc_launch_claimant_close(&claimant);
            break;
        case 11:
            (void)vc_launch_claimant_reset(&claimant);
            break;
        case 12:
            ++platform.observation.identity.sequence;
            ++platform.observation.identity.caller.module_generation;
            ++platform.observation.presentation.sequence;
            ++platform.observation.presentation.module_generation;
            break;
        case 13:
            (void)vc_launch_claimant_plugin_unload(&claimant);
            break;
        case 14:
            (void)vc_launch_claimant_stop(&claimant);
            break;
        default:
            (void)vc_launch_claimant_observe(&claimant);
            break;
        }
        fuzz_check_invariants(&claimant);
    }
    platform.unload_ok = true;
    (void)vc_launch_claimant_destroy(&claimant);
    return 0;
}

#ifdef VC_LAUNCH_CLAIMANT_FUZZ_STANDALONE
int main(void)
{
    uint8_t data[512];
    uint32_t random_state = UINT32_C(0xc1a14a17);
    unsigned int run;
    size_t index;

    for (run = 0; run < 10000; ++run) {
        for (index = 0; index < sizeof(data); ++index) {
            random_state =
                random_state * UINT32_C(1664525) +
                UINT32_C(1013904223);
            data[index] = (uint8_t)(random_state >> 24);
        }
        (void)LLVMFuzzerTestOneInput(data, sizeof(data));
    }
    return 0;
}
#endif
