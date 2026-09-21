#ifndef VITACHEAT_LAUNCH_BROKER_H
#define VITACHEAT_LAUNCH_BROKER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VC_LAUNCH_ABI_VERSION UINT16_C(1)
#define VC_LAUNCH_REQUEST_WIRE_SIZE UINT16_C(64)
#define VC_LAUNCH_RESPONSE_WIRE_SIZE UINT16_C(72)
#define VC_LAUNCH_DEFAULT_TTL_MS UINT64_C(5000)
#define VC_LAUNCH_MAX_TTL_MS UINT64_C(15000)

typedef enum vc_launch_operation {
    VC_LAUNCH_OPERATION_NONE = 0,
    VC_LAUNCH_OPERATION_SUBMIT = 1,
    VC_LAUNCH_OPERATION_CLAIM = 2,
    VC_LAUNCH_OPERATION_CANCEL = 3,
    VC_LAUNCH_OPERATION_STATUS = 4
} vc_launch_operation;

typedef enum vc_launch_caller_role {
    VC_LAUNCH_CALLER_SCE_SHELL = 1,
    VC_LAUNCH_CALLER_GAME_PLUGIN = 2
} vc_launch_caller_role;

typedef enum vc_launch_capability {
    VC_LAUNCH_CAPABILITY_LAUNCH = UINT32_C(1) << 0,
    VC_LAUNCH_CAPABILITY_CLAIM = UINT32_C(1) << 1,
    VC_LAUNCH_CAPABILITY_CANCEL = UINT32_C(1) << 2,
    VC_LAUNCH_CAPABILITY_STATUS = UINT32_C(1) << 3
} vc_launch_capability;

#define VC_LAUNCH_CAPABILITY_ALL                                             \
    (VC_LAUNCH_CAPABILITY_LAUNCH | VC_LAUNCH_CAPABILITY_CLAIM |             \
     VC_LAUNCH_CAPABILITY_CANCEL | VC_LAUNCH_CAPABILITY_STATUS)

typedef enum vc_launch_state {
    VC_LAUNCH_STATE_ABSENT = 0,
    VC_LAUNCH_STATE_PENDING = 1,
    VC_LAUNCH_STATE_CLAIMED = 2,
    VC_LAUNCH_STATE_EXPIRED = 3,
    VC_LAUNCH_STATE_CANCELLED = 4,
    VC_LAUNCH_STATE_STALE_TARGET = 5,
    VC_LAUNCH_STATE_CLOCK_ROLLBACK = 6
} vc_launch_state;

typedef enum vc_launch_status {
    VC_LAUNCH_STATUS_OK = 0,
    VC_LAUNCH_STATUS_ABSENT = 1,
    VC_LAUNCH_STATUS_PENDING = 2,
    VC_LAUNCH_STATUS_CLAIMED = 3,
    VC_LAUNCH_STATUS_EXPIRED = 4,
    VC_LAUNCH_STATUS_CANCELLED = 5,
    VC_LAUNCH_STATUS_STALE_TARGET = 6,
    VC_LAUNCH_STATUS_NOT_READY = 7,
    VC_LAUNCH_STATUS_CLOCK_ROLLBACK = 8,
    VC_LAUNCH_STATUS_INVALID_ARGUMENT = -1,
    VC_LAUNCH_STATUS_INVALID_SIZE = -2,
    VC_LAUNCH_STATUS_UNSUPPORTED_VERSION = -3,
    VC_LAUNCH_STATUS_UNSUPPORTED_OPERATION = -4,
    VC_LAUNCH_STATUS_INVALID_ROLE = -5,
    VC_LAUNCH_STATUS_INVALID_CAPABILITY = -6,
    VC_LAUNCH_STATUS_INVALID_RESERVED = -7,
    VC_LAUNCH_STATUS_INVALID_IDENTITY = -8,
    VC_LAUNCH_STATUS_WRONG_TARGET = -9,
    VC_LAUNCH_STATUS_WRONG_REQUEST_ID = -10,
    VC_LAUNCH_STATUS_ID_EXHAUSTED = -11,
    VC_LAUNCH_STATUS_MALFORMED_FIELD = -12
} vc_launch_status;

/*
 * These are native typed commands, not packed wire structs. struct_size is the
 * corresponding byte-ABI size and must equal VC_LAUNCH_REQUEST_WIRE_SIZE.
 */
typedef struct vc_launch_request {
    uint16_t version;
    uint16_t struct_size;
    uint16_t operation;
    uint16_t caller_role;
    uint32_t capabilities;
    uint32_t target_process_id;
    uint64_t target_generation;
    uint64_t request_id;
    uint64_t now_ms;
    uint64_t ttl_ms;
    uint32_t presentation_ready;
    uint32_t reserved0;
    uint64_t reserved1;
} vc_launch_request;

typedef struct vc_launch_response {
    uint16_t version;
    uint16_t struct_size;
    uint16_t operation;
    uint16_t reserved0;
    int32_t status;
    uint32_t launch_state;
    uint32_t capabilities;
    uint32_t target_process_id;
    uint64_t target_generation;
    uint64_t request_id;
    uint64_t created_ms;
    uint64_t deadline_ms;
    uint64_t observed_ms;
    uint64_t reserved1;
} vc_launch_response;

typedef struct vc_launch_record {
    uint64_t request_id;
    uint64_t target_generation;
    uint64_t created_ms;
    uint64_t deadline_ms;
    uint32_t target_process_id;
    uint32_t state;
} vc_launch_record;

typedef struct vc_launch_broker {
    vc_launch_record record;
    uint64_t next_request_id;
    uint64_t last_now_ms;
    uint64_t foreground_generation;
    uint32_t foreground_process_id;
    bool has_time;
    bool id_exhausted;
    bool initialized;
} vc_launch_broker;

/*
 * Wire roles describe the operation being requested; they are not caller
 * authentication. A privileged adapter must derive the role and process
 * identity from its trusted call context before constructing a request.
 */
void vc_launch_request_init(vc_launch_request *request,
                            vc_launch_operation operation,
                            vc_launch_caller_role caller_role);

vc_launch_status vc_launch_request_encode(const vc_launch_request *request,
                                          uint8_t *buffer,
                                          size_t buffer_size,
                                          size_t *encoded_size);

vc_launch_status vc_launch_request_decode(const uint8_t *buffer,
                                          size_t buffer_size,
                                          vc_launch_request *request);

vc_launch_status vc_launch_response_encode(const vc_launch_response *response,
                                           uint8_t *buffer,
                                           size_t buffer_size,
                                           size_t *encoded_size);

vc_launch_status vc_launch_response_decode(const uint8_t *buffer,
                                           size_t buffer_size,
                                           vc_launch_response *response);

vc_launch_status vc_launch_broker_init(vc_launch_broker *broker,
                                       uint64_t first_request_id);

/*
 * The broker stores at most one pending request and retains its terminal
 * record until a later submission replaces it. first_request_id seeds a
 * strictly increasing counter. UINT64_MAX may be issued once; wrap fails with
 * VC_LAUNCH_STATUS_ID_EXHAUSTED. Service reset does not reset this counter.
 *
 * Every time-taking call uses the caller's monotonic millisecond domain.
 * Rollback invalidates a pending request. Expiry happens at now_ms >= deadline.
 */
vc_launch_status vc_launch_broker_foreground_changed(
    vc_launch_broker *broker,
    uint32_t target_process_id,
    uint64_t target_generation,
    uint64_t now_ms);

vc_launch_status vc_launch_broker_process_exit(vc_launch_broker *broker,
                                               uint32_t target_process_id,
                                               uint64_t target_generation,
                                               uint64_t now_ms);

vc_launch_status vc_launch_broker_plugin_unload(vc_launch_broker *broker,
                                                uint32_t target_process_id,
                                                uint64_t target_generation,
                                                uint64_t now_ms);

vc_launch_status vc_launch_broker_service_reset(vc_launch_broker *broker,
                                                uint64_t now_ms);

vc_launch_status vc_launch_broker_tick(vc_launch_broker *broker,
                                       uint64_t now_ms);

/*
 * Native operations validate the complete request before authority changes.
 * Rejected requests leave the request record unchanged except when their valid
 * timestamp deterministically triggers expiry or clock-rollback cleanup.
 */
vc_launch_status vc_launch_broker_submit(vc_launch_broker *broker,
                                         const vc_launch_request *request,
                                         vc_launch_response *response);

vc_launch_status vc_launch_broker_claim(vc_launch_broker *broker,
                                        const vc_launch_request *request,
                                        vc_launch_response *response);

vc_launch_status vc_launch_broker_cancel(vc_launch_broker *broker,
                                         const vc_launch_request *request,
                                         vc_launch_response *response);

vc_launch_status vc_launch_broker_status(vc_launch_broker *broker,
                                         const vc_launch_request *request,
                                         vc_launch_response *response);

vc_launch_status vc_launch_broker_dispatch_wire(
    vc_launch_broker *broker,
    const uint8_t *request_buffer,
    size_t request_size,
    uint8_t *response_buffer,
    size_t response_capacity,
    size_t *response_size);

#ifdef __cplusplus
}
#endif

#endif
