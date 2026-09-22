#ifndef VITACHEAT_MEMORY_READ_H
#define VITACHEAT_MEMORY_READ_H

#include "vitacheat/launch_broker.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VC_MEMORY_READ_ABI_VERSION UINT16_C(1)
#define VC_MEMORY_READ_REQUEST_WIRE_SIZE UINT16_C(80)
#define VC_MEMORY_READ_RESPONSE_HEADER_WIRE_SIZE UINT16_C(64)
#define VC_MEMORY_READ_MAX_PAYLOAD UINT32_C(256)
#define VC_MEMORY_READ_RESPONSE_WIRE_MAX                           \
    (VC_MEMORY_READ_RESPONSE_HEADER_WIRE_SIZE +                    \
     VC_MEMORY_READ_MAX_PAYLOAD)
#define VC_MEMORY_READ_CAPABILITY_READ_TARGET (UINT32_C(1) << 0)

typedef enum vc_memory_read_operation {
    VC_MEMORY_READ_OPERATION_NONE = 0,
    VC_MEMORY_READ_OPERATION_READ = 1,
    VC_MEMORY_READ_OPERATION_STATUS = 2
} vc_memory_read_operation;

typedef enum vc_memory_read_state {
    VC_MEMORY_READ_STATE_INACTIVE = 0,
    VC_MEMORY_READ_STATE_ACTIVE = 1,
    VC_MEMORY_READ_STATE_EXPIRED = 2,
    VC_MEMORY_READ_STATE_REVOKED = 3,
    VC_MEMORY_READ_STATE_ERROR = 4
} vc_memory_read_state;

typedef enum vc_memory_read_status {
    VC_MEMORY_READ_STATUS_OK = 0,
    VC_MEMORY_READ_STATUS_INVALID_ARGUMENT = -1,
    VC_MEMORY_READ_STATUS_INVALID_SIZE = -2,
    VC_MEMORY_READ_STATUS_UNSUPPORTED_VERSION = -3,
    VC_MEMORY_READ_STATUS_UNSUPPORTED_OPERATION = -4,
    VC_MEMORY_READ_STATUS_INVALID_ROLE = -5,
    VC_MEMORY_READ_STATUS_INVALID_CAPABILITY = -6,
    VC_MEMORY_READ_STATUS_INVALID_RESERVED = -7,
    VC_MEMORY_READ_STATUS_MALFORMED_FIELD = -8,
    VC_MEMORY_READ_STATUS_UNAUTHENTICATED_CALLER = -9,
    VC_MEMORY_READ_STATUS_UNAUTHORIZED_CALLER = -10,
    VC_MEMORY_READ_STATUS_NO_AUTHORIZATION = -11,
    VC_MEMORY_READ_STATUS_STALE_SESSION = -12,
    VC_MEMORY_READ_STATUS_STALE_TARGET = -13,
    VC_MEMORY_READ_STATUS_STALE_SNAPSHOT = -14,
    VC_MEMORY_READ_STATUS_STALE_MODULE = -15,
    VC_MEMORY_READ_STATUS_PERMISSION_DENIED = -16,
    VC_MEMORY_READ_STATUS_RANGE_DENIED = -17,
    VC_MEMORY_READ_STATUS_QUOTA_EXHAUSTED = -18,
    VC_MEMORY_READ_STATUS_READ_FAULT = -19,
    VC_MEMORY_READ_STATUS_SHORT_READ = -20,
    VC_MEMORY_READ_STATUS_MUTATED_TARGET = -21,
    VC_MEMORY_READ_STATUS_COPY_FAULT = -22,
    VC_MEMORY_READ_STATUS_JOURNAL_RETRY_REQUIRED = -23,
    VC_MEMORY_READ_STATUS_EXPIRED = -24,
    VC_MEMORY_READ_STATUS_CLOCK_ROLLBACK = -25,
    VC_MEMORY_READ_STATUS_BUSY = -26,
    VC_MEMORY_READ_STATUS_STOPPED = -27,
    VC_MEMORY_READ_STATUS_BOUNDED_ERROR = -28
} vc_memory_read_status;

/*
 * Native typed request. The byte ABI is always encoded explicitly in little
 * endian and never depends on this structure's representation.
 */
typedef struct vc_memory_read_request {
    uint16_t version;
    uint16_t struct_size;
    uint16_t operation;
    uint16_t caller_role;
    uint32_t capabilities;
    uint32_t reserved0;
    uint64_t request_id;
    uint64_t session_id;
    uint32_t target_process_id;
    uint32_t module_id;
    uint64_t target_generation;
    uint64_t attestation_revision;
    uint64_t module_load_generation;
    uint32_t segment_index;
    uint32_t segment_offset;
    uint32_t length;
    uint32_t reserved1;
} vc_memory_read_request;

/*
 * total_size is the fixed 64-byte header plus payload_size. STATUS responses
 * have no payload. READ responses contain at most 256 bytes.
 */
typedef struct vc_memory_read_response {
    uint16_t version;
    uint16_t header_size;
    uint16_t total_size;
    uint16_t operation;
    int32_t status;
    uint32_t state;
    uint32_t capabilities;
    uint32_t payload_size;
    uint64_t request_id;
    uint64_t session_id;
    uint64_t remaining_bytes;
    uint32_t remaining_operations;
    uint32_t reserved0;
    uint64_t reserved1;
    uint8_t payload[VC_MEMORY_READ_MAX_PAYLOAD];
} vc_memory_read_response;

void vc_memory_read_request_init(
    vc_memory_read_request *request,
    vc_memory_read_operation operation);

vc_memory_read_status vc_memory_read_request_encode(
    const vc_memory_read_request *request,
    uint8_t *buffer,
    size_t buffer_size,
    size_t *encoded_size);

vc_memory_read_status vc_memory_read_request_decode(
    const uint8_t *buffer,
    size_t buffer_size,
    vc_memory_read_request *request);

vc_memory_read_status vc_memory_read_response_encode(
    const vc_memory_read_response *response,
    uint8_t *buffer,
    size_t buffer_size,
    size_t *encoded_size);

vc_memory_read_status vc_memory_read_response_decode(
    const uint8_t *buffer,
    size_t buffer_size,
    vc_memory_read_response *response);

#ifdef __cplusplus
}
#endif

#endif
