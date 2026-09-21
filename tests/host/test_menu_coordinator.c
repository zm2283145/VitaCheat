#include "vitacheat/menu_coordinator.h"

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
#define ALLOWLIST_REVISION UINT64_C(7)

typedef struct fake_event {
    char operation;
    vc_thread_id thread_id;
} fake_event;

typedef enum fake_mutation {
    MUTATION_NONE = 0,
    MUTATION_REVISION,
    MUTATION_PROCESS_GENERATION,
    MUTATION_MODULE_GENERATION,
    MUTATION_TITLE,
    MUTATION_FOREGROUND,
    MUTATION_OVERLAY,
    MUTATION_PRESENTATION,
    MUTATION_CLAIMANT_CANCEL
} fake_mutation;

typedef struct fake_platform {
    vc_launch_claimant *claimant;
    vc_menu_coordinator *coordinator;
    vc_launch_claimant_observation observation;
    fake_event events[256];
    uint64_t now_ms;
    uint64_t allowlist_revision;
    vc_thread_id suspend_failure;
    vc_thread_id resume_failure;
    unsigned int resume_failures_remaining;
    unsigned int suspend_calls;
    unsigned int resume_calls;
    unsigned int ownership_calls;
    unsigned int runtime_calls;
    unsigned int transport_calls;
    unsigned int unload_calls;
    unsigned int busy_reentries;
    unsigned int mutate_suspend_call;
    unsigned int mutate_resume_call;
    unsigned int mutate_runtime_call;
    fake_mutation ownership_mutation;
    fake_mutation suspend_mutation;
    fake_mutation resume_mutation;
    fake_mutation runtime_mutation;
    vc_thread_id rejected_ownership_thread;
    bool ownership_ok;
    bool runtime_ok;
    bool unload_ok;
    bool reenter;
    bool retire_resumed_threads;
} fake_platform;

typedef struct fixture {
    vc_launch_claimant claimant;
    vc_menu_coordinator coordinator;
    fake_platform platform;
    vc_menu_open_lease lease;
} fixture;

static const vc_thread_id gameplay_threads[] = {10, 20, 30};

static vc_menu_protected_threads protected_threads(void)
{
    vc_menu_protected_threads protected_set;

    protected_set.plugin_control_worker = 100;
    protected_set.input_hook = 101;
    protected_set.renderer_present_hook = 102;
    protected_set.watchdog_worker = 103;
    protected_set.cleanup_worker = 104;
    protected_set.current_thread = 105;
    return protected_set;
}

static void set_ready_observation(fake_platform *platform)
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

    platform->observation.overlay.sequence = 3;
    platform->observation.overlay.generation = 1;
    platform->observation.overlay.state =
        VC_LAUNCH_OVERLAY_CLOSED;
    platform->observation.presentation.sequence = 2;
    platform->observation.presentation.process_id = TARGET_PID;
    platform->observation.presentation.process_generation =
        TARGET_GENERATION;
    platform->observation.presentation.module_generation =
        MODULE_GENERATION;
    platform->observation.presentation.state =
        VC_LAUNCH_PRESENTATION_READY;
}

static void apply_mutation(fake_platform *platform,
                           fake_mutation mutation)
{
    switch (mutation) {
    case MUTATION_REVISION:
        ++platform->allowlist_revision;
        break;
    case MUTATION_PROCESS_GENERATION:
        ++platform->observation.identity.sequence;
        ++platform->observation.identity.caller.process_generation;
        ++platform->observation.identity.foreground.sequence;
        ++platform->observation.identity.foreground.target_generation;
        ++platform->observation.presentation.sequence;
        ++platform->observation.presentation.process_generation;
        break;
    case MUTATION_MODULE_GENERATION:
        ++platform->observation.identity.sequence;
        ++platform->observation.identity.caller.module_generation;
        ++platform->observation.presentation.sequence;
        ++platform->observation.presentation.module_generation;
        break;
    case MUTATION_TITLE:
        ++platform->observation.identity.sequence;
        ++platform->observation.identity.foreground.sequence;
        platform->observation.identity.title_id[8] ^= 1u;
        platform->observation.identity.foreground.title_id[8] ^= 1u;
        break;
    case MUTATION_FOREGROUND:
        ++platform->observation.identity.sequence;
        ++platform->observation.identity.foreground.sequence;
        platform->observation.identity.foreground.present = 0;
        platform->observation.identity.foreground.target_process_id = 0;
        platform->observation.identity.foreground.target_generation = 0;
        platform->observation.identity.foreground.title_id_size = 0;
        memset(platform->observation.identity.foreground.title_id, 0,
               sizeof(platform->observation.identity.foreground.title_id));
        break;
    case MUTATION_OVERLAY:
        ++platform->observation.overlay.sequence;
        platform->observation.overlay.state = VC_LAUNCH_OVERLAY_OPEN;
        break;
    case MUTATION_PRESENTATION:
        ++platform->observation.presentation.sequence;
        platform->observation.presentation.state =
            VC_LAUNCH_PRESENTATION_LOST;
        break;
    case MUTATION_CLAIMANT_CANCEL:
        CHECK(vc_launch_claimant_cancel(
                  platform->claimant) ==
              VC_LAUNCH_CLAIMANT_RESULT_OK);
        break;
    case MUTATION_NONE:
    default:
        break;
    }
}

static void check_coordinator_adapter(fake_platform *platform)
{
    if (platform->coordinator == NULL ||
        atomic_load_explicit(
            &platform->coordinator->adapter_active,
            memory_order_acquire) == 0u) {
        return;
    }
    CHECK(atomic_load_explicit(
              &platform->coordinator->transaction_busy,
              memory_order_acquire) == 0u);
    if (platform->reenter) {
        vc_menu_open_lease lease = platform->coordinator->lease;

        CHECK(vc_menu_coordinator_tick(
                  platform->coordinator,
                  platform->now_ms) ==
              VC_MENU_COORDINATOR_RESULT_BUSY);
        CHECK(vc_menu_coordinator_close(
                  platform->coordinator,
                  platform->now_ms, &lease) ==
              VC_MENU_COORDINATOR_RESULT_BUSY);
        CHECK(vc_menu_coordinator_begin_open(
                  platform->coordinator,
                  platform->now_ms, 1000, 100,
                  &lease) ==
              VC_MENU_COORDINATOR_RESULT_BUSY);
        CHECK(memcmp(&lease, &platform->coordinator->lease,
                     sizeof(lease)) == 0);
        ++platform->busy_reentries;
    }
}

static bool fake_claimant_observation(
    void *context,
    vc_launch_claimant_observation *observation)
{
    fake_platform *platform = (fake_platform *)context;

    check_coordinator_adapter(platform);
    *observation = platform->observation;
    return true;
}

static bool fake_claimant_time(void *context, uint64_t *now_ms)
{
    fake_platform *platform = (fake_platform *)context;

    check_coordinator_adapter(platform);
    *now_ms = platform->now_ms;
    return true;
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
    size_t encoded_size = 0;

    check_coordinator_adapter(platform);
    ++platform->transport_calls;
    CHECK(vc_launch_request_decode(
              request_wire, request_size, &request) ==
          VC_LAUNCH_STATUS_OK);
    memset(&response, 0, sizeof(response));
    response.version = VC_LAUNCH_ABI_VERSION;
    response.struct_size = VC_LAUNCH_RESPONSE_WIRE_SIZE;
    response.operation = request.operation;
    response.target_process_id = request.target_process_id;
    response.target_generation = request.target_generation;
    response.request_id =
        request.operation == VC_LAUNCH_OPERATION_STATUS
            ? REQUEST_ID
            : request.request_id;
    response.created_ms = request.now_ms;
    response.deadline_ms = request.now_ms + 5000;
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
    CHECK(vc_launch_response_encode(
              &response, response_wire, response_capacity,
              &encoded_size) == VC_LAUNCH_STATUS_OK);
    *response_size = encoded_size;
    return VC_LAUNCH_CLAIMANT_TRANSPORT_OK;
}

static bool fake_unload(
    void *context,
    const vc_launch_claimant_identity_snapshot *identity,
    uint64_t now_ms)
{
    fake_platform *platform = (fake_platform *)context;

    check_coordinator_adapter(platform);
    CHECK(identity->caller.process_id == TARGET_PID);
    CHECK(identity->caller.process_generation == TARGET_GENERATION);
    CHECK(now_ms != 0 && now_ms <= platform->now_ms);
    ++platform->unload_calls;
    return platform->unload_ok;
}

static bool fake_runtime_snapshot(
    void *context,
    vc_menu_runtime_snapshot *snapshot)
{
    fake_platform *platform = (fake_platform *)context;

    check_coordinator_adapter(platform);
    ++platform->runtime_calls;
    if (!platform->runtime_ok) {
        return false;
    }
    snapshot->observation = platform->observation;
    snapshot->allowlist_revision =
        platform->allowlist_revision;
    if (platform->runtime_mutation != MUTATION_NONE &&
        platform->runtime_calls ==
            platform->mutate_runtime_call) {
        fake_mutation mutation =
            platform->runtime_mutation;

        platform->runtime_mutation = MUTATION_NONE;
        apply_mutation(platform, mutation);
    }
    return true;
}

static bool fake_verify_ownership(
    void *context,
    const vc_launch_claimant_identity_snapshot *target,
    const vc_thread_id *thread_ids,
    size_t thread_count)
{
    fake_platform *platform = (fake_platform *)context;
    size_t index;

    check_coordinator_adapter(platform);
    ++platform->ownership_calls;
    CHECK(target->caller.process_id == TARGET_PID);
    CHECK(target->caller.process_generation == TARGET_GENERATION);
    CHECK(thread_count > 0 && thread_count <= VC_PAUSE_MAX_THREADS);
    for (index = 0; index < thread_count; ++index) {
        CHECK(thread_ids[index] > 0);
        if (thread_ids[index] ==
            platform->rejected_ownership_thread) {
            return false;
        }
        if (index != 0) {
            CHECK(thread_ids[index - 1u] < thread_ids[index]);
        }
    }
    if (target->caller.process_id !=
            platform->observation.identity.caller.process_id ||
        target->caller.process_generation !=
            platform->observation.identity.caller
                .process_generation ||
        target->caller.module_generation !=
            platform->observation.identity.caller
                .module_generation) {
        return false;
    }
    if (platform->ownership_mutation != MUTATION_NONE) {
        fake_mutation mutation = platform->ownership_mutation;

        platform->ownership_mutation = MUTATION_NONE;
        apply_mutation(platform, mutation);
    }
    return platform->ownership_ok;
}

static void record_event(fake_platform *platform,
                         char operation,
                         vc_thread_id thread_id)
{
    CHECK(platform->suspend_calls + platform->resume_calls <
          sizeof(platform->events) / sizeof(platform->events[0]));
    if (platform->suspend_calls + platform->resume_calls <
        sizeof(platform->events) / sizeof(platform->events[0])) {
        size_t index =
            platform->suspend_calls + platform->resume_calls;

        platform->events[index].operation = operation;
        platform->events[index].thread_id = thread_id;
    }
}

static int fake_suspend(void *context, vc_thread_id thread_id)
{
    fake_platform *platform = (fake_platform *)context;

    check_coordinator_adapter(platform);
    record_event(platform, 'S', thread_id);
    ++platform->suspend_calls;
    if (platform->suspend_mutation != MUTATION_NONE &&
        platform->suspend_calls ==
            platform->mutate_suspend_call) {
        fake_mutation mutation = platform->suspend_mutation;

        platform->suspend_mutation = MUTATION_NONE;
        apply_mutation(platform, mutation);
    }
    return thread_id == platform->suspend_failure ? -1 : 0;
}

static int fake_resume(void *context, vc_thread_id thread_id)
{
    fake_platform *platform = (fake_platform *)context;

    check_coordinator_adapter(platform);
    record_event(platform, 'R', thread_id);
    ++platform->resume_calls;
    if (platform->resume_mutation != MUTATION_NONE &&
        platform->resume_calls ==
            platform->mutate_resume_call) {
        fake_mutation mutation = platform->resume_mutation;

        platform->resume_mutation = MUTATION_NONE;
        apply_mutation(platform, mutation);
    }
    if (thread_id == platform->resume_failure &&
        platform->resume_failures_remaining != 0) {
        --platform->resume_failures_remaining;
        return -1;
    }
    if (platform->retire_resumed_threads) {
        platform->rejected_ownership_thread =
            thread_id;
    }
    return 0;
}

static vc_launch_claimant_dependencies claimant_dependencies(
    fake_platform *platform)
{
    vc_launch_claimant_dependencies dependencies;

    memset(&dependencies, 0, sizeof(dependencies));
    dependencies.get_observation = fake_claimant_observation;
    dependencies.get_time = fake_claimant_time;
    dependencies.transport = fake_transport;
    dependencies.plugin_unload = fake_unload;
    dependencies.context = platform;
    return dependencies;
}

static vc_menu_coordinator_dependencies coordinator_dependencies(
    fake_platform *platform)
{
    vc_menu_coordinator_dependencies dependencies;

    memset(&dependencies, 0, sizeof(dependencies));
    dependencies.get_runtime_snapshot =
        fake_runtime_snapshot;
    dependencies.verify_thread_ownership =
        fake_verify_ownership;
    dependencies.suspend_thread = fake_suspend;
    dependencies.resume_thread = fake_resume;
    dependencies.context = platform;
    return dependencies;
}

static vc_menu_thread_allowlist allowlist_for(
    const vc_thread_id *thread_ids,
    size_t thread_count)
{
    vc_menu_thread_allowlist allowlist;

    memset(&allowlist, 0, sizeof(allowlist));
    allowlist.process_id = TARGET_PID;
    allowlist.process_generation = TARGET_GENERATION;
    allowlist.module_generation = MODULE_GENERATION;
    allowlist.revision = ALLOWLIST_REVISION;
    allowlist.thread_ids = thread_ids;
    allowlist.thread_count = thread_count;
    allowlist.protected_threads = protected_threads();
    return allowlist;
}

static void initialize_fixture(fixture *fixture_value)
{
    vc_launch_claimant_dependencies claimant_deps;
    vc_menu_coordinator_dependencies coordinator_deps;
    vc_menu_thread_allowlist allowlist;

    memset(fixture_value, 0, sizeof(*fixture_value));
    fixture_value->platform.claimant =
        &fixture_value->claimant;
    fixture_value->platform.coordinator =
        &fixture_value->coordinator;
    fixture_value->platform.now_ms = 100;
    fixture_value->platform.allowlist_revision =
        ALLOWLIST_REVISION;
    fixture_value->platform.ownership_ok = true;
    fixture_value->platform.runtime_ok = true;
    fixture_value->platform.unload_ok = true;
    set_ready_observation(&fixture_value->platform);

    claimant_deps =
        claimant_dependencies(&fixture_value->platform);
    CHECK(vc_launch_claimant_init(
              &fixture_value->claimant, &claimant_deps,
              VC_LAUNCH_CLAIMANT_DEFAULT_AUTHORIZATION_TTL_MS) ==
          VC_LAUNCH_CLAIMANT_RESULT_OK);
    CHECK(vc_launch_claimant_start(
              &fixture_value->claimant) ==
          VC_LAUNCH_CLAIMANT_RESULT_OK);
    CHECK(vc_launch_claimant_bind_identity(
              &fixture_value->claimant) ==
          VC_LAUNCH_CLAIMANT_RESULT_OK);
    fixture_value->claimant.overlay_closed_stable = true;

    coordinator_deps =
        coordinator_dependencies(&fixture_value->platform);
    CHECK(vc_menu_coordinator_init(
              &fixture_value->coordinator,
              &fixture_value->claimant,
              &coordinator_deps) ==
          VC_MENU_COORDINATOR_RESULT_OK);
    CHECK(vc_menu_coordinator_start(
              &fixture_value->coordinator) ==
          VC_MENU_COORDINATOR_RESULT_OK);
    CHECK(vc_menu_coordinator_bind_target(
              &fixture_value->coordinator,
              &fixture_value->platform.observation.identity) ==
          VC_MENU_COORDINATOR_RESULT_OK);
    allowlist = allowlist_for(
        gameplay_threads,
        sizeof(gameplay_threads) / sizeof(gameplay_threads[0]));
    CHECK(vc_menu_coordinator_configure_allowlist(
              &fixture_value->coordinator, &allowlist) ==
          VC_MENU_COORDINATOR_RESULT_OK);
}

static void authorize(fixture *fixture_value)
{
    CHECK(vc_launch_claimant_request_claim(
              &fixture_value->claimant) ==
          VC_LAUNCH_CLAIMANT_RESULT_OK);
    CHECK(vc_launch_claimant_worker_step(
              &fixture_value->claimant) ==
          VC_LAUNCH_CLAIMANT_RESULT_OK);
    CHECK(vc_launch_claimant_worker_step(
              &fixture_value->claimant) ==
          VC_LAUNCH_CLAIMANT_RESULT_OK);
    CHECK(vc_launch_claimant_get_status(
              &fixture_value->claimant) ==
          VC_LAUNCH_CLAIMANT_STATUS_OPEN_AUTHORIZED);
}

static void begin_open(fixture *fixture_value)
{
    authorize(fixture_value);
    CHECK(vc_menu_coordinator_begin_open(
              &fixture_value->coordinator,
              fixture_value->platform.now_ms,
              1000, 100, &fixture_value->lease) ==
          VC_MENU_COORDINATOR_RESULT_OK);
}

static void acknowledge_open(fixture *fixture_value)
{
    ++fixture_value->platform.now_ms;
    CHECK(vc_menu_coordinator_acknowledge_open(
              &fixture_value->coordinator,
              fixture_value->platform.now_ms,
              &fixture_value->lease) ==
          VC_MENU_COORDINATOR_RESULT_OK);
}

static void check_event(const fake_platform *platform,
                        size_t index,
                        char operation,
                        vc_thread_id thread_id)
{
    CHECK(index <
          platform->suspend_calls + platform->resume_calls);
    if (index <
        platform->suspend_calls + platform->resume_calls) {
        CHECK(platform->events[index].operation == operation);
        CHECK(platform->events[index].thread_id == thread_id);
    }
}

static void test_happy_path_and_lease_replay(void)
{
    fixture fixture_value;
    vc_menu_open_lease provisional;
    vc_menu_open_lease open_lease;
    vc_menu_open_lease wrong;
    vc_menu_coordinator_state state;

    initialize_fixture(&fixture_value);
    begin_open(&fixture_value);
    provisional = fixture_value.lease;
    CHECK(vc_menu_coordinator_get_status(
              &fixture_value.coordinator) ==
          VC_MENU_COORDINATOR_STATUS_AWAITING_OPEN_ACK);
    CHECK(fixture_value.platform.suspend_calls == 3);
    check_event(&fixture_value.platform, 0, 'S', 10);
    check_event(&fixture_value.platform, 1, 'S', 20);
    check_event(&fixture_value.platform, 2, 'S', 30);

    acknowledge_open(&fixture_value);
    open_lease = fixture_value.lease;
    CHECK(memcmp(&provisional, &open_lease,
                 sizeof(open_lease)) != 0);
    CHECK(vc_launch_claimant_get_status(
              &fixture_value.claimant) ==
          VC_LAUNCH_CLAIMANT_STATUS_OPEN);
    CHECK(vc_menu_coordinator_acknowledge_open(
              &fixture_value.coordinator,
              fixture_value.platform.now_ms,
              &provisional) ==
          VC_MENU_COORDINATOR_RESULT_WRONG_LEASE);
    provisional = open_lease;
    CHECK(vc_menu_coordinator_begin_open(
              &fixture_value.coordinator,
              fixture_value.platform.now_ms,
              1000, 100, &provisional) ==
          VC_MENU_COORDINATOR_RESULT_BUSY);
    CHECK(memcmp(&provisional, &open_lease,
                 sizeof(open_lease)) == 0);

    wrong = open_lease;
    ++wrong.opaque[0];
    CHECK(vc_menu_coordinator_close(
              &fixture_value.coordinator,
              fixture_value.platform.now_ms, &wrong) ==
          VC_MENU_COORDINATOR_RESULT_WRONG_LEASE);
    CHECK(fixture_value.platform.resume_calls == 0);
    CHECK(vc_menu_coordinator_close(
              &fixture_value.coordinator,
              fixture_value.platform.now_ms,
              &fixture_value.lease) ==
          VC_MENU_COORDINATOR_RESULT_OK);
    CHECK(fixture_value.lease.opaque[0] == 0);
    CHECK(fixture_value.platform.resume_calls == 3);
    check_event(&fixture_value.platform, 3, 'R', 30);
    check_event(&fixture_value.platform, 4, 'R', 20);
    check_event(&fixture_value.platform, 5, 'R', 10);
    CHECK(vc_launch_claimant_get_status(
              &fixture_value.claimant) ==
          VC_LAUNCH_CLAIMANT_STATUS_CANCELLED);
    CHECK(vc_menu_coordinator_get_state(
              &fixture_value.coordinator, &state) ==
          VC_MENU_COORDINATOR_RESULT_OK);
    CHECK(!state.cleanup_pending);
    CHECK(!state.pause_owned);
    CHECK(!state.menu_authority_active);
}

static void test_allowlist_validation(void)
{
    fixture fixture_value;
    vc_menu_thread_allowlist allowlist;
    vc_thread_id ids[VC_PAUSE_MAX_THREADS + 1u];
    vc_thread_id protected_ids[6];
    size_t index;

    initialize_fixture(&fixture_value);
    allowlist = allowlist_for(ids, 0);
    CHECK(vc_menu_coordinator_configure_allowlist(
              &fixture_value.coordinator, &allowlist) ==
          VC_MENU_COORDINATOR_RESULT_INVALID_ALLOWLIST);
    allowlist.thread_count = VC_PAUSE_MAX_THREADS + 1u;
    CHECK(vc_menu_coordinator_configure_allowlist(
              &fixture_value.coordinator, &allowlist) ==
          VC_MENU_COORDINATOR_RESULT_INVALID_ALLOWLIST);

    for (index = 0; index < VC_PAUSE_MAX_THREADS; ++index) {
        ids[index] = (vc_thread_id)(index + 1u);
    }
    allowlist = allowlist_for(ids, VC_PAUSE_MAX_THREADS);
    allowlist.protected_threads.plugin_control_worker = 1000;
    allowlist.protected_threads.input_hook = 1001;
    allowlist.protected_threads.renderer_present_hook = 1002;
    allowlist.protected_threads.watchdog_worker = 1003;
    allowlist.protected_threads.cleanup_worker = 1004;
    allowlist.protected_threads.current_thread = 1005;
    CHECK(vc_menu_coordinator_configure_allowlist(
              &fixture_value.coordinator, &allowlist) ==
          VC_MENU_COORDINATOR_RESULT_OK);

    ids[0] = 0;
    allowlist = allowlist_for(ids, 3);
    CHECK(vc_menu_coordinator_configure_allowlist(
              &fixture_value.coordinator, &allowlist) ==
          VC_MENU_COORDINATOR_RESULT_INVALID_ALLOWLIST);
    ids[0] = 10;
    ids[1] = 9;
    ids[2] = 30;
    CHECK(vc_menu_coordinator_configure_allowlist(
              &fixture_value.coordinator, &allowlist) ==
          VC_MENU_COORDINATOR_RESULT_INVALID_ALLOWLIST);
    ids[1] = 10;
    CHECK(vc_menu_coordinator_configure_allowlist(
              &fixture_value.coordinator, &allowlist) ==
          VC_MENU_COORDINATOR_RESULT_INVALID_ALLOWLIST);
    ids[0] = -1;
    ids[1] = 20;
    CHECK(vc_menu_coordinator_configure_allowlist(
              &fixture_value.coordinator, &allowlist) ==
          VC_MENU_COORDINATOR_RESULT_INVALID_ALLOWLIST);

    protected_ids[0] = 100;
    protected_ids[1] = 101;
    protected_ids[2] = 102;
    protected_ids[3] = 103;
    protected_ids[4] = 104;
    protected_ids[5] = 105;
    for (index = 0; index < 6; ++index) {
        size_t position;

        for (position = 0; position < 3; ++position) {
            vc_thread_id protected_list[3] = {1, 2, 3};

            protected_list[position] = protected_ids[index];
            if (position == 0) {
                protected_list[1] =
                    (vc_thread_id)(protected_ids[index] + 1);
                protected_list[2] =
                    (vc_thread_id)(protected_ids[index] + 2);
            } else if (position == 1) {
                protected_list[0] =
                    (vc_thread_id)(protected_ids[index] - 1);
                protected_list[2] =
                    (vc_thread_id)(protected_ids[index] + 1);
            } else {
                protected_list[0] =
                    (vc_thread_id)(protected_ids[index] - 2);
                protected_list[1] =
                    (vc_thread_id)(protected_ids[index] - 1);
            }
            allowlist = allowlist_for(protected_list, 3);
            CHECK(vc_menu_coordinator_configure_allowlist(
                      &fixture_value.coordinator, &allowlist) ==
                  VC_MENU_COORDINATOR_RESULT_INVALID_ALLOWLIST);
        }
    }

    allowlist = allowlist_for(gameplay_threads, 3);
    allowlist.process_generation = TARGET_GENERATION + 1u;
    CHECK(vc_menu_coordinator_configure_allowlist(
              &fixture_value.coordinator, &allowlist) ==
          VC_MENU_COORDINATOR_RESULT_INVALID_ALLOWLIST);
    allowlist = allowlist_for(gameplay_threads, 3);
    allowlist.module_generation = MODULE_GENERATION + 1u;
    CHECK(vc_menu_coordinator_configure_allowlist(
              &fixture_value.coordinator, &allowlist) ==
          VC_MENU_COORDINATOR_RESULT_INVALID_ALLOWLIST);

    initialize_fixture(&fixture_value);
    authorize(&fixture_value);
    fixture_value.platform.ownership_ok = false;
    CHECK(vc_menu_coordinator_begin_open(
              &fixture_value.coordinator, 100, 1000, 100,
              &fixture_value.lease) ==
          VC_MENU_COORDINATOR_RESULT_OWNERSHIP_FAILED);
    CHECK(vc_launch_claimant_get_status(
              &fixture_value.claimant) ==
          VC_LAUNCH_CLAIMANT_STATUS_OPEN_AUTHORIZED);
    fixture_value.platform.ownership_ok = true;
    CHECK(vc_menu_coordinator_begin_open(
              &fixture_value.coordinator, 100, 1000, 100,
              &fixture_value.lease) ==
          VC_MENU_COORDINATOR_RESULT_OK);

    initialize_fixture(&fixture_value);
    authorize(&fixture_value);
    fixture_value.platform.ownership_mutation =
        MUTATION_REVISION;
    CHECK(vc_menu_coordinator_begin_open(
              &fixture_value.coordinator, 100, 1000, 100,
              &fixture_value.lease) ==
          VC_MENU_COORDINATOR_RESULT_STALE);
    CHECK(fixture_value.platform.suspend_calls == 0);
    CHECK(vc_launch_claimant_get_status(
              &fixture_value.claimant) ==
          VC_LAUNCH_CLAIMANT_STATUS_CANCELLED);
}

static void test_authorization_and_deadline_validation(void)
{
    fixture fixture_value;
    vc_launch_open_authorization consumed;
    vc_launch_claimant_identity_snapshot wrong_target;
    vc_menu_thread_allowlist allowlist;

    initialize_fixture(&fixture_value);
    CHECK(vc_menu_coordinator_begin_open(
              &fixture_value.coordinator, 100, 1000, 100,
              &fixture_value.lease) ==
          VC_MENU_COORDINATOR_RESULT_NO_AUTHORIZATION);
    authorize(&fixture_value);
    CHECK(vc_launch_claimant_consume_open_authorization(
              &fixture_value.claimant, &consumed) ==
          VC_LAUNCH_CLAIMANT_RESULT_OK);
    CHECK(vc_menu_coordinator_begin_open(
              &fixture_value.coordinator, 100, 1000, 100,
              &fixture_value.lease) ==
          VC_MENU_COORDINATOR_RESULT_NO_AUTHORIZATION);

    initialize_fixture(&fixture_value);
    authorize(&fixture_value);
    CHECK(vc_menu_coordinator_begin_open(
              &fixture_value.coordinator, 100, 0, 1,
              &fixture_value.lease) ==
          VC_MENU_COORDINATOR_RESULT_INVALID_ARGUMENT);
    CHECK(vc_menu_coordinator_begin_open(
              &fixture_value.coordinator, 100,
              VC_PAUSE_MAX_DURATION_MS + 1u, 1,
              &fixture_value.lease) ==
          VC_MENU_COORDINATOR_RESULT_INVALID_ARGUMENT);
    CHECK(vc_menu_coordinator_begin_open(
              &fixture_value.coordinator, 100, 100, 0,
              &fixture_value.lease) ==
          VC_MENU_COORDINATOR_RESULT_INVALID_ARGUMENT);
    CHECK(vc_menu_coordinator_begin_open(
              &fixture_value.coordinator, 100, 100,
              VC_MENU_COORDINATOR_MAX_ACK_MS + 1u,
              &fixture_value.lease) ==
          VC_MENU_COORDINATOR_RESULT_INVALID_ARGUMENT);
    CHECK(vc_menu_coordinator_begin_open(
              &fixture_value.coordinator, UINT64_MAX - 5u,
              10, 5, &fixture_value.lease) ==
          VC_MENU_COORDINATOR_RESULT_INVALID_ARGUMENT);
    CHECK(fixture_value.platform.suspend_calls == 0);
    CHECK(vc_launch_claimant_get_status(
              &fixture_value.claimant) ==
          VC_LAUNCH_CLAIMANT_STATUS_OPEN_AUTHORIZED);

    initialize_fixture(&fixture_value);
    authorize(&fixture_value);
    wrong_target = fixture_value.platform.observation.identity;
    ++wrong_target.sequence;
    CHECK(vc_menu_coordinator_bind_target(
              &fixture_value.coordinator, &wrong_target) ==
          VC_MENU_COORDINATOR_RESULT_OK);
    allowlist = allowlist_for(gameplay_threads, 3);
    CHECK(vc_menu_coordinator_configure_allowlist(
              &fixture_value.coordinator, &allowlist) ==
          VC_MENU_COORDINATOR_RESULT_OK);
    CHECK(vc_menu_coordinator_begin_open(
              &fixture_value.coordinator, 100, 1000, 100,
              &fixture_value.lease) ==
          VC_MENU_COORDINATOR_RESULT_STALE);
    CHECK(vc_launch_claimant_get_status(
              &fixture_value.claimant) ==
          VC_LAUNCH_CLAIMANT_STATUS_CANCELLED);
}

static void test_suspend_failure_and_retry_cleanup(void)
{
    const vc_thread_id failures_to_test[] = {10, 20, 30};
    size_t index;

    for (index = 0;
         index < sizeof(failures_to_test) /
                     sizeof(failures_to_test[0]);
         ++index) {
        fixture fixture_value;
        vc_menu_coordinator_result result;

        initialize_fixture(&fixture_value);
        authorize(&fixture_value);
        fixture_value.platform.suspend_failure =
            failures_to_test[index];
        result = vc_menu_coordinator_begin_open(
            &fixture_value.coordinator, 100, 1000, 100,
            &fixture_value.lease);
        CHECK(result ==
              VC_MENU_COORDINATOR_RESULT_PAUSE_FAILED);
        CHECK(!fixture_value.coordinator.pause.active);
        CHECK(fixture_value.lease.opaque[0] == 0);
        CHECK(vc_launch_claimant_get_status(
                  &fixture_value.claimant) ==
              VC_LAUNCH_CLAIMANT_STATUS_CANCELLED);
    }

    {
        fixture fixture_value;
        vc_menu_coordinator_state state;

        initialize_fixture(&fixture_value);
        authorize(&fixture_value);
        fixture_value.platform.suspend_failure = 30;
        fixture_value.platform.resume_failure = 20;
        fixture_value.platform.resume_failures_remaining = 2;
        CHECK(vc_menu_coordinator_begin_open(
                  &fixture_value.coordinator, 100, 1000, 100,
                  &fixture_value.lease) ==
              VC_MENU_COORDINATOR_RESULT_CLEANUP_PENDING);
        CHECK(vc_menu_coordinator_get_state(
                  &fixture_value.coordinator, &state) ==
              VC_MENU_COORDINATOR_RESULT_OK);
        CHECK(state.cleanup_pending);
        CHECK(state.pause_owned);
        CHECK(vc_menu_coordinator_begin_open(
                  &fixture_value.coordinator, 100, 1000, 100,
                  &fixture_value.lease) ==
              VC_MENU_COORDINATOR_RESULT_BUSY);
        CHECK(vc_menu_coordinator_retry_cleanup(
                  &fixture_value.coordinator, 101) ==
              VC_MENU_COORDINATOR_RESULT_OK);
        CHECK(!fixture_value.coordinator.pause.active);
    }
}

static void test_callback_mutation_and_reentrancy(void)
{
    const fake_mutation mutations[] = {
        MUTATION_REVISION,
        MUTATION_PROCESS_GENERATION,
        MUTATION_MODULE_GENERATION,
        MUTATION_TITLE,
        MUTATION_FOREGROUND,
        MUTATION_OVERLAY,
        MUTATION_PRESENTATION
    };
    size_t index;

    for (index = 0;
         index < sizeof(mutations) / sizeof(mutations[0]);
         ++index) {
        fixture fixture_value;
        vc_menu_coordinator_result result;

        initialize_fixture(&fixture_value);
        authorize(&fixture_value);
        fixture_value.platform.suspend_mutation =
            mutations[index];
        fixture_value.platform.mutate_suspend_call = 1;
        result = vc_menu_coordinator_begin_open(
            &fixture_value.coordinator, 100, 1000, 100,
            &fixture_value.lease);
        CHECK(result == VC_MENU_COORDINATOR_RESULT_STALE ||
              result ==
                  VC_MENU_COORDINATOR_RESULT_CLEANUP_PENDING);
        CHECK(fixture_value.lease.opaque[0] == 0);
        CHECK(fixture_value.platform.resume_calls <= 1);
    }

    {
        fixture fixture_value;

        initialize_fixture(&fixture_value);
        authorize(&fixture_value);
        fixture_value.platform.reenter = true;
        CHECK(vc_menu_coordinator_begin_open(
                  &fixture_value.coordinator, 100, 1000, 100,
                  &fixture_value.lease) ==
              VC_MENU_COORDINATOR_RESULT_OK);
        CHECK(fixture_value.platform.busy_reentries != 0);
        CHECK(fixture_value.platform.suspend_calls == 3);
    }
}

static void test_claimant_revocation_boundaries(void)
{
    fixture fixture_value;
    vc_menu_coordinator_result result;

    initialize_fixture(&fixture_value);
    authorize(&fixture_value);
    fixture_value.platform.suspend_mutation =
        MUTATION_CLAIMANT_CANCEL;
    fixture_value.platform.mutate_suspend_call = 1;
    result = vc_menu_coordinator_begin_open(
        &fixture_value.coordinator, 100, 1000, 100,
        &fixture_value.lease);
    CHECK(result == VC_MENU_COORDINATOR_RESULT_STALE ||
          result == VC_MENU_COORDINATOR_RESULT_CLEANUP_PENDING);
    CHECK(fixture_value.lease.opaque[0] == 0);
    CHECK(!fixture_value.coordinator.pause.active);
    CHECK(fixture_value.platform.resume_calls == 1);

    initialize_fixture(&fixture_value);
    begin_open(&fixture_value);
    fixture_value.platform.runtime_mutation =
        MUTATION_CLAIMANT_CANCEL;
    fixture_value.platform.mutate_runtime_call =
        fixture_value.platform.runtime_calls + 2u;
    CHECK(vc_menu_coordinator_acknowledge_open(
              &fixture_value.coordinator, 101,
              &fixture_value.lease) ==
          VC_MENU_COORDINATOR_RESULT_ACK_FAILED);
    CHECK(fixture_value.lease.opaque[0] == 0);
    CHECK(!fixture_value.coordinator.pause.active);
}

static void test_ack_timeout_clock_and_identity_cleanup(void)
{
    fixture fixture_value;
    vc_menu_open_lease stale_lease;

    initialize_fixture(&fixture_value);
    begin_open(&fixture_value);
    stale_lease = fixture_value.lease;
    fixture_value.platform.now_ms = 199;
    CHECK(vc_menu_coordinator_tick(
              &fixture_value.coordinator, 199) ==
          VC_MENU_COORDINATOR_RESULT_OK);
    fixture_value.platform.now_ms = 200;
    CHECK(vc_menu_coordinator_tick(
              &fixture_value.coordinator, 200) ==
          VC_MENU_COORDINATOR_RESULT_EXPIRED);
    CHECK(fixture_value.platform.resume_calls == 3);
    CHECK(vc_menu_coordinator_close(
              &fixture_value.coordinator, 200,
              &stale_lease) ==
          VC_MENU_COORDINATOR_RESULT_WRONG_LEASE);
    CHECK(vc_menu_coordinator_retry_cleanup(
              &fixture_value.coordinator, 200) ==
          VC_MENU_COORDINATOR_RESULT_NO_ACTION);

    initialize_fixture(&fixture_value);
    begin_open(&fixture_value);
    CHECK(vc_launch_claimant_cancel(
              &fixture_value.claimant) ==
          VC_LAUNCH_CLAIMANT_RESULT_OK);
    fixture_value.platform.now_ms = 101;
    CHECK(vc_menu_coordinator_acknowledge_open(
              &fixture_value.coordinator, 101,
              &fixture_value.lease) ==
          VC_MENU_COORDINATOR_RESULT_STALE);
    CHECK(!fixture_value.coordinator.pause.active);

    initialize_fixture(&fixture_value);
    begin_open(&fixture_value);
    acknowledge_open(&fixture_value);
    fixture_value.platform.now_ms = 100;
    CHECK(vc_menu_coordinator_tick(
              &fixture_value.coordinator, 100) ==
          VC_MENU_COORDINATOR_RESULT_CLOCK_ROLLBACK);
    CHECK(!fixture_value.coordinator.pause.active);

    initialize_fixture(&fixture_value);
    begin_open(&fixture_value);
    acknowledge_open(&fixture_value);
    ++fixture_value.platform.observation.overlay.sequence;
    fixture_value.platform.observation.overlay.state =
        VC_LAUNCH_OVERLAY_OPEN;
    fixture_value.platform.now_ms = 102;
    CHECK(vc_menu_coordinator_notify_overlay(
              &fixture_value.coordinator,
              &fixture_value.platform.observation.overlay,
              102) == VC_MENU_COORDINATOR_RESULT_STALE);
    CHECK(!fixture_value.coordinator.pause.active);

    initialize_fixture(&fixture_value);
    begin_open(&fixture_value);
    acknowledge_open(&fixture_value);
    ++fixture_value.platform.observation.presentation.sequence;
    fixture_value.platform.observation.presentation.state =
        VC_LAUNCH_PRESENTATION_LOST;
    fixture_value.platform.now_ms = 102;
    CHECK(vc_menu_coordinator_notify_presentation(
              &fixture_value.coordinator,
              &fixture_value.platform.observation.presentation,
              102) == VC_MENU_COORDINATOR_RESULT_STALE);
    CHECK(!fixture_value.coordinator.pause.active);

    initialize_fixture(&fixture_value);
    begin_open(&fixture_value);
    acknowledge_open(&fixture_value);
    apply_mutation(
        &fixture_value.platform,
        MUTATION_PROCESS_GENERATION);
    fixture_value.platform.now_ms = 102;
    CHECK(vc_menu_coordinator_target_changed(
              &fixture_value.coordinator, 102) ==
          VC_MENU_COORDINATOR_RESULT_CLEANUP_PENDING);
    CHECK(fixture_value.coordinator.pause.active);
    CHECK(vc_menu_coordinator_target_exit(
              &fixture_value.coordinator,
              TARGET_PID, TARGET_GENERATION, 103) ==
          VC_MENU_COORDINATOR_RESULT_STALE);
    CHECK(!fixture_value.coordinator.pause.active);

    initialize_fixture(&fixture_value);
    begin_open(&fixture_value);
    acknowledge_open(&fixture_value);
    fixture_value.platform.now_ms = 1099;
    CHECK(vc_menu_coordinator_tick(
              &fixture_value.coordinator, 1099) ==
          VC_MENU_COORDINATOR_RESULT_OK);
    fixture_value.platform.now_ms = 1100;
    CHECK(vc_menu_coordinator_tick(
              &fixture_value.coordinator, 1100) ==
          VC_MENU_COORDINATOR_RESULT_EXPIRED);
    CHECK(!fixture_value.coordinator.pause.active);
}

static void test_exit_stop_unload_and_failed_resume(void)
{
    fixture fixture_value;
    vc_menu_coordinator_state state;

    initialize_fixture(&fixture_value);
    begin_open(&fixture_value);
    acknowledge_open(&fixture_value);
    fixture_value.platform.resume_failure = 20;
    fixture_value.platform.resume_failures_remaining = 1;
    CHECK(vc_menu_coordinator_stop(
              &fixture_value.coordinator, 102) ==
          VC_MENU_COORDINATOR_RESULT_CLEANUP_PENDING);
    CHECK(vc_menu_coordinator_get_status(
              &fixture_value.coordinator) ==
          VC_MENU_COORDINATOR_STATUS_STOPPED);
    CHECK(vc_menu_coordinator_get_state(
              &fixture_value.coordinator, &state) ==
          VC_MENU_COORDINATOR_RESULT_OK);
    CHECK(state.cleanup_pending);
    CHECK(!state.menu_authority_active);
    CHECK(vc_menu_coordinator_start(
              &fixture_value.coordinator) ==
          VC_MENU_COORDINATOR_RESULT_CLEANUP_PENDING);
    CHECK(vc_menu_coordinator_retry_cleanup(
              &fixture_value.coordinator, 103) ==
          VC_MENU_COORDINATOR_RESULT_OK);
    CHECK(!fixture_value.coordinator.target_bound);
    CHECK(!fixture_value.coordinator.allowlist_configured);
    CHECK(vc_menu_coordinator_start(
              &fixture_value.coordinator) ==
          VC_MENU_COORDINATOR_RESULT_OK);

    initialize_fixture(&fixture_value);
    begin_open(&fixture_value);
    acknowledge_open(&fixture_value);
    fixture_value.platform.retire_resumed_threads = true;
    CHECK(vc_menu_coordinator_close(
              &fixture_value.coordinator, 102,
              &fixture_value.lease) ==
          VC_MENU_COORDINATOR_RESULT_OK);
    CHECK(fixture_value.platform.resume_calls == 3);
    CHECK(!fixture_value.coordinator.pause.active);

    initialize_fixture(&fixture_value);
    begin_open(&fixture_value);
    acknowledge_open(&fixture_value);
    fixture_value.platform.resume_mutation =
        MUTATION_PROCESS_GENERATION;
    fixture_value.platform.mutate_resume_call = 1;
    CHECK(vc_menu_coordinator_close(
              &fixture_value.coordinator, 102,
              &fixture_value.lease) ==
          VC_MENU_COORDINATOR_RESULT_CLEANUP_PENDING);
    CHECK(fixture_value.coordinator.pause.active);
    CHECK(vc_menu_coordinator_target_exit(
              &fixture_value.coordinator,
              TARGET_PID, TARGET_GENERATION, 103) ==
          VC_MENU_COORDINATOR_RESULT_STALE);
    CHECK(!fixture_value.coordinator.pause.active);

    initialize_fixture(&fixture_value);
    begin_open(&fixture_value);
    CHECK(vc_menu_coordinator_target_exit(
              &fixture_value.coordinator,
              TARGET_PID, TARGET_GENERATION, 101) ==
          VC_MENU_COORDINATOR_RESULT_STALE);
    CHECK(!fixture_value.coordinator.pause.active);
    CHECK(fixture_value.platform.resume_calls == 0);
    CHECK(vc_menu_coordinator_target_exit(
              &fixture_value.coordinator,
              TARGET_PID + 1u, TARGET_GENERATION, 102) ==
          VC_MENU_COORDINATOR_RESULT_NO_ACTION);

    initialize_fixture(&fixture_value);
    begin_open(&fixture_value);
    acknowledge_open(&fixture_value);
    fixture_value.platform.now_ms = 102;
    CHECK(vc_menu_coordinator_plugin_unload(
              &fixture_value.coordinator, 102) ==
          VC_MENU_COORDINATOR_RESULT_OK);
    CHECK(fixture_value.platform.unload_calls == 1);
    CHECK(vc_menu_coordinator_get_status(
              &fixture_value.coordinator) ==
          VC_MENU_COORDINATOR_STATUS_STOPPED);

    initialize_fixture(&fixture_value);
    begin_open(&fixture_value);
    acknowledge_open(&fixture_value);
    fixture_value.platform.now_ms = 102;
    fixture_value.platform.unload_ok = false;
    CHECK(vc_menu_coordinator_plugin_unload(
              &fixture_value.coordinator, 102) ==
          VC_MENU_COORDINATOR_RESULT_CLEANUP_PENDING);
    CHECK(vc_menu_coordinator_get_status(
              &fixture_value.coordinator) ==
          VC_MENU_COORDINATOR_STATUS_STOPPED);
    fixture_value.platform.unload_ok = true;
    CHECK(vc_menu_coordinator_retry_cleanup(
              &fixture_value.coordinator, 103) ==
          VC_MENU_COORDINATOR_RESULT_OK);
    CHECK(fixture_value.platform.unload_calls == 2);

    initialize_fixture(&fixture_value);
    begin_open(&fixture_value);
    acknowledge_open(&fixture_value);
    CHECK(vc_menu_coordinator_service_stop(
              &fixture_value.coordinator, 102) ==
          VC_MENU_COORDINATOR_RESULT_OK);
    CHECK(fixture_value.claimant.phase ==
          VC_LAUNCH_CLAIMANT_PHASE_STOPPED);
    CHECK(vc_menu_coordinator_get_status(
              &fixture_value.coordinator) ==
          VC_MENU_COORDINATOR_STATUS_STOPPED);
}

static void test_reset_and_formatter(void)
{
    fixture fixture_value;
    char one[1] = {'x'};
    char short_buffer[8];
    char exact[23];
    char large[128];
    size_t length;

    initialize_fixture(&fixture_value);
    begin_open(&fixture_value);
    CHECK(vc_menu_coordinator_reset(
              &fixture_value.coordinator, 101) ==
          VC_MENU_COORDINATOR_RESULT_OK);
    CHECK(!fixture_value.coordinator.target_bound);
    CHECK(!fixture_value.coordinator.allowlist_configured);
    CHECK(fixture_value.claimant.lifecycle_generation > 1);

    length = vc_menu_coordinator_format_status(
        &fixture_value.coordinator, NULL, 0);
    CHECK(length == strlen("Menu coordinator ready"));
    CHECK(vc_menu_coordinator_format_status(
              &fixture_value.coordinator, one,
              sizeof(one)) == length);
    CHECK(one[0] == '\0');
    memset(short_buffer, 'x', sizeof(short_buffer));
    CHECK(vc_menu_coordinator_format_status(
              &fixture_value.coordinator, short_buffer,
              sizeof(short_buffer)) == length);
    CHECK(short_buffer[sizeof(short_buffer) - 1u] == '\0');
    CHECK(vc_menu_coordinator_format_status(
              &fixture_value.coordinator, exact,
              sizeof(exact)) == length);
    CHECK(strcmp(exact, "Menu coordinator ready") == 0);

    CHECK(vc_menu_coordinator_stop(
              &fixture_value.coordinator, 102) ==
          VC_MENU_COORDINATOR_RESULT_OK);
    CHECK(vc_menu_coordinator_format_status(
              &fixture_value.coordinator, large,
              sizeof(large)) ==
          strlen("Menu coordinator stopped"));
    CHECK(strstr(large, "12345678") == NULL);
    CHECK(strstr(large, "010203") == NULL);
    CHECK(strstr(large, "PCSA") == NULL);
    CHECK(strstr(large, "10") == NULL);
}

int main(void)
{
    test_happy_path_and_lease_replay();
    test_allowlist_validation();
    test_authorization_and_deadline_validation();
    test_suspend_failure_and_retry_cleanup();
    test_callback_mutation_and_reentrancy();
    test_claimant_revocation_boundaries();
    test_ack_timeout_clock_and_identity_cleanup();
    test_exit_stop_unload_and_failed_resume();
    test_reset_and_formatter();

    if (failures != 0) {
        fprintf(stderr,
                "%d menu coordinator test(s) failed\n",
                failures);
        return 1;
    }
    puts("all menu coordinator tests passed");
    return 0;
}
