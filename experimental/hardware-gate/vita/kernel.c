#include "vitacheat/hardware_gate.h"

#include <psp2common/kernel/modulemgr.h>
#include <psp2kern/kernel/modulemgr.h>
#include <psp2kern/kernel/sysmem.h>
#include <psp2kern/kernel/sysroot.h>
#include <psp2kern/kernel/threadmgr.h>
#include <psp2kern/types.h>

#ifndef VITACHEAT_HARDWARE_GATE_ENABLE
#define VITACHEAT_HARDWARE_GATE_ENABLE 0
#endif

#ifndef VITACHEAT_HARDWARE_GATE_API_AVAILABLE
#define VITACHEAT_HARDWARE_GATE_API_AVAILABLE 0
#endif

#ifndef VITACHEAT_HARDWARE_GATE_TITLE_ID
#define VITACHEAT_HARDWARE_GATE_TITLE_ID "DISABLED0"
#endif

#ifndef VITACHEAT_HARDWARE_GATE_MODULE_NAME
#define VITACHEAT_HARDWARE_GATE_MODULE_NAME "VitaCheatHgClient"
#endif

#ifndef VITACHEAT_HARDWARE_GATE_FIRMWARE
#define VITACHEAT_HARDWARE_GATE_FIRMWARE UINT32_C(0)
#endif

_Static_assert(sizeof(VITACHEAT_HARDWARE_GATE_TITLE_ID) ==
                   VC_HG_TITLE_ID_LENGTH + 1u,
               "hardware-gate title ID must be exactly nine bytes");
_Static_assert(sizeof(VITACHEAT_HARDWARE_GATE_MODULE_NAME) <=
                   VC_HG_MODULE_NAME_CAPACITY,
               "hardware-gate module name is too long");
_Static_assert(sizeof(void *) == sizeof(uint32_t),
               "hardware-gate Vita pointers must be 32-bit");
_Static_assert(
    VC_HG_PERMISSION_USER_READ ==
        SCE_KERNEL_MEMORY_REF_PERM_USER_R,
    "hardware-gate read permission must match VitaSDK");
_Static_assert(
    VC_HG_PERMISSION_KNOWN_MASK ==
        (SCE_KERNEL_MEMORY_REF_PERM_USER_R |
         SCE_KERNEL_MEMORY_REF_PERM_USER_W |
         SCE_KERNEL_MEMORY_REF_PERM_USER_X |
         SCE_KERNEL_MEMORY_REF_PERM_KERN_R |
         SCE_KERNEL_MEMORY_REF_PERM_KERN_W |
         SCE_KERNEL_MEMORY_REF_PERM_KERN_X),
    "hardware-gate permission mask must match VitaSDK");
_Static_assert(VITACHEAT_HARDWARE_GATE_FIRMWARE == UINT32_C(0) ||
                   VITACHEAT_HARDWARE_GATE_FIRMWARE ==
                       UINT32_C(0x03650000),
               "hardware-gate supports only the pinned 3.65 target");

static vc_hg_service g_hardware_gate = VC_HG_SERVICE_INITIALIZER;

typedef struct native_context {
    vc_hg_platform_diagnostic diagnostic;
} native_context;

static native_context g_native_context;

static void native_zero(void *value, SceSize size)
{
    volatile uint8_t *bytes = (volatile uint8_t *)value;

    while (size != 0u) {
        *bytes++ = 0u;
        --size;
    }
}

static void native_copy(
    uint8_t *destination,
    const uint8_t *source,
    SceSize size)
{
    while (size != 0u) {
        *destination++ = *source++;
        --size;
    }
}

static void native_diagnostic_reset(void *context)
{
    native_context *native = (native_context *)context;

    native_zero(&native->diagnostic,
                sizeof(native->diagnostic));
}

static void native_diagnostic_fail(
    void *context,
    vc_hg_diagnostic_stage stage,
    int32_t raw_result)
{
    native_context *native = (native_context *)context;

    native->diagnostic.stage = (uint32_t)stage;
    native->diagnostic.raw_result = raw_result;
}

static bool native_get_diagnostic(
    void *context,
    vc_hg_platform_diagnostic *diagnostic)
{
    native_context *native = (native_context *)context;

    *diagnostic = native->diagnostic;
    return true;
}

static bool native_normalize_text(
    uint8_t *destination,
    SceSize destination_size,
    const char *source,
    SceSize source_size)
{
    SceSize index;

    native_zero(destination, destination_size);
    for (index = 0u;
         index < source_size && index < destination_size;
         ++index) {
        destination[index] = (uint8_t)source[index];
        if (source[index] == '\0') {
            return index != 0u;
        }
    }
    return false;
}

static bool native_get_caller_pid(
    void *context,
    uint32_t *process_id)
{
    SceUID pid;

    native_diagnostic_reset(context);
    pid = ksceKernelGetProcessId();
    if (pid <= 0) {
        native_diagnostic_fail(
            context, VC_HG_DIAGNOSTIC_CALLER_PID,
            (int32_t)pid);
        return false;
    }
    *process_id = (uint32_t)pid;
    return true;
}

static bool native_get_time_us(void *context, uint64_t *now_us)
{
    SceInt64 value;

    value = ksceKernelGetSystemTimeWide();
    if (value <= 0) {
        native_diagnostic_fail(
            context, VC_HG_DIAGNOSTIC_SYSTEM_TIME,
            (int32_t)value);
        return false;
    }
    *now_us = (uint64_t)value;
    return true;
}

static bool native_get_title_id(
    void *context,
    uint32_t process_id,
    uint8_t title_id[VC_HG_TITLE_ID_CAPACITY])
{
    char raw_title[VC_HG_TITLE_ID_CAPACITY];
    int result;

    native_zero(raw_title, sizeof(raw_title));
    native_zero(title_id, VC_HG_TITLE_ID_CAPACITY);
    result = ksceKernelGetProcessTitleId(
        (SceUID)process_id, raw_title, sizeof(raw_title));
    if (result < 0) {
        native_diagnostic_fail(
            context, VC_HG_DIAGNOSTIC_TITLE_QUERY,
            result);
        native_zero(raw_title, sizeof(raw_title));
        native_zero(title_id, VC_HG_TITLE_ID_CAPACITY);
        return false;
    }
    if (!native_normalize_text(
            title_id, VC_HG_TITLE_ID_CAPACITY,
            raw_title, sizeof(raw_title))) {
        native_diagnostic_fail(
            context, VC_HG_DIAGNOSTIC_TITLE_NORMALIZE,
            result);
        native_zero(raw_title, sizeof(raw_title));
        native_zero(title_id, VC_HG_TITLE_ID_CAPACITY);
        return false;
    }
    native_zero(raw_title, sizeof(raw_title));
    return true;
}

static bool native_get_main_module(
    void *context,
    uint32_t process_id,
    vc_hg_module_snapshot *module)
{
    SceKernelModuleInfo info;
    SceUID module_id;
    SceUInt32 fingerprint = 0u;
    uint32_t index;
    bool ended = false;
    int result;

    native_zero(module, sizeof(*module));
    native_zero(&info, sizeof(info));
    info.size = sizeof(info);
    module_id = ksceKernelGetModuleIdByPid((SceUID)process_id);
    if (module_id <= 0) {
        native_diagnostic_fail(
            context, VC_HG_DIAGNOSTIC_MODULE_ID,
            (int32_t)module_id);
        native_zero(&info, sizeof(info));
        native_zero(module, sizeof(*module));
        return false;
    }
    result = ksceKernelGetModuleInfo(
        (SceUID)process_id, module_id, &info);
    if (result < 0) {
        native_diagnostic_fail(
            context, VC_HG_DIAGNOSTIC_MODULE_INFO,
            result);
        native_zero(&info, sizeof(info));
        native_zero(module, sizeof(*module));
        return false;
    }
    if (info.modid != module_id) {
        native_diagnostic_fail(
            context,
            VC_HG_DIAGNOSTIC_MODULE_ID_MISMATCH,
            result);
        native_zero(&info, sizeof(info));
        native_zero(module, sizeof(*module));
        return false;
    }
    result = ksceKernelGetModuleFingerprint(
        module_id, &fingerprint);
    if (result < 0) {
        native_diagnostic_fail(
            context,
            VC_HG_DIAGNOSTIC_MODULE_FINGERPRINT,
            result);
        native_zero(&info, sizeof(info));
        native_zero(module, sizeof(*module));
        return false;
    }
    if (fingerprint == 0u) {
        native_diagnostic_fail(
            context,
            VC_HG_DIAGNOSTIC_MODULE_FINGERPRINT_ZERO,
            result);
        native_zero(&info, sizeof(info));
        native_zero(module, sizeof(*module));
        return false;
    }
    if (!native_normalize_text(
            module->module_name,
            sizeof(module->module_name),
            info.module_name,
            sizeof(info.module_name))) {
        native_diagnostic_fail(
            context, VC_HG_DIAGNOSTIC_MODULE_NAME,
            result);
        native_zero(&info, sizeof(info));
        native_zero(module, sizeof(*module));
        return false;
    }
    module->process_id = process_id;
    module->module_id = module_id;
    module->module_fingerprint = fingerprint;
    for (index = 0u; index < VC_HG_MAX_SEGMENTS; ++index) {
        const SceKernelSegmentInfo *segment =
            &info.segments[index];

        if (segment->size == 0u &&
            segment->vaddr == NULL &&
            segment->memsz == 0u) {
            ended = true;
            continue;
        }
        if (ended ||
            segment->size != sizeof(*segment) ||
            segment->vaddr == NULL ||
            segment->memsz == 0u) {
            native_diagnostic_fail(
                context, VC_HG_DIAGNOSTIC_MODULE_SEGMENTS,
                0);
            native_zero(&info, sizeof(info));
            native_zero(module, sizeof(*module));
            return false;
        }
        module->segments[index].base =
            (uintptr_t)segment->vaddr;
        module->segments[index].size = segment->memsz;
        module->segments[index].permissions =
            segment->perms;
        ++module->segment_count;
    }
    native_zero(&info, sizeof(info));
    if (module->segment_count == 0u) {
        native_diagnostic_fail(
            context, VC_HG_DIAGNOSTIC_MODULE_SEGMENTS,
            0);
        native_zero(module, sizeof(*module));
        return false;
    }
    return true;
}

static bool native_copy_from_user(
    void *context,
    uint32_t process_id,
    void *destination,
    const void *user_source,
    size_t size)
{
    int result;

    if (size > UINT32_MAX) {
        native_diagnostic_fail(
            context, VC_HG_DIAGNOSTIC_REQUEST_COPY, 0);
        return false;
    }
    result = ksceKernelCopyFromUserProc(
        (SceUID)process_id, destination,
        user_source, (SceSize)size);
    if (result != 0) {
        native_diagnostic_fail(
            context, VC_HG_DIAGNOSTIC_REQUEST_COPY,
            result);
        return false;
    }
    return true;
}

static bool native_copy_to_user(
    void *context,
    uint32_t process_id,
    void *user_destination,
    const void *source,
    size_t size)
{
    int result;

    if (size > UINT32_MAX) {
        native_diagnostic_fail(
            context, VC_HG_DIAGNOSTIC_RESPONSE_COPY, 0);
        return false;
    }
    result = ksceKernelCopyToUserProc(
        (SceUID)process_id, user_destination,
        source, (SceSize)size);
    if (result != 0) {
        native_diagnostic_fail(
            context, VC_HG_DIAGNOSTIC_RESPONSE_COPY,
            result);
        return false;
    }
    return true;
}

static bool native_read_process(
    void *context,
    uint32_t process_id,
    void *destination,
    uintptr_t source_address,
    size_t size)
{
    (void)context;
    return size <= VC_HG_MAX_READ &&
           ksceKernelCopyFromUserProc(
               (SceUID)process_id, destination,
               (const void *)source_address,
               (SceSize)size) == 0;
}

int vchgGetStatus(
    const vc_hg_status_request *request,
    vc_hg_status_response *response)
{
    return vc_hg_service_get_status(
        &g_hardware_gate, request, response);
}

int vchgOpenSelf(
    const vc_hg_open_request *request,
    vc_hg_open_response *response)
{
    return vc_hg_service_open_self(
        &g_hardware_gate, request, response);
}

int vchgGetSelfMainModule(
    const vc_hg_session_request *request,
    vc_hg_module_response *response)
{
    return vc_hg_service_get_self_main_module(
        &g_hardware_gate, request, response);
}

int vchgReadSelfSegment(
    const vc_hg_read_request *request,
    vc_hg_read_response *response)
{
    return vc_hg_service_read_self_segment(
        &g_hardware_gate, request, response);
}

int vchgClose(
    const vc_hg_close_request *request,
    vc_hg_close_response *response)
{
    return vc_hg_service_close(
        &g_hardware_gate, request, response);
}

int _start(SceSize args, void *argp)
    __attribute__((weak, alias("module_start")));

int module_start(SceSize args, void *argp)
{
    static const uint8_t expected_title[] =
        VITACHEAT_HARDWARE_GATE_TITLE_ID;
    static const uint8_t expected_module[] =
        VITACHEAT_HARDWARE_GATE_MODULE_NAME;
    vc_hg_dependencies dependencies;
    vc_hg_config config;
    SceKernelFwInfo firmware;
    SceInt64 time_us;
    uint64_t first_handle;

    (void)args;
    (void)argp;
    native_zero(&dependencies, sizeof(dependencies));
    native_zero(&config, sizeof(config));
    native_zero(&firmware, sizeof(firmware));
    native_zero(&g_native_context, sizeof(g_native_context));
    native_copy(
        config.expected_title_id,
        expected_title, sizeof(expected_title));
    native_copy(
        config.expected_module_name,
        expected_module, sizeof(expected_module));
    config.timeout_us = VC_HG_DEFAULT_TIMEOUT_US;
    config.enabled = VITACHEAT_HARDWARE_GATE_ENABLE != 0;
    firmware.size = sizeof(firmware);
    config.api_available =
        VITACHEAT_HARDWARE_GATE_API_AVAILABLE != 0 &&
        ksceKernelGetSystemSwVersion(&firmware) == 0 &&
        firmware.version ==
            VITACHEAT_HARDWARE_GATE_FIRMWARE;
    dependencies.get_caller_pid = native_get_caller_pid;
    dependencies.get_time_us = native_get_time_us;
    dependencies.get_title_id = native_get_title_id;
    dependencies.get_main_module = native_get_main_module;
    dependencies.get_diagnostic = native_get_diagnostic;
    dependencies.copy_from_user = native_copy_from_user;
    dependencies.copy_to_user = native_copy_to_user;
    dependencies.read_process = native_read_process;
    dependencies.context = &g_native_context;
    time_us = ksceKernelGetSystemTimeWide();
    first_handle = time_us > 0
                       ? ((uint64_t)time_us << 1u) | UINT64_C(1)
                       : UINT64_C(1);
    if (vc_hg_service_init(
            &g_hardware_gate, &config, &dependencies,
            first_handle) != VC_HG_RESULT_OK ||
        vc_hg_service_start(
            &g_hardware_gate) != VC_HG_RESULT_OK) {
        native_zero(&g_hardware_gate,
                    sizeof(g_hardware_gate));
        native_zero(&g_native_context,
                    sizeof(g_native_context));
        return SCE_KERNEL_START_FAILED;
    }
    return SCE_KERNEL_START_SUCCESS;
}

int module_stop(SceSize args, void *argp)
{
    vc_hg_result result;

    (void)args;
    (void)argp;
    result = vc_hg_service_stop(&g_hardware_gate);
    if (result == VC_HG_RESULT_BUSY) {
        return SCE_KERNEL_STOP_CANCEL;
    }
    if (result != VC_HG_RESULT_OK) {
        return SCE_KERNEL_STOP_FAIL;
    }
    native_zero(&g_hardware_gate, sizeof(g_hardware_gate));
    native_zero(&g_native_context, sizeof(g_native_context));
    return SCE_KERNEL_STOP_SUCCESS;
}
