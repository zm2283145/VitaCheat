#ifndef VITACHEAT_HARDWARE_GATE_H
#define VITACHEAT_HARDWARE_GATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VC_HG_ABI_VERSION UINT16_C(1)
#define VC_HG_STATUS_DIAGNOSTIC_VERSION UINT16_C(2)
#define VC_HG_TITLE_ID_LENGTH UINT32_C(9)
#define VC_HG_TITLE_ID_CAPACITY UINT32_C(16)
#define VC_HG_MODULE_NAME_CAPACITY UINT32_C(28)
#define VC_HG_MAX_SEGMENTS UINT32_C(4)
#define VC_HG_MAX_READ UINT32_C(64)
#define VC_HG_DEFAULT_TIMEOUT_MS UINT32_C(2000)
#define VC_HG_DEFAULT_TIMEOUT_US \
    (UINT64_C(1000) * VC_HG_DEFAULT_TIMEOUT_MS)
#define VC_HG_CAPABILITY_SELF_SEGMENT_READ UINT32_C(0x00000001)
#define VC_HG_KNOWN_CAPABILITIES VC_HG_CAPABILITY_SELF_SEGMENT_READ
#define VC_HG_PERMISSION_USER_READ UINT32_C(0x01)
#define VC_HG_PERMISSION_KNOWN_MASK UINT32_C(0x77)
#define VC_HG_SERVICE_INITIALIZER {0}

#define VC_HG_ABI_FLAG_CALLER_PID_DERIVED UINT32_C(0x00000001)
#define VC_HG_ABI_FLAG_EXACT_TITLE_BOUND UINT32_C(0x00000002)
#define VC_HG_ABI_FLAG_EXACT_MODULE_BOUND UINT32_C(0x00000004)
#define VC_HG_ABI_FLAG_FIXED_BOUNCE UINT32_C(0x00000008)
#define VC_HG_ABI_FLAG_SAME_PROCESS_ONLY UINT32_C(0x00000010)
#define VC_HG_ABI_FLAGS                                                    \
    (VC_HG_ABI_FLAG_CALLER_PID_DERIVED |                                  \
     VC_HG_ABI_FLAG_EXACT_TITLE_BOUND |                                   \
     VC_HG_ABI_FLAG_EXACT_MODULE_BOUND |                                  \
     VC_HG_ABI_FLAG_FIXED_BOUNCE |                                        \
     VC_HG_ABI_FLAG_SAME_PROCESS_ONLY)

typedef enum vc_hg_result {
    VC_HG_RESULT_OK = 0,
    VC_HG_RESULT_INVALID_ARGUMENT = -1,
    VC_HG_RESULT_INVALID_SIZE = -2,
    VC_HG_RESULT_INVALID_VERSION = -3,
    VC_HG_RESULT_RESERVED_NOT_ZERO = -4,
    VC_HG_RESULT_CAPABILITY_MISMATCH = -5,
    VC_HG_RESULT_COPY_FROM_USER_FAILED = -6,
    VC_HG_RESULT_COPY_TO_USER_FAILED = -7,
    VC_HG_RESULT_DISABLED = -8,
    VC_HG_RESULT_NOT_RUNNING = -9,
    VC_HG_RESULT_BUSY = -10,
    VC_HG_RESULT_CALLER_UNAVAILABLE = -11,
    VC_HG_RESULT_TITLE_MISMATCH = -12,
    VC_HG_RESULT_MODULE_UNAVAILABLE = -13,
    VC_HG_RESULT_MODULE_MISMATCH = -14,
    VC_HG_RESULT_SESSION_ACTIVE = -15,
    VC_HG_RESULT_INVALID_HANDLE = -16,
    VC_HG_RESULT_EXPIRED = -17,
    VC_HG_RESULT_CALLER_MISMATCH = -18,
    VC_HG_RESULT_INVALID_SEGMENT = -19,
    VC_HG_RESULT_RANGE_DENIED = -20,
    VC_HG_RESULT_PERMISSION_DENIED = -21,
    VC_HG_RESULT_PLATFORM_FAILURE = -22,
    VC_HG_RESULT_HANDLE_EXHAUSTED = -23,
    VC_HG_RESULT_STOPPED = -24
} vc_hg_result;

/*
 * The first Vita run transported -13 as 0xBFFFFFF3 (bit 30 cleared).
 * Decode only the gate's narrow, known result range; preserve all SCE errors.
 */
static inline int32_t vc_hg_decode_syscall_result(int32_t raw_result) {
    const uint32_t raw_bits = (uint32_t)raw_result;
    const int32_t decoded = (int32_t)(raw_bits | UINT32_C(0x40000000));

    if ((raw_bits & UINT32_C(0xC0000000)) == UINT32_C(0x80000000) &&
        decoded >= (int32_t)VC_HG_RESULT_STOPPED &&
        decoded < (int32_t)VC_HG_RESULT_OK) {
        return decoded;
    }
    return raw_result;
}

typedef enum vc_hg_diagnostic_stage {
    VC_HG_DIAGNOSTIC_NONE = 0,
    VC_HG_DIAGNOSTIC_CALLER_PID = 1,
    VC_HG_DIAGNOSTIC_REQUEST_COPY = 2,
    VC_HG_DIAGNOSTIC_SYSTEM_TIME = 3,
    VC_HG_DIAGNOSTIC_TITLE_QUERY = 4,
    VC_HG_DIAGNOSTIC_TITLE_NORMALIZE = 5,
    VC_HG_DIAGNOSTIC_MODULE_ID = 6,
    VC_HG_DIAGNOSTIC_MODULE_INFO = 7,
    VC_HG_DIAGNOSTIC_MODULE_ID_MISMATCH = 8,
    VC_HG_DIAGNOSTIC_MODULE_FINGERPRINT = 9,
    VC_HG_DIAGNOSTIC_MODULE_FINGERPRINT_ZERO = 10,
    VC_HG_DIAGNOSTIC_MODULE_NAME = 11,
    VC_HG_DIAGNOSTIC_MODULE_SEGMENTS = 12,
    VC_HG_DIAGNOSTIC_RESPONSE_COPY = 13,
    VC_HG_DIAGNOSTIC_MODULE_PROCESS_UID = 14,
    VC_HG_DIAGNOSTIC_STAGE_COUNT = 15
} vc_hg_diagnostic_stage;

typedef enum vc_hg_runtime_status {
    VC_HG_RUNTIME_DISABLED = 0,
    VC_HG_RUNTIME_READY = 1,
    VC_HG_RUNTIME_SESSION_OPEN = 2,
    VC_HG_RUNTIME_STOPPED = 3,
    VC_HG_RUNTIME_API_MISMATCH = 4,
    VC_HG_RUNTIME_UNAVAILABLE = 5
} vc_hg_runtime_status;

typedef struct vc_hg_status_request {
    uint16_t version;
    uint16_t struct_size;
    uint32_t capabilities;
    uint32_t reserved0;
    uint32_t reserved1;
} vc_hg_status_request;

typedef struct vc_hg_status_response {
    uint16_t version;
    uint16_t struct_size;
    int32_t status;
    uint32_t capabilities;
    uint32_t max_read;
    uint32_t timeout_ms;
    uint32_t max_segments;
    uint32_t abi_flags;
    int32_t last_result;
} vc_hg_status_response;

typedef struct vc_hg_status_response_v2 {
    vc_hg_status_response base;
    uint32_t diagnostic_stage;
    int32_t diagnostic_raw_result;
    uint32_t reserved0;
    uint32_t reserved1;
} vc_hg_status_response_v2;

typedef vc_hg_status_request vc_hg_open_request;

typedef struct vc_hg_open_response {
    uint16_t version;
    uint16_t struct_size;
    int32_t status;
    uint32_t capabilities;
    uint32_t reserved0;
    uint64_t handle;
    uint64_t deadline_us;
    uint32_t segment_count;
    uint32_t module_fingerprint;
} vc_hg_open_response;

typedef struct vc_hg_session_request {
    uint16_t version;
    uint16_t struct_size;
    uint32_t capabilities;
    uint64_t handle;
    uint64_t reserved0;
} vc_hg_session_request;

typedef struct vc_hg_public_segment {
    uint32_t segment_index;
    uint32_t permissions;
    uint32_t size;
    uint32_t reserved0;
} vc_hg_public_segment;

typedef struct vc_hg_module_response {
    uint16_t version;
    uint16_t struct_size;
    int32_t status;
    uint32_t capabilities;
    uint32_t segment_count;
    uint64_t handle;
    uint32_t module_fingerprint;
    uint32_t module_name_size;
    uint8_t module_name[VC_HG_MODULE_NAME_CAPACITY];
    uint32_t reserved0;
    vc_hg_public_segment segments[VC_HG_MAX_SEGMENTS];
} vc_hg_module_response;

typedef struct vc_hg_read_request {
    uint16_t version;
    uint16_t struct_size;
    uint32_t capabilities;
    uint64_t handle;
    uint32_t segment_index;
    uint32_t offset;
    uint32_t length;
    uint32_t reserved0;
} vc_hg_read_request;

typedef struct vc_hg_read_response {
    uint16_t version;
    uint16_t struct_size;
    int32_t status;
    uint32_t length;
    uint32_t reserved0;
    uint8_t bytes[VC_HG_MAX_READ];
} vc_hg_read_response;

typedef vc_hg_session_request vc_hg_close_request;

typedef struct vc_hg_close_response {
    uint16_t version;
    uint16_t struct_size;
    int32_t status;
    uint32_t reserved0;
    uint32_t reserved1;
} vc_hg_close_response;

_Static_assert(sizeof(vc_hg_status_request) == 16,
               "hardware-gate status request ABI changed");
_Static_assert(sizeof(vc_hg_status_response) == 32,
               "hardware-gate status response ABI changed");
_Static_assert(sizeof(vc_hg_status_response_v2) == 48,
               "hardware-gate status response v2 ABI changed");
_Static_assert(offsetof(vc_hg_status_response_v2, diagnostic_stage) == 32,
               "hardware-gate status response v2 prefix changed");
_Static_assert(sizeof(vc_hg_open_request) == 16,
               "hardware-gate open request ABI changed");
_Static_assert(sizeof(vc_hg_open_response) == 40,
               "hardware-gate open response ABI changed");
_Static_assert(sizeof(vc_hg_session_request) == 24,
               "hardware-gate session request ABI changed");
_Static_assert(sizeof(vc_hg_public_segment) == 16,
               "hardware-gate segment ABI changed");
_Static_assert(sizeof(vc_hg_module_response) == 128,
               "hardware-gate module response ABI changed");
_Static_assert(sizeof(vc_hg_read_request) == 32,
               "hardware-gate read request ABI changed");
_Static_assert(sizeof(vc_hg_read_response) == 80,
               "hardware-gate read response ABI changed");
_Static_assert(sizeof(vc_hg_close_request) == 24,
               "hardware-gate close request ABI changed");
_Static_assert(sizeof(vc_hg_close_response) == 16,
               "hardware-gate close response ABI changed");

typedef struct vc_hg_segment_snapshot {
    uintptr_t base;
    uint32_t size;
    uint32_t permissions;
} vc_hg_segment_snapshot;

typedef struct vc_hg_module_snapshot {
    uint32_t process_id;
    int32_t kernel_module_id;
    int32_t process_module_id;
    uint32_t module_fingerprint;
    uint32_t segment_count;
    uint8_t module_name[VC_HG_MODULE_NAME_CAPACITY];
    vc_hg_segment_snapshot segments[VC_HG_MAX_SEGMENTS];
} vc_hg_module_snapshot;

typedef struct vc_hg_platform_diagnostic {
    uint32_t stage;
    int32_t raw_result;
} vc_hg_platform_diagnostic;

_Static_assert(sizeof(vc_hg_platform_diagnostic) == 8,
               "hardware-gate platform diagnostic size changed");

typedef bool (*vc_hg_get_caller_pid_fn)(
    void *context,
    uint32_t *process_id);
typedef bool (*vc_hg_get_time_us_fn)(
    void *context,
    uint64_t *now_us);
typedef bool (*vc_hg_get_title_id_fn)(
    void *context,
    uint32_t process_id,
    uint8_t title_id[VC_HG_TITLE_ID_CAPACITY]);
typedef bool (*vc_hg_get_main_module_fn)(
    void *context,
    uint32_t process_id,
    vc_hg_module_snapshot *module);
typedef bool (*vc_hg_get_diagnostic_fn)(
    void *context,
    vc_hg_platform_diagnostic *diagnostic);
typedef bool (*vc_hg_copy_from_user_fn)(
    void *context,
    uint32_t process_id,
    void *destination,
    const void *user_source,
    size_t size);
typedef bool (*vc_hg_copy_to_user_fn)(
    void *context,
    uint32_t process_id,
    void *user_destination,
    const void *source,
    size_t size);
typedef bool (*vc_hg_read_process_fn)(
    void *context,
    uint32_t process_id,
    void *destination,
    uintptr_t source_address,
    size_t size);

typedef struct vc_hg_dependencies {
    vc_hg_get_caller_pid_fn get_caller_pid;
    vc_hg_get_time_us_fn get_time_us;
    vc_hg_get_title_id_fn get_title_id;
    vc_hg_get_main_module_fn get_main_module;
    vc_hg_get_diagnostic_fn get_diagnostic;
    vc_hg_copy_from_user_fn copy_from_user;
    vc_hg_copy_to_user_fn copy_to_user;
    vc_hg_read_process_fn read_process;
    void *context;
} vc_hg_dependencies;

typedef struct vc_hg_config {
    uint8_t expected_title_id[VC_HG_TITLE_ID_CAPACITY];
    uint8_t expected_module_name[VC_HG_MODULE_NAME_CAPACITY];
    uint64_t timeout_us;
    bool enabled;
    bool api_available;
} vc_hg_config;

typedef struct vc_hg_session {
    vc_hg_module_snapshot module;
    uint64_t handle;
    uint64_t deadline_us;
    uint32_t process_id;
    bool active;
} vc_hg_session;

typedef struct vc_hg_service {
    vc_hg_dependencies dependencies;
    vc_hg_config config;
    vc_hg_session session;
    atomic_uint transaction_busy;
    atomic_uint adapter_active;
    uint64_t next_handle;
    vc_hg_result last_result;
    uint32_t last_diagnostic_stage;
    int32_t last_diagnostic_raw_result;
    uint32_t marker;
    bool running;
    bool handle_exhausted;
} vc_hg_service;

vc_hg_result vc_hg_service_init(
    vc_hg_service *service,
    const vc_hg_config *config,
    const vc_hg_dependencies *dependencies,
    uint64_t first_handle);

vc_hg_result vc_hg_service_start(vc_hg_service *service);
vc_hg_result vc_hg_service_stop(vc_hg_service *service);

vc_hg_result vc_hg_service_get_status(
    vc_hg_service *service,
    const void *request_user,
    void *response_user);

vc_hg_result vc_hg_service_open_self(
    vc_hg_service *service,
    const void *request_user,
    void *response_user);

vc_hg_result vc_hg_service_get_self_main_module(
    vc_hg_service *service,
    const void *request_user,
    void *response_user);

vc_hg_result vc_hg_service_read_self_segment(
    vc_hg_service *service,
    const void *request_user,
    void *response_user);

vc_hg_result vc_hg_service_close(
    vc_hg_service *service,
    const void *request_user,
    void *response_user);

int vchgGetStatus(
    const vc_hg_status_request *request,
    vc_hg_status_response *response);
int vchgOpenSelf(
    const vc_hg_open_request *request,
    vc_hg_open_response *response);
int vchgGetSelfMainModule(
    const vc_hg_session_request *request,
    vc_hg_module_response *response);
int vchgReadSelfSegment(
    const vc_hg_read_request *request,
    vc_hg_read_response *response);
int vchgClose(
    const vc_hg_close_request *request,
    vc_hg_close_response *response);

#ifdef __cplusplus
}
#endif

#endif
