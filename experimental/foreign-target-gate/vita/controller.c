#include "fixture_protocol.h"
#include "vitacheat/foreign_target_controller.h"
#include "vitacheat/foreign_target_gate.h"
#include "vitacheat/foreign_target_startup.h"

#include <psp2/appmgr.h>
#include <psp2/io/fcntl.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/threadmgr.h>

#include <stdbool.h>
#include <stdint.h>

#define memcmp sceClibMemcmp
#define memset sceClibMemset
#define snprintf sceClibSnprintf

#ifndef VITACHEAT_FOREIGN_GATE_TARGET_TITLE_ID
#error "The foreign-target controller requires a target title ID"
#endif

#define VC_FTG_MAX_TEST_RESULTS 40u
#define VC_FTG_MAX_DIAGNOSTIC_SNAPSHOTS 4u

static const uint8_t g_expected_sentinel[VC_FTG_MAX_READ] =
    VC_FTG_SENTINEL_BYTES;

typedef struct test_result {
    const char *name;
    int code;
    bool passed;
} test_result;

typedef struct diagnostic_snapshot {
    const char *name;
    uint32_t create_callback_count;
    uint32_t start_callback_count;
    uint32_t start_revalidation_count;
    uint32_t ignored_non_target_count;
    uint32_t target_create_match_count;
    uint32_t target_create_authorized_count;
    uint32_t counter_saturation_flags;
    uint32_t last_lifecycle_event;
    int32_t last_lifecycle_result;
    uint32_t last_lifecycle_stage;
} diagnostic_snapshot;

typedef vc_ftg_controller_counts lifecycle_counts;

typedef struct controller_state {
    test_result results[VC_FTG_MAX_TEST_RESULTS];
    diagnostic_snapshot
        diagnostics[VC_FTG_MAX_DIAGNOSTIC_SNAPSHOTS];
    uint32_t result_count;
    uint32_t diagnostic_count;
    uint32_t passed;
    uint32_t failed;
    uint64_t run_id;
    uint64_t transaction_id;
    uint64_t previous_run_id;
    const char *phase_name;
    const char *phase_result_path;
    uint32_t phase_index;
    uint32_t test_offset;
    uint32_t expected_test_count;
    bool result_cleared;
    bool launch_committed;
    bool persistence_failed;
} controller_state;

typedef struct result_writer {
    SceUID fd;
    size_t size;
    uint32_t fingerprint;
    bool valid;
} result_writer;

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

static uint32_t result_fingerprint_update(
    uint32_t fingerprint,
    const uint8_t *bytes,
    size_t size)
{
    size_t index;

    for (index = 0u; index < size; ++index) {
        fingerprint ^= bytes[index];
        fingerprint *= UINT32_C(16777619);
    }
    return fingerprint;
}

static void result_writer_write(
    result_writer *writer,
    const void *buffer,
    size_t size)
{
    if (writer == NULL || !writer->valid) {
        return;
    }
    if (!write_all(writer->fd, buffer, size)) {
        writer->valid = false;
        return;
    }
    writer->fingerprint = result_fingerprint_update(
        writer->fingerprint,
        (const uint8_t *)buffer,
        size);
    writer->size += size;
}

static bool verify_result_file(
    const char *path,
    size_t expected_size,
    uint32_t expected_fingerprint)
{
    uint8_t buffer[256];
    size_t observed_size = 0u;
    uint32_t observed_fingerprint = UINT32_C(2166136261);
    SceUID fd;
    bool valid = false;

    fd = sceIoOpen(path, SCE_O_RDONLY, 0);
    if (fd < 0) {
        return false;
    }
    for (;;) {
        const SceSSize read_size = sceIoRead(
            fd, buffer, sizeof(buffer));

        if (read_size < 0) {
            break;
        }
        if (read_size == 0) {
            valid = observed_size == expected_size &&
                    observed_fingerprint ==
                        expected_fingerprint;
            break;
        }
        observed_size += (size_t)read_size;
        observed_fingerprint = result_fingerprint_update(
            observed_fingerprint,
            buffer,
            (size_t)read_size);
        if (observed_size > expected_size) {
            break;
        }
    }
    if (sceIoClose(fd) < 0) {
        valid = false;
    }
    memset(buffer, 0, sizeof(buffer));
    return valid;
}

static bool write_results_path(
    const controller_state *state,
    const char *path)
{
    char line[640];
    result_writer writer;
    uint32_t index;
    int size;

    memset(&writer, 0, sizeof(writer));
    writer.fd = sceIoOpen(
        path,
        SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC,
        0666);
    if (writer.fd < 0) {
        return false;
    }
    writer.fingerprint = UINT32_C(2166136261);
    writer.valid = true;
    size = snprintf(
        line,
        sizeof(line),
        "{\n"
        "  \"schema\":\"" VC_FTG_CONTROLLER_RESULT_SCHEMA "\",\n"
        "  \"build_id\":\"" VC_FTG_CONTROLLER_RESULT_BUILD_ID "\",\n"
        "  \"run_id\":\"%08x%08x\",\n"
        "  \"transaction_id\":\"%08x%08x\",\n"
        "  \"previous_run_id\":\"%08x%08x\",\n"
        "  \"phase\":\"%s\",\n"
        "  \"phase_index\":%u,\n"
        "  \"phase_count\":%u,\n"
        "  \"test_offset\":%u,\n"
        "  \"test_count\":%u,\n"
        "  \"result_cleared\":%s,\n"
        "  \"launch_committed\":%s,\n"
        "  \"target_title_id\":\"%s\",\n"
        "  \"controller_title_id\":\"VCFC00001\",\n"
        "  \"target_firmware\":\"3.65\",\n"
        "  \"scope\":\"source-owned-foreign-generation-read\",\n"
        "  \"tests\":[\n",
        (unsigned int)(state->run_id >> 32u),
        (unsigned int)state->run_id,
        (unsigned int)(state->transaction_id >> 32u),
        (unsigned int)state->transaction_id,
        (unsigned int)(state->previous_run_id >> 32u),
        (unsigned int)state->previous_run_id,
        state->phase_name,
        state->phase_index,
        VC_FTG_CONTROLLER_PHASE_COUNT,
        state->test_offset,
        state->expected_test_count,
        state->result_cleared ? "true" : "false",
        state->launch_committed ? "true" : "false",
        VITACHEAT_FOREIGN_GATE_TARGET_TITLE_ID);
    if (size > 0 && (size_t)size < sizeof(line)) {
        result_writer_write(&writer, line, (size_t)size);
    } else {
        writer.valid = false;
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
            result_writer_write(&writer, line, (size_t)size);
        } else {
            writer.valid = false;
        }
    }
    size = snprintf(
        line,
        sizeof(line),
        "\n  ],\n"
        "  \"lifecycle_diagnostics\":[\n");
    if (size > 0 && (size_t)size < sizeof(line)) {
        result_writer_write(&writer, line, (size_t)size);
    } else {
        writer.valid = false;
    }
    for (index = 0u; index < state->diagnostic_count; ++index) {
        const diagnostic_snapshot *snapshot =
            &state->diagnostics[index];
        size = snprintf(
            line,
            sizeof(line),
            "%s    {\"name\":\"%s\",\"create_callbacks\":%u,"
            "\"start_callbacks\":%u,\"start_revalidations\":%u,"
            "\"ignored_non_target\":%u,\"target_create_matches\":%u,"
            "\"target_create_authorized\":%u,\"saturation_flags\":%u,"
            "\"last_event\":%u,\"last_result\":%d,\"last_stage\":%u}",
            index == 0u ? "" : ",\n",
            snapshot->name,
            snapshot->create_callback_count,
            snapshot->start_callback_count,
            snapshot->start_revalidation_count,
            snapshot->ignored_non_target_count,
            snapshot->target_create_match_count,
            snapshot->target_create_authorized_count,
            snapshot->counter_saturation_flags,
            snapshot->last_lifecycle_event,
            snapshot->last_lifecycle_result,
            snapshot->last_lifecycle_stage);
        if (size > 0 && (size_t)size < sizeof(line)) {
            result_writer_write(&writer, line, (size_t)size);
        } else {
            writer.valid = false;
        }
    }
    size = snprintf(
        line,
        sizeof(line),
        "\n  ],\n"
        "  \"summary\":{\"passed\":%u,\"failed\":%u,"
        "\"expected_tests\":%u,\"phase_complete\":%s,"
        "\"result\":\"%s\"}\n"
        "}\n",
        state->passed,
        state->failed,
        state->expected_test_count,
        state->failed == 0u &&
                state->result_count ==
                    state->expected_test_count
            ? "true"
            : "false",
        state->failed == 0u &&
                state->result_count ==
                    state->expected_test_count
            ? "pass"
            : "fail");
    if (size > 0 && (size_t)size < sizeof(line)) {
        result_writer_write(&writer, line, (size_t)size);
    } else {
        writer.valid = false;
    }
    if (writer.valid) {
        writer.valid = sceIoSyncByFd(writer.fd, 0) >= 0;
    }
    if (sceIoClose(writer.fd) < 0) {
        writer.valid = false;
    }
    if (writer.valid) {
        writer.valid = verify_result_file(
            path, writer.size, writer.fingerprint);
    }
    memset(line, 0, sizeof(line));
    return writer.valid;
}

static bool write_results(const controller_state *state)
{
    return write_results_path(
               state, state->phase_result_path) &&
           write_results_path(
               state, VC_FTG_CONTROLLER_RESULT_PATH);
}

static void record_diagnostic_snapshot(
    controller_state *state,
    const char *name,
    const vc_ftg_status_response_v2 *status)
{
    if (state->diagnostic_count <
        VC_FTG_MAX_DIAGNOSTIC_SNAPSHOTS) {
        diagnostic_snapshot *snapshot =
            &state->diagnostics[state->diagnostic_count++];

        snapshot->name = name;
        snapshot->create_callback_count =
            status->create_callback_count;
        snapshot->start_callback_count =
            status->start_callback_count;
        snapshot->start_revalidation_count =
            status->start_revalidation_count;
        snapshot->ignored_non_target_count =
            status->ignored_non_target_count;
        snapshot->target_create_match_count =
            status->target_create_match_count;
        snapshot->target_create_authorized_count =
            status->target_create_authorized_count;
        snapshot->counter_saturation_flags =
            status->counter_saturation_flags;
        snapshot->last_lifecycle_event =
            status->last_lifecycle_event;
        snapshot->last_lifecycle_result =
            status->last_lifecycle_result;
        snapshot->last_lifecycle_stage =
            status->last_lifecycle_stage;
    }
}

static void capture_lifecycle_counts(
    lifecycle_counts *counts,
    const vc_ftg_status_response_v2 *status)
{
    counts->create_callback_count =
        status->create_callback_count;
    counts->start_callback_count =
        status->start_callback_count;
    counts->start_revalidation_count =
        status->start_revalidation_count;
    counts->target_create_match_count =
        status->target_create_match_count;
    counts->target_create_authorized_count =
        status->target_create_authorized_count;
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
    if (!write_results(state)) {
        state->persistence_failed = true;
        if (passed) {
            test_result *result =
                &state->results[state->result_count - 1u];

            result->passed = false;
            result->code = VC_FTG_RESULT_PLATFORM_FAILURE;
            --state->passed;
            ++state->failed;
        }
        passed = false;
    }
    return passed;
}

static void fail_last_result(
    controller_state *state,
    int code)
{
    test_result *result;

    if (state->result_count == 0u) {
        ++state->failed;
        if (!write_results(state)) {
            state->persistence_failed = true;
        }
        return;
    }
    result = &state->results[state->result_count - 1u];
    if (result->passed) {
        result->passed = false;
        result->code = code;
        --state->passed;
        ++state->failed;
    }
    if (!write_results(state)) {
        state->persistence_failed = true;
    }
}

static void init_status_request(
    vc_ftg_status_request *request)
{
    memset(request, 0, sizeof(*request));
    request->version = VC_FTG_STATUS_VERSION_CURRENT;
    request->struct_size = sizeof(*request);
}

static void init_open_request(
    vc_ftg_open_request *request)
{
    memset(request, 0, sizeof(*request));
    request->version = VC_FTG_ABI_VERSION;
    request->struct_size = sizeof(*request);
}

static int get_status(vc_ftg_status_response_v2 *response)
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

    init_open_request(&request);
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
    result = sceIoSyncByFd(fd, 0) >= 0;
    if (sceIoClose(fd) < 0) {
        result = false;
    }
    if (!result) {
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

static bool read_exact_file(
    const char *path,
    uint8_t *buffer,
    size_t size)
{
    SceUID fd;
    size_t offset = 0u;
    uint8_t extra = 0u;
    bool result = false;

    fd = sceIoOpen(path, SCE_O_RDONLY, 0);
    if (fd < 0) {
        return false;
    }
    while (offset < size) {
        const SceSSize read_size = sceIoRead(
            fd,
            buffer + offset,
            (SceSize)(size - offset));

        if (read_size <= 0) {
            goto finish;
        }
        offset += (size_t)read_size;
    }
    result = sceIoRead(fd, &extra, sizeof(extra)) == 0;

finish:
    if (sceIoClose(fd) < 0) {
        result = false;
    }
    return result;
}

static vc_ftg_controller_checkpoint_observation read_checkpoint(
    vc_ftg_controller_checkpoint *checkpoint)
{
    uint8_t bytes[VC_FTG_CONTROLLER_CHECKPOINT_SIZE + 1u];
    SceUID fd;
    size_t size = 0u;
    vc_ftg_controller_checkpoint_observation observation;

    memset(bytes, 0, sizeof(bytes));
    fd = sceIoOpen(
        VC_FTG_CONTROLLER_CHECKPOINT_PATH,
        SCE_O_RDONLY,
        0);
    if (fd < 0) {
        observation = vc_ftg_controller_checkpoint_observe(
            NULL, 0u, checkpoint);
        goto finish;
    }
    while (size < sizeof(bytes)) {
        const SceSSize read_size = sceIoRead(
            fd,
            bytes + size,
            (SceSize)(sizeof(bytes) - size));

        if (read_size < 0) {
            observation =
                VC_FTG_CONTROLLER_CHECKPOINT_INVALID;
            goto close;
        }
        if (read_size == 0) {
            break;
        }
        size += (size_t)read_size;
    }
    observation = vc_ftg_controller_checkpoint_observe(
        bytes, size, checkpoint);

close:
    if (sceIoClose(fd) < 0) {
        observation = VC_FTG_CONTROLLER_CHECKPOINT_INVALID;
    }

finish:
    memset(bytes, 0, sizeof(bytes));
    return observation;
}

static bool write_checkpoint(
    vc_ftg_controller_checkpoint *checkpoint)
{
    uint8_t expected[VC_FTG_CONTROLLER_CHECKPOINT_SIZE];
    uint8_t observed[VC_FTG_CONTROLLER_CHECKPOINT_SIZE];
    SceUID fd;
    bool result = false;

    memset(expected, 0, sizeof(expected));
    memset(observed, 0, sizeof(observed));
    if (!vc_ftg_controller_checkpoint_encode(
            checkpoint, expected)) {
        goto finish;
    }
    fd = sceIoOpen(
        VC_FTG_CONTROLLER_CHECKPOINT_PATH,
        SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC,
        0666);
    if (fd < 0) {
        goto finish;
    }
    result = write_all(fd, expected, sizeof(expected));
    if (result) {
        result = sceIoSyncByFd(fd, 0) >= 0;
    }
    if (sceIoClose(fd) < 0) {
        result = false;
    }
    if (!result) {
        goto finish;
    }
    result = read_exact_file(
                 VC_FTG_CONTROLLER_CHECKPOINT_PATH,
                 observed,
                 sizeof(observed)) &&
             memcmp(expected, observed, sizeof(expected)) == 0;

finish:
    memset(expected, 0, sizeof(expected));
    memset(observed, 0, sizeof(observed));
    return result;
}

static void init_controller_state(
    controller_state *state,
    uint64_t run_id,
    uint64_t transaction_id,
    uint64_t previous_run_id,
    const char *phase_name,
    const char *phase_result_path,
    uint32_t phase_index,
    uint32_t test_offset,
    uint32_t expected_test_count,
    bool result_cleared)
{
    memset(state, 0, sizeof(*state));
    state->run_id = run_id;
    state->transaction_id = transaction_id;
    state->previous_run_id = previous_run_id;
    state->phase_name = phase_name;
    state->phase_result_path = phase_result_path;
    state->phase_index = phase_index;
    state->test_offset = test_offset;
    state->expected_test_count = expected_test_count;
    state->result_cleared = result_cleared;
}

static bool status_ready_without_target(
    const vc_ftg_status_response_v2 *status)
{
    return status->base.version ==
               VC_FTG_STATUS_VERSION_CURRENT &&
           status->base.struct_size == sizeof(*status) &&
           status->base.status == VC_FTG_RUNTIME_READY &&
           status->base.capabilities ==
               VC_FTG_CAPABILITY_FOREIGN_SEGMENT_READ &&
           status->base.max_read == VC_FTG_MAX_READ &&
           status->base.timeout_ms ==
               VC_FTG_DEFAULT_TIMEOUT_MS &&
           status->base.abi_flags == VC_FTG_ABI_FLAGS &&
           status->base.target_state == VC_FTG_TARGET_NONE &&
           status->target_create_match_count == 0u &&
           status->target_create_authorized_count == 0u &&
           status->start_revalidation_count == 0u &&
           status->counter_saturation_flags == 0u;
}

static bool generation_profile_valid(
    const vc_ftg_status_response_v2 *status,
    const lifecycle_counts *baseline)
{
    lifecycle_counts current;

    capture_lifecycle_counts(&current, status);
    return status->counter_saturation_flags == 0u &&
           vc_ftg_controller_generation_profile_valid(
               &current, baseline);
}

static int wait_for_target(
    bool available,
    vc_ftg_status_response_v2 *status)
{
    unsigned int attempt;
    int result = VC_FTG_RESULT_TARGET_UNAVAILABLE;

    for (attempt = 0u; attempt < 50u; ++attempt) {
        result = get_status(status);
        if (result != VC_FTG_RESULT_OK) {
            return result;
        }
        if ((status->base.target_state ==
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

static bool restore_launch_pending(
    vc_ftg_controller_checkpoint *checkpoint)
{
    return vc_ftg_controller_checkpoint_cancel_launch(
               checkpoint) &&
           write_checkpoint(checkpoint);
}

static bool prepare_target_launch(
    controller_state *state,
    vc_ftg_controller_checkpoint *checkpoint,
    const char *test_name)
{
    vc_ftg_controller_launch_guard guard;

    vc_ftg_controller_launch_guard_init(&guard);
    if (state->persistence_failed ||
        !write_checkpoint(checkpoint)) {
        (void)record_result(
            state,
            test_name,
            false,
            VC_FTG_RESULT_PLATFORM_FAILURE);
        return false;
    }
    if (!vc_ftg_controller_checkpoint_commit_launch(
            checkpoint) ||
        !write_checkpoint(checkpoint)) {
        (void)restore_launch_pending(checkpoint);
        (void)record_result(
            state,
            test_name,
            false,
            VC_FTG_RESULT_PLATFORM_FAILURE);
        return false;
    }
    if (!vc_ftg_controller_launch_guard_checkpoint_verified(
            &guard)) {
        (void)restore_launch_pending(checkpoint);
        return false;
    }
    state->launch_committed = true;
    if (!record_result(
            state,
            test_name,
            true,
            VC_FTG_RESULT_OK)) {
        state->launch_committed = false;
        (void)restore_launch_pending(checkpoint);
        return false;
    }
    return vc_ftg_controller_launch_guard_results_verified(
               &guard) &&
           vc_ftg_controller_launch_guard_ready(&guard);
}

static void reject_target_launch(
    controller_state *state,
    vc_ftg_controller_checkpoint *checkpoint,
    int result)
{
    state->launch_committed = false;
    if (!restore_launch_pending(checkpoint)) {
        result = VC_FTG_RESULT_PLATFORM_FAILURE;
    }
    fail_last_result(state, result);
}

static int finish_controller(controller_state *state)
{
    const bool persisted = write_results(state);

    if (!persisted) {
        state->persistence_failed = true;
    }
    sceClibPrintf(
        "VCFG phase=%u summary passed=%u failed=%u path=%s\n",
        state->phase_index,
        state->passed,
        state->failed,
        VC_FTG_CONTROLLER_RESULT_PATH);
    return !state->persistence_failed &&
                   state->failed == 0u &&
                   state->result_count ==
                       state->expected_test_count
               ? 0
               : 1;
}

static int run_phase_1(
    uint64_t run_id,
    bool result_cleared)
{
    controller_state state;
    vc_ftg_status_response_v2 status;
    lifecycle_counts baseline_counts;
    vc_ftg_controller_checkpoint checkpoint;
    vc_ftg_open_response open_response;
    int result;

    init_controller_state(
        &state,
        run_id,
        run_id,
        0u,
        "baseline-and-generation-1-launch",
        VC_FTG_CONTROLLER_PHASE_1_RESULT_PATH,
        1u,
        VC_FTG_CONTROLLER_PHASE_1_TEST_OFFSET,
        VC_FTG_CONTROLLER_PHASE_1_TEST_COUNT,
        result_cleared);
    if (!record_result(
            &state,
            "clear-controller-result",
            result_cleared,
            result_cleared
                ? VC_FTG_RESULT_OK
                : VC_FTG_RESULT_PLATFORM_FAILURE)) {
        goto finish;
    }
    if (!record_result(
            &state,
            "controller-run-identity",
            run_id != 0u,
            run_id != 0u
                ? VC_FTG_RESULT_OK
                : VC_FTG_RESULT_PLATFORM_FAILURE)) {
        goto finish;
    }
    result = get_status(&status);
    record_diagnostic_snapshot(
        &state, "pre-target-baseline", &status);
    capture_lifecycle_counts(&baseline_counts, &status);
    if (!record_result(
            &state,
            "status-ready",
            result == VC_FTG_RESULT_OK &&
                status_ready_without_target(&status),
            result)) {
        goto finish;
    }
    result = open_target(&open_response);
    if (!record_result(
            &state,
            "controller-before-target-unavailable",
            result == VC_FTG_RESULT_TARGET_UNAVAILABLE,
            result)) {
        goto finish;
    }
    {
        const bool cleared = clear_artifact(VC_FTG_LAYOUT_PATH);

        if (!record_result(
                &state,
                "clear-fixture-layout",
                cleared,
                cleared
                    ? VC_FTG_RESULT_OK
                    : VC_FTG_RESULT_PLATFORM_FAILURE)) {
            goto finish;
        }
    }
    {
        const bool cleared = clear_artifact(VC_FTG_STARTUP_PATH);

        if (!record_result(
                &state,
                "clear-startup-stage",
                cleared,
                cleared
                    ? VC_FTG_RESULT_OK
                    : VC_FTG_RESULT_PLATFORM_FAILURE)) {
            goto finish;
        }
    }
    {
        const bool cleared =
            clear_artifact(VC_FTG_TARGET_RESULT_PATH);

        if (!record_result(
                &state,
                "clear-fixture-result",
                cleared,
                cleared
                    ? VC_FTG_RESULT_OK
                    : VC_FTG_RESULT_PLATFORM_FAILURE)) {
            goto finish;
        }
    }
    {
        const bool checkpoint_ready =
            vc_ftg_controller_checkpoint_init_generation_1(
                &checkpoint,
                run_id,
                run_id,
                &baseline_counts) &&
            write_checkpoint(&checkpoint);

        if (!checkpoint_ready ||
            !prepare_target_launch(
                &state,
                &checkpoint,
                "commit-target-launch")) {
            goto finish;
        }
    }
    result = launch_target();
    if (result < 0) {
        reject_target_launch(&state, &checkpoint, result);
        goto finish;
    }
    memset(&checkpoint, 0, sizeof(checkpoint));
    return 0;

finish:
    memset(&checkpoint, 0, sizeof(checkpoint));
    return finish_controller(&state);
}

static int run_phase_2(
    uint64_t run_id,
    uint64_t previous_run_id,
    vc_ftg_controller_checkpoint *checkpoint,
    bool result_cleared)
{
    controller_state state;
    vc_ftg_status_response_v2 status;
    lifecycle_counts exit_counts;
    vc_ftg_open_response open_response;
    vc_ftg_read_response read_response;
    vc_ftg_fixture_layout layout;
    uint64_t first_handle = 0u;
    int result;

    init_controller_state(
        &state,
        run_id,
        checkpoint->transaction_id,
        previous_run_id,
        "generation-1-validation-and-generation-2-launch",
        VC_FTG_CONTROLLER_PHASE_2_RESULT_PATH,
        2u,
        VC_FTG_CONTROLLER_PHASE_2_TEST_OFFSET,
        VC_FTG_CONTROLLER_PHASE_2_TEST_COUNT,
        result_cleared);
    if (!result_cleared) {
        state.failed = 1u;
        goto finish;
    }
    result = wait_for_target(true, &status);
    record_diagnostic_snapshot(
        &state, "generation-1-target-ready", &status);
    if (!record_result(
            &state,
            "target-start-observed",
            result == VC_FTG_RESULT_OK,
            result)) {
        goto finish;
    }
    {
        const bool profile_valid =
            generation_profile_valid(
                &status, &checkpoint->baseline_counts);

        if (!record_result(
                &state,
                "generation-1-create-bound-diagnostics",
                profile_valid,
                profile_valid
                    ? VC_FTG_RESULT_OK
                    : status.last_lifecycle_result)) {
            goto finish;
        }
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
    record_diagnostic_snapshot(
        &state, "generation-1-target-exited", &status);
    capture_lifecycle_counts(&exit_counts, &status);
    if (!record_result(
            &state,
            "target-exit-observed",
            result == VC_FTG_RESULT_OK,
            result)) {
        goto finish;
    }
    if (!record_result(
            &state,
            "generation-1-exit-diagnostics",
            status.counter_saturation_flags == 0u &&
                status.last_lifecycle_event ==
                    VC_FTG_PROCESS_EXITED &&
                status.last_lifecycle_result ==
                    VC_FTG_RESULT_OK &&
                status.last_lifecycle_stage ==
                    VC_FTG_DIAGNOSTIC_TARGET_EXITED,
            status.last_lifecycle_result)) {
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
        const bool checkpoint_ready =
            vc_ftg_controller_checkpoint_advance_generation_2(
                checkpoint,
                run_id,
                first_handle,
                &exit_counts);
        const bool cleared =
            checkpoint_ready &&
            clear_artifact(VC_FTG_LAYOUT_PATH);

        if (!record_result(
                &state,
                "clear-fixture-layout-before-relaunch",
                cleared,
                cleared
                    ? VC_FTG_RESULT_OK
                    : VC_FTG_RESULT_PLATFORM_FAILURE)) {
            goto finish;
        }
    }
    {
        const bool cleared = clear_artifact(VC_FTG_STARTUP_PATH);

        if (!record_result(
                &state,
                "clear-startup-stage-before-relaunch",
                cleared,
                cleared
                    ? VC_FTG_RESULT_OK
                    : VC_FTG_RESULT_PLATFORM_FAILURE)) {
            goto finish;
        }
    }
    {
        const bool cleared =
            clear_artifact(VC_FTG_TARGET_RESULT_PATH);

        if (!record_result(
                &state,
                "clear-fixture-result-before-relaunch",
                cleared,
                cleared
                    ? VC_FTG_RESULT_OK
                    : VC_FTG_RESULT_PLATFORM_FAILURE)) {
            goto finish;
        }
    }
    if (!prepare_target_launch(
            &state,
            checkpoint,
            "commit-target-relaunch")) {
        goto finish;
    }
    result = launch_target();
    if (result < 0) {
        reject_target_launch(&state, checkpoint, result);
        goto finish;
    }
    return 0;

finish:
    return finish_controller(&state);
}

static int run_phase_3(
    uint64_t run_id,
    uint64_t previous_run_id,
    vc_ftg_controller_checkpoint *checkpoint,
    bool result_cleared)
{
    controller_state state;
    vc_ftg_status_response_v2 status;
    vc_ftg_open_response open_response;
    vc_ftg_read_response read_response;
    vc_ftg_fixture_layout layout;
    uint64_t second_handle = 0u;
    int result;

    init_controller_state(
        &state,
        run_id,
        checkpoint->transaction_id,
        previous_run_id,
        "generation-2-validation",
        VC_FTG_CONTROLLER_PHASE_3_RESULT_PATH,
        3u,
        VC_FTG_CONTROLLER_PHASE_3_TEST_OFFSET,
        VC_FTG_CONTROLLER_PHASE_3_TEST_COUNT,
        result_cleared);
    if (!result_cleared) {
        state.failed = 1u;
        goto finish;
    }
    result = wait_for_target(true, &status);
    record_diagnostic_snapshot(
        &state, "generation-2-target-ready", &status);
    if (!record_result(
            &state,
            "target-relaunch-observed",
            result == VC_FTG_RESULT_OK,
            result)) {
        goto finish;
    }
    {
        const bool profile_valid =
            generation_profile_valid(
                &status, &checkpoint->exit_counts);

        if (!record_result(
                &state,
                "generation-2-create-bound-diagnostics",
                profile_valid,
                profile_valid
                    ? VC_FTG_RESULT_OK
                    : status.last_lifecycle_result)) {
            goto finish;
        }
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
        checkpoint->first_handle,
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
                open_response.handle !=
                    checkpoint->first_handle,
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
        bool checkpoint_cleared;

        memset(&close_request, 0, sizeof(close_request));
        memset(&close_response, 0, sizeof(close_response));
        close_request.version = VC_FTG_ABI_VERSION;
        close_request.struct_size = sizeof(close_request);
        close_request.capabilities =
            VC_FTG_CAPABILITY_FOREIGN_SEGMENT_READ;
        close_request.handle = second_handle;
        result = vc_ftg_decode_syscall_result(
            vcfgClose(&close_request, &close_response));
        checkpoint_cleared =
            result == VC_FTG_RESULT_OK &&
            clear_artifact(VC_FTG_CONTROLLER_CHECKPOINT_PATH);
        (void)record_result(
            &state,
            "close",
            checkpoint_cleared,
            checkpoint_cleared
                ? result
                : VC_FTG_RESULT_PLATFORM_FAILURE);
    }

finish:
    return finish_controller(&state);
}

int main(void)
{
    vc_ftg_controller_checkpoint checkpoint;
    vc_ftg_status_response_v2 status;
    SceInt64 run_time;
    uint64_t run_id = 0u;
    vc_ftg_controller_checkpoint_observation
        checkpoint_observation;
    uint64_t previous_run_id;
    bool result_cleared;
    int result;

    memset(&checkpoint, 0, sizeof(checkpoint));
    run_time = sceKernelGetSystemTimeWide();
    if (run_time > 0) {
        run_id = (uint64_t)run_time;
    }
    checkpoint_observation = read_checkpoint(&checkpoint);
    if (checkpoint_observation ==
        VC_FTG_CONTROLLER_CHECKPOINT_INVALID) {
        sceClibPrintf(
            "VCFG checkpoint rejected: malformed or partial\n");
        return 1;
    }
    if (checkpoint_observation ==
        VC_FTG_CONTROLLER_CHECKPOINT_VALID) {
        if (run_id == 0u ||
            run_id == checkpoint.previous_run_id) {
            sceClibPrintf(
                "VCFG checkpoint rejected: invalid run identity\n");
            return 1;
        }
        if (checkpoint.phase ==
            VC_FTG_CONTROLLER_PHASE_WAIT_GENERATION_1) {
            previous_run_id = checkpoint.previous_run_id;
            if (!vc_ftg_controller_checkpoint_begin_resume(
                    &checkpoint, run_id) ||
                !write_checkpoint(&checkpoint)) {
                (void)clear_artifact(
                    VC_FTG_CONTROLLER_CHECKPOINT_PATH);
                sceClibPrintf(
                    "VCFG checkpoint rejected: phase 2 claim failed\n");
                return 1;
            }
            result_cleared =
                clear_artifact(VC_FTG_CONTROLLER_RESULT_PATH) &&
                clear_artifact(
                    VC_FTG_CONTROLLER_PHASE_2_RESULT_PATH);
            return run_phase_2(
                run_id,
                previous_run_id,
                &checkpoint,
                result_cleared);
        }
        if (checkpoint.phase ==
            VC_FTG_CONTROLLER_PHASE_WAIT_GENERATION_2) {
            previous_run_id = checkpoint.previous_run_id;
            if (!vc_ftg_controller_checkpoint_begin_resume(
                    &checkpoint, run_id) ||
                !write_checkpoint(&checkpoint)) {
                (void)clear_artifact(
                    VC_FTG_CONTROLLER_CHECKPOINT_PATH);
                sceClibPrintf(
                    "VCFG checkpoint rejected: phase 3 claim failed\n");
                return 1;
            }
            result_cleared =
                clear_artifact(VC_FTG_CONTROLLER_RESULT_PATH) &&
                clear_artifact(
                    VC_FTG_CONTROLLER_PHASE_3_RESULT_PATH);
            return run_phase_3(
                run_id,
                previous_run_id,
                &checkpoint,
                result_cleared);
        }
        sceClibPrintf(
            "VCFG checkpoint rejected: invalid phase\n");
        return 1;
    }

    result = get_status(&status);
    if (result != VC_FTG_RESULT_OK ||
        !status_ready_without_target(&status)) {
        sceClibPrintf(
            "VCFG new transaction rejected: status=%d state=%u\n",
            result,
            status.base.target_state);
        return 1;
    }
    result_cleared =
        clear_artifact(VC_FTG_CONTROLLER_RESULT_PATH) &&
        clear_artifact(VC_FTG_CONTROLLER_PHASE_1_RESULT_PATH) &&
        clear_artifact(VC_FTG_CONTROLLER_PHASE_2_RESULT_PATH) &&
        clear_artifact(VC_FTG_CONTROLLER_PHASE_3_RESULT_PATH) &&
        clear_artifact(VC_FTG_CONTROLLER_CHECKPOINT_PATH);
    return run_phase_1(run_id, result_cleared);
}
