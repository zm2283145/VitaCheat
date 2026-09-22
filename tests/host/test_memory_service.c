#include "vitacheat/memory_service.h"

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
#define PLUGIN_GENERATION UINT64_C(0x1112131415161718)
#define MODULE_GENERATION UINT64_C(0x2122232425262728)
#define REQUEST_ID UINT64_C(0x3132333435363738)
#define SEGMENT_BASE UINT32_C(0x2000)
#define SEGMENT_SIZE UINT32_C(512)

typedef struct fake_platform {
    vc_launch_claimant *claimant;
    vc_menu_coordinator *coordinator;
    vc_target_attestation *attestation;
    vc_memory_service *memory_service;
    vc_launch_claimant_observation observation;
    vc_target_snapshot target_source;
    vc_launch_trusted_caller memory_caller;
    vc_launch_attestation_result caller_result;
    uint8_t memory[SEGMENT_SIZE];
    uint64_t now_ms;
    uint64_t allowlist_revision;
    size_t read_bytes;
    uint64_t observed_generation;
    uint64_t observed_revision;
    uint32_t last_read_address;
    uint32_t last_read_length;
    unsigned int copy_from_calls;
    unsigned int copy_to_calls;
    unsigned int read_calls;
    unsigned int busy_reentries;
    bool copy_from_ok;
    bool copy_to_ok;
    bool foreground_ok;
    bool read_ok;
    bool cleanup_ok;
    bool reenter_memory;
    bool mutate_foreground_during_read;
} fake_platform;

typedef struct fixture {
    vc_launch_claimant claimant;
    vc_menu_coordinator coordinator;
    vc_target_attestation attestation;
    vc_memory_service service;
    fake_platform platform;
    vc_menu_open_lease lease;
    uint64_t session_id;
} fixture;

static const vc_thread_id gameplay_threads[] = {10, 20, 30};

static void check_memory_callback(fake_platform *platform)
{
    if (platform->memory_service == NULL) {
        return;
    }
    CHECK(atomic_load_explicit(
              &platform->memory_service->adapter_active,
              memory_order_acquire) != 0u);
    CHECK(atomic_load_explicit(
              &platform->memory_service->transaction_busy,
              memory_order_acquire) == 0u);
    if (platform->reenter_memory) {
        CHECK(vc_memory_service_tick(
                  platform->memory_service,
                  platform->now_ms) ==
              VC_MEMORY_SERVICE_RESULT_BUSY);
        ++platform->busy_reentries;
    }
}

static void set_observation(fake_platform *platform)
{
    static const uint8_t title[] = {
        'P', 'C', 'S', 'A', '0', '0', '1', '3', '3'
    };
    vc_launch_claimant_identity_snapshot *identity =
        &platform->observation.identity;

    memset(&platform->observation, 0,
           sizeof(platform->observation));
    identity->sequence = 1;
    identity->caller.role = VC_LAUNCH_CALLER_GAME_PLUGIN;
    identity->caller.process_id = TARGET_PID;
    identity->caller.process_generation = TARGET_GENERATION;
    identity->caller.module_generation = PLUGIN_GENERATION;
    identity->foreground.sequence = 1;
    identity->foreground.present = 1;
    identity->foreground.target_process_id = TARGET_PID;
    identity->foreground.target_generation =
        TARGET_GENERATION;
    identity->foreground.title_id_size = sizeof(title);
    memcpy(identity->foreground.title_id,
           title, sizeof(title));
    identity->title_id_size = sizeof(title);
    memcpy(identity->title_id, title, sizeof(title));
    platform->observation.overlay.sequence = 3;
    platform->observation.overlay.generation = 1;
    platform->observation.overlay.state =
        VC_LAUNCH_OVERLAY_CLOSED;
    platform->observation.presentation.sequence = 2;
    platform->observation.presentation.process_id =
        TARGET_PID;
    platform->observation.presentation.process_generation =
        TARGET_GENERATION;
    platform->observation.presentation.module_generation =
        PLUGIN_GENERATION;
    platform->observation.presentation.state =
        VC_LAUNCH_PRESENTATION_READY;
}

static void set_target_source(fake_platform *platform)
{
    static const uint8_t title[] = {
        'P', 'C', 'S', 'A', '0', '0', '1', '3', '3'
    };
    static const uint8_t version[] = {'1', '.', '0', '0'};
    /*
     * Synthetic placeholder facts only. These are not measured hardware
     * values and this test performs no real B2/bolt write or device access.
     */
    static const uint8_t fingerprint[] = {
        0xde, 0xad, 0xbe, 0xef, 0x01, 0x23, 0x45, 0x67
    };
    vc_target_snapshot *source = &platform->target_source;
    size_t index;

    memset(source, 0, sizeof(*source));
    source->identity.process_id = TARGET_PID;
    source->identity.process_generation = TARGET_GENERATION;
    source->identity.foreground_sequence = 1;
    source->identity.title_id_size = sizeof(title);
    memcpy(source->identity.title_id, title, sizeof(title));
    source->identity.version_size = sizeof(version);
    memcpy(source->identity.version, version, sizeof(version));
    source->identity.fingerprint_algorithm =
        UINT32_C(0x54455354);
    source->identity.fingerprint_size = sizeof(fingerprint);
    memcpy(source->identity.fingerprint,
           fingerprint, sizeof(fingerprint));
    source->module_count = 1;
    source->modules[0].module_id = 0;
    source->modules[0].load_generation = MODULE_GENERATION;
    source->modules[0].segment_count = 2;
    source->modules[0].segments[0].segment_index = 0;
    source->modules[0].segments[0].base = UINT32_C(0x1000);
    source->modules[0].segments[0].size = UINT32_C(0x100);
    source->modules[0].segments[0].permissions =
        VC_TARGET_PERMISSION_EXECUTE;
    source->modules[0].segments[1].segment_index = 1;
    source->modules[0].segments[1].base = SEGMENT_BASE;
    source->modules[0].segments[1].size = SEGMENT_SIZE;
    source->modules[0].segments[1].permissions =
        VC_TARGET_PERMISSION_READ;
    source->thread_count = 9;
    source->threads[0].thread_id = 10;
    source->threads[1].thread_id = 20;
    source->threads[2].thread_id = 30;
    source->threads[3].thread_id = 100;
    source->threads[4].thread_id = 101;
    source->threads[5].thread_id = 102;
    source->threads[6].thread_id = 103;
    source->threads[7].thread_id = 104;
    source->threads[8].thread_id = 105;
    for (index = 0; index < source->thread_count; ++index) {
        source->threads[index].process_id = TARGET_PID;
        source->threads[index].process_generation =
            TARGET_GENERATION;
    }
    for (index = 0; index < sizeof(platform->memory); ++index) {
        platform->memory[index] = (uint8_t)(index ^ 0xa5u);
    }
}

static bool fake_claimant_observation(
    void *context,
    vc_launch_claimant_observation *observation)
{
    fake_platform *platform = (fake_platform *)context;

    *observation = platform->observation;
    return true;
}

static bool fake_claimant_time(void *context, uint64_t *now_ms)
{
    fake_platform *platform = (fake_platform *)context;

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

    (void)platform;
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

static bool fake_plugin_unload(
    void *context,
    const vc_launch_claimant_identity_snapshot *identity,
    uint64_t now_ms)
{
    fake_platform *platform = (fake_platform *)context;

    (void)identity;
    (void)now_ms;
    return platform->cleanup_ok;
}

static bool fake_runtime_snapshot(
    void *context,
    vc_menu_runtime_snapshot *snapshot)
{
    fake_platform *platform = (fake_platform *)context;

    snapshot->observation = platform->observation;
    snapshot->allowlist_revision =
        platform->allowlist_revision;
    return true;
}

static bool fake_verify_threads(
    void *context,
    const vc_launch_claimant_identity_snapshot *target,
    const vc_thread_id *thread_ids,
    size_t thread_count)
{
    (void)context;
    return target->caller.process_id == TARGET_PID &&
           thread_ids != NULL &&
           thread_count > 0 && thread_count <= 3;
}

static bool fake_validate_target(
    void *context,
    const vc_launch_claimant_identity_snapshot *target,
    uint64_t target_snapshot_revision,
    const vc_thread_id *thread_ids,
    size_t thread_count,
    const vc_menu_protected_threads *protected_threads)
{
    fake_platform *platform = (fake_platform *)context;

    return target->caller.process_id == TARGET_PID &&
           target_snapshot_revision ==
               platform->attestation->snapshot.revision &&
           thread_ids != NULL && thread_count == 3 &&
           protected_threads != NULL;
}

static int fake_suspend(void *context, vc_thread_id thread_id)
{
    (void)context;
    (void)thread_id;
    return 0;
}

static int fake_resume(void *context, vc_thread_id thread_id)
{
    (void)context;
    (void)thread_id;
    return 0;
}

static bool fake_target_begin(
    void *context,
    vc_target_identity *identity,
    uint32_t *module_count,
    uint32_t *thread_count,
    uint64_t *mutation_token)
{
    fake_platform *platform = (fake_platform *)context;

    *identity = platform->target_source.identity;
    *module_count = platform->target_source.module_count;
    *thread_count = platform->target_source.thread_count;
    *mutation_token = 7;
    return true;
}

static bool fake_target_module(
    void *context,
    uint64_t mutation_token,
    uint32_t module_index,
    vc_target_module *module)
{
    fake_platform *platform = (fake_platform *)context;

    CHECK(mutation_token == 7);
    *module = platform->target_source.modules[module_index];
    return true;
}

static bool fake_target_thread(
    void *context,
    uint64_t mutation_token,
    uint32_t thread_index,
    vc_target_thread *thread)
{
    fake_platform *platform = (fake_platform *)context;

    CHECK(mutation_token == 7);
    *thread = platform->target_source.threads[thread_index];
    return true;
}

static bool fake_target_end(
    void *context,
    uint64_t mutation_token,
    uint64_t *completion_token)
{
    (void)context;
    *completion_token = mutation_token;
    return true;
}

static bool fake_copy_from(
    void *context,
    void *destination,
    const void *source,
    size_t size)
{
    fake_platform *platform = (fake_platform *)context;

    ++platform->copy_from_calls;
    check_memory_callback(platform);
    if (platform->copy_from_ok) {
        memmove(destination, source, size);
    } else if (size != 0) {
        memmove(destination, source, size / 2u);
    }
    return platform->copy_from_ok;
}

static bool fake_copy_to(
    void *context,
    void *destination,
    const void *source,
    size_t size)
{
    fake_platform *platform = (fake_platform *)context;

    ++platform->copy_to_calls;
    check_memory_callback(platform);
    if (platform->copy_to_ok) {
        memmove(destination, source, size);
    } else if (size != 0) {
        memmove(destination, source, size / 2u);
    }
    return platform->copy_to_ok;
}

static vc_launch_attestation_result fake_attest_caller(
    void *context,
    const void *caller_context,
    vc_launch_trusted_caller *caller)
{
    fake_platform *platform = (fake_platform *)context;

    (void)caller_context;
    check_memory_callback(platform);
    if (platform->caller_result ==
        VC_LAUNCH_ATTESTATION_ACCEPTED) {
        *caller = platform->memory_caller;
    }
    return platform->caller_result;
}

static bool fake_foreground(
    void *context,
    vc_launch_foreground_snapshot *foreground)
{
    fake_platform *platform = (fake_platform *)context;

    check_memory_callback(platform);
    if (platform->foreground_ok) {
        *foreground =
            platform->observation.identity.foreground;
    }
    return platform->foreground_ok;
}

static bool fake_read_target(
    void *context,
    const vc_memory_target_read *request,
    uint8_t *destination,
    size_t destination_capacity,
    vc_memory_target_read_observation *observation)
{
    fake_platform *platform = (fake_platform *)context;
    size_t offset;
    size_t bytes;

    check_memory_callback(platform);
    ++platform->read_calls;
    platform->last_read_address = request->address;
    platform->last_read_length = request->length;
    CHECK(request->target_process_id == TARGET_PID);
    CHECK(request->target_generation == TARGET_GENERATION);
    CHECK(request->attestation_revision ==
          platform->attestation->snapshot.revision);
    CHECK(request->length <= destination_capacity);
    offset = request->address - SEGMENT_BASE;
    bytes = platform->read_bytes == SIZE_MAX
                ? request->length
                : platform->read_bytes;
    if (bytes > destination_capacity) {
        bytes = destination_capacity;
    }
    if (offset < sizeof(platform->memory) && bytes != 0) {
        size_t available = sizeof(platform->memory) - offset;
        size_t copied = bytes < available ? bytes : available;

        memcpy(destination, platform->memory + offset, copied);
    }
    observation->bytes_read =
        platform->read_bytes == SIZE_MAX
            ? request->length
            : platform->read_bytes;
    observation->target_generation =
        platform->observed_generation != 0
            ? platform->observed_generation
            : request->target_generation;
    observation->attestation_revision =
        platform->observed_revision != 0
            ? platform->observed_revision
            : request->attestation_revision;
    if (platform->mutate_foreground_during_read) {
        ++platform->observation.identity.sequence;
        ++platform->observation.identity.foreground.sequence;
    }
    return platform->read_ok;
}

static bool fake_cleanup(void *context)
{
    fake_platform *platform = (fake_platform *)context;

    check_memory_callback(platform);
    return platform->cleanup_ok;
}

static void initialize_fixture(fixture *fixture_value)
{
    vc_launch_claimant_dependencies claimant_dependencies;
    vc_menu_coordinator_dependencies coordinator_dependencies;
    vc_target_dependencies target_dependencies;
    vc_memory_service_dependencies memory_dependencies;
    vc_menu_thread_allowlist allowlist;
    vc_menu_protected_threads protected_threads;

    memset(fixture_value, 0, sizeof(*fixture_value));
    fixture_value->platform.claimant =
        &fixture_value->claimant;
    fixture_value->platform.coordinator =
        &fixture_value->coordinator;
    fixture_value->platform.attestation =
        &fixture_value->attestation;
    fixture_value->platform.memory_service =
        &fixture_value->service;
    fixture_value->platform.now_ms = 100;
    fixture_value->platform.allowlist_revision = 7;
    fixture_value->platform.copy_from_ok = true;
    fixture_value->platform.copy_to_ok = true;
    fixture_value->platform.foreground_ok = true;
    fixture_value->platform.read_ok = true;
    fixture_value->platform.cleanup_ok = true;
    fixture_value->platform.read_bytes = SIZE_MAX;
    fixture_value->platform.caller_result =
        VC_LAUNCH_ATTESTATION_ACCEPTED;
    set_observation(&fixture_value->platform);
    set_target_source(&fixture_value->platform);
    fixture_value->platform.memory_caller =
        fixture_value->platform.observation.identity.caller;

    memset(&target_dependencies, 0,
           sizeof(target_dependencies));
    target_dependencies.begin_snapshot = fake_target_begin;
    target_dependencies.read_module = fake_target_module;
    target_dependencies.read_thread = fake_target_thread;
    target_dependencies.end_snapshot = fake_target_end;
    target_dependencies.cleanup = fake_cleanup;
    target_dependencies.context = &fixture_value->platform;
    CHECK(vc_target_attestation_init(
              &fixture_value->attestation,
              &target_dependencies) == VC_TARGET_STATUS_OK);
    CHECK(vc_target_attestation_start(
              &fixture_value->attestation) ==
          VC_TARGET_STATUS_OK);
    CHECK(vc_target_attestation_refresh(
              &fixture_value->attestation) ==
          VC_TARGET_STATUS_OK);

    memset(&claimant_dependencies, 0,
           sizeof(claimant_dependencies));
    claimant_dependencies.get_observation =
        fake_claimant_observation;
    claimant_dependencies.get_time = fake_claimant_time;
    claimant_dependencies.transport = fake_transport;
    claimant_dependencies.plugin_unload =
        fake_plugin_unload;
    claimant_dependencies.context = &fixture_value->platform;
    CHECK(vc_launch_claimant_init(
              &fixture_value->claimant,
              &claimant_dependencies,
              VC_LAUNCH_CLAIMANT_DEFAULT_AUTHORIZATION_TTL_MS) ==
          VC_LAUNCH_CLAIMANT_RESULT_OK);
    CHECK(vc_launch_claimant_start(
              &fixture_value->claimant) ==
          VC_LAUNCH_CLAIMANT_RESULT_OK);
    CHECK(vc_launch_claimant_bind_identity(
              &fixture_value->claimant) ==
          VC_LAUNCH_CLAIMANT_RESULT_OK);
    fixture_value->claimant.overlay_closed_stable = true;

    memset(&coordinator_dependencies, 0,
           sizeof(coordinator_dependencies));
    coordinator_dependencies.get_runtime_snapshot =
        fake_runtime_snapshot;
    coordinator_dependencies.verify_thread_ownership =
        fake_verify_threads;
    coordinator_dependencies.validate_target_attestation =
        fake_validate_target;
    coordinator_dependencies.suspend_thread = fake_suspend;
    coordinator_dependencies.resume_thread = fake_resume;
    coordinator_dependencies.context =
        &fixture_value->platform;
    CHECK(vc_menu_coordinator_init(
              &fixture_value->coordinator,
              &fixture_value->claimant,
              &coordinator_dependencies) ==
          VC_MENU_COORDINATOR_RESULT_OK);
    CHECK(vc_menu_coordinator_start(
              &fixture_value->coordinator) ==
          VC_MENU_COORDINATOR_RESULT_OK);
    CHECK(vc_menu_coordinator_bind_target(
              &fixture_value->coordinator,
              &fixture_value->platform.observation.identity) ==
          VC_MENU_COORDINATOR_RESULT_OK);
    memset(&protected_threads, 0, sizeof(protected_threads));
    protected_threads.plugin_control_worker = 100;
    protected_threads.input_hook = 101;
    protected_threads.renderer_present_hook = 102;
    protected_threads.watchdog_worker = 103;
    protected_threads.cleanup_worker = 104;
    protected_threads.current_thread = 105;
    memset(&allowlist, 0, sizeof(allowlist));
    allowlist.process_id = TARGET_PID;
    allowlist.process_generation = TARGET_GENERATION;
    allowlist.module_generation = PLUGIN_GENERATION;
    allowlist.revision =
        fixture_value->platform.allowlist_revision;
    allowlist.target_snapshot_revision =
        fixture_value->attestation.snapshot.revision;
    allowlist.thread_ids = gameplay_threads;
    allowlist.thread_count =
        sizeof(gameplay_threads) /
        sizeof(gameplay_threads[0]);
    allowlist.protected_threads = protected_threads;
    CHECK(vc_menu_coordinator_configure_allowlist(
              &fixture_value->coordinator,
              &allowlist) ==
          VC_MENU_COORDINATOR_RESULT_OK);

    memset(&memory_dependencies, 0,
           sizeof(memory_dependencies));
    memory_dependencies.copy_from_user = fake_copy_from;
    memory_dependencies.copy_to_user = fake_copy_to;
    memory_dependencies.attest_caller =
        fake_attest_caller;
    memory_dependencies.get_foreground = fake_foreground;
    memory_dependencies.read_target = fake_read_target;
    memory_dependencies.cleanup = fake_cleanup;
    memory_dependencies.context = &fixture_value->platform;
    CHECK(vc_memory_service_init(
              &fixture_value->service,
              &fixture_value->attestation,
              &fixture_value->coordinator,
              &memory_dependencies, 41) ==
          VC_MEMORY_SERVICE_RESULT_OK);
    CHECK(vc_memory_service_start(
              &fixture_value->service,
              fixture_value->platform.now_ms) ==
          VC_MEMORY_SERVICE_RESULT_OK);
}

static void open_menu(fixture *fixture_value)
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
    CHECK(vc_menu_coordinator_begin_open(
              &fixture_value->coordinator,
              fixture_value->platform.now_ms,
              1000, 100, &fixture_value->lease) ==
          VC_MENU_COORDINATOR_RESULT_OK);
    ++fixture_value->platform.now_ms;
    CHECK(vc_menu_coordinator_acknowledge_open(
              &fixture_value->coordinator,
              fixture_value->platform.now_ms,
              &fixture_value->lease) ==
          VC_MENU_COORDINATOR_RESULT_OK);
}

static void activate(
    fixture *fixture_value,
    uint64_t byte_budget,
    uint32_t operation_budget)
{
    vc_memory_read_status status;

    CHECK(vc_memory_service_activate(
              &fixture_value->service,
              fixture_value->platform.now_ms,
              byte_budget, operation_budget,
              &fixture_value->session_id, &status) ==
          VC_MEMORY_SERVICE_RESULT_OK);
    CHECK(status == VC_MEMORY_READ_STATUS_OK);
    CHECK(fixture_value->session_id != 0);
}

static vc_memory_read_request request_for(
    const fixture *fixture_value,
    vc_memory_read_operation operation,
    uint64_t request_id)
{
    vc_memory_read_request request;

    vc_memory_read_request_init(&request, operation);
    request.request_id = request_id;
    request.session_id = fixture_value->session_id;
    request.target_process_id = TARGET_PID;
    request.target_generation = TARGET_GENERATION;
    request.attestation_revision =
        fixture_value->attestation.snapshot.revision;
    if (operation == VC_MEMORY_READ_OPERATION_READ) {
        request.module_id = 0;
        request.module_load_generation = MODULE_GENERATION;
        request.segment_index = 1;
        request.length = 4;
    }
    return request;
}

static vc_memory_service_result dispatch_request(
    fixture *fixture_value,
    const vc_memory_read_request *request,
    uint8_t *request_wire,
    uint8_t *response_wire,
    size_t response_capacity,
    size_t *response_size,
    vc_memory_read_status *status)
{
    size_t encoded_size = 0;

    CHECK(vc_memory_read_request_encode(
              request, request_wire,
              VC_MEMORY_READ_REQUEST_WIRE_SIZE,
              &encoded_size) == VC_MEMORY_READ_STATUS_OK);
    CHECK(encoded_size == VC_MEMORY_READ_REQUEST_WIRE_SIZE);
    return vc_memory_service_dispatch(
        &fixture_value->service, NULL,
        request_wire, encoded_size,
        response_wire, response_capacity,
        fixture_value->platform.now_ms,
        response_size, status);
}

static vc_memory_read_status read_once(
    fixture *fixture_value,
    vc_memory_read_request *request,
    vc_memory_read_response *response)
{
    uint8_t request_wire[VC_MEMORY_READ_REQUEST_WIRE_SIZE];
    uint8_t response_wire[VC_MEMORY_READ_RESPONSE_WIRE_MAX];
    vc_memory_read_status status;
    size_t response_size = 0;

    memset(response_wire, 0xcc, sizeof(response_wire));
    CHECK(dispatch_request(
              fixture_value, request, request_wire,
              response_wire, sizeof(response_wire),
              &response_size, &status) ==
          VC_MEMORY_SERVICE_RESULT_OK);
    CHECK(vc_memory_read_response_decode(
              response_wire, response_size, response) ==
          VC_MEMORY_READ_STATUS_OK);
    CHECK(response->status == status);
    return status;
}

static void test_authorization_and_happy_path(void)
{
    fixture fixture_value;
    vc_memory_read_request request;
    vc_memory_read_response response;
    vc_memory_read_status status;
    vc_menu_open_lineage lineage;
    uint64_t session_id = 99;
    uint64_t deadline_ms;

    initialize_fixture(&fixture_value);
    request = request_for(
        &fixture_value, VC_MEMORY_READ_OPERATION_READ, 1);
    request.session_id = 1;
    CHECK(read_once(
              &fixture_value, &request, &response) ==
          VC_MEMORY_READ_STATUS_NO_AUTHORIZATION);
    CHECK(fixture_value.platform.read_calls == 0);
    CHECK(vc_memory_service_activate(
              &fixture_value.service,
              fixture_value.platform.now_ms,
              100, 10, &session_id, &status) ==
          VC_MEMORY_SERVICE_RESULT_OK);
    CHECK(status == VC_MEMORY_READ_STATUS_NO_AUTHORIZATION);
    CHECK(session_id == 0);

    open_menu(&fixture_value);
    memset(&lineage, 0, sizeof(lineage));
    CHECK(vc_menu_coordinator_inspect_open_lineage(
              &fixture_value.coordinator,
              fixture_value.platform.now_ms,
              &lineage) ==
          VC_MENU_COORDINATOR_RESULT_OK);
    CHECK(lineage.target_snapshot_revision ==
          fixture_value.attestation.snapshot.revision);
    CHECK(vc_menu_coordinator_validate_open_lineage(
              &fixture_value.coordinator,
              fixture_value.platform.now_ms,
              &lineage) ==
          VC_MENU_COORDINATOR_RESULT_OK);
    activate(&fixture_value, 100, 10);
    CHECK(vc_memory_service_activate(
              &fixture_value.service,
              fixture_value.platform.now_ms,
              100, 10, &session_id, &status) ==
          VC_MEMORY_SERVICE_RESULT_BUSY);

    request = request_for(
        &fixture_value, VC_MEMORY_READ_OPERATION_READ, 1);
    request.session_id = fixture_value.session_id + 1u;
    CHECK(read_once(
              &fixture_value, &request, &response) ==
          VC_MEMORY_READ_STATUS_STALE_SESSION);
    CHECK(fixture_value.platform.read_calls == 0);
    CHECK(fixture_value.service.session.remaining_operations == 10);
    request.session_id = fixture_value.session_id;
    request.segment_offset = 7;
    deadline_ms = fixture_value.service.session.deadline_ms;
    CHECK(read_once(
              &fixture_value, &request, &response) ==
          VC_MEMORY_READ_STATUS_OK);
    CHECK(fixture_value.platform.read_calls == 1);
    CHECK(fixture_value.platform.last_read_address ==
          SEGMENT_BASE + 7u);
    CHECK(response.payload_size == 4);
    CHECK(memcmp(response.payload,
                 fixture_value.platform.memory + 7, 4) == 0);
    CHECK(response.remaining_bytes == 96);
    CHECK(response.remaining_operations == 9);

    request = request_for(
        &fixture_value, VC_MEMORY_READ_OPERATION_STATUS, 2);
    CHECK(read_once(
              &fixture_value, &request, &response) ==
          VC_MEMORY_READ_STATUS_OK);
    CHECK(response.payload_size == 0);
    CHECK(response.remaining_bytes == 96);
    CHECK(response.remaining_operations == 8);
    CHECK(fixture_value.service.session.deadline_ms ==
          deadline_ms);

    request = request_for(
        &fixture_value, VC_MEMORY_READ_OPERATION_READ, 1);
    CHECK(read_once(
              &fixture_value, &request, &response) ==
          VC_MEMORY_READ_STATUS_STALE_SESSION);
    CHECK(fixture_value.platform.read_calls == 1);
}

static void test_activation_budget_and_snapshot_bounds(void)
{
    fixture fixture_value;
    vc_memory_read_status status = VC_MEMORY_READ_STATUS_OK;
    uint64_t session_id = 0;

    initialize_fixture(&fixture_value);
    open_menu(&fixture_value);
    CHECK(vc_memory_service_activate(
              &fixture_value.service,
              fixture_value.platform.now_ms,
              VC_MEMORY_SERVICE_MAX_BYTE_BUDGET + 1u,
              1, &session_id, &status) ==
          VC_MEMORY_SERVICE_RESULT_INVALID_ARGUMENT);
    CHECK(vc_memory_service_activate(
              &fixture_value.service,
              fixture_value.platform.now_ms,
              1, VC_MEMORY_SERVICE_MAX_OPERATION_BUDGET + 1u,
              &session_id, &status) ==
          VC_MEMORY_SERVICE_RESULT_INVALID_ARGUMENT);
    CHECK(vc_target_attestation_refresh(
              &fixture_value.attestation) ==
          VC_TARGET_STATUS_OK);
    CHECK(vc_memory_service_activate(
              &fixture_value.service,
              fixture_value.platform.now_ms,
              1, 1, &session_id, &status) ==
          VC_MEMORY_SERVICE_RESULT_OK);
    CHECK(status == VC_MEMORY_READ_STATUS_STALE_SNAPSHOT);
    CHECK(session_id == 0);

    initialize_fixture(&fixture_value);
    open_menu(&fixture_value);
    activate(
        &fixture_value,
        VC_MEMORY_SERVICE_MAX_BYTE_BUDGET,
        VC_MEMORY_SERVICE_MAX_OPERATION_BUDGET);
    CHECK(fixture_value.service.session.remaining_bytes ==
          VC_MEMORY_SERVICE_MAX_BYTE_BUDGET);
    CHECK(fixture_value.service.session.remaining_operations ==
          VC_MEMORY_SERVICE_MAX_OPERATION_BUDGET);
}

static void test_copy_identity_and_alias(void)
{
    fixture fixture_value;
    vc_memory_read_request request;
    vc_memory_read_response response;
    uint8_t wire[VC_MEMORY_READ_RESPONSE_WIRE_MAX];
    vc_memory_read_status status;
    size_t response_size = 0;
    uint64_t before_bytes;
    uint32_t before_operations;

    initialize_fixture(&fixture_value);
    open_menu(&fixture_value);
    activate(&fixture_value, 100, 10);
    request = request_for(
        &fixture_value, VC_MEMORY_READ_OPERATION_READ, 1);
    before_bytes =
        fixture_value.service.session.remaining_bytes;
    before_operations =
        fixture_value.service.session.remaining_operations;

    fixture_value.platform.copy_from_ok = false;
    memset(wire, 0xa5, sizeof(wire));
    CHECK(vc_memory_read_request_encode(
              &request, wire,
              VC_MEMORY_READ_REQUEST_WIRE_SIZE,
              &response_size) == VC_MEMORY_READ_STATUS_OK);
    response_size = 9;
    CHECK(vc_memory_service_dispatch(
              &fixture_value.service, NULL,
              wire, VC_MEMORY_READ_REQUEST_WIRE_SIZE,
              wire, sizeof(wire),
              fixture_value.platform.now_ms,
              &response_size, &status) ==
          VC_MEMORY_SERVICE_RESULT_COPY_FROM_USER_FAILED);
    CHECK(response_size == 0);
    CHECK(fixture_value.service.session.remaining_bytes ==
          before_bytes);
    CHECK(fixture_value.service.session.remaining_operations ==
          before_operations);
    CHECK(fixture_value.platform.read_calls == 0);
    fixture_value.platform.copy_from_ok = true;

    CHECK(dispatch_request(
              &fixture_value, &request, wire, wire,
              VC_MEMORY_READ_RESPONSE_HEADER_WIRE_SIZE + 3u,
              &response_size, &status) ==
          VC_MEMORY_SERVICE_RESULT_OUTPUT_TOO_SMALL);
    CHECK(fixture_value.platform.read_calls == 0);
    CHECK(fixture_value.service.session.remaining_bytes ==
          before_bytes);

    fixture_value.platform.memory_caller.role =
        VC_LAUNCH_CALLER_SCE_SHELL;
    CHECK(dispatch_request(
              &fixture_value, &request, wire, wire,
              sizeof(wire), &response_size, &status) ==
          VC_MEMORY_SERVICE_RESULT_UNAUTHENTICATED_CALLER);
    CHECK(fixture_value.platform.read_calls == 0);
    fixture_value.platform.memory_caller =
        fixture_value.platform.observation.identity.caller;
    ++fixture_value.platform.memory_caller.module_generation;
    CHECK(dispatch_request(
              &fixture_value, &request, wire, wire,
              sizeof(wire), &response_size, &status) ==
          VC_MEMORY_SERVICE_RESULT_OK);
    CHECK(status ==
          VC_MEMORY_READ_STATUS_UNAUTHENTICATED_CALLER);
    CHECK(fixture_value.platform.read_calls == 0);
    fixture_value.platform.memory_caller =
        fixture_value.platform.observation.identity.caller;

    request.target_process_id ^= 1u;
    CHECK(read_once(
              &fixture_value, &request, &response) ==
          VC_MEMORY_READ_STATUS_STALE_TARGET);
    CHECK(fixture_value.platform.read_calls == 0);
    request.target_process_id = TARGET_PID;
    request.request_id = 2;

    CHECK(vc_memory_read_request_encode(
              &request, wire,
              VC_MEMORY_READ_REQUEST_WIRE_SIZE,
              &response_size) == VC_MEMORY_READ_STATUS_OK);
    CHECK(vc_memory_service_dispatch(
              &fixture_value.service, NULL,
              wire, VC_MEMORY_READ_REQUEST_WIRE_SIZE,
              wire, sizeof(wire),
              fixture_value.platform.now_ms,
              &response_size, &status) ==
          VC_MEMORY_SERVICE_RESULT_OK);
    CHECK(vc_memory_read_response_decode(
              wire, response_size, &response) ==
          VC_MEMORY_READ_STATUS_OK);
    CHECK(response.status == VC_MEMORY_READ_STATUS_OK);
}

static void test_dispatch_envelope_and_busy_errors(void)
{
    fixture fixture_value;
    vc_memory_read_request request;
    vc_memory_read_response response;
    uint8_t request_wire[VC_MEMORY_READ_REQUEST_WIRE_SIZE + 1u];
    uint8_t response_wire[VC_MEMORY_READ_RESPONSE_WIRE_MAX];
    vc_memory_read_status status;
    size_t encoded_size = 0;
    size_t response_size = 7;

    initialize_fixture(&fixture_value);
    open_menu(&fixture_value);
    activate(&fixture_value, 100, 10);
    request = request_for(
        &fixture_value, VC_MEMORY_READ_OPERATION_STATUS, 1);
    CHECK(vc_memory_read_request_encode(
              &request, request_wire,
              VC_MEMORY_READ_REQUEST_WIRE_SIZE,
              &encoded_size) == VC_MEMORY_READ_STATUS_OK);

    CHECK(vc_memory_service_dispatch(
              &fixture_value.service, NULL,
              request_wire, 0, response_wire,
              sizeof(response_wire),
              fixture_value.platform.now_ms,
              &response_size, &status) ==
          VC_MEMORY_SERVICE_RESULT_INVALID_INPUT_SIZE);
    CHECK(response_size == 0);
    CHECK(vc_memory_service_dispatch(
              &fixture_value.service, NULL,
              request_wire, sizeof(request_wire),
              response_wire, sizeof(response_wire),
              fixture_value.platform.now_ms,
              &response_size, &status) ==
          VC_MEMORY_SERVICE_RESULT_INVALID_INPUT_SIZE);
    CHECK(vc_memory_service_dispatch(
              &fixture_value.service, NULL,
              NULL, VC_MEMORY_READ_REQUEST_WIRE_SIZE,
              response_wire, sizeof(response_wire),
              fixture_value.platform.now_ms,
              &response_size, &status) ==
          VC_MEMORY_SERVICE_RESULT_INVALID_ARGUMENT);
    CHECK(vc_memory_service_dispatch(
              &fixture_value.service, NULL,
              request_wire, VC_MEMORY_READ_REQUEST_WIRE_SIZE,
              NULL, sizeof(response_wire),
              fixture_value.platform.now_ms,
              &response_size, &status) ==
          VC_MEMORY_SERVICE_RESULT_INVALID_ARGUMENT);

    request_wire[0] = 2;
    CHECK(vc_memory_service_dispatch(
              &fixture_value.service, NULL,
              request_wire, VC_MEMORY_READ_REQUEST_WIRE_SIZE,
              response_wire, sizeof(response_wire),
              fixture_value.platform.now_ms,
              &response_size, &status) ==
          VC_MEMORY_SERVICE_RESULT_OK);
    CHECK(status ==
          VC_MEMORY_READ_STATUS_UNSUPPORTED_VERSION);
    CHECK(vc_memory_read_response_decode(
              response_wire, response_size, &response) ==
          VC_MEMORY_READ_STATUS_OK);
    CHECK(response.operation ==
          VC_MEMORY_READ_OPERATION_NONE);
    CHECK(response.session_id == 0);
    CHECK(response.payload_size == 0);
    CHECK(fixture_value.platform.read_calls == 0);

    atomic_store_explicit(
        &fixture_value.service.transaction_busy, 1u,
        memory_order_release);
    CHECK(vc_memory_service_tick(
              &fixture_value.service,
              fixture_value.platform.now_ms) ==
          VC_MEMORY_SERVICE_RESULT_BUSY);
    atomic_store_explicit(
        &fixture_value.service.transaction_busy, 0u,
        memory_order_release);
    atomic_store_explicit(
        &fixture_value.service.adapter_active, 1u,
        memory_order_release);
    CHECK(vc_memory_service_target_changed(
              &fixture_value.service) ==
          VC_MEMORY_SERVICE_RESULT_BUSY);
    atomic_store_explicit(
        &fixture_value.service.adapter_active, 0u,
        memory_order_release);
}

static void test_symbolic_ranges(void)
{
    fixture fixture_value;
    vc_memory_read_request request;
    vc_memory_read_response response;

    initialize_fixture(&fixture_value);
    open_menu(&fixture_value);
    activate(&fixture_value, 2048, 20);

    request = request_for(
        &fixture_value, VC_MEMORY_READ_OPERATION_READ, 1);
    request.segment_offset = 0;
    request.length = 1;
    CHECK(read_once(
              &fixture_value, &request, &response) ==
          VC_MEMORY_READ_STATUS_OK);
    CHECK(response.payload[0] ==
          fixture_value.platform.memory[0]);

    request.request_id = 2;
    request.segment_offset = SEGMENT_SIZE - 1u;
    CHECK(read_once(
              &fixture_value, &request, &response) ==
          VC_MEMORY_READ_STATUS_OK);
    CHECK(response.payload[0] ==
          fixture_value.platform.memory[SEGMENT_SIZE - 1u]);

    request.request_id = 3;
    request.length = 2;
    CHECK(read_once(
              &fixture_value, &request, &response) ==
          VC_MEMORY_READ_STATUS_RANGE_DENIED);
    CHECK(response.remaining_operations == 17);
    CHECK(read_once(
              &fixture_value, &request, &response) ==
          VC_MEMORY_READ_STATUS_STALE_SESSION);
    CHECK(response.remaining_operations == 17);
    request.request_id = 4;
    request.segment_index = 2;
    request.segment_offset = 0;
    request.length = 1;
    CHECK(read_once(
              &fixture_value, &request, &response) ==
          VC_MEMORY_READ_STATUS_RANGE_DENIED);
    request.request_id = 5;
    request.segment_index = 0;
    CHECK(read_once(
              &fixture_value, &request, &response) ==
          VC_MEMORY_READ_STATUS_PERMISSION_DENIED);
    request.request_id = 6;
    request.segment_index = 1;
    ++request.module_load_generation;
    CHECK(read_once(
              &fixture_value, &request, &response) ==
          VC_MEMORY_READ_STATUS_STALE_MODULE);
    request.request_id = 7;
    request.module_load_generation = MODULE_GENERATION;
    request.module_id = 1;
    CHECK(read_once(
              &fixture_value, &request, &response) ==
          VC_MEMORY_READ_STATUS_STALE_MODULE);
    request.request_id = 8;
    request.module_id = 0;
    ++request.attestation_revision;
    CHECK(read_once(
              &fixture_value, &request, &response) ==
          VC_MEMORY_READ_STATUS_STALE_SNAPSHOT);
    CHECK(fixture_value.platform.read_calls == 2);

    request.attestation_revision =
        fixture_value.attestation.snapshot.revision;
    request.request_id = 9;
    request.segment_offset = 0;
    request.length = VC_MEMORY_READ_MAX_PAYLOAD;
    CHECK(read_once(
              &fixture_value, &request, &response) ==
          VC_MEMORY_READ_STATUS_OK);
    CHECK(response.payload_size ==
          VC_MEMORY_READ_MAX_PAYLOAD);
    CHECK(response.remaining_operations == 12);
}

static void test_read_failures_and_reentry(void)
{
    fixture fixture_value;
    vc_memory_read_request request;
    vc_memory_read_response response;

    initialize_fixture(&fixture_value);
    open_menu(&fixture_value);
    activate(&fixture_value, 100, 10);
    request = request_for(
        &fixture_value, VC_MEMORY_READ_OPERATION_READ, 1);
    fixture_value.platform.reenter_memory = true;
    fixture_value.platform.read_ok = false;
    fixture_value.platform.read_bytes = 2;
    CHECK(read_once(
              &fixture_value, &request, &response) ==
          VC_MEMORY_READ_STATUS_READ_FAULT);
    CHECK(response.payload_size == 0);
    CHECK(fixture_value.platform.busy_reentries >= 5);
    CHECK(!fixture_value.service.journal.valid);

    request.request_id = 2;
    fixture_value.platform.read_ok = true;
    fixture_value.platform.read_bytes = 0;
    CHECK(read_once(
              &fixture_value, &request, &response) ==
          VC_MEMORY_READ_STATUS_SHORT_READ);
    CHECK(response.payload_size == 0);
    request.request_id = 3;
    fixture_value.platform.read_bytes = 3;
    CHECK(read_once(
              &fixture_value, &request, &response) ==
          VC_MEMORY_READ_STATUS_SHORT_READ);
    request.request_id = 4;
    fixture_value.platform.read_bytes = 5;
    CHECK(read_once(
              &fixture_value, &request, &response) ==
          VC_MEMORY_READ_STATUS_READ_FAULT);

    request.request_id = 5;
    fixture_value.platform.read_bytes = SIZE_MAX;
    fixture_value.platform.observed_revision =
        fixture_value.attestation.snapshot.revision + 1u;
    CHECK(read_once(
              &fixture_value, &request, &response) ==
          VC_MEMORY_READ_STATUS_MUTATED_TARGET);
    CHECK(!fixture_value.service.session.active);
    CHECK(!fixture_value.service.journal.valid);
    CHECK(response.payload_size == 0);
}

static void test_journal_and_budgets(void)
{
    fixture fixture_value;
    vc_memory_read_request request;
    vc_memory_read_request different;
    vc_memory_read_response response;
    uint8_t request_wire[VC_MEMORY_READ_REQUEST_WIRE_SIZE];
    uint8_t first_response[VC_MEMORY_READ_RESPONSE_WIRE_MAX];
    uint8_t retry_response[VC_MEMORY_READ_RESPONSE_WIRE_MAX];
    vc_memory_read_status status;
    size_t first_size = 0;
    size_t retry_size = 0;

    initialize_fixture(&fixture_value);
    open_menu(&fixture_value);
    activate(&fixture_value, 4, 2);
    request = request_for(
        &fixture_value, VC_MEMORY_READ_OPERATION_READ, 1);
    fixture_value.platform.copy_to_ok = false;
    CHECK(dispatch_request(
              &fixture_value, &request, request_wire,
              first_response, sizeof(first_response),
              &first_size, &status) ==
          VC_MEMORY_SERVICE_RESULT_COPY_TO_USER_FAILED);
    CHECK(first_size == 0);
    CHECK(fixture_value.platform.read_calls == 1);
    CHECK(fixture_value.service.journal.valid);
    CHECK(fixture_value.service.session.remaining_bytes == 0);
    CHECK(fixture_value.service.session.remaining_operations == 1);

    different = request;
    different.request_id = 2;
    CHECK(dispatch_request(
              &fixture_value, &different, request_wire,
              retry_response, sizeof(retry_response),
              &retry_size, &status) ==
          VC_MEMORY_SERVICE_RESULT_RESULT_PENDING);
    CHECK(fixture_value.platform.read_calls == 1);

    fixture_value.platform.copy_to_ok = true;
    CHECK(dispatch_request(
              &fixture_value, &request, request_wire,
              retry_response, sizeof(retry_response),
              &retry_size, &status) ==
          VC_MEMORY_SERVICE_RESULT_OK);
    CHECK(fixture_value.platform.read_calls == 1);
    CHECK(!fixture_value.service.journal.valid);
    CHECK(retry_size ==
          VC_MEMORY_READ_RESPONSE_HEADER_WIRE_SIZE + 4u);
    CHECK(memcmp(first_response, retry_response,
                 retry_size / 2u) == 0);

    request = request_for(
        &fixture_value, VC_MEMORY_READ_OPERATION_STATUS, 2);
    CHECK(read_once(
              &fixture_value, &request, &response) ==
          VC_MEMORY_READ_STATUS_OK);
    CHECK(response.remaining_operations == 0);
    request.request_id = 3;
    CHECK(read_once(
              &fixture_value, &request, &response) ==
          VC_MEMORY_READ_STATUS_QUOTA_EXHAUSTED);
    CHECK(fixture_value.platform.read_calls == 1);

    CHECK(vc_memory_service_menu_closed(
              &fixture_value.service) ==
          VC_MEMORY_SERVICE_RESULT_OK);
    CHECK(!fixture_value.service.session.active);
    CHECK(!fixture_value.service.journal.valid);
}

static void test_status_journal_and_lifecycle_invalidation(void)
{
    fixture fixture_value;
    vc_memory_read_request request;
    uint8_t request_wire[VC_MEMORY_READ_REQUEST_WIRE_SIZE];
    uint8_t first_response[VC_MEMORY_READ_RESPONSE_WIRE_MAX];
    uint8_t retry_response[VC_MEMORY_READ_RESPONSE_WIRE_MAX];
    vc_memory_read_status status;
    size_t first_size = 0;
    size_t retry_size = 0;

    initialize_fixture(&fixture_value);
    open_menu(&fixture_value);
    activate(&fixture_value, 100, 10);
    request = request_for(
        &fixture_value, VC_MEMORY_READ_OPERATION_STATUS, 1);
    fixture_value.platform.copy_to_ok = false;
    CHECK(dispatch_request(
              &fixture_value, &request, request_wire,
              first_response, sizeof(first_response),
              &first_size, &status) ==
          VC_MEMORY_SERVICE_RESULT_COPY_TO_USER_FAILED);
    CHECK(fixture_value.service.journal.valid);
    CHECK(fixture_value.platform.read_calls == 0);
    fixture_value.platform.copy_to_ok = true;
    CHECK(dispatch_request(
              &fixture_value, &request, request_wire,
              retry_response, sizeof(retry_response),
              &retry_size, &status) ==
          VC_MEMORY_SERVICE_RESULT_OK);
    CHECK(status == VC_MEMORY_READ_STATUS_OK);
    CHECK(fixture_value.platform.read_calls == 0);

    request = request_for(
        &fixture_value, VC_MEMORY_READ_OPERATION_READ, 2);
    fixture_value.platform.copy_to_ok = false;
    CHECK(dispatch_request(
              &fixture_value, &request, request_wire,
              first_response, sizeof(first_response),
              &first_size, &status) ==
          VC_MEMORY_SERVICE_RESULT_COPY_TO_USER_FAILED);
    CHECK(fixture_value.service.journal.valid);
    CHECK(vc_memory_service_presentation_changed(
              &fixture_value.service,
              VC_LAUNCH_PRESENTATION_LOST) ==
          VC_MEMORY_SERVICE_RESULT_OK);
    CHECK(!fixture_value.service.journal.valid);
    CHECK(!fixture_value.service.session.active);
}

static void test_deterministic_budget_model(void)
{
    fixture fixture_value;
    vc_memory_read_request request;
    vc_memory_read_response response;
    uint64_t expected_bytes = 16;
    uint32_t expected_operations = 4;
    uint64_t request_id;

    initialize_fixture(&fixture_value);
    open_menu(&fixture_value);
    activate(&fixture_value, expected_bytes,
             expected_operations);
    for (request_id = 1; request_id <= 4; ++request_id) {
        vc_memory_read_operation operation =
            (request_id & 1u) != 0
                ? VC_MEMORY_READ_OPERATION_READ
                : VC_MEMORY_READ_OPERATION_STATUS;

        request = request_for(
            &fixture_value, operation, request_id);
        request.length =
            operation == VC_MEMORY_READ_OPERATION_READ
                ? 4u
                : 0u;
        CHECK(read_once(
                  &fixture_value, &request, &response) ==
              VC_MEMORY_READ_STATUS_OK);
        --expected_operations;
        if (operation == VC_MEMORY_READ_OPERATION_READ) {
            expected_bytes -= 4u;
        }
        CHECK(response.remaining_operations ==
              expected_operations);
        CHECK(response.remaining_bytes == expected_bytes);
    }
    request = request_for(
        &fixture_value, VC_MEMORY_READ_OPERATION_STATUS, 5);
    CHECK(read_once(
              &fixture_value, &request, &response) ==
          VC_MEMORY_READ_STATUS_QUOTA_EXHAUSTED);
    CHECK(response.remaining_operations == 0);
    CHECK(response.remaining_bytes == expected_bytes);
}

static void test_lifecycle_revocation(void)
{
    fixture fixture_value;
    vc_memory_read_status status;
    uint64_t session_id;

    initialize_fixture(&fixture_value);
    open_menu(&fixture_value);
    activate(&fixture_value, 100, 10);
    CHECK(vc_memory_service_overlay_changed(
              &fixture_value.service,
              VC_LAUNCH_OVERLAY_CLOSED) ==
          VC_MEMORY_SERVICE_RESULT_OK);
    CHECK(fixture_value.service.session.active);
    CHECK(vc_memory_service_overlay_changed(
              &fixture_value.service,
              VC_LAUNCH_OVERLAY_OPEN) ==
          VC_MEMORY_SERVICE_RESULT_OK);
    CHECK(!fixture_value.service.session.active);

    initialize_fixture(&fixture_value);
    open_menu(&fixture_value);
    activate(&fixture_value, 100, 10);
    CHECK(vc_memory_service_presentation_changed(
              &fixture_value.service,
              VC_LAUNCH_PRESENTATION_READY) ==
          VC_MEMORY_SERVICE_RESULT_OK);
    CHECK(fixture_value.service.session.active);
    CHECK(vc_memory_service_presentation_changed(
              &fixture_value.service,
              VC_LAUNCH_PRESENTATION_LOST) ==
          VC_MEMORY_SERVICE_RESULT_OK);
    CHECK(!fixture_value.service.session.active);

    initialize_fixture(&fixture_value);
    open_menu(&fixture_value);
    activate(&fixture_value, 100, 10);
    CHECK(vc_memory_service_process_exit(
              &fixture_value.service,
              TARGET_PID + 1u, TARGET_GENERATION) ==
          VC_MEMORY_SERVICE_RESULT_STALE_TRUSTED_STATE);
    CHECK(fixture_value.service.session.active);
    CHECK(vc_memory_service_process_exit(
              &fixture_value.service,
              TARGET_PID, TARGET_GENERATION) ==
          VC_MEMORY_SERVICE_RESULT_OK);
    CHECK(!fixture_value.service.session.active);

    initialize_fixture(&fixture_value);
    open_menu(&fixture_value);
    activate(&fixture_value, 100, 10);
    CHECK(vc_memory_service_plugin_unload(
              &fixture_value.service, TARGET_PID,
              TARGET_GENERATION, PLUGIN_GENERATION + 1u) ==
          VC_MEMORY_SERVICE_RESULT_STALE_TRUSTED_STATE);
    CHECK(vc_memory_service_plugin_unload(
              &fixture_value.service, TARGET_PID,
              TARGET_GENERATION, PLUGIN_GENERATION) ==
          VC_MEMORY_SERVICE_RESULT_OK);
    CHECK(!fixture_value.service.session.active);

    initialize_fixture(&fixture_value);
    open_menu(&fixture_value);
    activate(&fixture_value, 100, 10);
    CHECK(vc_memory_service_target_changed(
              &fixture_value.service) ==
          VC_MEMORY_SERVICE_RESULT_OK);
    CHECK(!fixture_value.service.session.active);

    initialize_fixture(&fixture_value);
    open_menu(&fixture_value);
    activate(&fixture_value, 100, 10);
    CHECK(vc_memory_service_reset(
              &fixture_value.service,
              fixture_value.platform.now_ms) ==
          VC_MEMORY_SERVICE_RESULT_OK);
    CHECK(!fixture_value.service.session.active);
    CHECK(vc_memory_service_activate(
              &fixture_value.service,
              fixture_value.platform.now_ms,
              100, 10, &session_id, &status) ==
          VC_MEMORY_SERVICE_RESULT_OK);
    CHECK(status == VC_MEMORY_READ_STATUS_OK);
    CHECK(session_id != fixture_value.session_id);
    CHECK(vc_memory_service_stop(
              &fixture_value.service,
              fixture_value.platform.now_ms) ==
          VC_MEMORY_SERVICE_RESULT_OK);
    CHECK(!fixture_value.service.session.active);
    CHECK(fixture_value.service.phase ==
          VC_MEMORY_SERVICE_PHASE_STOPPED);

    initialize_fixture(&fixture_value);
    open_menu(&fixture_value);
    activate(&fixture_value, 100, 10);
    CHECK(vc_memory_service_tick(
              &fixture_value.service, 1101) ==
          VC_MEMORY_SERVICE_RESULT_OK);
    CHECK(!fixture_value.service.session.active);

    initialize_fixture(&fixture_value);
    open_menu(&fixture_value);
    activate(&fixture_value, 100, 10);
    CHECK(vc_memory_service_tick(
              &fixture_value.service, 100) ==
          VC_MEMORY_SERVICE_RESULT_OK);
    CHECK(!fixture_value.service.session.active);
}

static void test_trusted_state_mutation_and_formatter(void)
{
    fixture fixture_value;
    vc_memory_read_request request;
    vc_memory_read_response response;
    char text[128];

    initialize_fixture(&fixture_value);
    open_menu(&fixture_value);
    activate(&fixture_value, 100, 10);
    request = request_for(
        &fixture_value, VC_MEMORY_READ_OPERATION_READ, 1);
    fixture_value.platform.mutate_foreground_during_read = true;
    CHECK(read_once(
              &fixture_value, &request, &response) ==
          VC_MEMORY_READ_STATUS_MUTATED_TARGET);
    CHECK(fixture_value.platform.read_calls == 1);
    CHECK(!fixture_value.service.session.active);
    CHECK(!fixture_value.service.journal.valid);

    memset(text, 0, sizeof(text));
    CHECK(vc_memory_service_format_status(
              &fixture_value.service,
              text, sizeof(text)) == strlen(text));
    CHECK(strstr(text, "12345678") == NULL);
    CHECK(strstr(text, "PCSA") == NULL);
    CHECK(strstr(text, "41") == NULL);
}

static void test_actual_coordinator_close_revokes_on_tick(void)
{
    fixture fixture_value;

    initialize_fixture(&fixture_value);
    open_menu(&fixture_value);
    activate(&fixture_value, 100, 10);
    ++fixture_value.platform.now_ms;
    CHECK(vc_menu_coordinator_close(
              &fixture_value.coordinator,
              fixture_value.platform.now_ms,
              &fixture_value.lease) ==
          VC_MENU_COORDINATOR_RESULT_OK);
    CHECK(vc_memory_service_tick(
              &fixture_value.service,
              fixture_value.platform.now_ms) ==
          VC_MEMORY_SERVICE_RESULT_OK);
    CHECK(!fixture_value.service.session.active);
}

int main(void)
{
    test_authorization_and_happy_path();
    test_activation_budget_and_snapshot_bounds();
    test_copy_identity_and_alias();
    test_dispatch_envelope_and_busy_errors();
    test_symbolic_ranges();
    test_read_failures_and_reentry();
    test_journal_and_budgets();
    test_status_journal_and_lifecycle_invalidation();
    test_deterministic_budget_model();
    test_lifecycle_revocation();
    test_trusted_state_mutation_and_formatter();
    test_actual_coordinator_close_revokes_on_tick();

    if (failures != 0) {
        fprintf(stderr, "%d memory service test(s) failed\n", failures);
        return 1;
    }
    puts("memory service tests passed");
    return 0;
}
