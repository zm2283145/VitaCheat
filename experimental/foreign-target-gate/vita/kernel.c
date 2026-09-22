#include "vitacheat/foreign_target_gate.h"

#include <psp2common/kernel/modulemgr.h>
#include <psp2kern/kernel/modulemgr.h>
#include <psp2kern/kernel/proc_event.h>
#include <psp2kern/kernel/sysmem.h>
#include <psp2kern/kernel/sysroot.h>
#include <psp2kern/kernel/threadmgr.h>
#include <psp2kern/types.h>

#ifndef VITACHEAT_FOREIGN_GATE_ENABLE
#define VITACHEAT_FOREIGN_GATE_ENABLE 0
#endif

#ifndef VITACHEAT_FOREIGN_GATE_API_AVAILABLE
#define VITACHEAT_FOREIGN_GATE_API_AVAILABLE 0
#endif

#ifndef VITACHEAT_FOREIGN_GATE_TARGET_TITLE_ID
#define VITACHEAT_FOREIGN_GATE_TARGET_TITLE_ID "DISABLED0"
#endif

#ifndef VITACHEAT_FOREIGN_GATE_CONTROLLER_TITLE_ID
#define VITACHEAT_FOREIGN_GATE_CONTROLLER_TITLE_ID "DISABLED1"
#endif

#ifndef VITACHEAT_FOREIGN_GATE_TARGET_MODULE
#define VITACHEAT_FOREIGN_GATE_TARGET_MODULE "DisabledTarget"
#endif

#ifndef VITACHEAT_FOREIGN_GATE_FIRMWARE
#define VITACHEAT_FOREIGN_GATE_FIRMWARE UINT32_C(0)
#endif

_Static_assert(sizeof(VITACHEAT_FOREIGN_GATE_TARGET_TITLE_ID) ==
                   VC_FTG_TITLE_ID_LENGTH + 1u,
               "foreign target title ID must be nine bytes");
_Static_assert(sizeof(VITACHEAT_FOREIGN_GATE_CONTROLLER_TITLE_ID) ==
                   VC_FTG_TITLE_ID_LENGTH + 1u,
               "foreign controller title ID must be nine bytes");
_Static_assert(sizeof(VITACHEAT_FOREIGN_GATE_TARGET_MODULE) <=
                   VC_FTG_MODULE_NAME_CAPACITY,
               "foreign target module name is too long");
_Static_assert(sizeof(void *) == sizeof(uint32_t),
               "foreign-target Vita pointers must be 32-bit");
_Static_assert(
    VC_FTG_PERMISSION_USER_READ ==
        SCE_KERNEL_MEMORY_REF_PERM_USER_R,
    "foreign-target read permission must match VitaSDK");
_Static_assert(
    VC_FTG_PERMISSION_KNOWN_MASK ==
        (SCE_KERNEL_MEMORY_REF_PERM_USER_R |
         SCE_KERNEL_MEMORY_REF_PERM_USER_W |
         SCE_KERNEL_MEMORY_REF_PERM_USER_X |
         SCE_KERNEL_MEMORY_REF_PERM_KERN_R |
         SCE_KERNEL_MEMORY_REF_PERM_KERN_W |
         SCE_KERNEL_MEMORY_REF_PERM_KERN_X),
    "foreign-target permission mask must match VitaSDK");
_Static_assert(VITACHEAT_FOREIGN_GATE_FIRMWARE == UINT32_C(0) ||
                   VITACHEAT_FOREIGN_GATE_FIRMWARE ==
                       UINT32_C(0x03650000),
               "foreign-target gate supports only firmware 3.65");

static vc_ftg_service g_foreign_gate = VC_FTG_SERVICE_INITIALIZER;
static SceUID g_proc_event_uid = -1;

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

static bool native_get_caller(
    void *context,
    uint32_t *process_id)
{
    const SceUID pid = ksceKernelGetProcessId();

    (void)context;
    if (pid <= 0) {
        return false;
    }
    *process_id = (uint32_t)pid;
    return true;
}

static bool native_get_time(
    void *context,
    uint64_t *now_us)
{
    const SceInt64 value = ksceKernelGetSystemTimeWide();

    (void)context;
    if (value <= 0) {
        return false;
    }
    *now_us = (uint64_t)value;
    return true;
}

static bool native_get_title(
    void *context,
    uint32_t process_id,
    uint8_t title_id[VC_FTG_TITLE_ID_CAPACITY])
{
    char raw_title[VC_FTG_TITLE_ID_CAPACITY];
    int result;

    (void)context;
    native_zero(raw_title, sizeof(raw_title));
    native_zero(title_id, VC_FTG_TITLE_ID_CAPACITY);
    result = ksceKernelSysrootGetProcessTitleId(
        (SceUID)process_id,
        raw_title,
        sizeof(raw_title));
    if (result < 0 ||
        !native_normalize_text(
            title_id,
            VC_FTG_TITLE_ID_CAPACITY,
            raw_title,
            sizeof(raw_title))) {
        native_zero(raw_title, sizeof(raw_title));
        native_zero(title_id, VC_FTG_TITLE_ID_CAPACITY);
        return false;
    }
    native_zero(raw_title, sizeof(raw_title));
    return true;
}

static bool native_get_module(
    void *context,
    uint32_t process_id,
    vc_ftg_module_snapshot *module)
{
    SceKernelModuleInfo info;
    SceUID kernel_module_id;
    SceUInt32 fingerprint = 0u;
    uint32_t index;
    bool ended = false;
    int result;

    (void)context;
    native_zero(module, sizeof(*module));
    native_zero(&info, sizeof(info));
    info.size = sizeof(info);
    kernel_module_id =
        ksceKernelGetModuleIdByPid((SceUID)process_id);
    if (kernel_module_id <= 0) {
        return false;
    }
    result = ksceKernelGetModuleInfo(
        (SceUID)process_id,
        kernel_module_id,
        &info);
    if (result < 0 || info.modid <= 0) {
        native_zero(&info, sizeof(info));
        return false;
    }
    result = ksceKernelGetModuleFingerprint(
        kernel_module_id, &fingerprint);
    if (result < 0 || fingerprint == 0u ||
        !native_normalize_text(
            module->module_name,
            sizeof(module->module_name),
            info.module_name,
            sizeof(info.module_name))) {
        native_zero(&info, sizeof(info));
        native_zero(module, sizeof(*module));
        return false;
    }
    module->process_id = process_id;
    module->kernel_module_id = kernel_module_id;
    module->process_module_id = info.modid;
    module->module_fingerprint = fingerprint;
    for (index = 0u; index < VC_FTG_MAX_SEGMENTS; ++index) {
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
            native_zero(&info, sizeof(info));
            native_zero(module, sizeof(*module));
            return false;
        }
        module->segments[index].base =
            (uintptr_t)segment->vaddr;
        module->segments[index].size = segment->memsz;
        module->segments[index].permissions = segment->perms;
        ++module->segment_count;
    }
    native_zero(&info, sizeof(info));
    return module->segment_count != 0u;
}

static bool native_copy_from(
    void *context,
    uint32_t process_id,
    void *destination,
    const void *source,
    size_t size)
{
    (void)context;
    return size <= UINT32_MAX &&
           ksceKernelCopyFromUserProc(
               (SceUID)process_id,
               destination,
               source,
               (SceSize)size) == 0;
}

static bool native_copy_to(
    void *context,
    uint32_t process_id,
    void *destination,
    const void *source,
    size_t size)
{
    (void)context;
    return size <= UINT32_MAX &&
           ksceKernelCopyToUserProc(
               (SceUID)process_id,
               destination,
               source,
               (SceSize)size) == 0;
}

static bool native_read_process(
    void *context,
    uint32_t process_id,
    void *destination,
    uintptr_t source_address,
    size_t size)
{
    (void)context;
    return size <= VC_FTG_MAX_READ &&
           ksceKernelCopyFromUserProc(
               (SceUID)process_id,
               destination,
               (const void *)source_address,
               (SceSize)size) == 0;
}

static int native_process_created(
    SceUID pid,
    SceProcEventInvokeParam2 *param,
    int flags)
{
    (void)param;
    (void)flags;
    (void)vc_ftg_service_process_event(
        &g_foreign_gate,
        VC_FTG_PROCESS_CREATED,
        (uint32_t)pid);
    return 0;
}

static int native_process_started(
    SceUID pid,
    int event_type,
    SceProcEventInvokeParam1 *param,
    int flags)
{
    (void)param;
    (void)flags;
    (void)vc_ftg_service_process_event_with_type(
        &g_foreign_gate,
        VC_FTG_PROCESS_STARTED,
        (uint32_t)pid,
        (uint32_t)event_type);
    return 0;
}

static int native_process_exited(
    SceUID pid,
    SceProcEventInvokeParam1 *param,
    int flags)
{
    (void)param;
    (void)flags;
    (void)vc_ftg_service_process_event(
        &g_foreign_gate,
        VC_FTG_PROCESS_EXITED,
        (uint32_t)pid);
    return 0;
}

static int native_process_killed(
    SceUID pid,
    SceProcEventInvokeParam1 *param,
    int flags)
{
    (void)param;
    (void)flags;
    (void)vc_ftg_service_process_event(
        &g_foreign_gate,
        VC_FTG_PROCESS_KILLED,
        (uint32_t)pid);
    return 0;
}

static const SceProcEventHandler g_process_handlers = {
    sizeof(g_process_handlers),
    native_process_created,
    native_process_exited,
    native_process_killed,
    NULL,
    native_process_started,
    NULL
};

int vcfgGetStatus(
    const vc_ftg_status_request *request,
    void *response)
{
    return vc_ftg_service_get_status(
        &g_foreign_gate, request, response);
}

int vcfgOpenExactFixture(
    const vc_ftg_open_request *request,
    vc_ftg_open_response *response)
{
    return vc_ftg_service_open_exact_fixture(
        &g_foreign_gate, request, response);
}

int vcfgReadFixtureSegment(
    const vc_ftg_read_request *request,
    vc_ftg_read_response *response)
{
    return vc_ftg_service_read_fixture_segment(
        &g_foreign_gate, request, response);
}

int vcfgClose(
    const vc_ftg_close_request *request,
    vc_ftg_close_response *response)
{
    return vc_ftg_service_close(
        &g_foreign_gate, request, response);
}

int _start(SceSize args, void *argp)
    __attribute__((weak, alias("module_start")));

int module_start(SceSize args, void *argp)
{
    static const uint8_t target_title[] =
        VITACHEAT_FOREIGN_GATE_TARGET_TITLE_ID;
    static const uint8_t controller_title[] =
        VITACHEAT_FOREIGN_GATE_CONTROLLER_TITLE_ID;
    static const uint8_t target_module[] =
        VITACHEAT_FOREIGN_GATE_TARGET_MODULE;
    vc_ftg_dependencies dependencies;
    vc_ftg_config config;
    SceKernelFwInfo firmware;
    SceInt64 time_us;
    uint64_t seed;

    (void)args;
    (void)argp;
    g_proc_event_uid = -1;
    native_zero(&dependencies, sizeof(dependencies));
    native_zero(&config, sizeof(config));
    native_zero(&firmware, sizeof(firmware));
    native_copy(
        config.target_title_id,
        target_title,
        sizeof(target_title));
    native_copy(
        config.controller_title_id,
        controller_title,
        sizeof(controller_title));
    native_copy(
        config.target_module_name,
        target_module,
        sizeof(target_module));
    config.timeout_us = VC_FTG_DEFAULT_TIMEOUT_US;
    config.enabled = VITACHEAT_FOREIGN_GATE_ENABLE != 0;
    firmware.size = sizeof(firmware);
    config.api_available =
        VITACHEAT_FOREIGN_GATE_API_AVAILABLE != 0 &&
        ksceKernelGetSystemSwVersion(&firmware) == 0 &&
        firmware.version == VITACHEAT_FOREIGN_GATE_FIRMWARE;
    config.foreign_lifecycle_enabled = config.api_available;

    dependencies.get_caller_pid = native_get_caller;
    dependencies.get_time_us = native_get_time;
    dependencies.get_title_id = native_get_title;
    dependencies.get_main_module = native_get_module;
    dependencies.copy_from_user = native_copy_from;
    dependencies.copy_to_user = native_copy_to;
    dependencies.read_process = native_read_process;

    time_us = ksceKernelGetSystemTimeWide();
    seed = time_us > 0 ? (uint64_t)time_us : UINT64_C(1);
    if (vc_ftg_service_init(
            &g_foreign_gate,
            &config,
            &dependencies,
            UINT64_C(1),
            UINT64_C(1),
            (seed << 1u) | UINT64_C(1)) !=
            VC_FTG_RESULT_OK ||
        vc_ftg_service_start(&g_foreign_gate) !=
            VC_FTG_RESULT_OK) {
        native_zero(
            &g_foreign_gate, sizeof(g_foreign_gate));
        return SCE_KERNEL_START_FAILED;
    }
    if (!config.api_available) {
        (void)vc_ftg_service_set_registered(
            &g_foreign_gate,
            false,
            VC_FTG_DIAGNOSTIC_REGISTRATION_FAILED);
        return SCE_KERNEL_START_SUCCESS;
    }
    g_proc_event_uid = ksceKernelRegisterProcEventHandler(
        "VitaCheatForeignGate",
        &g_process_handlers,
        0);
    if (g_proc_event_uid <= 0) {
        (void)vc_ftg_service_set_registered(
            &g_foreign_gate,
            false,
            VC_FTG_DIAGNOSTIC_REGISTRATION_FAILED);
        return SCE_KERNEL_START_SUCCESS;
    }
    (void)vc_ftg_service_set_registered(
        &g_foreign_gate,
        true,
        VC_FTG_DIAGNOSTIC_REGISTRATION_FAILED);
    return SCE_KERNEL_START_SUCCESS;
}

int module_stop(SceSize args, void *argp)
{
    vc_ftg_result result;
    int unregister_result;

    (void)args;
    (void)argp;
    result = vc_ftg_service_stop(&g_foreign_gate);
    if (result == VC_FTG_RESULT_BUSY) {
        return SCE_KERNEL_STOP_CANCEL;
    }
    if (result != VC_FTG_RESULT_OK) {
        return SCE_KERNEL_STOP_FAIL;
    }
    if (g_proc_event_uid > 0) {
        unregister_result =
            ksceKernelUnregisterProcEventHandler(
                g_proc_event_uid);
        if (unregister_result < 0) {
            return SCE_KERNEL_STOP_CANCEL;
        }
        g_proc_event_uid = -1;
    }
    if (atomic_load_explicit(
            &g_foreign_gate.callback_active,
            memory_order_acquire) != 0u ||
        atomic_load_explicit(
            &g_foreign_gate.operation_active,
            memory_order_acquire) != 0u) {
        return SCE_KERNEL_STOP_CANCEL;
    }
    if (!vc_ftg_service_runtime_unload_allowed(
            &g_foreign_gate)) {
        return SCE_KERNEL_STOP_CANCEL;
    }
    native_zero(
        &g_foreign_gate, sizeof(g_foreign_gate));
    return SCE_KERNEL_STOP_SUCCESS;
}
