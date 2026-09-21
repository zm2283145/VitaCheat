#include "vitacheat/launch_service.h"

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
#define SHELL_PID UINT32_C(0x10005)
#define SHELL_GENERATION UINT64_C(0x50001)

typedef struct fake_platform {
    vc_launch_service *service;
    vc_launch_trusted_caller caller;
    vc_launch_foreground_snapshot foreground;
    vc_launch_attestation_result attestation;
    bool foreground_ok;
    bool copy_from_ok;
    bool copy_to_ok;
    bool cleanup_ok;
    bool reenter_callbacks;
    bool inside_reentry;
    size_t copy_from_limit;
    size_t copy_to_limit;
    size_t last_copy_from_size;
    size_t last_copy_to_size;
    unsigned int copy_from_calls;
    unsigned int copy_to_calls;
    unsigned int attest_calls;
    unsigned int foreground_calls;
    unsigned int cleanup_calls;
    unsigned int busy_reentries;
} fake_platform;

static void maybe_reenter(fake_platform *platform)
{
    vc_launch_status lifecycle_status;

    if (!platform->reenter_callbacks || platform->inside_reentry ||
        platform->service == NULL) {
        return;
    }
    platform->inside_reentry = true;
    CHECK(vc_launch_service_tick(platform->service, 1,
                                 &lifecycle_status) ==
          VC_LAUNCH_SERVICE_STATUS_BUSY);
    CHECK(vc_launch_service_init(
              platform->service,
              &platform->service->dependencies,
              platform->service->initial_request_id) ==
          VC_LAUNCH_SERVICE_STATUS_BUSY);
    ++platform->busy_reentries;
    platform->inside_reentry = false;
}

static bool fake_copy_from_user(void *context,
                                void *destination,
                                const void *user_source,
                                size_t size)
{
    fake_platform *platform = (fake_platform *)context;
    size_t copy_size = size;

    ++platform->copy_from_calls;
    platform->last_copy_from_size = size;
    if (copy_size > platform->copy_from_limit) {
        copy_size = platform->copy_from_limit;
    }
    if (copy_size != 0) {
        memmove(destination, user_source, copy_size);
    }
    maybe_reenter(platform);
    return platform->copy_from_ok && copy_size == size;
}

static bool fake_copy_to_user(void *context,
                              void *user_destination,
                              const void *source,
                              size_t size)
{
    fake_platform *platform = (fake_platform *)context;
    size_t copy_size = size;

    ++platform->copy_to_calls;
    platform->last_copy_to_size = size;
    if (copy_size > platform->copy_to_limit) {
        copy_size = platform->copy_to_limit;
    }
    if (copy_size != 0) {
        memmove(user_destination, source, copy_size);
    }
    maybe_reenter(platform);
    return platform->copy_to_ok && copy_size == size;
}

static vc_launch_attestation_result fake_attest_caller(
    void *context,
    const void *caller_context,
    vc_launch_trusted_caller *caller)
{
    fake_platform *platform = (fake_platform *)context;

    (void)caller_context;
    ++platform->attest_calls;
    maybe_reenter(platform);
    if (platform->attestation == VC_LAUNCH_ATTESTATION_ACCEPTED) {
        *caller = platform->caller;
    }
    return platform->attestation;
}

static bool fake_get_foreground(
    void *context,
    vc_launch_foreground_snapshot *foreground)
{
    fake_platform *platform = (fake_platform *)context;

    ++platform->foreground_calls;
    maybe_reenter(platform);
    if (platform->foreground_ok) {
        *foreground = platform->foreground;
    }
    return platform->foreground_ok;
}

static bool fake_cleanup(void *context)
{
    fake_platform *platform = (fake_platform *)context;

    ++platform->cleanup_calls;
    maybe_reenter(platform);
    return platform->cleanup_ok;
}

static void set_foreground(fake_platform *platform,
                           uint64_t sequence,
                           uint32_t process_id,
                           uint64_t generation,
                           const char *title_id)
{
    size_t title_size = title_id != NULL ? strlen(title_id) : 0;

    memset(&platform->foreground, 0, sizeof(platform->foreground));
    platform->foreground.sequence = sequence;
    if (title_id == NULL) {
        return;
    }
    CHECK(title_size <= VC_LAUNCH_SERVICE_TITLE_ID_MAX);
    platform->foreground.present = 1;
    platform->foreground.target_process_id = process_id;
    platform->foreground.target_generation = generation;
    platform->foreground.title_id_size = (uint32_t)title_size;
    memcpy(platform->foreground.title_id, title_id, title_size);
}

static void set_shell_caller(fake_platform *platform)
{
    memset(&platform->caller, 0, sizeof(platform->caller));
    platform->caller.role = VC_LAUNCH_CALLER_SCE_SHELL;
    platform->caller.process_id = SHELL_PID;
    platform->caller.process_generation = SHELL_GENERATION;
    platform->caller.module_generation = 1;
}

static void set_game_caller(fake_platform *platform,
                            uint32_t process_id,
                            uint64_t generation)
{
    memset(&platform->caller, 0, sizeof(platform->caller));
    platform->caller.role = VC_LAUNCH_CALLER_GAME_PLUGIN;
    platform->caller.process_id = process_id;
    platform->caller.process_generation = generation;
    platform->caller.module_generation = 2;
}

static vc_launch_service_dependencies dependencies_for(
    fake_platform *platform)
{
    vc_launch_service_dependencies dependencies;

    memset(&dependencies, 0, sizeof(dependencies));
    dependencies.copy_from_user = fake_copy_from_user;
    dependencies.copy_to_user = fake_copy_to_user;
    dependencies.attest_caller = fake_attest_caller;
    dependencies.get_foreground = fake_get_foreground;
    dependencies.cleanup = fake_cleanup;
    dependencies.context = platform;
    return dependencies;
}

static void prepare_service(vc_launch_service *service,
                            fake_platform *platform,
                            uint64_t first_request_id)
{
    vc_launch_service_dependencies dependencies;
    vc_launch_status lifecycle_status;

    memset(service, 0, sizeof(*service));
    memset(platform, 0, sizeof(*platform));
    platform->service = service;
    platform->attestation = VC_LAUNCH_ATTESTATION_ACCEPTED;
    platform->foreground_ok = true;
    platform->copy_from_ok = true;
    platform->copy_to_ok = true;
    platform->cleanup_ok = true;
    platform->copy_from_limit = SIZE_MAX;
    platform->copy_to_limit = SIZE_MAX;
    set_shell_caller(platform);
    set_foreground(platform, 1, TARGET_PID, TARGET_GENERATION,
                   "PCSA00001");
    dependencies = dependencies_for(platform);
    CHECK(vc_launch_service_init(service, &dependencies,
                                 first_request_id) ==
          VC_LAUNCH_SERVICE_STATUS_OK);
    CHECK(vc_launch_service_start(service, 0, &lifecycle_status) ==
          VC_LAUNCH_SERVICE_STATUS_OK);
    CHECK(lifecycle_status == VC_LAUNCH_STATUS_OK);
}

static vc_launch_request request_for(vc_launch_operation operation,
                                     vc_launch_caller_role role,
                                     uint32_t process_id,
                                     uint64_t generation,
                                     uint64_t request_id,
                                     uint64_t now_ms)
{
    vc_launch_request request;

    vc_launch_request_init(&request, operation, role);
    request.target_process_id = process_id;
    request.target_generation = generation;
    request.request_id = request_id;
    request.now_ms = now_ms;
    switch (operation) {
    case VC_LAUNCH_OPERATION_SUBMIT:
        request.capabilities = VC_LAUNCH_CAPABILITY_LAUNCH;
        request.request_id = 0;
        request.ttl_ms = VC_LAUNCH_DEFAULT_TTL_MS;
        break;
    case VC_LAUNCH_OPERATION_CLAIM:
        request.capabilities = VC_LAUNCH_CAPABILITY_CLAIM;
        request.presentation_ready = 1;
        break;
    case VC_LAUNCH_OPERATION_CANCEL:
        request.capabilities = VC_LAUNCH_CAPABILITY_CANCEL;
        break;
    case VC_LAUNCH_OPERATION_STATUS:
        request.capabilities = VC_LAUNCH_CAPABILITY_STATUS;
        break;
    default:
        break;
    }
    return request;
}

static void encode_request(const vc_launch_request *request,
                           uint8_t *wire)
{
    size_t encoded_size = 0;

    CHECK(vc_launch_request_encode(request, wire,
                                   VC_LAUNCH_REQUEST_WIRE_SIZE,
                                   &encoded_size) ==
          VC_LAUNCH_STATUS_OK);
    CHECK(encoded_size == VC_LAUNCH_REQUEST_WIRE_SIZE);
}

static vc_launch_service_status dispatch_wire(
    vc_launch_service *service,
    const uint8_t *request_wire,
    size_t request_size,
    uint8_t *response_wire,
    size_t response_capacity,
    uint64_t now_ms,
    size_t *response_size,
    vc_launch_status *operation_status)
{
    return vc_launch_service_dispatch(
        service, NULL, request_wire, request_size,
        response_wire, response_capacity, now_ms,
        response_size, operation_status);
}

static vc_launch_service_status dispatch_request(
    vc_launch_service *service,
    const vc_launch_request *request,
    uint8_t *request_wire,
    uint8_t *response_wire,
    uint64_t trusted_now_ms,
    size_t *response_size,
    vc_launch_status *operation_status)
{
    encode_request(request, request_wire);
    return dispatch_wire(service, request_wire,
                         VC_LAUNCH_REQUEST_WIRE_SIZE,
                         response_wire,
                         VC_LAUNCH_RESPONSE_WIRE_SIZE,
                         trusted_now_ms, response_size,
                         operation_status);
}

static uint64_t submit_success(vc_launch_service *service,
                               fake_platform *platform,
                               uint64_t now_ms,
                               uint64_t ttl_ms)
{
    uint8_t request_wire[VC_LAUNCH_REQUEST_WIRE_SIZE];
    uint8_t response_wire[VC_LAUNCH_RESPONSE_WIRE_SIZE];
    vc_launch_request request = request_for(
        VC_LAUNCH_OPERATION_SUBMIT, VC_LAUNCH_CALLER_SCE_SHELL,
        platform->foreground.target_process_id,
        platform->foreground.target_generation, 0, now_ms);
    vc_launch_response response;
    vc_launch_status operation_status;
    size_t response_size;

    request.ttl_ms = ttl_ms;
    set_shell_caller(platform);
    CHECK(dispatch_request(service, &request, request_wire,
                           response_wire, now_ms, &response_size,
                           &operation_status) ==
          VC_LAUNCH_SERVICE_STATUS_OK);
    CHECK(operation_status == VC_LAUNCH_STATUS_PENDING);
    CHECK(response_size == VC_LAUNCH_RESPONSE_WIRE_SIZE);
    CHECK(vc_launch_response_decode(response_wire, response_size,
                                    &response) == VC_LAUNCH_STATUS_OK);
    return response.request_id;
}

static void test_init_start_stop_and_cleanup(void)
{
    vc_launch_service service = VC_LAUNCH_SERVICE_INITIALIZER;
    fake_platform platform;
    vc_launch_service_dependencies dependencies;
    vc_launch_status lifecycle_status;
    vc_launch_status operation_status;
    uint8_t request_wire[VC_LAUNCH_REQUEST_WIRE_SIZE] = {0};
    uint8_t response_wire[VC_LAUNCH_RESPONSE_WIRE_SIZE] = {0};
    size_t response_size = 9;

    memset(&platform, 0, sizeof(platform));
    CHECK(dispatch_wire(&service, request_wire, sizeof(request_wire),
                        response_wire, sizeof(response_wire), 0,
                        &response_size, &operation_status) ==
          VC_LAUNCH_SERVICE_STATUS_NOT_INITIALIZED);
    CHECK(response_size == 0);

    platform.service = &service;
    platform.attestation = VC_LAUNCH_ATTESTATION_ACCEPTED;
    platform.foreground_ok = true;
    platform.copy_from_ok = true;
    platform.copy_to_ok = true;
    platform.cleanup_ok = false;
    platform.copy_from_limit = SIZE_MAX;
    platform.copy_to_limit = SIZE_MAX;
    set_shell_caller(&platform);
    set_foreground(&platform, 1, TARGET_PID, TARGET_GENERATION,
                   "PCSA00001");
    dependencies = dependencies_for(&platform);
    CHECK(vc_launch_service_init(&service, &dependencies, 7) ==
          VC_LAUNCH_SERVICE_STATUS_OK);
    CHECK(vc_launch_service_init(&service, &dependencies, 7) ==
          VC_LAUNCH_SERVICE_STATUS_OK);
    CHECK(vc_launch_service_init(&service, &dependencies, 8) ==
          VC_LAUNCH_SERVICE_STATUS_INVALID_ARGUMENT);
    CHECK(dispatch_wire(&service, request_wire, sizeof(request_wire),
                        response_wire, sizeof(response_wire), 0,
                        &response_size, &operation_status) ==
          VC_LAUNCH_SERVICE_STATUS_NOT_INITIALIZED);

    CHECK(vc_launch_service_start(&service, 10, &lifecycle_status) ==
          VC_LAUNCH_SERVICE_STATUS_OK);
    CHECK(vc_launch_service_start(&service, 5, &lifecycle_status) ==
          VC_LAUNCH_SERVICE_STATUS_OK);
    CHECK(submit_success(&service, &platform, 10, 100) == 7);
    platform.reenter_callbacks = true;
    CHECK(vc_launch_service_stop(&service, 11, &lifecycle_status) ==
          VC_LAUNCH_SERVICE_STATUS_PLATFORM_FAILURE);
    CHECK(service.phase == VC_LAUNCH_SERVICE_PHASE_STOPPED);
    CHECK(service.broker.record.state == VC_LAUNCH_STATE_ABSENT);
    CHECK(service.broker.foreground_process_id == 0);
    CHECK(!service.journal.valid);
    CHECK(platform.cleanup_calls == 1);
    CHECK(platform.busy_reentries == 1);
    CHECK(dispatch_wire(&service, request_wire, sizeof(request_wire),
                        response_wire, sizeof(response_wire), 12,
                        &response_size, &operation_status) ==
          VC_LAUNCH_SERVICE_STATUS_STOPPED);
    CHECK(vc_launch_service_stop(&service, 12, &lifecycle_status) ==
          VC_LAUNCH_SERVICE_STATUS_OK);
    CHECK(platform.cleanup_calls == 1);
    CHECK(vc_launch_service_start(&service, 12, &lifecycle_status) ==
          VC_LAUNCH_SERVICE_STATUS_OK);
    CHECK(vc_launch_service_reset(&service, 12, &lifecycle_status) ==
          VC_LAUNCH_SERVICE_STATUS_OK);
    CHECK(vc_launch_service_reset(&service, 12, &lifecycle_status) ==
          VC_LAUNCH_SERVICE_STATUS_OK);
}

static void test_attestation_and_role_confusion(void)
{
    vc_launch_service service;
    fake_platform platform;
    vc_launch_broker before;
    vc_launch_request request;
    vc_launch_status operation_status;
    uint8_t request_wire[VC_LAUNCH_REQUEST_WIRE_SIZE];
    uint8_t response_wire[VC_LAUNCH_RESPONSE_WIRE_SIZE];
    size_t response_size;
    uint64_t request_id;

    prepare_service(&service, &platform, 1);
    request_id = submit_success(&service, &platform, 100, 100);
    before = service.broker;

    request = request_for(
        VC_LAUNCH_OPERATION_CLAIM, VC_LAUNCH_CALLER_GAME_PLUGIN,
        TARGET_PID, TARGET_GENERATION, request_id, 101);
    encode_request(&request, request_wire);
    set_shell_caller(&platform);
    CHECK(dispatch_wire(&service, request_wire, sizeof(request_wire),
                        response_wire, sizeof(response_wire), 101,
                        &response_size, &operation_status) ==
          VC_LAUNCH_SERVICE_STATUS_UNAUTHENTICATED_CALLER);
    CHECK(memcmp(&before, &service.broker, sizeof(before)) == 0);

    platform.attestation = VC_LAUNCH_ATTESTATION_UNKNOWN;
    CHECK(dispatch_wire(&service, request_wire, sizeof(request_wire),
                        response_wire, sizeof(response_wire), 101,
                        &response_size, &operation_status) ==
          VC_LAUNCH_SERVICE_STATUS_UNAUTHENTICATED_CALLER);
    CHECK(memcmp(&before, &service.broker, sizeof(before)) == 0);
    platform.attestation = VC_LAUNCH_ATTESTATION_FAILED;
    CHECK(dispatch_wire(&service, request_wire, sizeof(request_wire),
                        response_wire, sizeof(response_wire), 101,
                        &response_size, &operation_status) ==
          VC_LAUNCH_SERVICE_STATUS_PLATFORM_FAILURE);
    CHECK(memcmp(&before, &service.broker, sizeof(before)) == 0);
    platform.attestation = VC_LAUNCH_ATTESTATION_ACCEPTED;

    set_game_caller(&platform, TARGET_PID + 1, TARGET_GENERATION);
    CHECK(dispatch_wire(&service, request_wire, sizeof(request_wire),
                        response_wire, sizeof(response_wire), 101,
                        &response_size, &operation_status) ==
          VC_LAUNCH_SERVICE_STATUS_UNAUTHENTICATED_CALLER);
    CHECK(memcmp(&before, &service.broker, sizeof(before)) == 0);

    set_shell_caller(&platform);
    request = request_for(
        VC_LAUNCH_OPERATION_STATUS, VC_LAUNCH_CALLER_SCE_SHELL,
        TARGET_PID, TARGET_GENERATION, request_id, 101);
    encode_request(&request, request_wire);
    platform.foreground_ok = false;
    CHECK(dispatch_wire(&service, request_wire, sizeof(request_wire),
                        response_wire, sizeof(response_wire), 101,
                        &response_size, &operation_status) ==
          VC_LAUNCH_SERVICE_STATUS_PLATFORM_FAILURE);
    CHECK(memcmp(&before, &service.broker, sizeof(before)) == 0);
    platform.foreground_ok = true;

    set_game_caller(&platform, TARGET_PID, TARGET_GENERATION);
    platform.caller.module_generation = 0;
    request = request_for(
        VC_LAUNCH_OPERATION_STATUS, VC_LAUNCH_CALLER_GAME_PLUGIN,
        TARGET_PID, TARGET_GENERATION, request_id, 101);
    encode_request(&request, request_wire);
    CHECK(dispatch_wire(&service, request_wire, sizeof(request_wire),
                        response_wire, sizeof(response_wire), 101,
                        &response_size, &operation_status) ==
          VC_LAUNCH_SERVICE_STATUS_UNAUTHENTICATED_CALLER);
    CHECK(memcmp(&before, &service.broker, sizeof(before)) == 0);

    set_shell_caller(&platform);
    request = request_for(
        VC_LAUNCH_OPERATION_SUBMIT, VC_LAUNCH_CALLER_SCE_SHELL,
        TARGET_PID + 1, TARGET_GENERATION, 0, 101);
    CHECK(dispatch_request(&service, &request, request_wire,
                           response_wire, 101, &response_size,
                           &operation_status) ==
          VC_LAUNCH_SERVICE_STATUS_STALE_TRUSTED_SNAPSHOT);
    CHECK(memcmp(&before, &service.broker, sizeof(before)) == 0);

    set_game_caller(&platform, TARGET_PID, TARGET_GENERATION);
    request = request_for(
        VC_LAUNCH_OPERATION_SUBMIT, VC_LAUNCH_CALLER_SCE_SHELL,
        TARGET_PID, TARGET_GENERATION, 0, 101);
    CHECK(dispatch_request(&service, &request, request_wire,
                           response_wire, 101, &response_size,
                           &operation_status) ==
          VC_LAUNCH_SERVICE_STATUS_UNAUTHENTICATED_CALLER);
    CHECK(memcmp(&before, &service.broker, sizeof(before)) == 0);

    request = request_for(
        VC_LAUNCH_OPERATION_STATUS, VC_LAUNCH_CALLER_GAME_PLUGIN,
        TARGET_PID, TARGET_GENERATION, request_id, 101);
    CHECK(dispatch_request(&service, &request, request_wire,
                           response_wire, 101, &response_size,
                           &operation_status) ==
          VC_LAUNCH_SERVICE_STATUS_OK);
    CHECK(operation_status == VC_LAUNCH_STATUS_PENDING);
}

static void test_attested_game_status_discovery(void)
{
    vc_launch_service service;
    fake_platform platform;
    vc_launch_request request;
    vc_launch_response response;
    vc_launch_status operation_status;
    uint8_t request_wire[VC_LAUNCH_REQUEST_WIRE_SIZE];
    uint8_t response_wire[VC_LAUNCH_RESPONSE_WIRE_SIZE];
    uint64_t request_id;
    size_t response_size;

    prepare_service(&service, &platform, 90);
    request_id = submit_success(&service, &platform, 100, 100);
    request = request_for(
        VC_LAUNCH_OPERATION_STATUS, VC_LAUNCH_CALLER_GAME_PLUGIN,
        TARGET_PID, TARGET_GENERATION, 0, 101);
    set_game_caller(&platform, TARGET_PID, TARGET_GENERATION);
    CHECK(dispatch_request(&service, &request, request_wire,
                           response_wire, 101, &response_size,
                           &operation_status) ==
          VC_LAUNCH_SERVICE_STATUS_OK);
    CHECK(operation_status == VC_LAUNCH_STATUS_PENDING);
    CHECK(vc_launch_response_decode(
              response_wire, response_size, &response) ==
          VC_LAUNCH_STATUS_OK);
    CHECK(response.request_id == request_id);

    set_shell_caller(&platform);
    request.caller_role = VC_LAUNCH_CALLER_SCE_SHELL;
    CHECK(dispatch_wire(&service, request_wire, sizeof(request_wire),
                        response_wire, sizeof(response_wire), 101,
                        &response_size, &operation_status) ==
          VC_LAUNCH_SERVICE_STATUS_UNAUTHENTICATED_CALLER);

    request = request_for(
        VC_LAUNCH_OPERATION_STATUS, VC_LAUNCH_CALLER_GAME_PLUGIN,
        TARGET_PID, TARGET_GENERATION, 0, 102);
    set_game_caller(&platform, TARGET_PID + 1u, TARGET_GENERATION);
    CHECK(dispatch_request(&service, &request, request_wire,
                           response_wire, 102, &response_size,
                           &operation_status) ==
          VC_LAUNCH_SERVICE_STATUS_UNAUTHENTICATED_CALLER);
}

static void test_buffer_boundaries_and_copy_faults(void)
{
    vc_launch_service service;
    fake_platform platform;
    vc_launch_broker before;
    vc_launch_request request;
    vc_launch_response response;
    vc_launch_status operation_status;
    uint8_t request_wire[VC_LAUNCH_RESPONSE_WIRE_SIZE];
    uint8_t response_wire[VC_LAUNCH_RESPONSE_WIRE_SIZE];
    size_t response_size;

    prepare_service(&service, &platform, 10);
    request = request_for(
        VC_LAUNCH_OPERATION_SUBMIT, VC_LAUNCH_CALLER_SCE_SHELL,
        TARGET_PID, TARGET_GENERATION, 0, 100);
    encode_request(&request, request_wire);
    before = service.broker;

    CHECK(dispatch_wire(&service, NULL, VC_LAUNCH_REQUEST_WIRE_SIZE,
                        response_wire, sizeof(response_wire), 100,
                        &response_size, &operation_status) ==
          VC_LAUNCH_SERVICE_STATUS_INVALID_ARGUMENT);
    CHECK(dispatch_wire(&service, request_wire,
                        VC_LAUNCH_REQUEST_WIRE_SIZE,
                        NULL, sizeof(response_wire), 100,
                        &response_size, &operation_status) ==
          VC_LAUNCH_SERVICE_STATUS_INVALID_ARGUMENT);
    CHECK(dispatch_wire(&service, request_wire, 0,
                        response_wire, sizeof(response_wire), 100,
                        &response_size, &operation_status) ==
          VC_LAUNCH_SERVICE_STATUS_INVALID_INPUT_SIZE);
    CHECK(dispatch_wire(&service, request_wire,
                        VC_LAUNCH_REQUEST_WIRE_SIZE - 1,
                        response_wire, sizeof(response_wire), 100,
                        &response_size, &operation_status) ==
          VC_LAUNCH_SERVICE_STATUS_INVALID_INPUT_SIZE);
    CHECK(dispatch_wire(&service, request_wire,
                        VC_LAUNCH_REQUEST_WIRE_SIZE + 1,
                        response_wire, sizeof(response_wire), 100,
                        &response_size, &operation_status) ==
          VC_LAUNCH_SERVICE_STATUS_INVALID_INPUT_SIZE);
    CHECK(dispatch_wire(&service, request_wire, SIZE_MAX,
                        response_wire, sizeof(response_wire), 100,
                        &response_size, &operation_status) ==
          VC_LAUNCH_SERVICE_STATUS_INVALID_INPUT_SIZE);
    CHECK(dispatch_wire(&service, request_wire,
                        VC_LAUNCH_REQUEST_WIRE_SIZE,
                        response_wire,
                        VC_LAUNCH_RESPONSE_WIRE_SIZE - 1, 100,
                        &response_size, &operation_status) ==
          VC_LAUNCH_SERVICE_STATUS_OUTPUT_TOO_SMALL);
    CHECK(platform.copy_from_calls == 0);
    CHECK(memcmp(&before, &service.broker, sizeof(before)) == 0);

    platform.copy_from_ok = false;
    platform.copy_from_limit = 0;
    CHECK(dispatch_wire(&service, request_wire,
                        VC_LAUNCH_REQUEST_WIRE_SIZE,
                        response_wire, SIZE_MAX, 100,
                        &response_size, &operation_status) ==
          VC_LAUNCH_SERVICE_STATUS_COPY_FROM_USER_FAILED);
    CHECK(memcmp(&before, &service.broker, sizeof(before)) == 0);
    platform.copy_from_limit = VC_LAUNCH_REQUEST_WIRE_SIZE / 2;
    CHECK(dispatch_wire(&service, request_wire,
                        VC_LAUNCH_REQUEST_WIRE_SIZE,
                        response_wire, SIZE_MAX, 100,
                        &response_size, &operation_status) ==
          VC_LAUNCH_SERVICE_STATUS_COPY_FROM_USER_FAILED);
    CHECK(memcmp(&before, &service.broker, sizeof(before)) == 0);

    platform.copy_from_ok = true;
    platform.copy_from_limit = SIZE_MAX;
    memset(response_wire, 0xa5, sizeof(response_wire));
    CHECK(dispatch_wire(&service, request_wire,
                        VC_LAUNCH_REQUEST_WIRE_SIZE,
                        response_wire, SIZE_MAX, 100,
                        &response_size, &operation_status) ==
          VC_LAUNCH_SERVICE_STATUS_OK);
    CHECK(platform.last_copy_from_size == VC_LAUNCH_REQUEST_WIRE_SIZE);
    CHECK(platform.last_copy_to_size == VC_LAUNCH_RESPONSE_WIRE_SIZE);
    CHECK(response_size == VC_LAUNCH_RESPONSE_WIRE_SIZE);
    CHECK(vc_launch_response_decode(response_wire, response_size,
                                    &response) == VC_LAUNCH_STATUS_OK);
    CHECK(response.reserved0 == 0 && response.reserved1 == 0);

    prepare_service(&service, &platform, 20);
    request = request_for(
        VC_LAUNCH_OPERATION_SUBMIT, VC_LAUNCH_CALLER_SCE_SHELL,
        TARGET_PID, TARGET_GENERATION, 0, 100);
    encode_request(&request, request_wire);
    memset(request_wire + VC_LAUNCH_REQUEST_WIRE_SIZE, 0,
           VC_LAUNCH_RESPONSE_WIRE_SIZE -
               VC_LAUNCH_REQUEST_WIRE_SIZE);
    CHECK(dispatch_wire(&service, request_wire,
                        VC_LAUNCH_REQUEST_WIRE_SIZE,
                        request_wire, sizeof(request_wire), 100,
                        &response_size, &operation_status) ==
          VC_LAUNCH_SERVICE_STATUS_OK);
    CHECK(vc_launch_response_decode(request_wire, response_size,
                                    &response) == VC_LAUNCH_STATUS_OK);
    CHECK(response.request_id == 20);
}

static void check_copy_out_retry(vc_launch_operation operation)
{
    vc_launch_service service;
    fake_platform platform;
    vc_launch_request request;
    vc_launch_response failed_response;
    vc_launch_response retry_response;
    vc_launch_status operation_status;
    vc_launch_status retry_status;
    vc_launch_status lifecycle_status;
    uint8_t request_wire[VC_LAUNCH_REQUEST_WIRE_SIZE];
    uint8_t response_wire[VC_LAUNCH_RESPONSE_WIRE_SIZE];
    uint8_t retry_wire[VC_LAUNCH_RESPONSE_WIRE_SIZE];
    uint8_t different_wire[VC_LAUNCH_REQUEST_WIRE_SIZE];
    uint8_t expected_response[VC_LAUNCH_RESPONSE_WIRE_SIZE];
    uint64_t request_id;
    size_t response_size;
    size_t retry_size;

    prepare_service(&service, &platform, 30);
    request_id = 0;
    if (operation == VC_LAUNCH_OPERATION_SUBMIT) {
        request = request_for(
            operation, VC_LAUNCH_CALLER_SCE_SHELL,
            TARGET_PID, TARGET_GENERATION, 0, 101);
        set_shell_caller(&platform);
    } else {
        request_id = submit_success(&service, &platform, 100, 1000);
        request = request_for(
            operation, VC_LAUNCH_CALLER_GAME_PLUGIN,
            TARGET_PID, TARGET_GENERATION, request_id, 101);
        set_game_caller(&platform, TARGET_PID, TARGET_GENERATION);
    }
    if (operation == VC_LAUNCH_OPERATION_STATUS) {
        request.caller_role = VC_LAUNCH_CALLER_GAME_PLUGIN;
    }
    encode_request(&request, request_wire);

    platform.copy_to_ok = false;
    platform.copy_to_limit = VC_LAUNCH_RESPONSE_WIRE_SIZE / 2;
    memset(response_wire, 0xa5, sizeof(response_wire));
    CHECK(dispatch_wire(&service, request_wire, sizeof(request_wire),
                        response_wire, sizeof(response_wire), 101,
                        &response_size, &operation_status) ==
          VC_LAUNCH_SERVICE_STATUS_COPY_TO_USER_FAILED);
    CHECK(response_size == 0);
    CHECK(service.journal.valid);
    CHECK(vc_launch_response_decode(service.journal.response,
                                    sizeof(service.journal.response),
                                    &failed_response) ==
          VC_LAUNCH_STATUS_OK);
    CHECK(failed_response.status == operation_status);
    memcpy(expected_response, service.journal.response,
           sizeof(expected_response));
    CHECK(vc_launch_service_tick(&service, 102, &lifecycle_status) ==
          VC_LAUNCH_SERVICE_STATUS_RESULT_PENDING);
    CHECK(service.journal.valid);
    {
        size_t index;

        for (index = platform.copy_to_limit;
             index < sizeof(response_wire); ++index) {
            CHECK(response_wire[index] == 0xa5);
        }
    }

    memcpy(different_wire, request_wire, sizeof(different_wire));
    different_wire[32] ^= 1u;
    platform.copy_to_ok = true;
    platform.copy_to_limit = SIZE_MAX;
    CHECK(dispatch_wire(&service, different_wire,
                        sizeof(different_wire),
                        retry_wire, sizeof(retry_wire), 102,
                        &retry_size, &retry_status) ==
          VC_LAUNCH_SERVICE_STATUS_RESULT_PENDING);
    CHECK(service.journal.valid);

    CHECK(dispatch_wire(&service, request_wire, sizeof(request_wire),
                        retry_wire, sizeof(retry_wire), 102,
                        &retry_size, &retry_status) ==
          VC_LAUNCH_SERVICE_STATUS_OK);
    CHECK(!service.journal.valid);
    CHECK(retry_status == operation_status);
    CHECK(retry_size == VC_LAUNCH_RESPONSE_WIRE_SIZE);
    CHECK(memcmp(expected_response, retry_wire,
                 sizeof(retry_wire)) == 0);
    CHECK(vc_launch_response_decode(retry_wire, retry_size,
                                    &retry_response) ==
          VC_LAUNCH_STATUS_OK);
    CHECK(memcmp(&failed_response, &retry_response,
                 sizeof(failed_response)) == 0);

    request.now_ms = 103;
    CHECK(dispatch_request(&service, &request, request_wire,
                           response_wire, 103, &response_size,
                           &operation_status) ==
          VC_LAUNCH_SERVICE_STATUS_OK);
    CHECK(operation_status == retry_status);
}

static void test_copy_out_failure_policy(void)
{
    check_copy_out_retry(VC_LAUNCH_OPERATION_SUBMIT);
    check_copy_out_retry(VC_LAUNCH_OPERATION_CLAIM);
    check_copy_out_retry(VC_LAUNCH_OPERATION_CANCEL);
    check_copy_out_retry(VC_LAUNCH_OPERATION_STATUS);
}

static void test_foreground_lifecycle_and_time(void)
{
    vc_launch_service service;
    fake_platform platform;
    vc_launch_request request;
    vc_launch_response response;
    vc_launch_status lifecycle_status;
    vc_launch_status operation_status;
    uint8_t request_wire[VC_LAUNCH_REQUEST_WIRE_SIZE];
    uint8_t response_wire[VC_LAUNCH_RESPONSE_WIRE_SIZE];
    uint64_t request_id;
    size_t response_size;

    prepare_service(&service, &platform, 100);
    request_id = submit_success(&service, &platform, 100, 10);

    set_foreground(&platform, 2, TARGET_PID, TARGET_GENERATION,
                   "PCSA00001");
    request = request_for(
        VC_LAUNCH_OPERATION_STATUS, VC_LAUNCH_CALLER_SCE_SHELL,
        TARGET_PID, TARGET_GENERATION, request_id, 101);
    set_shell_caller(&platform);
    CHECK(dispatch_request(&service, &request, request_wire,
                           response_wire, 101, &response_size,
                           &operation_status) ==
          VC_LAUNCH_SERVICE_STATUS_OK);
    CHECK(operation_status == VC_LAUNCH_STATUS_PENDING);

    set_foreground(&platform, 3, TARGET_PID, TARGET_GENERATION + 1,
                   "PCSA00001");
    CHECK(dispatch_request(&service, &request, request_wire,
                           response_wire, 102, &response_size,
                           &operation_status) ==
          VC_LAUNCH_SERVICE_STATUS_STALE_TRUSTED_SNAPSHOT);
    CHECK(service.broker.record.state == VC_LAUNCH_STATE_STALE_TARGET);

    request = request_for(
        VC_LAUNCH_OPERATION_SUBMIT, VC_LAUNCH_CALLER_SCE_SHELL,
        TARGET_PID, TARGET_GENERATION + 1, 0, 103);
    CHECK(dispatch_request(&service, &request, request_wire,
                           response_wire, 103, &response_size,
                           &operation_status) ==
          VC_LAUNCH_SERVICE_STATUS_OK);
    CHECK(operation_status == VC_LAUNCH_STATUS_PENDING);
    CHECK(vc_launch_response_decode(response_wire, response_size,
                                    &response) == VC_LAUNCH_STATUS_OK);
    request_id = response.request_id;

    set_foreground(&platform, 4, TARGET_PID, TARGET_GENERATION + 1,
                   "PCSA00002");
    request = request_for(
        VC_LAUNCH_OPERATION_STATUS, VC_LAUNCH_CALLER_SCE_SHELL,
        TARGET_PID, TARGET_GENERATION + 1, request_id, 104);
    CHECK(dispatch_request(&service, &request, request_wire,
                           response_wire, 104, &response_size,
                           &operation_status) ==
          VC_LAUNCH_SERVICE_STATUS_OK);
    CHECK(operation_status == VC_LAUNCH_STATUS_STALE_TARGET);

    request = request_for(
        VC_LAUNCH_OPERATION_SUBMIT, VC_LAUNCH_CALLER_SCE_SHELL,
        TARGET_PID, TARGET_GENERATION + 1, 0, 105);
    CHECK(dispatch_request(&service, &request, request_wire,
                           response_wire, 105, &response_size,
                           &operation_status) ==
          VC_LAUNCH_SERVICE_STATUS_OK);
    CHECK(vc_launch_response_decode(response_wire, response_size,
                                    &response) == VC_LAUNCH_STATUS_OK);
    request_id = response.request_id;

    set_foreground(&platform, 5, 0, 0, NULL);
    request = request_for(
        VC_LAUNCH_OPERATION_STATUS, VC_LAUNCH_CALLER_SCE_SHELL,
        TARGET_PID, TARGET_GENERATION + 1, request_id, 106);
    CHECK(dispatch_request(&service, &request, request_wire,
                           response_wire, 106, &response_size,
                           &operation_status) ==
          VC_LAUNCH_SERVICE_STATUS_STALE_TRUSTED_SNAPSHOT);
    CHECK(service.broker.record.state == VC_LAUNCH_STATE_STALE_TARGET);
    CHECK(service.foreground.present == 0u);

    set_foreground(&platform, 6, TARGET_PID, TARGET_GENERATION + 2,
                   "PCSA00003");
    CHECK(vc_launch_service_foreground_changed(
              &service, &platform.foreground, 107,
              &lifecycle_status) == VC_LAUNCH_SERVICE_STATUS_OK);
    request_id = submit_success(&service, &platform, 108, 100);
    CHECK(vc_launch_service_process_exit(
              &service, TARGET_PID, TARGET_GENERATION + 2,
              7, 109, &lifecycle_status) ==
          VC_LAUNCH_SERVICE_STATUS_OK);
    CHECK(lifecycle_status == VC_LAUNCH_STATUS_STALE_TARGET);
    CHECK(service.foreground.present == 0u);
    CHECK(vc_launch_service_process_exit(
              &service, TARGET_PID, TARGET_GENERATION + 2,
              7, 109, &lifecycle_status) ==
          VC_LAUNCH_SERVICE_STATUS_STALE_TRUSTED_SNAPSHOT);

    set_foreground(&platform, 8, TARGET_PID, TARGET_GENERATION + 3,
                   "PCSA00004");
    CHECK(vc_launch_service_foreground_changed(
              &service, &platform.foreground, 110,
              &lifecycle_status) == VC_LAUNCH_SERVICE_STATUS_OK);
    request_id = submit_success(&service, &platform, 111, 100);
    CHECK(vc_launch_service_plugin_unload(
              &service, TARGET_PID, TARGET_GENERATION + 3,
              112, &lifecycle_status) ==
          VC_LAUNCH_SERVICE_STATUS_OK);
    CHECK(lifecycle_status == VC_LAUNCH_STATUS_CANCELLED);
    CHECK(service.broker.record.request_id == request_id);
    CHECK(service.broker.record.state == VC_LAUNCH_STATE_CANCELLED);

    prepare_service(&service, &platform, 200);
    request_id = submit_success(&service, &platform, 200, 10);
    request = request_for(
        VC_LAUNCH_OPERATION_STATUS, VC_LAUNCH_CALLER_SCE_SHELL,
        TARGET_PID, TARGET_GENERATION, request_id, 199);
    CHECK(dispatch_request(&service, &request, request_wire,
                           response_wire, 199, &response_size,
                           &operation_status) ==
          VC_LAUNCH_SERVICE_STATUS_OK);
    CHECK(operation_status == VC_LAUNCH_STATUS_CLOCK_ROLLBACK);

    prepare_service(&service, &platform, 300);
    (void)submit_success(&service, &platform, 300, 10);
    CHECK(vc_launch_service_tick(&service, 309, &lifecycle_status) ==
          VC_LAUNCH_SERVICE_STATUS_OK);
    CHECK(lifecycle_status == VC_LAUNCH_STATUS_PENDING);
    CHECK(vc_launch_service_tick(&service, 310, &lifecycle_status) ==
          VC_LAUNCH_SERVICE_STATUS_OK);
    CHECK(lifecycle_status == VC_LAUNCH_STATUS_EXPIRED);
}

static void test_reentrancy_guard(void)
{
    vc_launch_service service;
    fake_platform platform;
    vc_launch_request request;
    vc_launch_status operation_status;
    uint8_t request_wire[VC_LAUNCH_REQUEST_WIRE_SIZE];
    uint8_t response_wire[VC_LAUNCH_RESPONSE_WIRE_SIZE];
    size_t response_size;

    prepare_service(&service, &platform, 1);
    platform.reenter_callbacks = true;
    request = request_for(
        VC_LAUNCH_OPERATION_SUBMIT, VC_LAUNCH_CALLER_SCE_SHELL,
        TARGET_PID, TARGET_GENERATION, 0, 100);
    CHECK(dispatch_request(&service, &request, request_wire,
                           response_wire, 100, &response_size,
                           &operation_status) ==
          VC_LAUNCH_SERVICE_STATUS_OK);
    CHECK(platform.busy_reentries == 4);
    CHECK(atomic_load_explicit(&service.transaction_busy,
                               memory_order_relaxed) == 0u);
}

static void test_golden_semantics_and_malformed(void)
{
    vc_launch_service service;
    fake_platform platform;
    vc_launch_broker broker;
    vc_launch_request request;
    vc_launch_status operation_status;
    vc_launch_status direct_status;
    uint8_t request_wire[VC_LAUNCH_REQUEST_WIRE_SIZE];
    uint8_t service_response[VC_LAUNCH_RESPONSE_WIRE_SIZE];
    uint8_t direct_response[VC_LAUNCH_RESPONSE_WIRE_SIZE];
    size_t service_size;
    size_t direct_size;
    uint64_t request_id = 0;
    unsigned int index;

    const vc_launch_operation operations[] = {
        VC_LAUNCH_OPERATION_SUBMIT,
        VC_LAUNCH_OPERATION_STATUS,
        VC_LAUNCH_OPERATION_CLAIM,
        VC_LAUNCH_OPERATION_CANCEL
    };

    prepare_service(&service, &platform, 50);
    CHECK(vc_launch_broker_init(&broker, 50) == VC_LAUNCH_STATUS_OK);
    CHECK(vc_launch_broker_foreground_changed(
              &broker, TARGET_PID, TARGET_GENERATION, 0) ==
          VC_LAUNCH_STATUS_OK);

    for (index = 0; index < sizeof(operations) / sizeof(operations[0]);
         ++index) {
        vc_launch_caller_role role =
            operations[index] == VC_LAUNCH_OPERATION_SUBMIT ||
                    operations[index] == VC_LAUNCH_OPERATION_STATUS
                ? VC_LAUNCH_CALLER_SCE_SHELL
                : VC_LAUNCH_CALLER_GAME_PLUGIN;
        uint64_t now_ms = 100 + index;

        request = request_for(operations[index], role, TARGET_PID,
                              TARGET_GENERATION, request_id, now_ms);
        if (role == VC_LAUNCH_CALLER_SCE_SHELL) {
            set_shell_caller(&platform);
        } else {
            set_game_caller(&platform, TARGET_PID, TARGET_GENERATION);
        }
        encode_request(&request, request_wire);
        direct_status = vc_launch_broker_dispatch_wire(
            &broker, request_wire, sizeof(request_wire),
            direct_response, sizeof(direct_response), &direct_size);
        CHECK(dispatch_wire(&service, request_wire, sizeof(request_wire),
                            service_response, sizeof(service_response),
                            now_ms, &service_size, &operation_status) ==
              VC_LAUNCH_SERVICE_STATUS_OK);
        CHECK(operation_status == direct_status);
        CHECK(service_size == direct_size);
        CHECK(memcmp(service_response, direct_response, direct_size) == 0);
        if (operations[index] == VC_LAUNCH_OPERATION_SUBMIT) {
            vc_launch_response response;

            CHECK(vc_launch_response_decode(service_response, service_size,
                                            &response) ==
                  VC_LAUNCH_STATUS_OK);
            request_id = response.request_id;
        }
    }

    prepare_service(&service, &platform, 70);
    request = request_for(
        VC_LAUNCH_OPERATION_SUBMIT, VC_LAUNCH_CALLER_SCE_SHELL,
        TARGET_PID, TARGET_GENERATION, 0, 100);
    encode_request(&request, request_wire);
    for (index = 0; index < 4; ++index) {
        vc_launch_broker before = service.broker;
        uint8_t saved;
        size_t offset;
        vc_launch_status expected;

        switch (index) {
        case 0:
            offset = 0;
            saved = request_wire[offset];
            request_wire[offset] = 2;
            expected = VC_LAUNCH_STATUS_UNSUPPORTED_VERSION;
            break;
        case 1:
            offset = 4;
            saved = request_wire[offset];
            request_wire[offset] = 0xff;
            expected = VC_LAUNCH_STATUS_UNSUPPORTED_OPERATION;
            break;
        case 2:
            offset = 8;
            saved = request_wire[offset];
            request_wire[offset] = 0x80;
            expected = VC_LAUNCH_STATUS_INVALID_CAPABILITY;
            break;
        default:
            offset = 56;
            saved = request_wire[offset];
            request_wire[offset] = 1;
            expected = VC_LAUNCH_STATUS_INVALID_RESERVED;
            break;
        }
        CHECK(dispatch_wire(&service, request_wire, sizeof(request_wire),
                            service_response, sizeof(service_response),
                            100, &service_size, &operation_status) ==
              VC_LAUNCH_SERVICE_STATUS_OK);
        CHECK(operation_status == expected);
        CHECK(memcmp(&before, &service.broker, sizeof(before)) == 0);
        request_wire[offset] = saved;
    }
}

static uint32_t next_random(uint32_t *state)
{
    *state = *state * UINT32_C(1664525) + UINT32_C(1013904223);
    return *state;
}

static void check_service_invariants(const vc_launch_service *service)
{
    CHECK(service->phase == VC_LAUNCH_SERVICE_PHASE_RUNNING);
    CHECK(service->broker.initialized);
    CHECK(service->broker.record.state <=
          VC_LAUNCH_STATE_CLOCK_ROLLBACK);
    CHECK(!service->journal.valid);
    if (service->broker.record.state != VC_LAUNCH_STATE_ABSENT) {
        CHECK(service->broker.record.request_id != 0);
        CHECK(service->broker.record.target_process_id != 0);
        CHECK(service->broker.record.target_generation != 0);
    }
}

static void test_bounded_service_model(void)
{
    vc_launch_service service;
    fake_platform platform;
    uint8_t request_wire[VC_LAUNCH_REQUEST_WIRE_SIZE];
    uint8_t response_wire[VC_LAUNCH_RESPONSE_WIRE_SIZE];
    vc_launch_status operation_status;
    vc_launch_status lifecycle_status;
    uint32_t random_state = UINT32_C(0x51a7e123);
    uint64_t now_ms = 1;
    uint64_t sequence = 1;
    size_t response_size;
    size_t step;

    prepare_service(&service, &platform, 1);
    for (step = 0; step < 5000; ++step) {
        uint32_t choice = next_random(&random_state) % 8u;
        size_t index;

        now_ms += next_random(&random_state) % 3u;
        for (index = 0; index < sizeof(request_wire); ++index) {
            request_wire[index] =
                (uint8_t)(next_random(&random_state) >> 24);
        }
        platform.copy_to_ok = true;
        platform.copy_to_limit = SIZE_MAX;
        if (choice <= 3u) {
            set_shell_caller(&platform);
            (void)dispatch_wire(
                &service, request_wire, sizeof(request_wire),
                response_wire, sizeof(response_wire), now_ms,
                &response_size, &operation_status);
            CHECK(response_size == 0 ||
                  response_size == VC_LAUNCH_RESPONSE_WIRE_SIZE);
        } else if (choice == 4u) {
            (void)vc_launch_service_tick(
                &service, now_ms, &lifecycle_status);
        } else if (choice == 5u) {
            ++sequence;
            set_foreground(&platform, sequence, TARGET_PID,
                           TARGET_GENERATION + sequence,
                           "PCSA00001");
            (void)vc_launch_service_foreground_changed(
                &service, &platform.foreground, now_ms,
                &lifecycle_status);
        } else if (choice == 6u) {
            (void)vc_launch_service_reset(
                &service, now_ms, &lifecycle_status);
            ++sequence;
            set_foreground(&platform, sequence, TARGET_PID,
                           TARGET_GENERATION + sequence,
                           "PCSA00001");
        } else if (service.foreground.present != 0u) {
            (void)vc_launch_service_plugin_unload(
                &service, service.foreground.target_process_id,
                service.foreground.target_generation, now_ms,
                &lifecycle_status);
        }
        check_service_invariants(&service);
    }
}

int main(void)
{
    test_init_start_stop_and_cleanup();
    test_attestation_and_role_confusion();
    test_attested_game_status_discovery();
    test_buffer_boundaries_and_copy_faults();
    test_copy_out_failure_policy();
    test_foreground_lifecycle_and_time();
    test_reentrancy_guard();
    test_golden_semantics_and_malformed();
    test_bounded_service_model();

    if (failures != 0) {
        fprintf(stderr, "%d launch service test(s) failed\n", failures);
        return 1;
    }

    puts("all launch service tests passed");
    return 0;
}
