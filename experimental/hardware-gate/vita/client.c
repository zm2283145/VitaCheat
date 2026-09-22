#include "vitacheat/hardware_gate.h"

#include <psp2/io/fcntl.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/modulemgr.h>
#include <psp2/kernel/threadmgr.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifndef VITACHEAT_HARDWARE_GATE_TITLE_ID
#error "The disposable hardware-gate client requires an explicit title ID"
#endif

#define VC_HG_RESULT_PATH \
    "ux0:data/vitacheat-hardware-gate-result.json"

static const uint8_t g_sentinel[VC_HG_MAX_READ]
    __attribute__((aligned(64), used)) = {
        0x56, 0x43, 0x48, 0x47, 0x2d, 0x53, 0x45, 0x4c,
        0x46, 0x2d, 0x52, 0x45, 0x41, 0x44, 0x2d, 0x56,
        0x31, 0x3a, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05,
        0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d,
        0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15,
        0x16, 0x17, 0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d,
        0x1e, 0x1f, 0x20, 0x21, 0x22, 0x23, 0x24, 0x25,
        0x26, 0x27, 0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d
    };

typedef struct client_state {
    SceUID log_fd;
    unsigned int passed;
    unsigned int failed;
    uint32_t diagnostic_stage;
    int32_t diagnostic_raw_result;
    int32_t diagnostic_query_result;
    int32_t diagnostic_syscall_result;
    bool first_result;
    bool diagnostic_queried;
} client_state;

static void write_all(SceUID fd, const char *text)
{
    size_t remaining;

    if (fd < 0 || text == NULL) {
        return;
    }
    remaining = strlen(text);
    while (remaining != 0u) {
        SceSSize written = sceIoWrite(
            fd, text, (SceSize)remaining);

        if (written <= 0) {
            return;
        }
        text += written;
        remaining -= (size_t)written;
    }
}

static void write_result_header(client_state *state)
{
    char line[256];

    state->log_fd = sceIoOpen(
        VC_HG_RESULT_PATH,
        SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC,
        0666);
    (void)snprintf(
        line, sizeof(line),
        "{\n"
        "  \"schema\":\"vitacheat.hardware-gate.result.v1\",\n"
        "  \"title_id\":\"%s\",\n"
        "  \"target_firmware\":\"3.65\",\n"
        "  \"scope\":\"same-process-source-owned-sentinel\",\n"
        "  \"tests\":[\n",
        VITACHEAT_HARDWARE_GATE_TITLE_ID);
    write_all(state->log_fd, line);
    sceClibPrintf(
        "VCHG schema=vitacheat.hardware-gate.result.v1 "
        "title=%s scope=self\n",
        VITACHEAT_HARDWARE_GATE_TITLE_ID);
}

static bool record_result(
    client_state *state,
    const char *name,
    bool passed,
    int code)
{
    char line[256];

    (void)snprintf(
        line, sizeof(line),
        "%s    {\"name\":\"%s\",\"result\":\"%s\","
        "\"code\":%d}",
        state->first_result ? "" : ",\n",
        name, passed ? "pass" : "fail", code);
    write_all(state->log_fd, line);
    state->first_result = false;
    if (passed) {
        ++state->passed;
    } else {
        ++state->failed;
    }
    sceClibPrintf(
        "VCHG test=%s result=%s code=%d\n",
        name, passed ? "pass" : "fail", code);
    return passed;
}

static void write_result_footer(client_state *state)
{
    char line[512];
    const char *stage_name;

    switch (state->diagnostic_stage) {
    case VC_HG_DIAGNOSTIC_NONE:
        stage_name = "none";
        break;
    case VC_HG_DIAGNOSTIC_CALLER_PID:
        stage_name = "caller-pid";
        break;
    case VC_HG_DIAGNOSTIC_REQUEST_COPY:
        stage_name = "request-copy";
        break;
    case VC_HG_DIAGNOSTIC_SYSTEM_TIME:
        stage_name = "system-time";
        break;
    case VC_HG_DIAGNOSTIC_TITLE_QUERY:
        stage_name = "title-query";
        break;
    case VC_HG_DIAGNOSTIC_TITLE_NORMALIZE:
        stage_name = "title-normalize";
        break;
    case VC_HG_DIAGNOSTIC_MODULE_ID:
        stage_name = "module-id";
        break;
    case VC_HG_DIAGNOSTIC_MODULE_INFO:
        stage_name = "module-info";
        break;
    case VC_HG_DIAGNOSTIC_MODULE_ID_MISMATCH:
        stage_name = "module-id-mismatch";
        break;
    case VC_HG_DIAGNOSTIC_MODULE_FINGERPRINT:
        stage_name = "module-fingerprint";
        break;
    case VC_HG_DIAGNOSTIC_MODULE_FINGERPRINT_ZERO:
        stage_name = "module-fingerprint-zero";
        break;
    case VC_HG_DIAGNOSTIC_MODULE_NAME:
        stage_name = "module-name";
        break;
    case VC_HG_DIAGNOSTIC_MODULE_SEGMENTS:
        stage_name = "module-segments";
        break;
    case VC_HG_DIAGNOSTIC_RESPONSE_COPY:
        stage_name = "response-copy";
        break;
    case VC_HG_DIAGNOSTIC_MODULE_PROCESS_UID:
        stage_name = "module-process-uid";
        break;
    default:
        stage_name = "invalid";
        break;
    }

    (void)snprintf(
        line, sizeof(line),
        "\n  ],\n"
        "  \"diagnostic\":{\"queried\":%s,"
        "\"stage\":%u,\"stage_name\":\"%s\","
        "\"raw_api_code\":%d,\"raw_api_hex\":\"0x%08x\","
        "\"query_code\":%d,\"syscall_code\":%d,"
        "\"syscall_hex\":\"0x%08x\"},\n"
        "  \"summary\":{\"passed\":%u,\"failed\":%u,"
        "\"result\":\"%s\"}\n"
        "}\n",
        state->diagnostic_queried ? "true" : "false",
        state->diagnostic_stage, stage_name,
        state->diagnostic_raw_result,
        (unsigned int)state->diagnostic_raw_result,
        state->diagnostic_query_result,
        state->diagnostic_syscall_result,
        (unsigned int)state->diagnostic_syscall_result,
        state->passed, state->failed,
        state->failed == 0u ? "pass" : "fail");
    write_all(state->log_fd, line);
    if (state->log_fd >= 0) {
        (void)sceIoClose(state->log_fd);
        state->log_fd = -1;
    }
    sceClibPrintf(
        "VCHG summary passed=%u failed=%u result=%s "
        "path=%s\n",
        state->passed, state->failed,
        state->failed == 0u ? "pass" : "fail",
        VC_HG_RESULT_PATH);
}

static void init_status_request(vc_hg_status_request *request)
{
    memset(request, 0, sizeof(*request));
    request->version = VC_HG_ABI_VERSION;
    request->struct_size = sizeof(*request);
}

static void capture_open_diagnostic(
    client_state *state,
    int32_t syscall_result)
{
    vc_hg_status_request request;
    vc_hg_status_response_v2 response;
    int raw_result;
    int result;

    init_status_request(&request);
    request.version = VC_HG_STATUS_DIAGNOSTIC_VERSION;
    memset(&response, 0, sizeof(response));
    raw_result = vchgGetStatus(&request, &response.base);
    result = vc_hg_decode_syscall_result(raw_result);
    state->diagnostic_queried = true;
    state->diagnostic_query_result = result;
    state->diagnostic_syscall_result = syscall_result;
    if (result == VC_HG_RESULT_OK &&
        response.base.version ==
            VC_HG_STATUS_DIAGNOSTIC_VERSION &&
        response.base.struct_size == sizeof(response) &&
        response.diagnostic_stage <
            VC_HG_DIAGNOSTIC_STAGE_COUNT &&
        response.reserved0 == 0u &&
        response.reserved1 == 0u) {
        state->diagnostic_stage =
            response.diagnostic_stage;
        state->diagnostic_raw_result =
            response.diagnostic_raw_result;
    }
    sceClibPrintf(
        "VCHG diagnostic query=%d stage=%u raw=%d "
        "raw_hex=0x%08x syscall=%d syscall_hex=0x%08x\n",
        result, state->diagnostic_stage,
        state->diagnostic_raw_result,
        (unsigned int)state->diagnostic_raw_result,
        syscall_result, (unsigned int)syscall_result);
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

static bool find_sentinel(
    uint32_t *segment_index,
    uint32_t *segment_offset,
    SceKernelModuleInfo *module)
{
    SceUID module_id;
    uintptr_t sentinel_begin = (uintptr_t)g_sentinel;
    uintptr_t sentinel_end =
        sentinel_begin + sizeof(g_sentinel);
    uint32_t index;

    memset(module, 0, sizeof(*module));
    module->size = sizeof(*module);
    module_id = sceKernelGetModuleIdByAddr(
        (void *)g_sentinel);
    if (module_id <= 0 ||
        sceKernelGetModuleInfo(module_id, module) < 0) {
        return false;
    }
    for (index = 0; index < VC_HG_MAX_SEGMENTS; ++index) {
        uintptr_t begin =
            (uintptr_t)module->segments[index].vaddr;
        uintptr_t end;

        if (module->segments[index].size !=
                sizeof(module->segments[index]) ||
            begin == (uintptr_t)0 ||
            module->segments[index].memsz == 0u ||
            begin > UINTPTR_MAX -
                module->segments[index].memsz) {
            continue;
        }
        end = begin + module->segments[index].memsz;
        if (sentinel_begin >= begin &&
            sentinel_end <= end) {
            *segment_index = index;
            *segment_offset =
                (uint32_t)(sentinel_begin - begin);
            return true;
        }
    }
    return false;
}

static int read_segment(
    uint64_t handle,
    uint32_t segment_index,
    uint32_t offset,
    uint32_t length,
    vc_hg_read_response *response)
{
    vc_hg_read_request request;

    init_read_request(
        &request, handle, segment_index, offset, length);
    memset(response, 0xa5, sizeof(*response));
    return vc_hg_decode_syscall_result(
        vchgReadSelfSegment(&request, response));
}

static bool response_exact(
    const vc_hg_read_response *response,
    const uint8_t *expected,
    uint32_t length)
{
    uint32_t index;

    if (response->version != VC_HG_ABI_VERSION ||
        response->struct_size != sizeof(*response) ||
        response->status != VC_HG_RESULT_OK ||
        response->length != length ||
        memcmp(response->bytes, expected, length) != 0) {
        return false;
    }
    for (index = length; index < VC_HG_MAX_READ; ++index) {
        if (response->bytes[index] != 0u) {
            return false;
        }
    }
    return true;
}

int main(void)
{
    client_state state;
    vc_hg_status_request status_request;
    vc_hg_status_response status_response;
    vc_hg_open_request open_request;
    vc_hg_open_response open_response;
    vc_hg_module_response module_response;
    vc_hg_session_request session_request;
    vc_hg_close_response close_response;
    vc_hg_read_request read_request;
    vc_hg_read_response read_response;
    SceKernelModuleInfo local_module;
    uint64_t first_handle = 0u;
    uint64_t second_handle = 0u;
    uint32_t sentinel_segment = 0u;
    uint32_t sentinel_offset = 0u;
    uintptr_t segment_base;
    uint32_t segment_size;
    int result;
    int raw_result;

    memset(&state, 0, sizeof(state));
    state.log_fd = -1;
    state.first_result = true;
    write_result_header(&state);

    init_status_request(&status_request);
    memset(&status_response, 0, sizeof(status_response));
    result = vc_hg_decode_syscall_result(
        vchgGetStatus(&status_request, &status_response));
    if (!record_result(
            &state, "status-ready",
            result == VC_HG_RESULT_OK &&
                status_response.status ==
                    VC_HG_RUNTIME_READY &&
                status_response.capabilities ==
                    VC_HG_CAPABILITY_SELF_SEGMENT_READ &&
                status_response.max_read == VC_HG_MAX_READ,
            result)) {
        goto finish;
    }

    open_request = status_request;
    memset(&open_response, 0, sizeof(open_response));
    raw_result = vchgOpenSelf(
        &open_request, &open_response);
    result = vc_hg_decode_syscall_result(raw_result);
    if (!record_result(
            &state, "open-self",
            result == VC_HG_RESULT_OK &&
                open_response.handle != 0u &&
                open_response.segment_count != 0u,
            result)) {
        capture_open_diagnostic(&state, raw_result);
        goto finish;
    }
    first_handle = open_response.handle;

    init_session_request(
        &session_request, first_handle);
    memset(&module_response, 0, sizeof(module_response));
    result = vc_hg_decode_syscall_result(
        vchgGetSelfMainModule(
            &session_request, &module_response));
    if (!record_result(
            &state, "main-module-snapshot",
            result == VC_HG_RESULT_OK &&
                module_response.handle == first_handle &&
                module_response.segment_count ==
                    open_response.segment_count &&
                module_response.module_fingerprint ==
                    open_response.module_fingerprint,
            result)) {
        goto finish;
    }

    if (!record_result(
            &state, "locate-owned-sentinel",
            find_sentinel(
                &sentinel_segment, &sentinel_offset,
                &local_module),
            VC_HG_RESULT_PLATFORM_FAILURE)) {
        goto finish;
    }
    if (!record_result(
            &state, "module-identity-match",
            sentinel_segment <
                module_response.segment_count &&
                module_response.segments[sentinel_segment].size ==
                    local_module.segments[
                        sentinel_segment].memsz,
            VC_HG_RESULT_MODULE_MISMATCH)) {
        goto finish;
    }

    result = read_segment(
        first_handle, sentinel_segment,
        sentinel_offset, 0u, &read_response);
    if (!record_result(
            &state, "read-zero-rejected",
            result == VC_HG_RESULT_RANGE_DENIED,
            result)) {
        goto finish;
    }

    result = read_segment(
        first_handle, sentinel_segment,
        sentinel_offset, 1u, &read_response);
    if (!record_result(
            &state, "read-one-exact",
            result == VC_HG_RESULT_OK &&
                response_exact(
                    &read_response, g_sentinel, 1u),
            result)) {
        goto finish;
    }

    result = read_segment(
        first_handle, sentinel_segment,
        sentinel_offset, 63u, &read_response);
    if (!record_result(
            &state, "read-sixty-three-exact",
            result == VC_HG_RESULT_OK &&
                response_exact(
                    &read_response, g_sentinel, 63u),
            result)) {
        goto finish;
    }

    result = read_segment(
        first_handle, sentinel_segment,
        sentinel_offset, 64u, &read_response);
    if (!record_result(
            &state, "read-sixty-four-exact",
            result == VC_HG_RESULT_OK &&
                response_exact(
                    &read_response, g_sentinel, 64u),
            result)) {
        goto finish;
    }

    result = read_segment(
        first_handle, sentinel_segment,
        sentinel_offset, 65u, &read_response);
    if (!record_result(
            &state, "read-sixty-five-rejected",
            result == VC_HG_RESULT_RANGE_DENIED,
            result)) {
        goto finish;
    }

    segment_base = (uintptr_t)local_module
                       .segments[sentinel_segment].vaddr;
    segment_size = local_module
                       .segments[sentinel_segment].memsz;
    result = read_segment(
        first_handle, sentinel_segment,
        0u, 1u, &read_response);
    if (!record_result(
            &state, "segment-start",
            result == VC_HG_RESULT_OK &&
                response_exact(
                    &read_response,
                    (const uint8_t *)segment_base, 1u),
            result)) {
        goto finish;
    }

    result = read_segment(
        first_handle, sentinel_segment,
        segment_size - 1u, 1u, &read_response);
    if (!record_result(
            &state, "segment-end",
            result == VC_HG_RESULT_OK &&
                response_exact(
                    &read_response,
                    (const uint8_t *)(
                        segment_base + segment_size - 1u),
                    1u),
            result)) {
        goto finish;
    }

    result = read_segment(
        first_handle, sentinel_segment,
        segment_size, 1u, &read_response);
    if (!record_result(
            &state, "segment-past-end-rejected",
            result == VC_HG_RESULT_RANGE_DENIED,
            result)) {
        goto finish;
    }

    result = read_segment(
        first_handle, sentinel_segment,
        UINT32_MAX, 64u, &read_response);
    if (!record_result(
            &state, "offset-length-overflow-rejected",
            result == VC_HG_RESULT_RANGE_DENIED,
            result)) {
        goto finish;
    }

    result = read_segment(
        first_handle, VC_HG_MAX_SEGMENTS,
        0u, 1u, &read_response);
    if (!record_result(
            &state, "invalid-segment-rejected",
            result == VC_HG_RESULT_INVALID_SEGMENT,
            result)) {
        goto finish;
    }

    result = read_segment(
        first_handle + 1u, sentinel_segment,
        sentinel_offset, 1u, &read_response);
    if (!record_result(
            &state, "invalid-handle-rejected",
            result == VC_HG_RESULT_INVALID_HANDLE,
            result)) {
        goto finish;
    }

    init_read_request(
        &read_request, first_handle,
        sentinel_segment, sentinel_offset, 1u);
    result = vc_hg_decode_syscall_result(
        vchgReadSelfSegment(
            &read_request,
            (vc_hg_read_response *)(uintptr_t)1u));
    if (!record_result(
            &state, "bad-output-pointer-rejected",
            result == VC_HG_RESULT_COPY_TO_USER_FAILED,
            result)) {
        goto finish;
    }

    memset(&open_response, 0, sizeof(open_response));
    result = vc_hg_decode_syscall_result(
        vchgOpenSelf(&open_request, &open_response));
    if (!record_result(
            &state, "single-session-bound",
            result == VC_HG_RESULT_SESSION_ACTIVE,
            result)) {
        goto finish;
    }

    init_session_request(
        &session_request, first_handle);
    memset(&close_response, 0, sizeof(close_response));
    result = vc_hg_decode_syscall_result(
        vchgClose(&session_request, &close_response));
    if (!record_result(
            &state, "close",
            result == VC_HG_RESULT_OK &&
                close_response.status == VC_HG_RESULT_OK,
            result)) {
        goto finish;
    }

    result = read_segment(
        first_handle, sentinel_segment,
        sentinel_offset, 1u, &read_response);
    if (!record_result(
            &state, "close-replay-rejected",
            result == VC_HG_RESULT_INVALID_HANDLE,
            result)) {
        goto finish;
    }

    memset(&open_response, 0, sizeof(open_response));
    result = vc_hg_decode_syscall_result(
        vchgOpenSelf(&open_request, &open_response));
    if (!record_result(
            &state, "reopen-nonrepeating",
            result == VC_HG_RESULT_OK &&
                open_response.handle != first_handle,
            result)) {
        goto finish;
    }
    second_handle = open_response.handle;

    init_session_request(
        &session_request, first_handle);
    result = vc_hg_decode_syscall_result(
        vchgGetSelfMainModule(
            &session_request, &module_response));
    if (!record_result(
            &state, "module-session-mismatch-rejected",
            result == VC_HG_RESULT_INVALID_HANDLE,
            result)) {
        goto finish;
    }

    (void)sceKernelDelayThread(
        (SceUInt)(VC_HG_DEFAULT_TIMEOUT_US +
                  UINT64_C(100000)));
    result = read_segment(
        second_handle, sentinel_segment,
        sentinel_offset, 1u, &read_response);
    (void)record_result(
        &state, "timeout-rejected",
        result == VC_HG_RESULT_EXPIRED,
        result);

finish:
    if (state.failed != 0u) {
        init_session_request(
            &session_request,
            open_response.handle != 0u
                ? open_response.handle
                : first_handle);
        (void)vc_hg_decode_syscall_result(
            vchgClose(
                &session_request, &close_response));
    }
    write_result_footer(&state);
    return state.failed == 0u ? 0 : 1;
}
