#ifndef VITACHEAT_FOREIGN_TARGET_GATE_H
#define VITACHEAT_FOREIGN_TARGET_GATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VC_FTG_ABI_VERSION UINT16_C(1)
#define VC_FTG_STATUS_VERSION_1 UINT16_C(1)
#define VC_FTG_STATUS_VERSION_2 UINT16_C(2)
#define VC_FTG_STATUS_VERSION_CURRENT VC_FTG_STATUS_VERSION_2
#define VC_FTG_TITLE_ID_LENGTH UINT32_C(9)
#define VC_FTG_TITLE_ID_CAPACITY UINT32_C(16)
#define VC_FTG_MODULE_NAME_CAPACITY UINT32_C(28)
#define VC_FTG_MAX_SEGMENTS UINT32_C(4)
#define VC_FTG_MAX_READ UINT32_C(64)
#define VC_FTG_DEFAULT_TIMEOUT_MS UINT32_C(2000)
#define VC_FTG_DEFAULT_TIMEOUT_US \
    (UINT64_C(1000) * VC_FTG_DEFAULT_TIMEOUT_MS)
#define VC_FTG_CAPABILITY_FOREIGN_SEGMENT_READ UINT32_C(0x00000001)
#define VC_FTG_PERMISSION_USER_READ UINT32_C(0x01)
#define VC_FTG_PERMISSION_KNOWN_MASK UINT32_C(0x77)
#define VC_FTG_SERVICE_INITIALIZER {0}

#define VC_FTG_ABI_FLAG_CALLER_PID_DERIVED UINT32_C(0x00000001)
#define VC_FTG_ABI_FLAG_EVENT_GENERATION_BOUND UINT32_C(0x00000002)
#define VC_FTG_ABI_FLAG_EXACT_TITLES_BOUND UINT32_C(0x00000004)
#define VC_FTG_ABI_FLAG_EXACT_MODULE_BOUND UINT32_C(0x00000008)
#define VC_FTG_ABI_FLAG_FIXED_BOUNCE UINT32_C(0x00000010)
#define VC_FTG_ABI_FLAG_NO_RAW_PID_OR_ADDRESS UINT32_C(0x00000020)
#define VC_FTG_ABI_FLAG_CREATE_GENERATION_BOUND UINT32_C(0x00000040)
#define VC_FTG_ABI_FLAGS_V1                                          \
    (VC_FTG_ABI_FLAG_CALLER_PID_DERIVED |                            \
     VC_FTG_ABI_FLAG_EVENT_GENERATION_BOUND |                        \
     VC_FTG_ABI_FLAG_EXACT_TITLES_BOUND |                            \
     VC_FTG_ABI_FLAG_EXACT_MODULE_BOUND |                            \
     VC_FTG_ABI_FLAG_FIXED_BOUNCE |                                  \
     VC_FTG_ABI_FLAG_NO_RAW_PID_OR_ADDRESS)
#define VC_FTG_ABI_FLAGS                                             \
    (VC_FTG_ABI_FLAGS_V1 |                                           \
     VC_FTG_ABI_FLAG_CREATE_GENERATION_BOUND)

#define VC_FTG_COUNTER_SAT_CREATE_CALLBACK UINT32_C(0x00000001)
#define VC_FTG_COUNTER_SAT_START_CALLBACK UINT32_C(0x00000002)
#define VC_FTG_COUNTER_SAT_START_REVALIDATION UINT32_C(0x00000004)
#define VC_FTG_COUNTER_SAT_IGNORED_NON_TARGET UINT32_C(0x00000008)
#define VC_FTG_COUNTER_SAT_TARGET_CREATE_MATCH UINT32_C(0x00000010)
#define VC_FTG_COUNTER_SAT_TARGET_CREATE_AUTHORIZED UINT32_C(0x00000020)
#define VC_FTG_COUNTER_SAT_KNOWN_MASK UINT32_C(0x0000003F)

typedef enum vc_ftg_result {
    VC_FTG_RESULT_OK = 0,
    VC_FTG_RESULT_INVALID_ARGUMENT = -1,
    VC_FTG_RESULT_INVALID_SIZE = -2,
    VC_FTG_RESULT_INVALID_VERSION = -3,
    VC_FTG_RESULT_RESERVED_NOT_ZERO = -4,
    VC_FTG_RESULT_CAPABILITY_MISMATCH = -5,
    VC_FTG_RESULT_COPY_FROM_USER_FAILED = -6,
    VC_FTG_RESULT_COPY_TO_USER_FAILED = -7,
    VC_FTG_RESULT_DISABLED = -8,
    VC_FTG_RESULT_NOT_RUNNING = -9,
    VC_FTG_RESULT_BUSY = -10,
    VC_FTG_RESULT_CALLER_UNAVAILABLE = -11,
    VC_FTG_RESULT_CALLER_TITLE_MISMATCH = -12,
    VC_FTG_RESULT_TARGET_UNAVAILABLE = -13,
    VC_FTG_RESULT_MODULE_UNAVAILABLE = -14,
    VC_FTG_RESULT_MODULE_MISMATCH = -15,
    VC_FTG_RESULT_LIFECYCLE_COMPROMISED = -16,
    VC_FTG_RESULT_INVALID_HANDLE = -17,
    VC_FTG_RESULT_EXPIRED = -18,
    VC_FTG_RESULT_GENERATION_MISMATCH = -19,
    VC_FTG_RESULT_REVISION_MISMATCH = -20,
    VC_FTG_RESULT_INVALID_SEGMENT = -21,
    VC_FTG_RESULT_RANGE_DENIED = -22,
    VC_FTG_RESULT_PERMISSION_DENIED = -23,
    VC_FTG_RESULT_PLATFORM_FAILURE = -24,
    VC_FTG_RESULT_HANDLE_EXHAUSTED = -25,
    VC_FTG_RESULT_GENERATION_EXHAUSTED = -26,
    VC_FTG_RESULT_REVISION_EXHAUSTED = -27,
    VC_FTG_RESULT_DIAGNOSTIC_ONLY = -28,
    VC_FTG_RESULT_STOPPED = -29
} vc_ftg_result;

static inline int32_t vc_ftg_decode_syscall_result(
    int32_t raw_result)
{
    const uint32_t raw_bits = (uint32_t)raw_result;
    const int32_t decoded =
        (int32_t)(raw_bits | UINT32_C(0x40000000));

    if ((raw_bits & UINT32_C(0xC0000000)) ==
            UINT32_C(0x80000000) &&
        decoded >= (int32_t)VC_FTG_RESULT_STOPPED &&
        decoded < (int32_t)VC_FTG_RESULT_OK) {
        return decoded;
    }
    return raw_result;
}

typedef enum vc_ftg_runtime_status {
    VC_FTG_RUNTIME_DISABLED = 0,
    VC_FTG_RUNTIME_DIAGNOSTIC_ONLY = 1,
    VC_FTG_RUNTIME_READY = 2,
    VC_FTG_RUNTIME_TARGET_AVAILABLE = 3,
    VC_FTG_RUNTIME_SESSION_OPEN = 4,
    VC_FTG_RUNTIME_FAIL_CLOSED = 5,
    VC_FTG_RUNTIME_STOPPED = 6,
    VC_FTG_RUNTIME_API_MISMATCH = 7
} vc_ftg_runtime_status;

typedef enum vc_ftg_target_state {
    VC_FTG_TARGET_NONE = 0,
    VC_FTG_TARGET_CREATED = 1,
    VC_FTG_TARGET_STARTED = 2
} vc_ftg_target_state;

typedef enum vc_ftg_process_event {
    VC_FTG_PROCESS_CREATED = 1,
    VC_FTG_PROCESS_STARTED = 2,
    VC_FTG_PROCESS_EXITED = 3,
    VC_FTG_PROCESS_KILLED = 4
} vc_ftg_process_event;

typedef enum vc_ftg_diagnostic_stage {
    VC_FTG_DIAGNOSTIC_NONE = 0,
    VC_FTG_DIAGNOSTIC_REGISTERED = 1,
    VC_FTG_DIAGNOSTIC_INITIAL_RECONCILIATION_UNPROVEN = 2,
    VC_FTG_DIAGNOSTIC_BACKGROUND_LIFECYCLE_UNPROVEN = 3,
    VC_FTG_DIAGNOSTIC_TARGET_CREATED = 4,
    VC_FTG_DIAGNOSTIC_TARGET_STARTED = 5,
    VC_FTG_DIAGNOSTIC_TARGET_EXITED = 6,
    VC_FTG_DIAGNOSTIC_TARGET_KILLED = 7,
    VC_FTG_DIAGNOSTIC_DUPLICATE_EVENT = 8,
    VC_FTG_DIAGNOSTIC_OUT_OF_ORDER_EVENT = 9,
    VC_FTG_DIAGNOSTIC_CALLBACK_CONTENTION = 10,
    VC_FTG_DIAGNOSTIC_TITLE_QUERY = 11,
    VC_FTG_DIAGNOSTIC_MODULE_QUERY = 12,
    VC_FTG_DIAGNOSTIC_MODULE_MISMATCH = 13,
    VC_FTG_DIAGNOSTIC_GENERATION_EXHAUSTED = 14,
    VC_FTG_DIAGNOSTIC_REVISION_EXHAUSTED = 15,
    VC_FTG_DIAGNOSTIC_REGISTRATION_FAILED = 16,
    VC_FTG_DIAGNOSTIC_UNREGISTER_FAILED = 17,
    VC_FTG_DIAGNOSTIC_TARGET_CREATE_BOUND = 18,
    VC_FTG_DIAGNOSTIC_TARGET_START_REVALIDATED = 19,
    VC_FTG_DIAGNOSTIC_TARGET_IDENTITY_MISMATCH = 20,
    VC_FTG_DIAGNOSTIC_NON_TARGET_IGNORED = 21,
    VC_FTG_DIAGNOSTIC_STAGE_COUNT = 22
} vc_ftg_diagnostic_stage;

typedef struct vc_ftg_status_request {
    uint16_t version;
    uint16_t struct_size;
    uint32_t capabilities;
    uint32_t reserved0;
    uint32_t reserved1;
} vc_ftg_status_request;

typedef struct vc_ftg_status_response {
    uint16_t version;
    uint16_t struct_size;
    int32_t status;
    uint32_t capabilities;
    uint32_t max_read;
    uint32_t timeout_ms;
    uint32_t abi_flags;
    uint32_t diagnostic_stage;
    int32_t last_result;
    uint32_t target_state;
    uint32_t reserved0;
    uint32_t reserved1;
    uint32_t reserved2;
} vc_ftg_status_response;

typedef struct vc_ftg_status_response_v2 {
    vc_ftg_status_response base;
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
    uint32_t reserved0;
    uint32_t reserved1;
} vc_ftg_status_response_v2;

typedef vc_ftg_status_request vc_ftg_open_request;

typedef struct vc_ftg_open_response {
    uint16_t version;
    uint16_t struct_size;
    int32_t status;
    uint32_t capabilities;
    uint64_t handle;
    uint32_t timeout_ms;
    uint32_t reserved0;
} vc_ftg_open_response;

typedef struct vc_ftg_session_request {
    uint16_t version;
    uint16_t struct_size;
    uint32_t capabilities;
    uint64_t handle;
    uint64_t reserved0;
} vc_ftg_session_request;

typedef struct vc_ftg_read_request {
    uint16_t version;
    uint16_t struct_size;
    uint32_t capabilities;
    uint64_t handle;
    uint32_t segment_index;
    uint32_t offset;
    uint32_t length;
    uint32_t reserved0;
} vc_ftg_read_request;

typedef struct vc_ftg_read_response {
    uint16_t version;
    uint16_t struct_size;
    int32_t status;
    uint32_t length;
    uint32_t reserved0;
    uint8_t bytes[VC_FTG_MAX_READ];
} vc_ftg_read_response;

typedef vc_ftg_session_request vc_ftg_close_request;

typedef struct vc_ftg_close_response {
    uint16_t version;
    uint16_t struct_size;
    int32_t status;
    uint32_t reserved0;
    uint32_t reserved1;
} vc_ftg_close_response;

_Static_assert(sizeof(vc_ftg_status_request) == 16,
               "foreign-target status request ABI changed");
_Static_assert(sizeof(vc_ftg_status_response) == 48,
               "foreign-target status response ABI changed");
_Static_assert(sizeof(vc_ftg_status_response_v2) == 96,
               "foreign-target status v2 response ABI changed");
_Static_assert(offsetof(vc_ftg_status_response_v2,
                       create_callback_count) == 48,
               "foreign-target status v2 append offset changed");
_Static_assert(sizeof(vc_ftg_open_response) == 32,
               "foreign-target open response ABI changed");
_Static_assert(sizeof(vc_ftg_session_request) == 24,
               "foreign-target session request ABI changed");
_Static_assert(sizeof(vc_ftg_read_request) == 32,
               "foreign-target read request ABI changed");
_Static_assert(sizeof(vc_ftg_read_response) == 80,
               "foreign-target read response ABI changed");
_Static_assert(sizeof(vc_ftg_close_response) == 16,
               "foreign-target close response ABI changed");

typedef struct vc_ftg_segment_snapshot {
    uintptr_t base;
    uint32_t size;
    uint32_t permissions;
} vc_ftg_segment_snapshot;

typedef struct vc_ftg_module_snapshot {
    uint32_t process_id;
    int32_t kernel_module_id;
    int32_t process_module_id;
    uint32_t module_fingerprint;
    uint32_t segment_count;
    uint8_t module_name[VC_FTG_MODULE_NAME_CAPACITY];
    vc_ftg_segment_snapshot segments[VC_FTG_MAX_SEGMENTS];
} vc_ftg_module_snapshot;

typedef bool (*vc_ftg_get_caller_pid_fn)(
    void *context,
    uint32_t *process_id);
typedef bool (*vc_ftg_get_time_us_fn)(
    void *context,
    uint64_t *now_us);
typedef bool (*vc_ftg_get_title_id_fn)(
    void *context,
    uint32_t process_id,
    uint8_t title_id[VC_FTG_TITLE_ID_CAPACITY]);
typedef bool (*vc_ftg_get_main_module_fn)(
    void *context,
    uint32_t process_id,
    vc_ftg_module_snapshot *module);
typedef bool (*vc_ftg_copy_from_user_fn)(
    void *context,
    uint32_t process_id,
    void *destination,
    const void *user_source,
    size_t size);
typedef bool (*vc_ftg_copy_to_user_fn)(
    void *context,
    uint32_t process_id,
    void *user_destination,
    const void *source,
    size_t size);
typedef bool (*vc_ftg_read_process_fn)(
    void *context,
    uint32_t process_id,
    void *destination,
    uintptr_t source_address,
    size_t size);

typedef struct vc_ftg_dependencies {
    vc_ftg_get_caller_pid_fn get_caller_pid;
    vc_ftg_get_time_us_fn get_time_us;
    vc_ftg_get_title_id_fn get_title_id;
    vc_ftg_get_main_module_fn get_main_module;
    vc_ftg_copy_from_user_fn copy_from_user;
    vc_ftg_copy_to_user_fn copy_to_user;
    vc_ftg_read_process_fn read_process;
    void *context;
} vc_ftg_dependencies;

typedef struct vc_ftg_config {
    uint8_t target_title_id[VC_FTG_TITLE_ID_CAPACITY];
    uint8_t controller_title_id[VC_FTG_TITLE_ID_CAPACITY];
    uint8_t target_module_name[VC_FTG_MODULE_NAME_CAPACITY];
    uint64_t timeout_us;
    bool enabled;
    bool api_available;
    bool foreign_lifecycle_enabled;
} vc_ftg_config;

typedef struct vc_ftg_registry {
    vc_ftg_module_snapshot module;
    uint64_t generation;
    uint64_t lifecycle_revision;
    uint32_t process_id;
    uint32_t state;
} vc_ftg_registry;

typedef struct vc_ftg_session {
    vc_ftg_module_snapshot module;
    uint64_t handle;
    uint64_t issued_at_us;
    uint64_t deadline_us;
    uint64_t target_generation;
    uint64_t lifecycle_revision;
    uint32_t caller_process_id;
    bool active;
} vc_ftg_session;

typedef struct vc_ftg_service {
    vc_ftg_dependencies dependencies;
    vc_ftg_config config;
    vc_ftg_registry registry;
    vc_ftg_session session;
    atomic_uint transaction_busy;
    atomic_uint operation_active;
    atomic_uint callback_active;
    atomic_uint fail_closed_stage;
    atomic_uint create_callback_count;
    atomic_uint start_callback_count;
    atomic_uint start_revalidation_count;
    atomic_uint ignored_non_target_count;
    atomic_uint target_create_match_count;
    atomic_uint target_create_authorized_count;
    atomic_uint counter_saturation_flags;
    atomic_uint last_lifecycle_event;
    atomic_int last_lifecycle_result;
    atomic_uint last_lifecycle_stage;
    uint64_t next_generation;
    uint64_t next_revision;
    uint64_t next_handle;
    int32_t last_result;
    uint32_t diagnostic_stage;
    uint32_t marker;
    bool running;
    bool registered;
    bool handle_exhausted;
    bool generation_exhausted;
    bool revision_exhausted;
    bool runtime_unload_blocked;
} vc_ftg_service;

vc_ftg_result vc_ftg_service_init(
    vc_ftg_service *service,
    const vc_ftg_config *config,
    const vc_ftg_dependencies *dependencies,
    uint64_t first_generation,
    uint64_t first_revision,
    uint64_t first_handle);

vc_ftg_result vc_ftg_service_start(vc_ftg_service *service);
vc_ftg_result vc_ftg_service_set_registered(
    vc_ftg_service *service,
    bool registered,
    vc_ftg_diagnostic_stage failure_stage);
vc_ftg_result vc_ftg_service_stop(vc_ftg_service *service);

vc_ftg_result vc_ftg_service_process_event(
    vc_ftg_service *service,
    vc_ftg_process_event event,
    uint32_t process_id);

vc_ftg_result vc_ftg_service_process_event_with_type(
    vc_ftg_service *service,
    vc_ftg_process_event event,
    uint32_t process_id,
    uint32_t event_type);

bool vc_ftg_service_runtime_unload_allowed(
    const vc_ftg_service *service);

vc_ftg_result vc_ftg_service_get_status(
    vc_ftg_service *service,
    const void *request_user,
    void *response_user);

vc_ftg_result vc_ftg_service_open_exact_fixture(
    vc_ftg_service *service,
    const void *request_user,
    void *response_user);

vc_ftg_result vc_ftg_service_read_fixture_segment(
    vc_ftg_service *service,
    const void *request_user,
    void *response_user);

vc_ftg_result vc_ftg_service_close(
    vc_ftg_service *service,
    const void *request_user,
    void *response_user);

int vcfgGetStatus(
    const vc_ftg_status_request *request,
    void *response);
int vcfgOpenExactFixture(
    const vc_ftg_open_request *request,
    vc_ftg_open_response *response);
int vcfgReadFixtureSegment(
    const vc_ftg_read_request *request,
    vc_ftg_read_response *response);
int vcfgClose(
    const vc_ftg_close_request *request,
    vc_ftg_close_response *response);

#ifdef __cplusplus
}
#endif

#endif
