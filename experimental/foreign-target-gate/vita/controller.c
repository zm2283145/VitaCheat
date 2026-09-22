#include "fixture_protocol.h"
#include "vitacheat/foreign_target_gate.h"
#include "vitacheat/foreign_target_startup.h"

#include <psp2/appmgr.h>
#include <psp2/io/fcntl.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/threadmgr.h>

#include <stdbool.h>
#include <stdint.h>

#define memcmp sceClibMemcmp
#define memcpy sceClibMemcpy
#define memset sceClibMemset
#define snprintf sceClibSnprintf

#ifndef VITACHEAT_FOREIGN_GATE_TARGET_TITLE_ID
#error "The foreign-target controller requires a target title ID"
#endif

#define VC_FTG_MAX_TEST_RESULTS 32u

static const uint8_t g_expected_sentinel[VC_FTG_MAX_READ] =
    VC_FTG_SENTINEL_BYTES;

typedef struct test_result {
    const char *name;
    int code;
    bool passed;
} test_result;

typedef struct controller_state {
    test_result results[VC_FTG_MAX_TEST_RESULTS];
    uint32_t result_count;
    uint32_t passed;
    uint32_t failed;
    uint64_t run_id;
} controller_state;

static bool write_all(
    SceUID fd,
    const void *buffer,
    size_t size)
{
    const uint8_t *bytes = (const uint8_t *)buffer;

    while (size != 0u) {
        const SceSSize written = sceIoWrite(
            fd, bytes, (SceSize)size);

        if (written <= 0) {
            return false;
        }
        bytes += written;
        size -= (size_t)written;
    }
    return true;
}

static void write_results(const controller_state *state)
{
    char line[320];
    SceUID fd;
    uint32_t index;
    int size;

    fd = sceIoOpen(
        VC_FTG_CONTROLLER_RESULT_PATH,
        SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC,
        0666);
    if (fd < 0) {
        return;
    }
    size = snprintf(
        line,
        sizeof(line),
        "{\n"
        "  \"schema\":\"" VC_FTG_CONTROLLER_RESULT_SCHEMA "\",\n"
        "  \"build_id\":\"" VC_FTG_CONTROLLER_RESULT_BUILD_ID "\",\n"
        "  \"run_id\":\"%08x%08x\",\n"
        "  \"target_title_id\":\"%s\",\n"
        "  \"controller_title_id\":\"VCFC00001\",\n"
        "  \"target_firmware\":\"3.65\",\n"
        "  \"scope\":\"source-owned-foreign-generation-read\",\n"
        "  \"tests\":[\n",
        (unsigned int)(state->run_id >> 32u),
        (unsigned int)state->run_id,
        VITACHEAT_FOREIGN_GATE_TARGET_TITLE_ID);
    if (size > 0 && (size_t)size < sizeof(line)) {
        (void)write_all(fd, line, (size_t)size);
    }
    for (index = 0u; index < state->result_count; ++index) {
        size = snprintf(
            line,
            sizeof(line),
            "%s    {\"name\":\"%s\",\"result\":\"%s\","
            "\"code\":%d}",
            index == 0u ? "" : ",\n",
            state->results[index].name,
            state->results[index].passed ? "pass" : "fail",
            state->results[index].code);
        if (size > 0 && (size_t)size < sizeof(line)) {
            (void)write_all(fd, line, (size_t)size);
        }
    }
    size = snprintf(
        line,
        sizeof(line),
        "\n  ],\n"
        "  \"summary\":{\"passed\":%u,\"failed\":%u,"
        "\"result\":\"%s\"}\n"
        "}\n",
        state->passed,
        state->failed,
        state->failed == 0u ? "pass" : "fail");
    if (size > 0 && (size_t)size < sizeof(line)) {
        (void)write_all(fd, line, (size_t)size);
    }
    (void)sceIoClose(fd);
}

static bool record_result(
    controller_state *state,
    const char *name,
    bool passed,
    int code)
{
    if (state->result_count < VC_FTG_MAX_TEST_RESULTS) {
        test_result *result =
            &state->results[state->result_count++];

        result->name = name;
        result->passed = passed;
        result->code = code;
    } else {
        passed = false;
    }
    if (passed) {
        ++state->passed;
    } else {
        ++state->failed;
    }
    sceClibPrintf(
        "VCFG test=%s result=%s code=%d\n",
        name,
        passed ? "pass" : "fail",
        code);
    write_results(state);
    return passed;
}

static void init_status_request(
    vc_ftg_status_request *request)
{
    memset(request, 0, sizeof(*request));
    request->version = VC_FTG_ABI_VERSION;
    request->struct_size = sizeof(*request);
}

static int get_status(vc_ftg_status_response *response)
{
    vc_ftg_status_request request;

    init_status_request(&request);
    memset(response, 0, sizeof(*response));
    return vc_ftg_decode_syscall_result(
        vcfgGetStatus(&request, response));
}

static int open_target(vc_ftg_open_response *response)
{
    vc_ftg_open_request request;

    init_status_request(&request);
    memset(response, 0, sizeof(*response));
    return vc_ftg_decode_syscall_result(
        vcfgOpenExactFixture(&request, response));
}

static int read_target(
    uint64_t handle,
    uint32_t segment_index,
    uint32_t offset,
    uint32_t length,
    vc_ftg_read_response *response)
{
    vc_ftg_read_request request;

    memset(&request, 0, sizeof(request));
    memset(response, 0xa5, sizeof(*response));
    request.version = VC_FTG_ABI_VERSION;
    request.struct_size = sizeof(request);
    request.capabilities =
        VC_FTG_CAPABILITY_FOREIGN_SEGMENT_READ;
    request.handle = handle;
    request.segment_index = segment_index;
    request.offset = offset;
    request.length = length;
    return vc_ftg_decode_syscall_result(
        vcfgReadFixtureSegment(&request, response));
}

static bool response_exact(
    const vc_ftg_read_response *response,
    uint32_t length)
{
    uint32_t index;

    if (response->version != VC_FTG_ABI_VERSION ||
        response->struct_size != sizeof(*response) ||
        response->status != VC_FTG_RESULT_OK ||
        response->length != length ||
        memcmp(
            response->bytes,
            g_expected_sentinel,
            length) != 0) {
        return false;
    }
    for (index = length; index < VC_FTG_MAX_READ; ++index) {
        if (response->bytes[index] != 0u) {
            return false;
        }
    }
    return true;
}

static bool read_layout(vc_ftg_fixture_layout *layout)
{
    SceUID fd;
    SceSSize read_size;

    memset(layout, 0, sizeof(*layout));
    fd = sceIoOpen(VC_FTG_LAYOUT_PATH, SCE_O_RDONLY, 0);
    if (fd < 0) {
        return false;
    }
    read_size = sceIoRead(fd, layout, sizeof(*layout));
    (void)sceIoClose(fd);
    return read_size == sizeof(*layout) &&
           layout->magic == VC_FTG_LAYOUT_MAGIC &&
           layout->version == VC_FTG_LAYOUT_VERSION &&
           layout->struct_size == sizeof(*layout) &&
           layout->segment_index < VC_FTG_MAX_SEGMENTS &&
           layout->sentinel_size == VC_FTG_MAX_READ &&
           layout->wrong_caller_open_result ==
               VC_FTG_RESULT_CALLER_TITLE_MISMATCH &&
           layout->reserved1 == 0u;
}

static bool clear_artifact(const char *path)
{
    SceUID fd = sceIoOpen(
        path,
        SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC,
        0666);
    uint8_t byte = 0u;
    SceSSize read_size;
    bool result;

    if (fd < 0) {
        return false;
    }
    if (sceIoClose(fd) < 0) {
        return false;
    }
    fd = sceIoOpen(path, SCE_O_RDONLY, 0);
    if (fd < 0) {
        return false;
    }
    read_size = sceIoRead(fd, &byte, sizeof(byte));
    result = read_size == 0;
    if (sceIoClose(fd) < 0) {
        result = false;
    }
    return result;
}

static int wait_for_target(
    bool available,
    vc_ftg_status_response *status)
{
    unsigned int attempt;
    int result = VC_FTG_RESULT_TARGET_UNAVAILABLE;

    for (attempt = 0u; attempt < 50u; ++attempt) {
        result = get_status(status);
        if (result != VC_FTG_RESULT_OK) {
            return result;
        }
        if ((status->target_state ==
             VC_FTG_TARGET_STARTED) == available) {
            return VC_FTG_RESULT_OK;
        }
        (void)sceKernelDelayThread(UINT32_C(100000));
    }
    return VC_FTG_RESULT_TARGET_UNAVAILABLE;
}

static int launch_target(void)
{
    return sceAppMgrLaunchAppByUri(
        0x20000,
        "psgm:play?titleid="
        VITACHEAT_FOREIGN_GATE_TARGET_TITLE_ID);
}

int main(void)
{
    controller_state state;
    vc_ftg_status_response status;
    vc_ftg_open_response open_response;
    vc_ftg_read_response read_response;
    vc_ftg_fixture_layout layout;
    uint64_t first_handle = 0u;
    uint64_t second_handle = 0u;
    SceInt64 run_time;
    int result;

    memset(&state, 0, sizeof(state));
    run_time = sceKernelGetSystemTimeWide();
    if (run_time > 0) {
        state.run_id = (uint64_t)run_time;
    }
    {
        const bool result_cleared =
            clear_artifact(VC_FTG_CONTROLLER_RESULT_PATH);

        if (!record_result(
                &state,
                "clear-controller-result",
                result_cleared,
                result_cleared
                    ? VC_FTG_RESULT_OK
                    : VC_FTG_RESULT_PLATFORM_FAILURE)) {
            goto finish;
        }
    }
    if (!record_result(
            &state,
            "controller-run-identity",
            state.run_id != 0u,
            state.run_id != 0u
                ? VC_FTG_RESULT_OK
                : VC_FTG_RESULT_PLATFORM_FAILURE)) {
        goto finish;
    }
    result = get_status(&status);
    if (!record_result(
            &state,
            "status-ready",
            result == VC_FTG_RESULT_OK &&
                status.version == VC_FTG_ABI_VERSION &&
                status.struct_size == sizeof(status) &&
                status.status == VC_FTG_RUNTIME_READY &&
                status.capabilities ==
                    VC_FTG_CAPABILITY_FOREIGN_SEGMENT_READ &&
                status.max_read == VC_FTG_MAX_READ &&
                status.timeout_ms ==
                    VC_FTG_DEFAULT_TIMEOUT_MS &&
                status.abi_flags == VC_FTG_ABI_FLAGS &&
                status.target_state == VC_FTG_TARGET_NONE,
            result)) {
        goto finish;
    }

    result = open_target(&open_response);
    if (!record_result(
            &state,
            "controller-before-target-unavailable",
            status.target_state == VC_FTG_TARGET_NONE &&
                result == VC_FTG_RESULT_TARGET_UNAVAILABLE,
            result)) {
        goto finish;
    }
    {
        const bool layout_cleared =
            clear_artifact(VC_FTG_LAYOUT_PATH);

        if (!record_result(
                &state,
                "clear-fixture-layout",
                layout_cleared,
                layout_cleared
                    ? VC_FTG_RESULT_OK
                    : VC_FTG_RESULT_PLATFORM_FAILURE)) {
            goto finish;
        }
    }
    {
        const bool startup_cleared =
            clear_artifact(VC_FTG_STARTUP_PATH);

        if (!record_result(
                &state,
                "clear-startup-stage",
                startup_cleared,
                startup_cleared
                    ? VC_FTG_RESULT_OK
                    : VC_FTG_RESULT_PLATFORM_FAILURE)) {
            goto finish;
        }
    }
    {
        const bool fixture_cleared =
            clear_artifact(VC_FTG_TARGET_RESULT_PATH);

        if (!record_result(
                &state,
                "clear-fixture-result",
                fixture_cleared,
                fixture_cleared
                    ? VC_FTG_RESULT_OK
                    : VC_FTG_RESULT_PLATFORM_FAILURE)) {
            goto finish;
        }
    }
    result = launch_target();
    if (!record_result(
            &state,
            "launch-target",
            result >= 0,
            result)) {
        goto finish;
    }
    result = wait_for_target(true, &status);
    if (!record_result(
            &state,
            "target-start-observed",
            result == VC_FTG_RESULT_OK,
            result)) {
        goto finish;
    }

    {
        const bool layout_ready = read_layout(&layout);

        if (!record_result(
                &state,
                "fixture-layout",
                layout_ready,
                layout_ready
                    ? VC_FTG_RESULT_OK
                    : VC_FTG_RESULT_PLATFORM_FAILURE)) {
            goto finish;
        }
    }
    result = open_target(&open_response);
    if (!record_result(
            &state,
            "open-exact-fixture",
            result == VC_FTG_RESULT_OK &&
                open_response.handle != 0u,
            result)) {
        goto finish;
    }
    first_handle = open_response.handle;

    result = read_target(
        first_handle,
        layout.segment_index,
        layout.segment_offset,
        0u,
        &read_response);
    if (!record_result(
            &state,
            "read-zero-rejected",
            result == VC_FTG_RESULT_RANGE_DENIED,
            result)) {
        goto finish;
    }
    result = read_target(
        first_handle,
        layout.segment_index,
        layout.segment_offset,
        1u,
        &read_response);
    if (!record_result(
            &state,
            "read-one-exact",
            result == VC_FTG_RESULT_OK &&
                response_exact(&read_response, 1u),
            result)) {
        goto finish;
    }
    result = read_target(
        first_handle,
        layout.segment_index,
        layout.segment_offset,
        63u,
        &read_response);
    if (!record_result(
            &state,
            "read-sixty-three-exact",
            result == VC_FTG_RESULT_OK &&
                response_exact(&read_response, 63u),
            result)) {
        goto finish;
    }
    result = read_target(
        first_handle,
        layout.segment_index,
        layout.segment_offset,
        64u,
        &read_response);
    if (!record_result(
            &state,
            "read-sixty-four-exact",
            result == VC_FTG_RESULT_OK &&
                response_exact(&read_response, 64u),
            result)) {
        goto finish;
    }
    result = read_target(
        first_handle,
        layout.segment_index,
        layout.segment_offset,
        65u,
        &read_response);
    if (!record_result(
            &state,
            "read-sixty-five-rejected",
            result == VC_FTG_RESULT_RANGE_DENIED,
            result)) {
        goto finish;
    }
    result = read_target(
        first_handle,
        layout.segment_index,
        UINT32_MAX,
        64u,
        &read_response);
    if (!record_result(
            &state,
            "range-overflow-rejected",
            result == VC_FTG_RESULT_RANGE_DENIED,
            result)) {
        goto finish;
    }
    result = read_target(
        first_handle + 1u,
        layout.segment_index,
        layout.segment_offset,
        1u,
        &read_response);
    if (!record_result(
            &state,
            "invalid-handle-rejected",
            result == VC_FTG_RESULT_INVALID_HANDLE,
            result)) {
        goto finish;
    }
    {
        vc_ftg_read_request bad_pointer_request;

        memset(&bad_pointer_request, 0,
               sizeof(bad_pointer_request));
        bad_pointer_request.version = VC_FTG_ABI_VERSION;
        bad_pointer_request.struct_size =
            sizeof(bad_pointer_request);
        bad_pointer_request.capabilities =
            VC_FTG_CAPABILITY_FOREIGN_SEGMENT_READ;
        bad_pointer_request.handle = first_handle;
        bad_pointer_request.segment_index =
            layout.segment_index;
        bad_pointer_request.offset = layout.segment_offset;
        bad_pointer_request.length = 1u;
        result = vc_ftg_decode_syscall_result(
            vcfgReadFixtureSegment(
                &bad_pointer_request,
                (vc_ftg_read_response *)(uintptr_t)1u));
    }
    if (!record_result(
            &state,
            "bad-output-pointer-rejected",
            result == VC_FTG_RESULT_COPY_TO_USER_FAILED,
            result)) {
        goto finish;
    }

    result = sceAppMgrDestroyAppByName(
        VITACHEAT_FOREIGN_GATE_TARGET_TITLE_ID);
    if (!record_result(
            &state,
            "destroy-source-owned-target",
            result >= 0,
            result)) {
        goto finish;
    }
    result = wait_for_target(false, &status);
    if (!record_result(
            &state,
            "target-exit-observed",
            result == VC_FTG_RESULT_OK,
            result)) {
        goto finish;
    }
    result = read_target(
        first_handle,
        layout.segment_index,
        layout.segment_offset,
        1u,
        &read_response);
    if (!record_result(
            &state,
            "stale-handle-after-exit-rejected",
            result == VC_FTG_RESULT_INVALID_HANDLE,
            result)) {
        goto finish;
    }

    {
        const bool layout_cleared =
            clear_artifact(VC_FTG_LAYOUT_PATH);

        if (!record_result(
                &state,
                "clear-fixture-layout-before-relaunch",
                layout_cleared,
                layout_cleared
                    ? VC_FTG_RESULT_OK
                    : VC_FTG_RESULT_PLATFORM_FAILURE)) {
            goto finish;
        }
    }
    {
        const bool startup_cleared =
            clear_artifact(VC_FTG_STARTUP_PATH);

        if (!record_result(
                &state,
                "clear-startup-stage-before-relaunch",
                startup_cleared,
                startup_cleared
                    ? VC_FTG_RESULT_OK
                    : VC_FTG_RESULT_PLATFORM_FAILURE)) {
            goto finish;
        }
    }
    {
        const bool fixture_cleared =
            clear_artifact(VC_FTG_TARGET_RESULT_PATH);

        if (!record_result(
                &state,
                "clear-fixture-result-before-relaunch",
                fixture_cleared,
                fixture_cleared
                    ? VC_FTG_RESULT_OK
                    : VC_FTG_RESULT_PLATFORM_FAILURE)) {
            goto finish;
        }
    }
    result = launch_target();
    if (!record_result(
            &state,
            "relaunch-target",
            result >= 0,
            result)) {
        goto finish;
    }
    result = wait_for_target(true, &status);
    if (!record_result(
            &state,
            "target-relaunch-observed",
            result == VC_FTG_RESULT_OK,
            result)) {
        goto finish;
    }
    {
        const bool layout_ready = read_layout(&layout);

        if (!record_result(
                &state,
                "fixture-layout-after-relaunch",
                layout_ready,
                layout_ready
                    ? VC_FTG_RESULT_OK
                    : VC_FTG_RESULT_PLATFORM_FAILURE)) {
            goto finish;
        }
    }
    result = read_target(
        first_handle,
        layout.segment_index,
        layout.segment_offset,
        1u,
        &read_response);
    if (!record_result(
            &state,
            "old-handle-after-relaunch-rejected",
            result == VC_FTG_RESULT_INVALID_HANDLE,
            result)) {
        goto finish;
    }
    result = open_target(&open_response);
    if (!record_result(
            &state,
            "open-relaunched-fixture",
            result == VC_FTG_RESULT_OK &&
                open_response.handle != 0u &&
                open_response.handle != first_handle,
            result)) {
        goto finish;
    }
    second_handle = open_response.handle;
    result = read_target(
        second_handle,
        layout.segment_index,
        layout.segment_offset,
        64u,
        &read_response);
    if (!record_result(
            &state,
            "relaunch-read-sixty-four-exact",
            result == VC_FTG_RESULT_OK &&
                response_exact(&read_response, 64u),
            result)) {
        goto finish;
    }
    {
        vc_ftg_close_request close_request;
        vc_ftg_close_response close_response;

        memset(&close_request, 0, sizeof(close_request));
        memset(&close_response, 0, sizeof(close_response));
        close_request.version = VC_FTG_ABI_VERSION;
        close_request.struct_size = sizeof(close_request);
        close_request.capabilities =
            VC_FTG_CAPABILITY_FOREIGN_SEGMENT_READ;
        close_request.handle = second_handle;
        result = vc_ftg_decode_syscall_result(
            vcfgClose(&close_request, &close_response));
    }
    (void)record_result(
        &state,
        "close",
        result == VC_FTG_RESULT_OK,
        result);

finish:
    write_results(&state);
    sceClibPrintf(
        "VCFG summary passed=%u failed=%u path=%s\n",
        state.passed,
        state.failed,
        VC_FTG_CONTROLLER_RESULT_PATH);
    return state.failed == 0u ? 0 : 1;
}
