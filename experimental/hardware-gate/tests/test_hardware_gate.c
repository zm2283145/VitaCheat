#include "vitacheat/hardware_gate.h"

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

#define TEST_PID UINT32_C(0x10203)
#define TEST_KERNEL_MODULE_ID INT32_C(0x40506)
#define TEST_PROCESS_MODULE_ID INT32_C(0x70809)
#define TEST_FINGERPRINT UINT32_C(0x89abcdef)

typedef struct fake_platform {
    vc_hg_service *service;
    vc_hg_module_snapshot module;
    vc_hg_platform_diagnostic diagnostic;
    vc_hg_platform_diagnostic failure_diagnostic;
    uint8_t title_id[VC_HG_TITLE_ID_CAPACITY];
    uint8_t memory[256];
    uint64_t now_us;
    uint32_t process_id;
    unsigned int caller_calls;
    unsigned int time_calls;
    unsigned int title_calls;
    unsigned int module_calls;
    unsigned int copy_from_calls;
    unsigned int copy_to_calls;
    unsigned int read_calls;
    vc_hg_result reentry_result;
    bool caller_ok;
    bool time_ok;
    bool title_ok;
    bool module_ok;
    bool copy_from_ok;
    bool copy_to_ok;
    bool read_ok;
    bool mutate_module_after_read;
    bool mutate_process_module_id_after_read;
    bool reenter_during_read;
} fake_platform;

typedef struct fixture {
    vc_hg_service service;
    vc_hg_config config;
    vc_hg_dependencies dependencies;
    fake_platform platform;
} fixture;

static bool fake_get_caller_pid(
    void *context,
    uint32_t *process_id)
{
    fake_platform *platform = (fake_platform *)context;

    ++platform->caller_calls;
    memset(&platform->diagnostic, 0,
           sizeof(platform->diagnostic));
    if (!platform->caller_ok) {
        platform->diagnostic =
            platform->failure_diagnostic;
        return false;
    }
    *process_id = platform->process_id;
    return true;
}

static bool fake_get_time_us(void *context, uint64_t *now_us)
{
    fake_platform *platform = (fake_platform *)context;

    ++platform->time_calls;
    if (!platform->time_ok) {
        platform->diagnostic =
            platform->failure_diagnostic;
        return false;
    }
    *now_us = platform->now_us;
    return true;
}

static bool fake_get_title_id(
    void *context,
    uint32_t process_id,
    uint8_t title_id[VC_HG_TITLE_ID_CAPACITY])
{
    fake_platform *platform = (fake_platform *)context;

    ++platform->title_calls;
    if (!platform->title_ok ||
        process_id != platform->process_id) {
        platform->diagnostic =
            platform->failure_diagnostic;
        return false;
    }
    memcpy(title_id, platform->title_id,
           VC_HG_TITLE_ID_CAPACITY);
    return true;
}

static bool fake_get_main_module(
    void *context,
    uint32_t process_id,
    vc_hg_module_snapshot *module)
{
    fake_platform *platform = (fake_platform *)context;

    ++platform->module_calls;
    if (!platform->module_ok ||
        process_id != platform->process_id) {
        platform->diagnostic =
            platform->failure_diagnostic;
        return false;
    }
    *module = platform->module;
    module->process_id = process_id;
    return true;
}

static bool fake_get_diagnostic(
    void *context,
    vc_hg_platform_diagnostic *diagnostic)
{
    fake_platform *platform = (fake_platform *)context;

    *diagnostic = platform->diagnostic;
    return true;
}

static bool fake_copy_from_user(
    void *context,
    uint32_t process_id,
    void *destination,
    const void *user_source,
    size_t size)
{
    fake_platform *platform = (fake_platform *)context;

    ++platform->copy_from_calls;
    if (!platform->copy_from_ok ||
        process_id != platform->process_id ||
        user_source == (const void *)(uintptr_t)1u) {
        platform->diagnostic =
            platform->failure_diagnostic;
        return false;
    }
    memcpy(destination, user_source, size);
    return true;
}

static bool fake_copy_to_user(
    void *context,
    uint32_t process_id,
    void *user_destination,
    const void *source,
    size_t size)
{
    fake_platform *platform = (fake_platform *)context;

    ++platform->copy_to_calls;
    if (!platform->copy_to_ok ||
        process_id != platform->process_id ||
        user_destination == (void *)(uintptr_t)1u) {
        platform->diagnostic =
            platform->failure_diagnostic;
        return false;
    }
    memcpy(user_destination, source, size);
    return true;
}

static bool fake_read_process(
    void *context,
    uint32_t process_id,
    void *destination,
    uintptr_t source_address,
    size_t size)
{
    fake_platform *platform = (fake_platform *)context;
    uintptr_t begin = (uintptr_t)platform->memory;
    uintptr_t end = begin + sizeof(platform->memory);

    ++platform->read_calls;
    if (platform->reenter_during_read) {
        vc_hg_status_request request;
        vc_hg_status_response response;

        memset(&request, 0, sizeof(request));
        request.version = VC_HG_ABI_VERSION;
        request.struct_size = sizeof(request);
        platform->reentry_result =
            vc_hg_service_get_status(
                platform->service, &request, &response);
    }
    if (platform->mutate_module_after_read) {
        ++platform->module.module_fingerprint;
    }
    if (platform->mutate_process_module_id_after_read) {
        ++platform->module.process_module_id;
    }
    if (!platform->read_ok ||
        process_id != platform->process_id ||
        source_address < begin ||
        source_address > end ||
        size > (size_t)(end - source_address)) {
        return false;
    }
    memcpy(
        destination, (const void *)source_address, size);
    return true;
}

static void initialize_fixture(
    fixture *fixture_value,
    bool enabled,
    bool api_available)
{
    size_t index;

    memset(fixture_value, 0, sizeof(*fixture_value));
    memcpy(fixture_value->config.expected_title_id,
           "VCHG00001", sizeof("VCHG00001"));
    memcpy(fixture_value->config.expected_module_name,
           "VitaCheatHgClient",
           sizeof("VitaCheatHgClient"));
    fixture_value->config.timeout_us =
        VC_HG_DEFAULT_TIMEOUT_US;
    fixture_value->config.enabled = enabled;
    fixture_value->config.api_available = api_available;

    fixture_value->platform.service =
        &fixture_value->service;
    fixture_value->platform.now_us = UINT64_C(1000000);
    fixture_value->platform.process_id = TEST_PID;
    fixture_value->platform.caller_ok = true;
    fixture_value->platform.time_ok = true;
    fixture_value->platform.title_ok = true;
    fixture_value->platform.module_ok = true;
    fixture_value->platform.failure_diagnostic.stage =
        VC_HG_DIAGNOSTIC_MODULE_ID;
    fixture_value->platform.failure_diagnostic.raw_result =
        (int32_t)UINT32_C(0x8002000e);
    fixture_value->platform.copy_from_ok = true;
    fixture_value->platform.copy_to_ok = true;
    fixture_value->platform.read_ok = true;
    memcpy(fixture_value->platform.title_id,
           "VCHG00001", sizeof("VCHG00001"));
    fixture_value->platform.module.process_id = TEST_PID;
    fixture_value->platform.module.kernel_module_id =
        TEST_KERNEL_MODULE_ID;
    fixture_value->platform.module.process_module_id =
        TEST_PROCESS_MODULE_ID;
    fixture_value->platform.module.module_fingerprint =
        TEST_FINGERPRINT;
    fixture_value->platform.module.segment_count = 2u;
    memcpy(fixture_value->platform.module.module_name,
           "VitaCheatHgClient",
           sizeof("VitaCheatHgClient"));
    fixture_value->platform.module.segments[0].base =
        (uintptr_t)fixture_value->platform.memory;
    fixture_value->platform.module.segments[0].size = 128u;
    fixture_value->platform.module.segments[0].permissions =
        VC_HG_PERMISSION_USER_READ | UINT32_C(0x04);
    fixture_value->platform.module.segments[1].base =
        (uintptr_t)(fixture_value->platform.memory + 128u);
    fixture_value->platform.module.segments[1].size = 128u;
    fixture_value->platform.module.segments[1].permissions =
        VC_HG_PERMISSION_USER_READ | UINT32_C(0x02);
    for (index = 0;
         index < sizeof(fixture_value->platform.memory);
         ++index) {
        fixture_value->platform.memory[index] =
            (uint8_t)(index ^ 0x5au);
    }

    fixture_value->dependencies.get_caller_pid =
        fake_get_caller_pid;
    fixture_value->dependencies.get_time_us =
        fake_get_time_us;
    fixture_value->dependencies.get_title_id =
        fake_get_title_id;
    fixture_value->dependencies.get_main_module =
        fake_get_main_module;
    fixture_value->dependencies.get_diagnostic =
        fake_get_diagnostic;
    fixture_value->dependencies.copy_from_user =
        fake_copy_from_user;
    fixture_value->dependencies.copy_to_user =
        fake_copy_to_user;
    fixture_value->dependencies.read_process =
        fake_read_process;
    fixture_value->dependencies.context =
        &fixture_value->platform;
    CHECK(vc_hg_service_init(
              &fixture_value->service,
              &fixture_value->config,
              &fixture_value->dependencies,
              UINT64_C(0x1020304050607080)) ==
          VC_HG_RESULT_OK);
    CHECK(vc_hg_service_start(
              &fixture_value->service) ==
          VC_HG_RESULT_OK);
}

static void init_status_request(vc_hg_status_request *request)
{
    memset(request, 0, sizeof(*request));
    request->version = VC_HG_ABI_VERSION;
    request->struct_size = sizeof(*request);
}

static void init_session_request(
    vc_hg_session_request *request,
    uint64_t handle)
{
    memset(request, 0, sizeof(*request));
    request->version = VC_HG_ABI_VERSION;
    request->struct_size = sizeof(*request);
    request->capabilities =
        VC_HG_CAPABILITY_SELF_SEGMENT_READ;
    request->handle = handle;
}

static void init_read_request(
    vc_hg_read_request *request,
    uint64_t handle,
    uint32_t segment_index,
    uint32_t offset,
    uint32_t length)
{
    memset(request, 0, sizeof(*request));
    request->version = VC_HG_ABI_VERSION;
    request->struct_size = sizeof(*request);
    request->capabilities =
        VC_HG_CAPABILITY_SELF_SEGMENT_READ;
    request->handle = handle;
    request->segment_index = segment_index;
    request->offset = offset;
    request->length = length;
}

static uint64_t open_session(fixture *fixture_value)
{
    vc_hg_open_request request;
    vc_hg_open_response response;

    init_status_request(&request);
    memset(&response, 0, sizeof(response));
    CHECK(vc_hg_service_open_self(
              &fixture_value->service,
              &request, &response) ==
          VC_HG_RESULT_OK);
    CHECK(response.version == VC_HG_ABI_VERSION);
    CHECK(response.struct_size == sizeof(response));
    CHECK(response.status == VC_HG_RESULT_OK);
    CHECK(response.capabilities ==
          VC_HG_CAPABILITY_SELF_SEGMENT_READ);
    CHECK(response.handle != 0u);
    CHECK(response.segment_count == 2u);
    CHECK(response.module_fingerprint ==
          fixture_value->platform.module
              .module_fingerprint);
    return response.handle;
}

static void test_status_modes(void)
{
    fixture fixture_value;
    vc_hg_status_request request;
    vc_hg_status_response response;

    initialize_fixture(&fixture_value, false, true);
    init_status_request(&request);
    CHECK(vc_hg_service_get_status(
              &fixture_value.service,
              &request, &response) ==
          VC_HG_RESULT_OK);
    CHECK(response.status == VC_HG_RUNTIME_DISABLED);
    CHECK(response.capabilities == 0u);
    CHECK(vc_hg_service_open_self(
              &fixture_value.service,
              &request, &response) ==
          VC_HG_RESULT_DISABLED);

    initialize_fixture(&fixture_value, true, false);
    CHECK(vc_hg_service_get_status(
              &fixture_value.service,
              &request, &response) ==
          VC_HG_RESULT_OK);
    CHECK(response.status ==
          VC_HG_RUNTIME_API_MISMATCH);
    CHECK(response.capabilities == 0u);
    CHECK(vc_hg_service_open_self(
              &fixture_value.service,
              &request, &response) ==
          VC_HG_RESULT_PLATFORM_FAILURE);

    initialize_fixture(&fixture_value, true, true);
    fixture_value.platform.module_ok = false;
    CHECK(vc_hg_service_open_self(
              &fixture_value.service,
              &request, &response) ==
          VC_HG_RESULT_MODULE_UNAVAILABLE);
    CHECK(vc_hg_service_get_status(
              &fixture_value.service,
              &request, &response) ==
          VC_HG_RESULT_OK);
    CHECK(response.status == VC_HG_RUNTIME_UNAVAILABLE);
    CHECK(response.last_result ==
          VC_HG_RESULT_MODULE_UNAVAILABLE);
}

static void test_diagnostic_status_and_syscall_decode(void)
{
    static const uint32_t stages[] = {
        VC_HG_DIAGNOSTIC_CALLER_PID,
        VC_HG_DIAGNOSTIC_REQUEST_COPY,
        VC_HG_DIAGNOSTIC_SYSTEM_TIME,
        VC_HG_DIAGNOSTIC_TITLE_QUERY,
        VC_HG_DIAGNOSTIC_TITLE_NORMALIZE,
        VC_HG_DIAGNOSTIC_MODULE_ID,
        VC_HG_DIAGNOSTIC_MODULE_INFO,
        VC_HG_DIAGNOSTIC_MODULE_ID_MISMATCH,
        VC_HG_DIAGNOSTIC_MODULE_FINGERPRINT,
        VC_HG_DIAGNOSTIC_MODULE_FINGERPRINT_ZERO,
        VC_HG_DIAGNOSTIC_MODULE_NAME,
        VC_HG_DIAGNOSTIC_MODULE_SEGMENTS,
        VC_HG_DIAGNOSTIC_RESPONSE_COPY,
        VC_HG_DIAGNOSTIC_MODULE_PROCESS_UID
    };
    fixture fixture_value;
    vc_hg_status_request request;
    vc_hg_open_response open_response;
    vc_hg_status_response_v2 response;
    struct {
        vc_hg_status_response response;
        uint32_t guard;
    } v1;
    size_t index;
    int32_t gate_result;

    for (gate_result = VC_HG_RESULT_STOPPED;
         gate_result < VC_HG_RESULT_OK;
         ++gate_result) {
        const int32_t syscall_result =
            (int32_t)((uint32_t)gate_result &
                      ~UINT32_C(0x40000000));

        CHECK(vc_hg_decode_syscall_result(
                  syscall_result) == gate_result);
    }
    CHECK(vc_hg_decode_syscall_result(0) == 0);
    CHECK(vc_hg_decode_syscall_result(
              (int32_t)UINT32_C(0x80020010)) ==
          (int32_t)UINT32_C(0x80020010));

    for (index = 0;
         index < sizeof(stages) / sizeof(stages[0]);
         ++index) {
        initialize_fixture(&fixture_value, true, true);
        fixture_value.platform.failure_diagnostic.stage =
            stages[index];
        fixture_value.platform.failure_diagnostic.raw_result =
            (int32_t)(UINT32_C(0x80020100) +
                      (uint32_t)index);
        switch (stages[index]) {
        case VC_HG_DIAGNOSTIC_CALLER_PID:
            fixture_value.platform.caller_ok = false;
            break;
        case VC_HG_DIAGNOSTIC_REQUEST_COPY:
            fixture_value.platform.copy_from_ok = false;
            break;
        case VC_HG_DIAGNOSTIC_SYSTEM_TIME:
            fixture_value.platform.time_ok = false;
            break;
        case VC_HG_DIAGNOSTIC_TITLE_QUERY:
        case VC_HG_DIAGNOSTIC_TITLE_NORMALIZE:
            fixture_value.platform.title_ok = false;
            break;
        case VC_HG_DIAGNOSTIC_RESPONSE_COPY:
            fixture_value.platform.copy_to_ok = false;
            break;
        default:
            fixture_value.platform.module_ok = false;
            break;
        }
        init_status_request(&request);
        gate_result = vc_hg_service_open_self(
            &fixture_value.service,
            &request, &open_response);
        CHECK(gate_result != VC_HG_RESULT_OK);

        fixture_value.platform.caller_ok = true;
        fixture_value.platform.time_ok = true;
        fixture_value.platform.title_ok = true;
        fixture_value.platform.module_ok = true;
        fixture_value.platform.copy_from_ok = true;
        fixture_value.platform.copy_to_ok = true;
        memset(&response, 0xa5, sizeof(response));
        request.version =
            VC_HG_STATUS_DIAGNOSTIC_VERSION;
        CHECK(vc_hg_service_get_status(
                  &fixture_value.service,
                  &request, &response) ==
              VC_HG_RESULT_OK);
        CHECK(response.base.version ==
              VC_HG_STATUS_DIAGNOSTIC_VERSION);
        CHECK(response.base.struct_size ==
              sizeof(response));
        CHECK(response.base.last_result ==
              gate_result);
        CHECK(response.diagnostic_stage ==
              stages[index]);
        CHECK(response.diagnostic_raw_result ==
              (int32_t)(UINT32_C(0x80020100) +
                        (uint32_t)index));
        CHECK(response.reserved0 == 0u);
        CHECK(response.reserved1 == 0u);
    }

    init_status_request(&request);
    memset(&v1, 0, sizeof(v1));
    v1.guard = UINT32_C(0x5a5aa5a5);
    CHECK(vc_hg_service_get_status(
              &fixture_value.service,
              &request, &v1.response) ==
          VC_HG_RESULT_OK);
    CHECK(v1.response.version == VC_HG_ABI_VERSION);
    CHECK(v1.response.struct_size ==
          sizeof(v1.response));
    CHECK(v1.guard == UINT32_C(0x5a5aa5a5));
}

static void test_open_and_module(void)
{
    fixture fixture_value;
    vc_hg_open_request open_request;
    vc_hg_open_response open_response;
    vc_hg_session_request session_request;
    vc_hg_module_response module_response;
    uint64_t handle;

    initialize_fixture(&fixture_value, true, true);
    handle = open_session(&fixture_value);
    CHECK(fixture_value.service.session.module
              .kernel_module_id == TEST_KERNEL_MODULE_ID);
    CHECK(fixture_value.service.session.module
              .process_module_id ==
          TEST_PROCESS_MODULE_ID);
    CHECK(fixture_value.platform.copy_from_calls == 1u);
    CHECK(fixture_value.platform.copy_to_calls == 1u);
    CHECK(fixture_value.platform.title_calls == 1u);
    CHECK(fixture_value.platform.module_calls == 1u);

    init_session_request(&session_request, handle);
    memset(&module_response, 0, sizeof(module_response));
    CHECK(vc_hg_service_get_self_main_module(
              &fixture_value.service,
              &session_request, &module_response) ==
          VC_HG_RESULT_OK);
    CHECK(module_response.struct_size ==
          sizeof(module_response));
    CHECK(module_response.handle == handle);
    CHECK(module_response.module_name_size ==
          sizeof("VitaCheatHgClient") - 1u);
    CHECK(memcmp(
              module_response.module_name,
              "VitaCheatHgClient",
              sizeof("VitaCheatHgClient")) == 0);
    CHECK(module_response.segment_count == 2u);
    CHECK(module_response.segments[0].segment_index == 0u);
    CHECK(module_response.segments[0].size == 128u);
    CHECK(module_response.segments[1].segment_index == 1u);
    CHECK(module_response.segments[1].size == 128u);

    init_status_request(&open_request);
    CHECK(vc_hg_service_open_self(
              &fixture_value.service,
              &open_request, &open_response) ==
          VC_HG_RESULT_SESSION_ACTIVE);

    initialize_fixture(&fixture_value, true, true);
    fixture_value.platform.module.process_module_id =
        fixture_value.platform.module.kernel_module_id;
    (void)open_session(&fixture_value);
}

static void test_exact_reads_and_ranges(void)
{
    fixture fixture_value;
    vc_hg_read_request request;
    vc_hg_read_response response;
    uint64_t handle;
    uint32_t lengths[] = {1u, 63u, 64u};
    size_t index;

    initialize_fixture(&fixture_value, true, true);
    handle = open_session(&fixture_value);
    for (index = 0;
         index < sizeof(lengths) / sizeof(lengths[0]);
         ++index) {
        unsigned int copy_from_before =
            fixture_value.platform.copy_from_calls;
        unsigned int copy_to_before =
            fixture_value.platform.copy_to_calls;
        unsigned int read_before =
            fixture_value.platform.read_calls;

        init_read_request(
            &request, handle, 0u, 7u, lengths[index]);
        memset(&response, 0xa5, sizeof(response));
        CHECK(vc_hg_service_read_self_segment(
                  &fixture_value.service,
                  &request, &response) ==
              VC_HG_RESULT_OK);
        CHECK(response.version == VC_HG_ABI_VERSION);
        CHECK(response.struct_size == sizeof(response));
        CHECK(response.length == lengths[index]);
        CHECK(memcmp(
                  response.bytes,
                  fixture_value.platform.memory + 7u,
                  lengths[index]) == 0);
        CHECK(response.bytes[VC_HG_MAX_READ - 1u] ==
              (lengths[index] == VC_HG_MAX_READ
                   ? fixture_value.platform
                         .memory[7u + VC_HG_MAX_READ - 1u]
                   : 0u));
        CHECK(fixture_value.platform.copy_from_calls ==
              copy_from_before + 1u);
        CHECK(fixture_value.platform.copy_to_calls ==
              copy_to_before + 1u);
        CHECK(fixture_value.platform.read_calls ==
              read_before + 1u);
    }

    init_read_request(&request, handle, 0u, 0u, 0u);
    CHECK(vc_hg_service_read_self_segment(
              &fixture_value.service,
              &request, &response) ==
          VC_HG_RESULT_RANGE_DENIED);
    init_read_request(&request, handle, 0u, 0u, 65u);
    CHECK(vc_hg_service_read_self_segment(
              &fixture_value.service,
              &request, &response) ==
          VC_HG_RESULT_RANGE_DENIED);
    init_read_request(&request, handle, 4u, 0u, 1u);
    CHECK(vc_hg_service_read_self_segment(
              &fixture_value.service,
              &request, &response) ==
          VC_HG_RESULT_INVALID_SEGMENT);
    init_read_request(
        &request, handle, 0u, UINT32_MAX, 64u);
    CHECK(vc_hg_service_read_self_segment(
              &fixture_value.service,
              &request, &response) ==
          VC_HG_RESULT_RANGE_DENIED);
    init_read_request(&request, handle, 0u, 127u, 1u);
    CHECK(vc_hg_service_read_self_segment(
              &fixture_value.service,
              &request, &response) ==
          VC_HG_RESULT_OK);
    init_read_request(&request, handle, 0u, 128u, 1u);
    CHECK(vc_hg_service_read_self_segment(
              &fixture_value.service,
              &request, &response) ==
          VC_HG_RESULT_RANGE_DENIED);

    fixture_value.platform.module.segments[0].permissions = 0u;
    init_read_request(&request, handle, 0u, 0u, 1u);
    CHECK(vc_hg_service_read_self_segment(
              &fixture_value.service,
              &request, &response) ==
          VC_HG_RESULT_MODULE_MISMATCH);

    initialize_fixture(&fixture_value, true, true);
    fixture_value.platform.module
        .segments[0].permissions = 0u;
    handle = open_session(&fixture_value);
    init_read_request(&request, handle, 0u, 0u, 1u);
    CHECK(vc_hg_service_read_self_segment(
              &fixture_value.service,
              &request, &response) ==
          VC_HG_RESULT_PERMISSION_DENIED);
}

static void test_malformed_requests(void)
{
    fixture fixture_value;
    vc_hg_status_request status_request;
    vc_hg_status_response status_response;
    vc_hg_read_request read_request;
    vc_hg_read_response read_response;
    uint64_t handle;

    initialize_fixture(&fixture_value, true, true);
    init_status_request(&status_request);
    status_request.version = 3u;
    CHECK(vc_hg_service_get_status(
              &fixture_value.service,
              &status_request, &status_response) ==
          VC_HG_RESULT_INVALID_VERSION);
    init_status_request(&status_request);
    status_request.version =
        VC_HG_STATUS_DIAGNOSTIC_VERSION;
    CHECK(vc_hg_service_open_self(
              &fixture_value.service,
              &status_request, &status_response) ==
          VC_HG_RESULT_INVALID_VERSION);
    init_status_request(&status_request);
    --status_request.struct_size;
    CHECK(vc_hg_service_get_status(
              &fixture_value.service,
              &status_request, &status_response) ==
          VC_HG_RESULT_INVALID_SIZE);
    init_status_request(&status_request);
    status_request.capabilities = 1u;
    CHECK(vc_hg_service_get_status(
              &fixture_value.service,
              &status_request, &status_response) ==
          VC_HG_RESULT_CAPABILITY_MISMATCH);
    init_status_request(&status_request);
    status_request.reserved1 = 1u;
    CHECK(vc_hg_service_get_status(
              &fixture_value.service,
              &status_request, &status_response) ==
          VC_HG_RESULT_RESERVED_NOT_ZERO);

    handle = open_session(&fixture_value);
    init_read_request(&read_request, handle, 0u, 0u, 1u);
    read_request.version = 2u;
    CHECK(vc_hg_service_read_self_segment(
              &fixture_value.service,
              &read_request, &read_response) ==
          VC_HG_RESULT_INVALID_VERSION);
    init_read_request(&read_request, handle, 0u, 0u, 1u);
    --read_request.struct_size;
    CHECK(vc_hg_service_read_self_segment(
              &fixture_value.service,
              &read_request, &read_response) ==
          VC_HG_RESULT_INVALID_SIZE);
    init_read_request(&read_request, handle, 0u, 0u, 1u);
    read_request.capabilities = 0u;
    CHECK(vc_hg_service_read_self_segment(
              &fixture_value.service,
              &read_request, &read_response) ==
          VC_HG_RESULT_CAPABILITY_MISMATCH);
    init_read_request(&read_request, handle, 0u, 0u, 1u);
    read_request.reserved0 = 1u;
    CHECK(vc_hg_service_read_self_segment(
              &fixture_value.service,
              &read_request, &read_response) ==
          VC_HG_RESULT_RESERVED_NOT_ZERO);
    init_read_request(&read_request, 0u, 0u, 0u, 1u);
    CHECK(vc_hg_service_read_self_segment(
              &fixture_value.service,
              &read_request, &read_response) ==
          VC_HG_RESULT_INVALID_HANDLE);
}

static void test_faults_lifecycle_and_identity(void)
{
    fixture fixture_value;
    vc_hg_read_request read_request;
    vc_hg_read_response read_response;
    vc_hg_session_request session_request;
    vc_hg_close_response close_response;
    vc_hg_module_response module_response;
    uint64_t first_handle;
    uint64_t second_handle;

    initialize_fixture(&fixture_value, true, true);
    first_handle = open_session(&fixture_value);
    init_read_request(
        &read_request, first_handle, 0u, 0u, 1u);
    CHECK(vc_hg_service_read_self_segment(
              &fixture_value.service,
              &read_request,
              (void *)(uintptr_t)1u) ==
          VC_HG_RESULT_COPY_TO_USER_FAILED);
    fixture_value.platform.read_ok = false;
    memset(&read_response, 0xa5, sizeof(read_response));
    CHECK(vc_hg_service_read_self_segment(
              &fixture_value.service,
              &read_request, &read_response) ==
          VC_HG_RESULT_PLATFORM_FAILURE);
    CHECK(read_response.bytes[0] == 0xa5u);
    fixture_value.platform.read_ok = true;

    fixture_value.platform.reenter_during_read = true;
    CHECK(vc_hg_service_read_self_segment(
              &fixture_value.service,
              &read_request, &read_response) ==
          VC_HG_RESULT_OK);
    CHECK(fixture_value.platform.reentry_result ==
          VC_HG_RESULT_BUSY);
    fixture_value.platform.reenter_during_read = false;

    init_session_request(
        &session_request, first_handle);
    CHECK(vc_hg_service_close(
              &fixture_value.service,
              &session_request, &close_response) ==
          VC_HG_RESULT_OK);
    CHECK(vc_hg_service_read_self_segment(
              &fixture_value.service,
              &read_request, &read_response) ==
          VC_HG_RESULT_INVALID_HANDLE);
    second_handle = open_session(&fixture_value);
    CHECK(second_handle != first_handle);
    CHECK(vc_hg_service_get_self_main_module(
              &fixture_value.service,
              &session_request, &module_response) ==
          VC_HG_RESULT_INVALID_HANDLE);

    init_read_request(
        &read_request, second_handle, 0u, 0u, 1u);
    fixture_value.platform.now_us +=
        VC_HG_DEFAULT_TIMEOUT_US;
    CHECK(vc_hg_service_read_self_segment(
              &fixture_value.service,
              &read_request, &read_response) ==
          VC_HG_RESULT_EXPIRED);
    CHECK(!fixture_value.service.session.active);

    first_handle = open_session(&fixture_value);
    init_read_request(
        &read_request, first_handle, 0u, 0u, 1u);
    fixture_value.platform.mutate_module_after_read = true;
    CHECK(vc_hg_service_read_self_segment(
              &fixture_value.service,
              &read_request, &read_response) ==
          VC_HG_RESULT_MODULE_MISMATCH);
    CHECK(!fixture_value.service.session.active);

    fixture_value.platform.mutate_module_after_read = false;
    initialize_fixture(&fixture_value, true, true);
    first_handle = open_session(&fixture_value);
    init_read_request(
        &read_request, first_handle, 0u, 0u, 1u);
    ++fixture_value.platform.module.kernel_module_id;
    CHECK(vc_hg_service_read_self_segment(
              &fixture_value.service,
              &read_request, &read_response) ==
          VC_HG_RESULT_MODULE_MISMATCH);
    CHECK(!fixture_value.service.session.active);

    initialize_fixture(&fixture_value, true, true);
    first_handle = open_session(&fixture_value);
    init_read_request(
        &read_request, first_handle, 0u, 0u, 1u);
    fixture_value.platform
        .mutate_process_module_id_after_read = true;
    CHECK(vc_hg_service_read_self_segment(
              &fixture_value.service,
              &read_request, &read_response) ==
          VC_HG_RESULT_MODULE_MISMATCH);
    CHECK(!fixture_value.service.session.active);

    initialize_fixture(&fixture_value, true, true);
    first_handle = open_session(&fixture_value);
    init_read_request(
        &read_request, first_handle, 0u, 0u, 1u);
    fixture_value.platform.process_id = TEST_PID + 1u;
    CHECK(vc_hg_service_read_self_segment(
              &fixture_value.service,
              &read_request, &read_response) ==
          VC_HG_RESULT_CALLER_MISMATCH);
    CHECK(!fixture_value.service.session.active);
}

static void test_fail_closed_inputs_and_stop(void)
{
    fixture fixture_value;
    vc_hg_status_request request;
    vc_hg_status_response response;

    initialize_fixture(&fixture_value, true, true);
    init_status_request(&request);
    fixture_value.platform.title_id[0] = 'X';
    CHECK(vc_hg_service_open_self(
              &fixture_value.service,
              &request, &response) ==
          VC_HG_RESULT_TITLE_MISMATCH);
    fixture_value.platform.title_id[0] = 'V';
    fixture_value.platform.module.module_name[0] = 'X';
    CHECK(vc_hg_service_open_self(
              &fixture_value.service,
              &request, &response) ==
          VC_HG_RESULT_MODULE_MISMATCH);
    fixture_value.platform.module.module_name[0] = 'V';
    fixture_value.platform.module.kernel_module_id = 0;
    CHECK(vc_hg_service_open_self(
              &fixture_value.service,
              &request, &response) ==
          VC_HG_RESULT_MODULE_MISMATCH);
    fixture_value.platform.module.kernel_module_id =
        TEST_KERNEL_MODULE_ID;
    fixture_value.platform.module.process_module_id = 0;
    CHECK(vc_hg_service_open_self(
              &fixture_value.service,
              &request, &response) ==
          VC_HG_RESULT_MODULE_MISMATCH);
    fixture_value.platform.module.process_module_id =
        TEST_PROCESS_MODULE_ID;
    fixture_value.platform.copy_from_ok = false;
    CHECK(vc_hg_service_open_self(
              &fixture_value.service,
              &request, &response) ==
          VC_HG_RESULT_COPY_FROM_USER_FAILED);
    fixture_value.platform.copy_from_ok = true;
    fixture_value.platform.copy_to_ok = false;
    CHECK(vc_hg_service_open_self(
              &fixture_value.service,
              &request, &response) ==
          VC_HG_RESULT_COPY_TO_USER_FAILED);
    CHECK(!fixture_value.service.session.active);
    fixture_value.platform.copy_to_ok = true;
    (void)open_session(&fixture_value);
    CHECK(vc_hg_service_stop(
              &fixture_value.service) ==
          VC_HG_RESULT_OK);
    CHECK(!fixture_value.service.session.active);
    CHECK(vc_hg_service_open_self(
              &fixture_value.service,
              &request, &response) ==
          VC_HG_RESULT_STOPPED);
    CHECK(vc_hg_service_get_status(
              &fixture_value.service,
              &request, &response) ==
          VC_HG_RESULT_OK);
    CHECK(response.status == VC_HG_RUNTIME_STOPPED);
}

int main(void)
{
    test_status_modes();
    test_diagnostic_status_and_syscall_decode();
    test_open_and_module();
    test_exact_reads_and_ranges();
    test_malformed_requests();
    test_faults_lifecycle_and_identity();
    test_fail_closed_inputs_and_stop();

    if (failures != 0) {
        fprintf(stderr, "%d hardware-gate test(s) failed\n",
                failures);
        return 1;
    }
    puts("hardware-gate tests passed");
    return 0;
}
