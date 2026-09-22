#include "fixture_protocol.h"
#include "vitacheat/foreign_target_gate.h"
#include "vitacheat/foreign_target_startup.h"

#include <psp2/appmgr.h>
#include <psp2/ctrl.h>
#include <psp2/io/fcntl.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/modulemgr.h>
#include <psp2/kernel/threadmgr.h>

#include <stdint.h>

#define memcpy sceClibMemcpy
#define memset sceClibMemset
#define snprintf sceClibSnprintf

#ifndef VITACHEAT_FOREIGN_GATE_CONTROLLER_TITLE_ID
#error "The foreign-target fixture requires a controller title ID"
#endif

static const uint8_t g_foreign_sentinel[VC_FTG_MAX_READ]
    __attribute__((aligned(64), used)) =
        VC_FTG_SENTINEL_BYTES;

static int write_all(
    SceUID fd,
    const void *buffer,
    size_t size)
{
    const uint8_t *bytes = (const uint8_t *)buffer;

    while (size != 0u) {
        const SceSSize written = sceIoWrite(
            fd, bytes, (SceSize)size);

        if (written < 0) {
            return (int)written;
        }
        if (written == 0) {
            return VC_FTG_STARTUP_STATUS_ZERO_WRITE;
        }
        bytes += written;
        size -= (size_t)written;
    }
    return 0;
}

static int find_sentinel(
    vc_ftg_fixture_layout *layout)
{
    SceKernelModuleInfo module;
    SceUID module_id;
    int result;
    const uintptr_t sentinel_begin =
        (uintptr_t)g_foreign_sentinel;
    const uintptr_t sentinel_end =
        sentinel_begin + sizeof(g_foreign_sentinel);
    uint32_t index;

    memset(&module, 0, sizeof(module));
    module.size = sizeof(module);
    module_id = sceKernelGetModuleIdByAddr(
        (void *)g_foreign_sentinel);
    if (module_id <= 0) {
        return module_id < 0
            ? (int)module_id
            : VC_FTG_STARTUP_STATUS_SENTINEL_NOT_FOUND;
    }
    result = sceKernelGetModuleInfo(module_id, &module);
    if (result < 0) {
        return result;
    }
    for (index = 0u; index < VC_FTG_MAX_SEGMENTS; ++index) {
        const uintptr_t begin =
            (uintptr_t)module.segments[index].vaddr;
        uintptr_t end;

        if (module.segments[index].size !=
                sizeof(module.segments[index]) ||
            begin == (uintptr_t)0 ||
            module.segments[index].memsz == 0u ||
            begin > UINTPTR_MAX -
                module.segments[index].memsz) {
            continue;
        }
        end = begin + module.segments[index].memsz;
        if (sentinel_begin >= begin &&
            sentinel_end <= end) {
            memset(layout, 0, sizeof(*layout));
            layout->magic = VC_FTG_LAYOUT_MAGIC;
            layout->version = VC_FTG_LAYOUT_VERSION;
            layout->struct_size = sizeof(*layout);
            layout->segment_index = index;
            layout->segment_offset =
                (uint32_t)(sentinel_begin - begin);
            layout->sentinel_size =
                sizeof(g_foreign_sentinel);
            return 0;
        }
    }
    return VC_FTG_STARTUP_STATUS_SENTINEL_NOT_FOUND;
}

static int write_layout(
    const vc_ftg_fixture_layout *layout)
{
    const SceUID fd = sceIoOpen(
        VC_FTG_LAYOUT_PATH,
        SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC,
        0666);
    int result;
    int close_result;

    if (fd < 0) {
        return (int)fd;
    }
    result = write_all(fd, layout, sizeof(*layout));
    close_result = sceIoClose(fd);
    if (result == 0 && close_result < 0) {
        result = close_result;
    }
    return result;
}

static int write_fixture_result(
    bool layout_ok,
    int wrong_caller_result,
    int launch_result)
{
    char json[384];
    const SceUID fd = sceIoOpen(
        VC_FTG_TARGET_RESULT_PATH,
        SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC,
        0666);
    int size;
    int result = VC_FTG_STARTUP_STATUS_FORMAT_FAILED;
    int close_result;

    if (fd < 0) {
        return (int)fd;
    }
    size = snprintf(
        json,
        sizeof(json),
        "{\n"
        "  \"schema\":\"vitacheat.foreign-target-fixture.v1\",\n"
        "  \"title_id\":\"VCFT00001\",\n"
        "  \"sentinel_layout\":\"%s\",\n"
        "  \"wrong_caller_open\":{\"result\":\"%s\",\"code\":%d},\n"
        "  \"controller_launch_code\":%d\n"
        "}\n",
        layout_ok ? "ready" : "failed",
        wrong_caller_result ==
                VC_FTG_RESULT_CALLER_TITLE_MISMATCH
            ? "pass"
            : "fail",
        wrong_caller_result,
        launch_result);
    if (size > 0 && (size_t)size < sizeof(json)) {
        result = write_all(fd, json, (size_t)size);
    }
    close_result = sceIoClose(fd);
    if (result == 0 && close_result < 0) {
        result = close_result;
    }
    memset(json, 0, sizeof(json));
    return result;
}

static int write_startup_record(
    vc_ftg_startup_record *record)
{
    uint8_t encoded[VC_FTG_STARTUP_RECORD_SIZE];
    SceUID fd;
    int result;
    int close_result;

    memset(encoded, 0, sizeof(encoded));
    if (!vc_ftg_startup_record_encode(record, encoded)) {
        return VC_FTG_STARTUP_STATUS_FORMAT_FAILED;
    }
    fd = sceIoOpen(
        VC_FTG_STARTUP_PATH,
        SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC,
        0666);
    if (fd < 0) {
        memset(encoded, 0, sizeof(encoded));
        return (int)fd;
    }
    result = write_all(fd, encoded, sizeof(encoded));
    close_result = sceIoClose(fd);
    if (result == 0 && close_result < 0) {
        result = close_result;
    }
    memset(encoded, 0, sizeof(encoded));
    return result;
}

static int32_t readiness_open_exact_fixture(
    void *context,
    uint64_t *handle)
{
    vc_ftg_open_request request;
    vc_ftg_open_response response;
    int32_t result;

    (void)context;
    memset(&request, 0, sizeof(request));
    memset(&response, 0, sizeof(response));
    request.version = VC_FTG_ABI_VERSION;
    request.struct_size = sizeof(request);
    result = vc_ftg_decode_syscall_result(
        vcfgOpenExactFixture(&request, &response));
    *handle = response.handle;
    memset(&request, 0, sizeof(request));
    memset(&response, 0, sizeof(response));
    return result;
}

static bool readiness_get_time(
    void *context,
    uint64_t *now_us)
{
    const SceInt64 value = sceKernelGetSystemTimeWide();

    (void)context;
    if (value <= 0) {
        return false;
    }
    *now_us = (uint64_t)value;
    return true;
}

static bool readiness_delay(
    void *context,
    uint32_t delay_us)
{
    (void)context;
    return sceKernelDelayThread(delay_us) >= 0;
}

int main(void)
{
    vc_ftg_startup_record startup;
    vc_ftg_readiness_dependencies readiness_dependencies;
    vc_ftg_readiness_observation readiness;
    vc_ftg_fixture_layout layout;
    SceCtrlData pad;
    bool caller_ready;
    bool layout_ok;
    bool input_consumed = false;
    int sentinel_result;
    int caller_result;
    int layout_result;
    int fixture_result;
    int launch_result = 0;

    vc_ftg_startup_record_init(&startup);
    memset(
        &readiness_dependencies,
        0,
        sizeof(readiness_dependencies));
    readiness_dependencies.open_exact_fixture =
        readiness_open_exact_fixture;
    readiness_dependencies.get_time_us =
        readiness_get_time;
    readiness_dependencies.delay_us =
        readiness_delay;
    memset(&readiness, 0, sizeof(readiness));
    if (write_startup_record(&startup) < 0) {
        return 1;
    }
    memset(&layout, 0, sizeof(layout));
    sentinel_result = find_sentinel(&layout);
    if (!vc_ftg_startup_record_complete(
            &startup,
            VC_FTG_STARTUP_STAGE_SENTINEL_LOOKUP_COMPLETE,
            sentinel_result) ||
        write_startup_record(&startup) < 0) {
        return 1;
    }
    caller_ready = vc_ftg_probe_wrong_caller_readiness(
        &readiness_dependencies, &readiness);
    caller_result = readiness.last_result;
    if (!vc_ftg_startup_record_complete_readiness(
            &startup,
            &readiness) ||
        write_startup_record(&startup) < 0) {
        return 1;
    }
    if (!caller_ready) {
        return 1;
    }
    layout.wrong_caller_open_result = caller_result;
    layout_result = sentinel_result == 0
        ? write_layout(&layout)
        : VC_FTG_STARTUP_STATUS_SKIPPED;
    layout_ok = sentinel_result == 0 && layout_result == 0;
    if (!vc_ftg_startup_record_complete(
            &startup,
            VC_FTG_STARTUP_STAGE_LAYOUT_WRITE_COMPLETE,
            layout_result) ||
        write_startup_record(&startup) < 0) {
        return 1;
    }
    fixture_result = write_fixture_result(
        layout_ok, caller_result, launch_result);
    if (!vc_ftg_startup_record_complete(
            &startup,
            VC_FTG_STARTUP_STAGE_FIXTURE_WRITE_COMPLETE,
            fixture_result) ||
        write_startup_record(&startup) < 0) {
        return 1;
    }
    sceClibPrintf(
        "VCFG target layout=%s wrong_caller=%d\n",
        layout_ok ? "ready" : "failed",
        caller_result);
    sceClibPrintf(
        "VCFG target press X to launch controller %s\n",
        VITACHEAT_FOREIGN_GATE_CONTROLLER_TITLE_ID);
    if (sceCtrlSetSamplingMode(
            SCE_CTRL_MODE_ANALOG_WIDE) < 0 ||
        !vc_ftg_startup_record_mark(
            &startup,
            VC_FTG_STARTUP_STAGE_PROMPT_READY) ||
        write_startup_record(&startup) < 0) {
        return 1;
    }
    for (;;) {
        memset(&pad, 0, sizeof(pad));
        if (!input_consumed &&
            sceCtrlPeekBufferPositive(0, &pad, 1) > 0 &&
            (pad.buttons & SCE_CTRL_CROSS) != 0u) {
            if (!vc_ftg_startup_record_mark(
                    &startup,
                    VC_FTG_STARTUP_STAGE_CROSS_OBSERVED) ||
                write_startup_record(&startup) < 0) {
                return 1;
            }
            launch_result = sceAppMgrLaunchAppByUri(
                0x20000,
                "psgm:play?titleid="
                VITACHEAT_FOREIGN_GATE_CONTROLLER_TITLE_ID);
            if (!vc_ftg_startup_record_complete(
                    &startup,
                    VC_FTG_STARTUP_STAGE_CONTROLLER_LAUNCH_COMPLETE,
                    launch_result) ||
                write_startup_record(&startup) < 0) {
                return 1;
            }
            fixture_result = write_fixture_result(
                layout_ok,
                caller_result,
                launch_result);
            sceClibPrintf(
                "VCFG target controller_launch=%d fixture_write=%d\n",
                launch_result,
                fixture_result);
            input_consumed = true;
            (void)sceKernelDelayThread(UINT32_C(500000));
        } else {
            (void)sceKernelDelayThread(UINT32_C(50000));
        }
    }
}
