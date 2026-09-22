#include "vitacheat/foreign_target_gate.h"
#include "vitacheat/foreign_target_startup.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define TARGET_PID UINT32_C(41)
#define CONTROLLER_PID UINT32_C(42)
#define OTHER_PID UINT32_C(43)
#define TARGET_KERNEL_MODULE_ID INT32_C(71)
#define TARGET_PROCESS_MODULE_ID INT32_C(72)
#define TARGET_FINGERPRINT UINT32_C(0x91abcdef)

static int failures;

#define CHECK(expression)                                                        \
    do {                                                                         \
        if (!(expression)) {                                                     \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expression); \
            ++failures;                                                          \
        }                                                                        \
    } while (0)

typedef struct fake_platform {
    vc_ftg_service *service;
    vc_ftg_module_snapshot target_module;
    uint8_t target_memory[256];
    uint64_t now_us;
    uint32_t caller_process_id;
    unsigned int copy_from_calls;
    unsigned int copy_to_calls;
    unsigned int read_calls;
    bool target_alive;
    bool controller_alive;
    bool copies_ok;
    bool module_ok;
    bool read_ok;
    bool exit_during_read;
    bool exit_during_copy_to;
    vc_ftg_result concurrent_event_result;
} fake_platform;

typedef struct fixture {
    vc_ftg_service service;
    vc_ftg_config config;
    vc_ftg_dependencies dependencies;
    fake_platform platform;
} fixture;

static bool fake_get_caller(
    void *context,
    uint32_t *process_id)
{
    fake_platform *platform = (fake_platform *)context;

    *process_id = platform->caller_process_id;
    return *process_id != 0u;
}

static bool fake_get_time(
    void *context,
    uint64_t *now_us)
{
    fake_platform *platform = (fake_platform *)context;

    *now_us = platform->now_us;
    return true;
}

static bool fake_get_title(
    void *context,
    uint32_t process_id,
    uint8_t title_id[VC_FTG_TITLE_ID_CAPACITY])
{
    fake_platform *platform = (fake_platform *)context;

    memset(title_id, 0, VC_FTG_TITLE_ID_CAPACITY);
    if (process_id == TARGET_PID && platform->target_alive) {
        memcpy(title_id, "VCFT00001", sizeof("VCFT00001"));
        return true;
    }
    if (process_id == CONTROLLER_PID &&
        platform->controller_alive) {
        memcpy(title_id, "VCFC00001", sizeof("VCFC00001"));
        return true;
    }
    if (process_id == OTHER_PID) {
        memcpy(title_id, "VCOX00001", sizeof("VCOX00001"));
        return true;
    }
    return false;
}

static bool fake_get_module(
    void *context,
    uint32_t process_id,
    vc_ftg_module_snapshot *module)
{
    fake_platform *platform = (fake_platform *)context;

    if (!platform->module_ok ||
        !platform->target_alive ||
        process_id != TARGET_PID) {
        return false;
    }
    *module = platform->target_module;
    return true;
}

static bool fake_copy_from(
    void *context,
    uint32_t process_id,
    void *destination,
    const void *source,
    size_t size)
{
    fake_platform *platform = (fake_platform *)context;

    ++platform->copy_from_calls;
    if (!platform->copies_ok ||
        (process_id != CONTROLLER_PID &&
         process_id != TARGET_PID &&
         process_id != OTHER_PID) ||
        source == (const void *)(uintptr_t)1u) {
        return false;
    }
    memcpy(destination, source, size);
    return true;
}

static bool fake_copy_to(
    void *context,
    uint32_t process_id,
    void *destination,
    const void *source,
    size_t size)
{
    fake_platform *platform = (fake_platform *)context;

    ++platform->copy_to_calls;
    if (!platform->copies_ok ||
        (process_id != CONTROLLER_PID &&
         process_id != TARGET_PID &&
         process_id != OTHER_PID) ||
        destination == (void *)(uintptr_t)1u) {
        return false;
    }
    memcpy(destination, source, size);
    if (platform->exit_during_copy_to) {
        platform->exit_during_copy_to = false;
        platform->target_alive = false;
        platform->concurrent_event_result =
            vc_ftg_service_process_event(
                platform->service,
                VC_FTG_PROCESS_EXITED,
                TARGET_PID);
    }
    return true;
}

static bool fake_read(
    void *context,
    uint32_t process_id,
    void *destination,
    uintptr_t source_address,
    size_t size)
{
    fake_platform *platform = (fake_platform *)context;
    const uintptr_t begin =
        (uintptr_t)platform->target_memory;
    const uintptr_t end =
        begin + sizeof(platform->target_memory);

    ++platform->read_calls;
    if (platform->exit_during_read) {
        platform->target_alive = false;
        platform->concurrent_event_result =
            vc_ftg_service_process_event(
                platform->service,
                VC_FTG_PROCESS_EXITED,
                TARGET_PID);
    }
    if (!platform->read_ok ||
        process_id != TARGET_PID ||
        source_address < begin ||
        source_address > end ||
        size > (size_t)(end - source_address)) {
        return false;
    }
    memcpy(destination, (const void *)source_address, size);
    return true;
}

static void initialize_fixture_with_sequences(
    fixture *fixture_value,
    bool full_gate,
    uint64_t first_generation,
    uint64_t first_revision,
    uint64_t first_handle)
{
    size_t index;

    memset(fixture_value, 0, sizeof(*fixture_value));
    memcpy(
        fixture_value->config.target_title_id,
        "VCFT00001",
        sizeof("VCFT00001"));
    memcpy(
        fixture_value->config.controller_title_id,
        "VCFC00001",
        sizeof("VCFC00001"));
    memcpy(
        fixture_value->config.target_module_name,
        "VitaCheatFtTarget",
        sizeof("VitaCheatFtTarget"));
    fixture_value->config.timeout_us =
        VC_FTG_DEFAULT_TIMEOUT_US;
    fixture_value->config.enabled = true;
    fixture_value->config.api_available = true;
    fixture_value->config.foreign_lifecycle_enabled =
        full_gate;

    fixture_value->platform.service =
        &fixture_value->service;
    fixture_value->platform.now_us = UINT64_C(1000000);
    fixture_value->platform.caller_process_id =
        CONTROLLER_PID;
    fixture_value->platform.target_alive = true;
    fixture_value->platform.controller_alive = true;
    fixture_value->platform.copies_ok = true;
    fixture_value->platform.module_ok = true;
    fixture_value->platform.read_ok = true;
    fixture_value->platform.target_module.process_id =
        TARGET_PID;
    fixture_value->platform.target_module.kernel_module_id =
        TARGET_KERNEL_MODULE_ID;
    fixture_value->platform.target_module.process_module_id =
        TARGET_PROCESS_MODULE_ID;
    fixture_value->platform.target_module.module_fingerprint =
        TARGET_FINGERPRINT;
    fixture_value->platform.target_module.segment_count = 2u;
    memcpy(
        fixture_value->platform.target_module.module_name,
        "VitaCheatFtTarget",
        sizeof("VitaCheatFtTarget"));
    fixture_value->platform.target_module.segments[0].base =
        (uintptr_t)fixture_value->platform.target_memory;
    fixture_value->platform.target_module.segments[0].size =
        128u;
    fixture_value->platform.target_module
        .segments[0]
        .permissions =
        VC_FTG_PERMISSION_USER_READ | UINT32_C(0x04);
    fixture_value->platform.target_module.segments[1].base =
        (uintptr_t)(fixture_value->platform.target_memory + 128u);
    fixture_value->platform.target_module.segments[1].size =
        128u;
    fixture_value->platform.target_module
        .segments[1]
        .permissions =
        UINT32_C(0x02);
    for (index = 0u;
         index < sizeof(fixture_value->platform.target_memory);
         ++index) {
        fixture_value->platform.target_memory[index] =
            (uint8_t)(index ^ 0xa5u);
    }

    fixture_value->dependencies.get_caller_pid =
        fake_get_caller;
    fixture_value->dependencies.get_time_us =
        fake_get_time;
    fixture_value->dependencies.get_title_id =
        fake_get_title;
    fixture_value->dependencies.get_main_module =
        fake_get_module;
    fixture_value->dependencies.copy_from_user =
        fake_copy_from;
    fixture_value->dependencies.copy_to_user =
        fake_copy_to;
    fixture_value->dependencies.read_process = fake_read;
    fixture_value->dependencies.context =
        &fixture_value->platform;

    CHECK(vc_ftg_service_init(
              &fixture_value->service,
              &fixture_value->config,
              &fixture_value->dependencies,
              first_generation,
              first_revision,
              first_handle) == VC_FTG_RESULT_OK);
    CHECK(vc_ftg_service_start(
              &fixture_value->service) ==
          VC_FTG_RESULT_OK);
    CHECK(vc_ftg_service_set_registered(
              &fixture_value->service,
              true,
              VC_FTG_DIAGNOSTIC_REGISTRATION_FAILED) ==
          VC_FTG_RESULT_OK);
}

static void initialize_fixture(
    fixture *fixture_value,
    bool full_gate)
{
    initialize_fixture_with_sequences(
        fixture_value,
        full_gate,
        UINT64_C(1),
        UINT64_C(1),
        UINT64_C(0x1020304050607080));
}

static void init_status_request(
    vc_ftg_status_request *request)
{
    memset(request, 0, sizeof(*request));
    request->version = VC_FTG_ABI_VERSION;
    request->struct_size = sizeof(*request);
}

static void init_read_request(
    vc_ftg_read_request *request,
    uint64_t handle,
    uint32_t segment,
    uint32_t offset,
    uint32_t length)
{
    memset(request, 0, sizeof(*request));
    request->version = VC_FTG_ABI_VERSION;
    request->struct_size = sizeof(*request);
    request->capabilities =
        VC_FTG_CAPABILITY_FOREIGN_SEGMENT_READ;
    request->handle = handle;
    request->segment_index = segment;
    request->offset = offset;
    request->length = length;
}

static uint64_t start_and_open(fixture *fixture_value)
{
    vc_ftg_open_request request;
    vc_ftg_open_response response;

    CHECK(vc_ftg_service_process_event(
              &fixture_value->service,
              VC_FTG_PROCESS_CREATED,
              TARGET_PID) == VC_FTG_RESULT_OK);
    CHECK(vc_ftg_service_process_event(
              &fixture_value->service,
              VC_FTG_PROCESS_STARTED,
              TARGET_PID) == VC_FTG_RESULT_OK);
    init_status_request(&request);
    memset(&response, 0, sizeof(response));
    CHECK(vc_ftg_service_open_exact_fixture(
              &fixture_value->service,
              &request,
              &response) == VC_FTG_RESULT_OK);
    CHECK(response.version == VC_FTG_ABI_VERSION);
    CHECK(response.struct_size == sizeof(response));
    CHECK(response.capabilities ==
          VC_FTG_CAPABILITY_FOREIGN_SEGMENT_READ);
    CHECK(response.handle != 0u);
    return response.handle;
}

static void test_diagnostic_only_status(void)
{
    fixture fixture_value;
    vc_ftg_status_request request;
    vc_ftg_status_response response;

    initialize_fixture(&fixture_value, false);
    init_status_request(&request);
    memset(&response, 0xa5, sizeof(response));
    CHECK(vc_ftg_service_get_status(
              &fixture_value.service,
              &request,
              &response) == VC_FTG_RESULT_OK);
    CHECK(response.status ==
          VC_FTG_RUNTIME_DIAGNOSTIC_ONLY);
    CHECK(response.capabilities == 0u);
    CHECK(response.max_read == VC_FTG_MAX_READ);
    CHECK(response.diagnostic_stage ==
          VC_FTG_DIAGNOSTIC_BACKGROUND_LIFECYCLE_UNPROVEN);
    CHECK(response.target_state == VC_FTG_TARGET_NONE);
    CHECK(vc_ftg_service_open_exact_fixture(
              &fixture_value.service,
              &request,
              &response) ==
          VC_FTG_RESULT_DIAGNOSTIC_ONLY);

    fixture_value.platform.caller_process_id = OTHER_PID;
    CHECK(vc_ftg_service_get_status(
              &fixture_value.service,
              &request,
              &response) ==
          VC_FTG_RESULT_CALLER_TITLE_MISMATCH);
}

static void test_startup_marker_cannot_authorize(void)
{
    fixture fixture_value;
    vc_ftg_startup_record startup;
    vc_ftg_readiness_observation readiness;
    vc_ftg_open_request request;
    vc_ftg_open_response response;

    initialize_fixture(&fixture_value, true);
    memset(&readiness, 0, sizeof(readiness));
    readiness.attempt_count = 3u;
    readiness.elapsed_ms = 40u;
    readiness.last_result =
        VC_FTG_RESULT_CALLER_TITLE_MISMATCH;
    readiness.probe_result = VC_FTG_RESULT_OK;
    vc_ftg_startup_record_init(&startup);
    CHECK(vc_ftg_startup_record_complete(
        &startup,
        VC_FTG_STARTUP_STAGE_SENTINEL_LOOKUP_COMPLETE,
        0));
    CHECK(vc_ftg_startup_record_complete_readiness(
        &startup, &readiness));
    CHECK(vc_ftg_startup_record_complete(
        &startup,
        VC_FTG_STARTUP_STAGE_LAYOUT_WRITE_COMPLETE,
        0));
    CHECK(vc_ftg_startup_record_complete(
        &startup,
        VC_FTG_STARTUP_STAGE_FIXTURE_WRITE_COMPLETE,
        0));
    CHECK(vc_ftg_startup_record_mark(
        &startup, VC_FTG_STARTUP_STAGE_PROMPT_READY));
    init_status_request(&request);
    memset(&response, 0, sizeof(response));
    CHECK(vc_ftg_service_open_exact_fixture(
              &fixture_value.service,
              &request,
              &response) ==
          VC_FTG_RESULT_TARGET_UNAVAILABLE);
    CHECK(!fixture_value.service.session.active);

    CHECK(vc_ftg_service_process_event(
              &fixture_value.service,
              VC_FTG_PROCESS_CREATED,
              TARGET_PID) == VC_FTG_RESULT_OK);
    CHECK(vc_ftg_service_process_event(
              &fixture_value.service,
              VC_FTG_PROCESS_STARTED,
              TARGET_PID) == VC_FTG_RESULT_OK);
    CHECK(startup.stage == VC_FTG_STARTUP_STAGE_PROMPT_READY);
    CHECK(!fixture_value.service.session.active);

    fixture_value.platform.caller_process_id = TARGET_PID;
    memset(&response, 0, sizeof(response));
    CHECK(vc_ftg_service_open_exact_fixture(
              &fixture_value.service,
              &request,
              &response) ==
          VC_FTG_RESULT_CALLER_TITLE_MISMATCH);
    CHECK(!fixture_value.service.session.active);
}

static void test_lifecycle_and_exact_reads(void)
{
    static const uint32_t lengths[] = {1u, 63u, 64u};
    fixture fixture_value;
    vc_ftg_read_request request;
    vc_ftg_read_response response;
    vc_ftg_status_request status_request;
    vc_ftg_status_response status_response;
    uint64_t handle;
    size_t index;

    initialize_fixture(&fixture_value, true);
    handle = start_and_open(&fixture_value);
    for (index = 0u;
         index < sizeof(lengths) / sizeof(lengths[0]);
         ++index) {
        uint32_t byte;

        init_read_request(
            &request, handle, 0u, 7u, lengths[index]);
        memset(&response, 0xa5, sizeof(response));
        CHECK(vc_ftg_service_read_fixture_segment(
                  &fixture_value.service,
                  &request,
                  &response) == VC_FTG_RESULT_OK);
        CHECK(response.length == lengths[index]);
        CHECK(memcmp(
                  response.bytes,
                  fixture_value.platform.target_memory + 7u,
                  lengths[index]) == 0);
        for (byte = lengths[index];
             byte < VC_FTG_MAX_READ;
             ++byte) {
            CHECK(response.bytes[byte] == 0u);
        }
    }

    init_read_request(&request, handle, 0u, 0u, 0u);
    CHECK(vc_ftg_service_read_fixture_segment(
              &fixture_value.service,
              &request,
              &response) == VC_FTG_RESULT_RANGE_DENIED);
    init_read_request(&request, handle, 0u, 0u, 65u);
    CHECK(vc_ftg_service_read_fixture_segment(
              &fixture_value.service,
              &request,
              &response) == VC_FTG_RESULT_RANGE_DENIED);
    init_read_request(&request, handle, 0u, 127u, 2u);
    CHECK(vc_ftg_service_read_fixture_segment(
              &fixture_value.service,
              &request,
              &response) == VC_FTG_RESULT_RANGE_DENIED);
    init_read_request(&request, handle, 2u, 0u, 1u);
    CHECK(vc_ftg_service_read_fixture_segment(
              &fixture_value.service,
              &request,
              &response) == VC_FTG_RESULT_INVALID_SEGMENT);
    init_read_request(&request, handle, 1u, 0u, 1u);
    CHECK(vc_ftg_service_read_fixture_segment(
              &fixture_value.service,
              &request,
              &response) == VC_FTG_RESULT_PERMISSION_DENIED);
    init_read_request(&request, handle + 1u, 0u, 0u, 1u);
    CHECK(vc_ftg_service_read_fixture_segment(
              &fixture_value.service,
              &request,
              &response) == VC_FTG_RESULT_INVALID_HANDLE);
    init_read_request(&request, handle, 0u, 0u, 1u);
    CHECK(vc_ftg_service_read_fixture_segment(
              &fixture_value.service,
              &request,
              (void *)(uintptr_t)1u) ==
          VC_FTG_RESULT_COPY_TO_USER_FAILED);
    CHECK(vc_ftg_service_read_fixture_segment(
              &fixture_value.service,
              (const void *)(uintptr_t)1u,
              &response) ==
          VC_FTG_RESULT_COPY_FROM_USER_FAILED);

    init_status_request(&status_request);
    CHECK(vc_ftg_service_get_status(
              &fixture_value.service,
              &status_request,
              &status_response) == VC_FTG_RESULT_OK);
    CHECK(status_response.status ==
          VC_FTG_RUNTIME_SESSION_OPEN);
    CHECK(status_response.target_state ==
          VC_FTG_TARGET_STARTED);
}

static void test_exit_relaunch_and_reuse(void)
{
    fixture fixture_value;
    vc_ftg_read_request request;
    vc_ftg_read_response response;
    vc_ftg_open_request open_request;
    vc_ftg_open_response open_response;
    uint64_t old_handle;
    uint64_t new_handle;

    initialize_fixture(&fixture_value, true);
    old_handle = start_and_open(&fixture_value);
    fixture_value.platform.target_alive = false;
    CHECK(vc_ftg_service_process_event(
              &fixture_value.service,
              VC_FTG_PROCESS_EXITED,
              TARGET_PID) == VC_FTG_RESULT_OK);
    init_read_request(
        &request, old_handle, 0u, 0u, 1u);
    CHECK(vc_ftg_service_read_fixture_segment(
              &fixture_value.service,
              &request,
              &response) == VC_FTG_RESULT_INVALID_HANDLE);

    fixture_value.platform.target_alive = true;
    CHECK(vc_ftg_service_process_event(
              &fixture_value.service,
              VC_FTG_PROCESS_CREATED,
              TARGET_PID) == VC_FTG_RESULT_OK);
    CHECK(vc_ftg_service_process_event(
              &fixture_value.service,
              VC_FTG_PROCESS_STARTED,
              TARGET_PID) == VC_FTG_RESULT_OK);
    init_status_request(&open_request);
    CHECK(vc_ftg_service_open_exact_fixture(
              &fixture_value.service,
              &open_request,
              &open_response) == VC_FTG_RESULT_OK);
    new_handle = open_response.handle;
    CHECK(new_handle != old_handle);
    init_read_request(
        &request, old_handle, 0u, 0u, 1u);
    CHECK(vc_ftg_service_read_fixture_segment(
              &fixture_value.service,
              &request,
              &response) == VC_FTG_RESULT_INVALID_HANDLE);
    init_read_request(
        &request, new_handle, 0u, 0u, 1u);
    CHECK(vc_ftg_service_read_fixture_segment(
              &fixture_value.service,
              &request,
              &response) == VC_FTG_RESULT_OK);
}

static void test_module_and_caller_revalidation(void)
{
    fixture fixture_value;
    vc_ftg_read_request request;
    vc_ftg_read_response response;
    uint64_t handle;

    initialize_fixture(&fixture_value, true);
    handle = start_and_open(&fixture_value);
    ++fixture_value.platform.target_module.process_module_id;
    init_read_request(&request, handle, 0u, 0u, 1u);
    CHECK(vc_ftg_service_read_fixture_segment(
              &fixture_value.service,
              &request,
              &response) == VC_FTG_RESULT_MODULE_MISMATCH);
    CHECK(!fixture_value.service.session.active);

    initialize_fixture(&fixture_value, true);
    handle = start_and_open(&fixture_value);
    fixture_value.platform.caller_process_id = OTHER_PID;
    init_read_request(&request, handle, 0u, 0u, 1u);
    CHECK(vc_ftg_service_read_fixture_segment(
              &fixture_value.service,
              &request,
              &response) ==
          VC_FTG_RESULT_CALLER_TITLE_MISMATCH);

    fixture_value.platform.caller_process_id =
        CONTROLLER_PID;
    fixture_value.platform.now_us +=
        VC_FTG_DEFAULT_TIMEOUT_US;
    CHECK(vc_ftg_service_read_fixture_segment(
              &fixture_value.service,
              &request,
              &response) == VC_FTG_RESULT_EXPIRED);

    initialize_fixture(&fixture_value, true);
    handle = start_and_open(&fixture_value);
    ++fixture_value.platform.target_module.module_fingerprint;
    init_read_request(&request, handle, 0u, 0u, 1u);
    CHECK(vc_ftg_service_read_fixture_segment(
              &fixture_value.service,
              &request,
              &response) == VC_FTG_RESULT_MODULE_MISMATCH);
    CHECK(!fixture_value.service.session.active);

    initialize_fixture(&fixture_value, true);
    handle = start_and_open(&fixture_value);
    fixture_value.platform.now_us = UINT64_C(999999);
    init_read_request(&request, handle, 0u, 0u, 1u);
    CHECK(vc_ftg_service_read_fixture_segment(
              &fixture_value.service,
              &request,
              &response) == VC_FTG_RESULT_EXPIRED);
}

static void test_duplicate_out_of_order_and_missed_events(void)
{
    fixture fixture_value;
    vc_ftg_status_request request;
    vc_ftg_status_response response;

    initialize_fixture(&fixture_value, true);
    CHECK(vc_ftg_service_process_event(
              &fixture_value.service,
              VC_FTG_PROCESS_STARTED,
              TARGET_PID) ==
          VC_FTG_RESULT_LIFECYCLE_COMPROMISED);
    init_status_request(&request);
    CHECK(vc_ftg_service_get_status(
              &fixture_value.service,
              &request,
              &response) == VC_FTG_RESULT_OK);
    CHECK(response.status ==
          VC_FTG_RUNTIME_FAIL_CLOSED);
    CHECK(response.diagnostic_stage ==
          VC_FTG_DIAGNOSTIC_OUT_OF_ORDER_EVENT);

    initialize_fixture(&fixture_value, true);
    CHECK(vc_ftg_service_process_event(
              &fixture_value.service,
              VC_FTG_PROCESS_CREATED,
              TARGET_PID) == VC_FTG_RESULT_OK);
    CHECK(vc_ftg_service_process_event(
              &fixture_value.service,
              VC_FTG_PROCESS_CREATED,
              TARGET_PID) ==
          VC_FTG_RESULT_LIFECYCLE_COMPROMISED);
    CHECK(vc_ftg_service_get_status(
              &fixture_value.service,
              &request,
              &response) == VC_FTG_RESULT_OK);
    CHECK(response.diagnostic_stage ==
          VC_FTG_DIAGNOSTIC_DUPLICATE_EVENT);
}

static void test_generation_and_revision_exhaustion(void)
{
    fixture fixture_value;

    initialize_fixture_with_sequences(
        &fixture_value,
        true,
        UINT64_MAX,
        UINT64_C(1),
        UINT64_C(1));
    CHECK(vc_ftg_service_process_event(
              &fixture_value.service,
              VC_FTG_PROCESS_CREATED,
              TARGET_PID) == VC_FTG_RESULT_OK);
    CHECK(vc_ftg_service_process_event(
              &fixture_value.service,
              VC_FTG_PROCESS_STARTED,
              TARGET_PID) == VC_FTG_RESULT_OK);
    fixture_value.platform.target_alive = false;
    CHECK(vc_ftg_service_process_event(
              &fixture_value.service,
              VC_FTG_PROCESS_EXITED,
              TARGET_PID) == VC_FTG_RESULT_OK);
    fixture_value.platform.target_alive = true;
    CHECK(vc_ftg_service_process_event(
              &fixture_value.service,
              VC_FTG_PROCESS_CREATED,
              TARGET_PID) == VC_FTG_RESULT_OK);
    CHECK(vc_ftg_service_process_event(
              &fixture_value.service,
              VC_FTG_PROCESS_STARTED,
              TARGET_PID) ==
          VC_FTG_RESULT_LIFECYCLE_COMPROMISED);
    CHECK(fixture_value.service.diagnostic_stage ==
          VC_FTG_DIAGNOSTIC_GENERATION_EXHAUSTED);

    initialize_fixture_with_sequences(
        &fixture_value,
        true,
        UINT64_C(1),
        UINT64_MAX,
        UINT64_C(1));
    CHECK(vc_ftg_service_process_event(
              &fixture_value.service,
              VC_FTG_PROCESS_CREATED,
              TARGET_PID) == VC_FTG_RESULT_OK);
    CHECK(vc_ftg_service_process_event(
              &fixture_value.service,
              VC_FTG_PROCESS_STARTED,
              TARGET_PID) ==
          VC_FTG_RESULT_LIFECYCLE_COMPROMISED);
    CHECK(fixture_value.service.diagnostic_stage ==
          VC_FTG_DIAGNOSTIC_REVISION_EXHAUSTED);
}

static void test_concurrent_exit_fails_closed(void)
{
    fixture fixture_value;
    vc_ftg_read_request request;
    vc_ftg_read_response response;
    unsigned int copy_to_before;
    uint64_t handle;

    initialize_fixture(&fixture_value, true);
    handle = start_and_open(&fixture_value);
    fixture_value.platform.exit_during_read = true;
    copy_to_before = fixture_value.platform.copy_to_calls;
    init_read_request(&request, handle, 0u, 0u, 64u);
    memset(&response, 0xa5, sizeof(response));
    CHECK(vc_ftg_service_read_fixture_segment(
              &fixture_value.service,
              &request,
              &response) ==
          VC_FTG_RESULT_LIFECYCLE_COMPROMISED);
    CHECK(fixture_value.platform.concurrent_event_result ==
          VC_FTG_RESULT_BUSY);
    CHECK(fixture_value.platform.copy_to_calls ==
          copy_to_before + 1u);
    CHECK(!fixture_value.service.session.active);
    {
        uint32_t byte;
        const uint8_t *bytes = (const uint8_t *)&response;

        for (byte = 0u; byte < sizeof(response); ++byte) {
            CHECK(bytes[byte] == 0u);
        }
    }

    initialize_fixture(&fixture_value, true);
    handle = start_and_open(&fixture_value);
    fixture_value.platform.exit_during_copy_to = true;
    init_read_request(&request, handle, 0u, 0u, 64u);
    memset(&response, 0xa5, sizeof(response));
    CHECK(vc_ftg_service_read_fixture_segment(
              &fixture_value.service,
              &request,
              &response) ==
          VC_FTG_RESULT_LIFECYCLE_COMPROMISED);
    CHECK(fixture_value.platform.concurrent_event_result ==
          VC_FTG_RESULT_BUSY);
    {
        uint32_t byte;

        const uint8_t *bytes = (const uint8_t *)&response;
        for (byte = 0u; byte < sizeof(response); ++byte) {
            CHECK(bytes[byte] == 0u);
        }
    }
}

static void test_malformed_abi_and_close(void)
{
    fixture fixture_value;
    vc_ftg_status_request status_request;
    vc_ftg_open_response open_response;
    vc_ftg_session_request close_request;
    vc_ftg_close_response close_response;
    vc_ftg_read_request read_request;
    vc_ftg_read_response read_response;
    uint64_t handle;

    initialize_fixture(&fixture_value, true);
    init_status_request(&status_request);
    status_request.struct_size = 0u;
    CHECK(vc_ftg_service_get_status(
              &fixture_value.service,
              &status_request,
              &open_response) ==
          VC_FTG_RESULT_INVALID_SIZE);
    init_status_request(&status_request);
    status_request.reserved0 = 1u;
    CHECK(vc_ftg_service_get_status(
              &fixture_value.service,
              &status_request,
              &open_response) ==
          VC_FTG_RESULT_RESERVED_NOT_ZERO);

    handle = start_and_open(&fixture_value);
    memset(&close_request, 0, sizeof(close_request));
    close_request.version = VC_FTG_ABI_VERSION;
    close_request.struct_size = sizeof(close_request);
    close_request.capabilities =
        VC_FTG_CAPABILITY_FOREIGN_SEGMENT_READ;
    close_request.handle = handle;
    CHECK(vc_ftg_service_close(
              &fixture_value.service,
              &close_request,
              &close_response) == VC_FTG_RESULT_OK);
    init_read_request(
        &read_request, handle, 0u, 0u, 1u);
    CHECK(vc_ftg_service_read_fixture_segment(
              &fixture_value.service,
              &read_request,
              &read_response) == VC_FTG_RESULT_INVALID_HANDLE);
}

static void test_unrelated_events_and_registration_failure(void)
{
    fixture fixture_value;
    vc_ftg_status_request request;
    vc_ftg_status_response response;

    initialize_fixture(&fixture_value, true);
    CHECK(vc_ftg_service_process_event(
              &fixture_value.service,
              VC_FTG_PROCESS_CREATED,
              OTHER_PID) == VC_FTG_RESULT_OK);
    CHECK(fixture_value.service.registry.state ==
          VC_FTG_TARGET_NONE);

    CHECK(vc_ftg_service_set_registered(
              &fixture_value.service,
              false,
              VC_FTG_DIAGNOSTIC_UNREGISTER_FAILED) ==
          VC_FTG_RESULT_OK);
    init_status_request(&request);
    CHECK(vc_ftg_service_get_status(
              &fixture_value.service,
              &request,
              &response) == VC_FTG_RESULT_OK);
    CHECK(response.status ==
          VC_FTG_RUNTIME_API_MISMATCH);
    CHECK(response.diagnostic_stage ==
          VC_FTG_DIAGNOSTIC_UNREGISTER_FAILED);

    initialize_fixture(&fixture_value, true);
    CHECK(vc_ftg_service_set_registered(
              &fixture_value.service,
              false,
              VC_FTG_DIAGNOSTIC_REGISTRATION_FAILED) ==
          VC_FTG_RESULT_OK);
    CHECK(vc_ftg_service_process_event(
              &fixture_value.service,
              VC_FTG_PROCESS_CREATED,
              TARGET_PID) ==
          VC_FTG_RESULT_LIFECYCLE_COMPROMISED);
    CHECK(vc_ftg_service_set_registered(
              &fixture_value.service,
              true,
              VC_FTG_DIAGNOSTIC_REGISTRATION_FAILED) ==
          VC_FTG_RESULT_OK);
    init_status_request(&request);
    CHECK(vc_ftg_service_get_status(
              &fixture_value.service,
              &request,
              &response) == VC_FTG_RESULT_OK);
    CHECK(response.status ==
          VC_FTG_RUNTIME_FAIL_CLOSED);
    CHECK(response.diagnostic_stage ==
          VC_FTG_DIAGNOSTIC_INITIAL_RECONCILIATION_UNPROVEN);
}

static void test_stop_rejects_stale_callbacks(void)
{
    fixture fixture_value;
    vc_ftg_status_request request;
    vc_ftg_status_response response;

    initialize_fixture(&fixture_value, true);
    (void)start_and_open(&fixture_value);
    CHECK(vc_ftg_service_stop(
              &fixture_value.service) == VC_FTG_RESULT_OK);
    CHECK(!fixture_value.service.session.active);
    CHECK(fixture_value.service.registry.state ==
          VC_FTG_TARGET_NONE);
    CHECK(vc_ftg_service_process_event(
              &fixture_value.service,
              VC_FTG_PROCESS_EXITED,
              TARGET_PID) == VC_FTG_RESULT_NOT_RUNNING);
    init_status_request(&request);
    CHECK(vc_ftg_service_get_status(
              &fixture_value.service,
              &request,
              &response) == VC_FTG_RESULT_OK);
    CHECK(response.status == VC_FTG_RUNTIME_STOPPED);
    CHECK(response.capabilities == 0u);
}

int main(void)
{
    test_diagnostic_only_status();
    test_startup_marker_cannot_authorize();
    test_lifecycle_and_exact_reads();
    test_exit_relaunch_and_reuse();
    test_module_and_caller_revalidation();
    test_duplicate_out_of_order_and_missed_events();
    test_generation_and_revision_exhaustion();
    test_concurrent_exit_fails_closed();
    test_malformed_abi_and_close();
    test_unrelated_events_and_registration_failure();
    test_stop_rejects_stale_callbacks();

    if (failures != 0) {
        fprintf(stderr, "%d foreign-target gate test(s) failed\n",
                failures);
        return 1;
    }
    puts("foreign-target gate tests passed");
    return 0;
}
