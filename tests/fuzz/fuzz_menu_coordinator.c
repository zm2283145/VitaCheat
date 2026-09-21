#include "vitacheat/menu_coordinator.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum {
    VC_MENU_COORDINATOR_FUZZ_MAX_STEPS = 256
};

typedef struct fuzz_menu_platform {
    vc_launch_claimant_observation observation;
    uint64_t now_ms;
    uint64_t revision;
    uint64_t owned_mask;
    vc_thread_id suspend_failure;
    vc_thread_id resume_failure;
    bool ownership_ok;
    bool runtime_ok;
    bool unload_ok;
} fuzz_menu_platform;

static void fuzz_check(int condition)
{
    if (!condition) {
        abort();
    }
}

static uint32_t fuzz_read_u32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}

static uint64_t fuzz_read_u64(const uint8_t *bytes)
{
    uint64_t value = 0;
    unsigned int index;

    for (index = 0; index < 8; ++index) {
        value |= (uint64_t)bytes[index] << (index * 8);
    }
    return value;
}

static void fuzz_set_observation(
    fuzz_menu_platform *platform,
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
    platform->observation.overlay.sequence = 2;
    platform->observation.overlay.generation = 1;
    platform->observation.overlay.state =
        VC_LAUNCH_OVERLAY_CLOSED;
    platform->observation.presentation.sequence = 1;
    platform->observation.presentation.process_id = process_id;
    platform->observation.presentation.process_generation =
        process_generation;
    platform->observation.presentation.module_generation =
        module_generation;
    platform->observation.presentation.state =
        VC_LAUNCH_PRESENTATION_READY;
}

static bool fuzz_claimant_observation(
    void *context,
    vc_launch_claimant_observation *observation)
{
    fuzz_menu_platform *platform =
        (fuzz_menu_platform *)context;

    *observation = platform->observation;
    return true;
}

static bool fuzz_claimant_time(void *context, uint64_t *now_ms)
{
    fuzz_menu_platform *platform =
        (fuzz_menu_platform *)context;

    *now_ms = platform->now_ms;
    return true;
}

static vc_launch_claimant_transport_result fuzz_transport(
    void *context,
    const uint8_t *request_wire,
    size_t request_size,
    uint8_t *response_wire,
    size_t response_capacity,
    size_t *response_size)
{
    fuzz_menu_platform *platform =
        (fuzz_menu_platform *)context;
    vc_launch_request request;
    vc_launch_response response;
    size_t encoded_size = 0;

    (void)platform;
    *response_size = 0;
    if (vc_launch_request_decode(
            request_wire, request_size, &request) !=
        VC_LAUNCH_STATUS_OK) {
        return VC_LAUNCH_CLAIMANT_TRANSPORT_FAILED;
    }
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
    response.deadline_ms =
        request.now_ms <= UINT64_MAX - 5000u
            ? request.now_ms + 5000u
            : UINT64_MAX;
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
    fuzz_menu_platform *platform =
        (fuzz_menu_platform *)context;

    (void)identity;
    (void)now_ms;
    return platform->unload_ok;
}

static bool fuzz_runtime(
    void *context,
    vc_menu_runtime_snapshot *snapshot)
{
    fuzz_menu_platform *platform =
        (fuzz_menu_platform *)context;

    if (!platform->runtime_ok) {
        return false;
    }
    snapshot->observation = platform->observation;
    snapshot->allowlist_revision = platform->revision;
    return true;
}

static bool fuzz_ownership(
    void *context,
    const vc_launch_claimant_identity_snapshot *target,
    const vc_thread_id *thread_ids,
    size_t thread_count)
{
    fuzz_menu_platform *platform =
        (fuzz_menu_platform *)context;
    size_t index;

    fuzz_check(target->caller.process_id != 0);
    fuzz_check(target->caller.process_generation != 0);
    fuzz_check(thread_count > 0 &&
               thread_count <= VC_PAUSE_MAX_THREADS);
    for (index = 0; index < thread_count; ++index) {
        fuzz_check(thread_ids[index] > 0);
        if (index != 0) {
            fuzz_check(thread_ids[index - 1u] <
                       thread_ids[index]);
        }
    }
    return platform->ownership_ok &&
           target->caller.process_id ==
               platform->observation.identity.caller.process_id &&
           target->caller.process_generation ==
               platform->observation.identity.caller
                   .process_generation &&
           target->caller.module_generation ==
               platform->observation.identity.caller
                   .module_generation;
}

static int fuzz_suspend(void *context, vc_thread_id thread_id)
{
    fuzz_menu_platform *platform =
        (fuzz_menu_platform *)context;

    fuzz_check(thread_id > 0 && thread_id <= 64);
    if (thread_id == platform->suspend_failure) {
        return -1;
    }
    platform->owned_mask |=
        UINT64_C(1) << ((uint32_t)thread_id - 1u);
    return 0;
}

static int fuzz_resume(void *context, vc_thread_id thread_id)
{
    fuzz_menu_platform *platform =
        (fuzz_menu_platform *)context;

    fuzz_check(thread_id > 0 && thread_id <= 64);
    if (thread_id == platform->resume_failure) {
        return -1;
    }
    fuzz_check((platform->owned_mask &
                (UINT64_C(1) <<
                 ((uint32_t)thread_id - 1u))) != 0);
    platform->owned_mask &=
        ~(UINT64_C(1) << ((uint32_t)thread_id - 1u));
    return 0;
}

static void fuzz_check_invariants(
    const vc_menu_coordinator *coordinator,
    const fuzz_menu_platform *platform)
{
    fuzz_check(coordinator->phase <=
               VC_MENU_COORDINATOR_PHASE_STOPPED);
    fuzz_check(vc_menu_coordinator_get_status(coordinator) <=
               VC_MENU_COORDINATOR_STATUS_ERROR);
    fuzz_check(!coordinator->menu_authority_active ||
               coordinator->pause.active);
    fuzz_check(!coordinator->acknowledged ||
               coordinator->menu_authority_active);
    fuzz_check(coordinator->pause.owned_mask ==
               platform->owned_mask);
    if (coordinator->phase ==
        VC_MENU_COORDINATOR_PHASE_STOPPED) {
        fuzz_check(vc_menu_coordinator_get_status(coordinator) ==
                   VC_MENU_COORDINATOR_STATUS_STOPPED);
        fuzz_check(!coordinator->menu_authority_active);
    }
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t data_size)
{
    vc_launch_claimant claimant =
        VC_LAUNCH_CLAIMANT_INITIALIZER;
    vc_menu_coordinator coordinator =
        VC_MENU_COORDINATOR_INITIALIZER;
    vc_launch_claimant_dependencies claimant_dependencies;
    vc_menu_coordinator_dependencies coordinator_dependencies;
    vc_menu_thread_allowlist allowlist;
    vc_menu_open_lease lease;
    vc_thread_id thread_ids[VC_PAUSE_MAX_THREADS];
    fuzz_menu_platform platform;
    uint32_t process_id;
    uint64_t process_generation;
    uint64_t module_generation;
    size_t thread_count;
    size_t cursor;
    size_t step;
    size_t index;

    if (data_size < 21) {
        return 0;
    }
    process_id = fuzz_read_u32(data);
    if (process_id == 0) {
        process_id = 1;
    }
    process_generation = fuzz_read_u64(data + 4);
    if (process_generation == 0) {
        process_generation = 1;
    }
    module_generation = fuzz_read_u64(data + 12);
    if (module_generation == 0) {
        module_generation = 1;
    }
    thread_count =
        (size_t)(data[20] % VC_PAUSE_MAX_THREADS) + 1u;
    for (index = 0; index < thread_count; ++index) {
        thread_ids[index] = (vc_thread_id)(index + 1u);
    }

    memset(&platform, 0, sizeof(platform));
    platform.runtime_ok = true;
    platform.ownership_ok = true;
    platform.unload_ok = true;
    platform.revision = 1;
    fuzz_set_observation(
        &platform, process_id,
        process_generation, module_generation);

    memset(&claimant_dependencies, 0,
           sizeof(claimant_dependencies));
    claimant_dependencies.get_observation =
        fuzz_claimant_observation;
    claimant_dependencies.get_time = fuzz_claimant_time;
    claimant_dependencies.transport = fuzz_transport;
    claimant_dependencies.plugin_unload = fuzz_unload;
    claimant_dependencies.context = &platform;
    fuzz_check(vc_launch_claimant_init(
                   &claimant, &claimant_dependencies,
                   VC_LAUNCH_CLAIMANT_DEFAULT_AUTHORIZATION_TTL_MS) ==
               VC_LAUNCH_CLAIMANT_RESULT_OK);
    fuzz_check(vc_launch_claimant_start(&claimant) ==
               VC_LAUNCH_CLAIMANT_RESULT_OK);
    fuzz_check(vc_launch_claimant_bind_identity(&claimant) ==
               VC_LAUNCH_CLAIMANT_RESULT_OK);
    claimant.overlay_closed_stable = true;

    memset(&coordinator_dependencies, 0,
           sizeof(coordinator_dependencies));
    coordinator_dependencies.get_runtime_snapshot =
        fuzz_runtime;
    coordinator_dependencies.verify_thread_ownership =
        fuzz_ownership;
    coordinator_dependencies.suspend_thread = fuzz_suspend;
    coordinator_dependencies.resume_thread = fuzz_resume;
    coordinator_dependencies.context = &platform;
    fuzz_check(vc_menu_coordinator_init(
                   &coordinator, &claimant,
                   &coordinator_dependencies) ==
               VC_MENU_COORDINATOR_RESULT_OK);
    fuzz_check(vc_menu_coordinator_start(&coordinator) ==
               VC_MENU_COORDINATOR_RESULT_OK);
    fuzz_check(vc_menu_coordinator_bind_target(
                   &coordinator,
                   &platform.observation.identity) ==
               VC_MENU_COORDINATOR_RESULT_OK);
    memset(&allowlist, 0, sizeof(allowlist));
    allowlist.process_id = process_id;
    allowlist.process_generation = process_generation;
    allowlist.module_generation = module_generation;
    allowlist.revision = platform.revision;
    allowlist.thread_ids = thread_ids;
    allowlist.thread_count = thread_count;
    allowlist.protected_threads.plugin_control_worker = 100;
    allowlist.protected_threads.input_hook = 101;
    allowlist.protected_threads.renderer_present_hook = 102;
    allowlist.protected_threads.watchdog_worker = 103;
    allowlist.protected_threads.cleanup_worker = 104;
    allowlist.protected_threads.current_thread = 105;
    fuzz_check(vc_menu_coordinator_configure_allowlist(
                   &coordinator, &allowlist) ==
               VC_MENU_COORDINATOR_RESULT_OK);
    memset(&lease, 0, sizeof(lease));

    cursor = 21;
    for (step = 0;
         cursor < data_size &&
         step < VC_MENU_COORDINATOR_FUZZ_MAX_STEPS;
         ++step) {
        const uint8_t command = data[cursor++];
        const uint8_t action = command & 15u;

        platform.runtime_ok = (command & 0x20u) == 0;
        platform.ownership_ok = (command & 0x40u) == 0;
        platform.unload_ok = (command & 0x80u) == 0;
        platform.suspend_failure =
            (command & 0x10u) != 0
                ? thread_ids[command % thread_count]
                : 0;
        platform.resume_failure =
            (command & 0x08u) != 0
                ? thread_ids[command % thread_count]
                : 0;
        if ((command & 1u) != 0 &&
            platform.now_ms != 0) {
            --platform.now_ms;
        } else if (platform.now_ms != UINT64_MAX) {
            ++platform.now_ms;
        }

        switch (action) {
        case 0:
            (void)vc_launch_claimant_request_claim(&claimant);
            break;
        case 1:
            (void)vc_launch_claimant_worker_step(&claimant);
            break;
        case 2:
            (void)vc_menu_coordinator_begin_open(
                &coordinator, platform.now_ms,
                (uint64_t)(command % 200u) + 1u,
                (uint64_t)(command % 50u) + 1u, &lease);
            break;
        case 3:
            (void)vc_menu_coordinator_acknowledge_open(
                &coordinator, platform.now_ms, &lease);
            break;
        case 4:
            (void)vc_menu_coordinator_tick(
                &coordinator, platform.now_ms);
            break;
        case 5:
            (void)vc_menu_coordinator_close(
                &coordinator, platform.now_ms, &lease);
            break;
        case 6:
            ++platform.revision;
            break;
        case 7:
            ++platform.observation.overlay.sequence;
            platform.observation.overlay.state =
                VC_LAUNCH_OVERLAY_OPEN;
            (void)vc_menu_coordinator_notify_overlay(
                &coordinator,
                &platform.observation.overlay,
                platform.now_ms);
            break;
        case 8:
            ++platform.observation.presentation.sequence;
            platform.observation.presentation.state =
                VC_LAUNCH_PRESENTATION_LOST;
            (void)vc_menu_coordinator_notify_presentation(
                &coordinator,
                &platform.observation.presentation,
                platform.now_ms);
            break;
        case 9:
            (void)vc_menu_coordinator_target_exit(
                &coordinator, process_id,
                process_generation, platform.now_ms);
            platform.owned_mask = 0;
            break;
        case 10:
            (void)vc_menu_coordinator_retry_cleanup(
                &coordinator, platform.now_ms);
            break;
        case 11:
            (void)vc_menu_coordinator_claimant_reset(
                &coordinator, platform.now_ms);
            break;
        case 12:
            (void)vc_menu_coordinator_stop(
                &coordinator, platform.now_ms);
            break;
        case 13:
            (void)vc_menu_coordinator_start(&coordinator);
            break;
        case 14:
            (void)vc_menu_coordinator_plugin_unload(
                &coordinator, platform.now_ms);
            break;
        default:
            (void)vc_menu_coordinator_service_stop(
                &coordinator, platform.now_ms);
            break;
        }
        fuzz_check_invariants(&coordinator, &platform);
    }

    platform.resume_failure = 0;
    platform.ownership_ok = true;
    platform.runtime_ok = true;
    platform.unload_ok = true;
    (void)vc_menu_coordinator_target_exit(
        &coordinator, process_id,
        process_generation, platform.now_ms);
    platform.owned_mask = 0;
    (void)vc_menu_coordinator_destroy(
        &coordinator, platform.now_ms);
    (void)vc_launch_claimant_destroy(&claimant);
    return 0;
}

#ifdef VC_MENU_COORDINATOR_FUZZ_STANDALONE
int main(void)
{
    uint8_t data[512];
    uint32_t random_state = UINT32_C(0x4d454e55);
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
