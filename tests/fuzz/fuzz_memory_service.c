#include "vitacheat/memory_service.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum {
    VC_MEMORY_FUZZ_MAX_STEPS = 64
};

typedef struct fuzz_environment {
    vc_launch_claimant claimant;
    vc_menu_coordinator coordinator;
    vc_target_attestation attestation;
    vc_memory_service service;
    vc_launch_claimant_observation observation;
    vc_target_snapshot target_source;
    vc_launch_trusted_caller caller;
    uint8_t memory[512];
    uint64_t now_ms;
    uint64_t allowlist_revision;
    size_t read_bytes;
    bool copy_from_ok;
    bool copy_to_ok;
    bool read_ok;
    bool mutate_read;
} fuzz_environment;

static void fuzz_check(bool condition)
{
    if (!condition) {
        abort();
    }
}

static uint32_t fuzz_u32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] |
           ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) |
           ((uint32_t)bytes[3] << 24);
}

static uint64_t fuzz_u64(const uint8_t *bytes)
{
    uint64_t value = 0;
    unsigned int index;

    for (index = 0; index < 8; ++index) {
        value |= (uint64_t)bytes[index] << (index * 8);
    }
    return value;
}

static void fuzz_set_observation(fuzz_environment *environment)
{
    static const uint8_t title[] = {
        'P', 'C', 'S', 'A', '0', '0', '1', '3', '3'
    };
    vc_launch_claimant_identity_snapshot *identity =
        &environment->observation.identity;

    memset(&environment->observation, 0,
           sizeof(environment->observation));
    identity->sequence = 1;
    identity->caller.role = VC_LAUNCH_CALLER_GAME_PLUGIN;
    identity->caller.process_id = 17;
    identity->caller.process_generation = 19;
    identity->caller.module_generation = 23;
    identity->foreground.sequence = 1;
    identity->foreground.present = 1;
    identity->foreground.target_process_id = 17;
    identity->foreground.target_generation = 19;
    identity->foreground.title_id_size = sizeof(title);
    memcpy(identity->foreground.title_id, title, sizeof(title));
    identity->title_id_size = sizeof(title);
    memcpy(identity->title_id, title, sizeof(title));
    environment->observation.overlay.sequence = 3;
    environment->observation.overlay.generation = 1;
    environment->observation.overlay.state =
        VC_LAUNCH_OVERLAY_CLOSED;
    environment->observation.presentation.sequence = 2;
    environment->observation.presentation.process_id = 17;
    environment->observation.presentation.process_generation = 19;
    environment->observation.presentation.module_generation = 23;
    environment->observation.presentation.state =
        VC_LAUNCH_PRESENTATION_READY;
}

static void fuzz_set_target(fuzz_environment *environment)
{
    static const uint8_t title[] = {
        'P', 'C', 'S', 'A', '0', '0', '1', '3', '3'
    };
    vc_target_snapshot *source = &environment->target_source;
    size_t index;

    memset(source, 0, sizeof(*source));
    source->identity.process_id = 17;
    source->identity.process_generation = 19;
    source->identity.foreground_sequence = 1;
    source->identity.title_id_size = sizeof(title);
    memcpy(source->identity.title_id, title, sizeof(title));
    source->module_count = 1;
    source->modules[0].module_id = 0;
    source->modules[0].load_generation = 29;
    source->modules[0].segment_count = 2;
    source->modules[0].segments[0].segment_index = 0;
    source->modules[0].segments[0].base = 0x1000;
    source->modules[0].segments[0].size = 0x100;
    source->modules[0].segments[0].permissions =
        VC_TARGET_PERMISSION_EXECUTE;
    source->modules[0].segments[1].segment_index = 1;
    source->modules[0].segments[1].base = 0x2000;
    source->modules[0].segments[1].size = 512;
    source->modules[0].segments[1].permissions =
        VC_TARGET_PERMISSION_READ;
    source->thread_count = 1;
    source->threads[0].thread_id = 10;
    source->threads[0].process_id = 17;
    source->threads[0].process_generation = 19;
    for (index = 0; index < sizeof(environment->memory); ++index) {
        environment->memory[index] = (uint8_t)index;
    }
}

static bool fuzz_observation(
    void *context,
    vc_launch_claimant_observation *observation)
{
    fuzz_environment *environment =
        (fuzz_environment *)context;

    *observation = environment->observation;
    return true;
}

static bool fuzz_time(void *context, uint64_t *now_ms)
{
    fuzz_environment *environment =
        (fuzz_environment *)context;

    *now_ms = environment->now_ms;
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
    vc_launch_request request;
    vc_launch_response response;
    size_t encoded_size = 0;

    (void)context;
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
            ? 31
            : request.request_id;
    response.created_ms = request.now_ms;
    response.deadline_ms = request.now_ms + 5000;
    response.observed_ms = request.now_ms;
    response.capabilities = request.capabilities;
    response.status =
        request.operation == VC_LAUNCH_OPERATION_STATUS
            ? VC_LAUNCH_STATUS_PENDING
            : VC_LAUNCH_STATUS_CLAIMED;
    response.launch_state =
        request.operation == VC_LAUNCH_OPERATION_STATUS
            ? VC_LAUNCH_STATE_PENDING
            : VC_LAUNCH_STATE_CLAIMED;
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
    (void)context;
    (void)identity;
    (void)now_ms;
    return true;
}

static bool fuzz_runtime(
    void *context,
    vc_menu_runtime_snapshot *snapshot)
{
    fuzz_environment *environment =
        (fuzz_environment *)context;

    snapshot->observation = environment->observation;
    snapshot->allowlist_revision =
        environment->allowlist_revision;
    return true;
}

static bool fuzz_threads(
    void *context,
    const vc_launch_claimant_identity_snapshot *target,
    const vc_thread_id *thread_ids,
    size_t thread_count)
{
    (void)context;
    return target->caller.process_id == 17 &&
           thread_ids != NULL && thread_count == 1;
}

static bool fuzz_target_policy(
    void *context,
    const vc_launch_claimant_identity_snapshot *target,
    uint64_t target_snapshot_revision,
    const vc_thread_id *thread_ids,
    size_t thread_count,
    const vc_menu_protected_threads *protected_threads)
{
    fuzz_environment *environment =
        (fuzz_environment *)context;

    return target->caller.process_id == 17 &&
           target_snapshot_revision ==
               environment->attestation.snapshot.revision &&
           thread_ids != NULL && thread_count == 1 &&
           protected_threads != NULL;
}

static int fuzz_thread_operation(
    void *context,
    vc_thread_id thread_id)
{
    (void)context;
    (void)thread_id;
    return 0;
}

static bool fuzz_target_begin(
    void *context,
    vc_target_identity *identity,
    uint32_t *module_count,
    uint32_t *thread_count,
    uint64_t *token)
{
    fuzz_environment *environment =
        (fuzz_environment *)context;

    *identity = environment->target_source.identity;
    *module_count = environment->target_source.module_count;
    *thread_count = environment->target_source.thread_count;
    *token = 1;
    return true;
}

static bool fuzz_target_module(
    void *context,
    uint64_t token,
    uint32_t index,
    vc_target_module *module)
{
    fuzz_environment *environment =
        (fuzz_environment *)context;

    (void)token;
    *module = environment->target_source.modules[index];
    return true;
}

static bool fuzz_target_thread(
    void *context,
    uint64_t token,
    uint32_t index,
    vc_target_thread *thread)
{
    fuzz_environment *environment =
        (fuzz_environment *)context;

    (void)token;
    *thread = environment->target_source.threads[index];
    return true;
}

static bool fuzz_target_end(
    void *context,
    uint64_t token,
    uint64_t *completion)
{
    (void)context;
    *completion = token;
    return true;
}

static bool fuzz_copy_from(
    void *context,
    void *destination,
    const void *source,
    size_t size)
{
    fuzz_environment *environment =
        (fuzz_environment *)context;

    if (environment->copy_from_ok) {
        memmove(destination, source, size);
    }
    return environment->copy_from_ok;
}

static bool fuzz_copy_to(
    void *context,
    void *destination,
    const void *source,
    size_t size)
{
    fuzz_environment *environment =
        (fuzz_environment *)context;

    if (environment->copy_to_ok) {
        memmove(destination, source, size);
    }
    return environment->copy_to_ok;
}

static vc_launch_attestation_result fuzz_attest(
    void *context,
    const void *caller_context,
    vc_launch_trusted_caller *caller)
{
    fuzz_environment *environment =
        (fuzz_environment *)context;

    (void)caller_context;
    *caller = environment->caller;
    return VC_LAUNCH_ATTESTATION_ACCEPTED;
}

static bool fuzz_foreground(
    void *context,
    vc_launch_foreground_snapshot *foreground)
{
    fuzz_environment *environment =
        (fuzz_environment *)context;

    *foreground =
        environment->observation.identity.foreground;
    return true;
}

static bool fuzz_read(
    void *context,
    const vc_memory_target_read *request,
    uint8_t *destination,
    size_t capacity,
    vc_memory_target_read_observation *observation)
{
    fuzz_environment *environment =
        (fuzz_environment *)context;
    size_t offset =
        (size_t)(request->address - UINT32_C(0x2000));
    size_t copied = environment->read_bytes;

    if (copied > request->length) {
        copied = request->length;
    }
    if (copied > capacity) {
        copied = capacity;
    }
    if (offset < sizeof(environment->memory) &&
        copied <= sizeof(environment->memory) - offset) {
        memcpy(destination,
               environment->memory + offset, copied);
    }
    observation->bytes_read = environment->read_bytes;
    observation->target_generation =
        request->target_generation;
    observation->attestation_revision =
        request->attestation_revision;
    if (environment->mutate_read) {
        ++environment->observation.identity.sequence;
        ++environment->observation.identity.foreground.sequence;
    }
    return environment->read_ok;
}

static bool fuzz_cleanup(void *context)
{
    (void)context;
    return true;
}

static bool fuzz_prepare(
    fuzz_environment *environment,
    uint64_t first_session,
    uint64_t byte_budget,
    uint32_t operation_budget,
    uint64_t *session_id)
{
    vc_target_dependencies target_dependencies;
    vc_launch_claimant_dependencies claimant_dependencies;
    vc_menu_coordinator_dependencies coordinator_dependencies;
    vc_memory_service_dependencies memory_dependencies;
    vc_menu_thread_allowlist allowlist;
    vc_memory_read_status status;
    vc_thread_id thread = 10;
    vc_menu_open_lease lease;

    memset(environment, 0, sizeof(*environment));
    environment->now_ms = 100;
    environment->allowlist_revision = 7;
    environment->copy_from_ok = true;
    environment->copy_to_ok = true;
    environment->read_ok = true;
    fuzz_set_observation(environment);
    fuzz_set_target(environment);
    environment->caller =
        environment->observation.identity.caller;

    memset(&target_dependencies, 0, sizeof(target_dependencies));
    target_dependencies.begin_snapshot = fuzz_target_begin;
    target_dependencies.read_module = fuzz_target_module;
    target_dependencies.read_thread = fuzz_target_thread;
    target_dependencies.end_snapshot = fuzz_target_end;
    target_dependencies.cleanup = fuzz_cleanup;
    target_dependencies.context = environment;
    if (vc_target_attestation_init(
            &environment->attestation,
            &target_dependencies) != VC_TARGET_STATUS_OK ||
        vc_target_attestation_start(
            &environment->attestation) != VC_TARGET_STATUS_OK ||
        vc_target_attestation_refresh(
            &environment->attestation) != VC_TARGET_STATUS_OK) {
        return false;
    }

    memset(&claimant_dependencies, 0,
           sizeof(claimant_dependencies));
    claimant_dependencies.get_observation = fuzz_observation;
    claimant_dependencies.get_time = fuzz_time;
    claimant_dependencies.transport = fuzz_transport;
    claimant_dependencies.plugin_unload = fuzz_unload;
    claimant_dependencies.context = environment;
    if (vc_launch_claimant_init(
            &environment->claimant,
            &claimant_dependencies, 1000) !=
            VC_LAUNCH_CLAIMANT_RESULT_OK ||
        vc_launch_claimant_start(&environment->claimant) !=
            VC_LAUNCH_CLAIMANT_RESULT_OK ||
        vc_launch_claimant_bind_identity(
            &environment->claimant) !=
            VC_LAUNCH_CLAIMANT_RESULT_OK) {
        return false;
    }
    environment->claimant.overlay_closed_stable = true;

    memset(&coordinator_dependencies, 0,
           sizeof(coordinator_dependencies));
    coordinator_dependencies.get_runtime_snapshot =
        fuzz_runtime;
    coordinator_dependencies.verify_thread_ownership =
        fuzz_threads;
    coordinator_dependencies.validate_target_attestation =
        fuzz_target_policy;
    coordinator_dependencies.suspend_thread =
        fuzz_thread_operation;
    coordinator_dependencies.resume_thread =
        fuzz_thread_operation;
    coordinator_dependencies.context = environment;
    if (vc_menu_coordinator_init(
            &environment->coordinator,
            &environment->claimant,
            &coordinator_dependencies) !=
            VC_MENU_COORDINATOR_RESULT_OK ||
        vc_menu_coordinator_start(
            &environment->coordinator) !=
            VC_MENU_COORDINATOR_RESULT_OK ||
        vc_menu_coordinator_bind_target(
            &environment->coordinator,
            &environment->observation.identity) !=
            VC_MENU_COORDINATOR_RESULT_OK) {
        return false;
    }
    memset(&allowlist, 0, sizeof(allowlist));
    allowlist.process_id = 17;
    allowlist.process_generation = 19;
    allowlist.module_generation = 23;
    allowlist.revision = environment->allowlist_revision;
    allowlist.target_snapshot_revision =
        environment->attestation.snapshot.revision;
    allowlist.thread_ids = &thread;
    allowlist.thread_count = 1;
    allowlist.protected_threads.plugin_control_worker = 100;
    allowlist.protected_threads.input_hook = 101;
    allowlist.protected_threads.renderer_present_hook = 102;
    allowlist.protected_threads.watchdog_worker = 103;
    allowlist.protected_threads.cleanup_worker = 104;
    allowlist.protected_threads.current_thread = 105;
    if (vc_menu_coordinator_configure_allowlist(
            &environment->coordinator, &allowlist) !=
            VC_MENU_COORDINATOR_RESULT_OK ||
        vc_launch_claimant_request_claim(
            &environment->claimant) !=
            VC_LAUNCH_CLAIMANT_RESULT_OK ||
        vc_launch_claimant_worker_step(
            &environment->claimant) !=
            VC_LAUNCH_CLAIMANT_RESULT_OK ||
        vc_launch_claimant_worker_step(
            &environment->claimant) !=
            VC_LAUNCH_CLAIMANT_RESULT_OK ||
        vc_menu_coordinator_begin_open(
            &environment->coordinator, 100, 1000, 100,
            &lease) != VC_MENU_COORDINATOR_RESULT_OK) {
        return false;
    }
    environment->now_ms = 101;
    if (vc_menu_coordinator_acknowledge_open(
            &environment->coordinator, 101, &lease) !=
        VC_MENU_COORDINATOR_RESULT_OK) {
        return false;
    }

    memset(&memory_dependencies, 0, sizeof(memory_dependencies));
    memory_dependencies.copy_from_user = fuzz_copy_from;
    memory_dependencies.copy_to_user = fuzz_copy_to;
    memory_dependencies.attest_caller = fuzz_attest;
    memory_dependencies.get_foreground = fuzz_foreground;
    memory_dependencies.read_target = fuzz_read;
    memory_dependencies.cleanup = fuzz_cleanup;
    memory_dependencies.context = environment;
    if (vc_memory_service_init(
            &environment->service,
            &environment->attestation,
            &environment->coordinator,
            &memory_dependencies, first_session) !=
            VC_MEMORY_SERVICE_RESULT_OK ||
        vc_memory_service_start(
            &environment->service, 101) !=
            VC_MEMORY_SERVICE_RESULT_OK ||
        vc_memory_service_activate(
            &environment->service, 101,
            byte_budget, operation_budget,
            session_id, &status) !=
            VC_MEMORY_SERVICE_RESULT_OK ||
        status != VC_MEMORY_READ_STATUS_OK) {
        return false;
    }
    return true;
}

static void fuzz_invariants(const fuzz_environment *environment)
{
    fuzz_check(environment->service.phase <=
               VC_MEMORY_SERVICE_PHASE_STOPPED);
    fuzz_check(environment->service.session.remaining_bytes <=
               VC_MEMORY_SERVICE_MAX_BYTE_BUDGET);
    fuzz_check(environment->service.session.remaining_operations <=
               VC_MEMORY_SERVICE_MAX_OPERATION_BUDGET);
    if (environment->service.journal.valid) {
        fuzz_check(environment->service.journal.response_size >=
                   VC_MEMORY_READ_RESPONSE_HEADER_WIRE_SIZE);
        fuzz_check(environment->service.journal.response_size <=
                   VC_MEMORY_READ_RESPONSE_WIRE_MAX);
    }
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    fuzz_environment environment;
    vc_memory_read_request request;
    vc_memory_read_response response;
    uint8_t request_wire[VC_MEMORY_READ_REQUEST_WIRE_SIZE];
    uint8_t response_wire[VC_MEMORY_READ_RESPONSE_WIRE_MAX];
    uint64_t session_id;
    uint64_t request_id = 1;
    uint64_t first_session;
    uint64_t byte_budget;
    uint32_t operation_budget;
    size_t cursor;
    size_t step;

    if (size < 16) {
        return 0;
    }
    first_session = fuzz_u64(data);
    if (first_session == 0) {
        first_session = 1;
    }
    byte_budget =
        (uint64_t)(fuzz_u32(data + 8) %
                   VC_MEMORY_SERVICE_MAX_BYTE_BUDGET) +
        1u;
    operation_budget =
        (fuzz_u32(data + 12) %
         VC_MEMORY_SERVICE_MAX_OPERATION_BUDGET) +
        1u;
    if (!fuzz_prepare(
            &environment, first_session, byte_budget,
            operation_budget, &session_id)) {
        abort();
    }

    cursor = 16;
    for (step = 0;
         cursor < size && step < VC_MEMORY_FUZZ_MAX_STEPS;
         ++step) {
        const uint8_t command = data[cursor++];
        const uint8_t action = command & 7u;

        if ((command & 0x20u) != 0 &&
            environment.now_ms != 0) {
            --environment.now_ms;
        } else if (environment.now_ms != UINT64_MAX) {
            environment.now_ms += command & 3u;
        }
        environment.copy_from_ok = (command & 0x40u) == 0;
        environment.copy_to_ok = (command & 0x80u) == 0;
        environment.read_ok = (command & 0x10u) == 0;
        environment.mutate_read = (command & 8u) != 0;
        environment.read_bytes =
            command % (VC_MEMORY_READ_MAX_PAYLOAD + 2u);

        if (action <= 3u) {
            vc_memory_read_status operation_status;
            vc_memory_service_result service_result;
            size_t response_size = 0;
            size_t request_size =
                VC_MEMORY_READ_REQUEST_WIRE_SIZE;
            size_t response_capacity =
                VC_MEMORY_READ_RESPONSE_WIRE_MAX;
            size_t index;

            if ((command & 4u) == 0) {
                vc_memory_read_request_init(
                    &request,
                    (command & 1u) != 0
                        ? VC_MEMORY_READ_OPERATION_STATUS
                        : VC_MEMORY_READ_OPERATION_READ);
                request.request_id = request_id++;
                request.session_id = session_id;
                request.target_process_id = 17;
                request.target_generation = 19;
                request.attestation_revision =
                    environment.attestation.snapshot.revision;
                if (request.operation ==
                    VC_MEMORY_READ_OPERATION_READ) {
                    request.module_id = 0;
                    request.module_load_generation = 29;
                    request.segment_index =
                        (command >> 1) & 3u;
                    request.segment_offset =
                        fuzz_u32(data + (cursor % (size - 3u))) %
                        520u;
                    request.length =
                        (uint32_t)(command %
                                   VC_MEMORY_READ_MAX_PAYLOAD) +
                        1u;
                }
                if (vc_memory_read_request_encode(
                        &request, request_wire,
                        sizeof(request_wire),
                        &response_size) !=
                    VC_MEMORY_READ_STATUS_OK) {
                    memset(request_wire, command,
                           sizeof(request_wire));
                }
            } else {
                for (index = 0;
                     index < sizeof(request_wire);
                     ++index) {
                    request_wire[index] =
                        data[(cursor + index) % size];
                }
            }
            if ((command & 2u) != 0) {
                request_size =
                    command %
                    (VC_MEMORY_READ_REQUEST_WIRE_SIZE + 2u);
            }
            if ((command & 1u) != 0) {
                response_capacity =
                    command %
                    (VC_MEMORY_READ_RESPONSE_WIRE_MAX + 1u);
            }
            service_result = vc_memory_service_dispatch(
                &environment.service, NULL,
                request_wire, request_size,
                response_wire, response_capacity,
                environment.now_ms, &response_size,
                &operation_status);
            (void)service_result;
            fuzz_check(response_size == 0 ||
                       (response_size >=
                            VC_MEMORY_READ_RESPONSE_HEADER_WIRE_SIZE &&
                        response_size <=
                            VC_MEMORY_READ_RESPONSE_WIRE_MAX));
            if (response_size != 0) {
                fuzz_check(vc_memory_read_response_decode(
                               response_wire, response_size,
                               &response) ==
                           VC_MEMORY_READ_STATUS_OK);
            }
        } else if (action == 4u) {
            (void)vc_memory_service_tick(
                &environment.service, environment.now_ms);
        } else if (action == 5u) {
            (void)vc_memory_service_overlay_changed(
                &environment.service,
                (command & 8u) != 0
                    ? VC_LAUNCH_OVERLAY_OPEN
                    : VC_LAUNCH_OVERLAY_CLOSED);
        } else if (action == 6u) {
            (void)vc_memory_service_target_changed(
                &environment.service);
        } else {
            (void)vc_memory_service_reset(
                &environment.service, environment.now_ms);
        }
        fuzz_invariants(&environment);
    }
    return 0;
}

#ifdef VC_MEMORY_SERVICE_FUZZ_STANDALONE
int main(void)
{
    uint8_t data[640];
    uint32_t random_state = UINT32_C(0x6d656d72);
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
