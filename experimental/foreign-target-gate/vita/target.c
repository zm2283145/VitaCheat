#include "fixture_protocol.h"
#include "vitacheat/foreign_target_gate.h"

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

static bool find_sentinel(
    vc_ftg_fixture_layout *layout)
{
    SceKernelModuleInfo module;
    SceUID module_id;
    const uintptr_t sentinel_begin =
        (uintptr_t)g_foreign_sentinel;
    const uintptr_t sentinel_end =
        sentinel_begin + sizeof(g_foreign_sentinel);
    uint32_t index;

    memset(&module, 0, sizeof(module));
    module.size = sizeof(module);
    module_id = sceKernelGetModuleIdByAddr(
        (void *)g_foreign_sentinel);
    if (module_id <= 0 ||
        sceKernelGetModuleInfo(module_id, &module) < 0) {
        return false;
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
            return true;
        }
    }
    return false;
}

static bool write_layout(
    const vc_ftg_fixture_layout *layout)
{
    const SceUID fd = sceIoOpen(
        VC_FTG_LAYOUT_PATH,
        SCE_O_WRONLY | SCE_O_CREAT | SCE_O_TRUNC,
        0666);
    bool result;

    if (fd < 0) {
        return false;
    }
    result = write_all(fd, layout, sizeof(*layout));
    if (sceIoClose(fd) < 0) {
        result = false;
    }
    return result;
}

static void write_fixture_result(
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

    if (fd < 0) {
        return;
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
        (void)write_all(fd, json, (size_t)size);
    }
    (void)sceIoClose(fd);
}

static int wrong_caller_open(void)
{
    vc_ftg_open_request request;
    vc_ftg_open_response response;

    memset(&request, 0, sizeof(request));
    memset(&response, 0, sizeof(response));
    request.version = VC_FTG_ABI_VERSION;
    request.struct_size = sizeof(request);
    return vc_ftg_decode_syscall_result(
        vcfgOpenExactFixture(&request, &response));
}

int main(void)
{
    vc_ftg_fixture_layout layout;
    SceCtrlData pad;
    bool layout_ok;
    int caller_result;
    int launch_result = 0;

    memset(&layout, 0, sizeof(layout));
    layout_ok = find_sentinel(&layout);
    caller_result = wrong_caller_open();
    layout.wrong_caller_open_result = caller_result;
    layout_ok = layout_ok && write_layout(&layout);
    write_fixture_result(
        layout_ok, caller_result, launch_result);
    sceClibPrintf(
        "VCFG target layout=%s wrong_caller=%d\n",
        layout_ok ? "ready" : "failed",
        caller_result);
    sceClibPrintf(
        "VCFG target press X to launch controller %s\n",
        VITACHEAT_FOREIGN_GATE_CONTROLLER_TITLE_ID);
    (void)sceCtrlSetSamplingMode(
        SCE_CTRL_MODE_ANALOG_WIDE);
    for (;;) {
        memset(&pad, 0, sizeof(pad));
        if (sceCtrlPeekBufferPositive(0, &pad, 1) > 0 &&
            (pad.buttons & SCE_CTRL_CROSS) != 0u) {
            launch_result = sceAppMgrLaunchAppByUri(
                0x20000,
                "psgm:play?titleid="
                VITACHEAT_FOREIGN_GATE_CONTROLLER_TITLE_ID);
            write_fixture_result(
                layout_ok,
                caller_result,
                launch_result);
            sceClibPrintf(
                "VCFG target controller_launch=%d\n",
                launch_result);
            (void)sceKernelDelayThread(UINT32_C(500000));
        } else {
            (void)sceKernelDelayThread(UINT32_C(50000));
        }
    }
}
