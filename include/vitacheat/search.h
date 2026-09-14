#ifndef VITACHEAT_SEARCH_H
#define VITACHEAT_SEARCH_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum vc_scalar_type {
    VC_SCALAR_U8 = 0,
    VC_SCALAR_S8,
    VC_SCALAR_U16,
    VC_SCALAR_S16,
    VC_SCALAR_U32,
    VC_SCALAR_S32
} vc_scalar_type;

typedef enum vc_relation {
    VC_RELATION_EQUAL = 0,
    VC_RELATION_NOT_EQUAL,
    VC_RELATION_CHANGED,
    VC_RELATION_UNCHANGED,
    VC_RELATION_INCREASED,
    VC_RELATION_DECREASED
} vc_relation;

typedef enum vc_status {
    VC_STATUS_OK = 0,
    VC_STATUS_TRUNCATED = 1,
    VC_STATUS_INVALID_ARGUMENT = -1,
    VC_STATUS_INVALID_QUERY = -2,
    VC_STATUS_INVALID_CANDIDATES = -3
} vc_status;

typedef struct vc_query {
    vc_scalar_type type;
    vc_relation relation;
    uint32_t value_bits;
    uint32_t stride;
} vc_query;

/* Returns 0 for an unsupported scalar type. */
size_t vc_scalar_width(vc_scalar_type type);

/*
 * Scan an immutable little-endian snapshot for a constant value relation.
 * Initial scans accept only EQUAL and NOT_EQUAL. Results are ascending,
 * region-relative 32-bit offsets. A stride of 1 examines every byte.
 * Output storage and metadata must not overlap any input, or each other.
 */
vc_status vc_search_initial(const uint8_t *current,
                            size_t size,
                            const vc_query *query,
                            uint32_t *out_offsets,
                            size_t capacity,
                            size_t *written,
                            size_t *total_matches);

/*
 * Refine a strictly increasing candidate list against two equal-size
 * snapshots. Exact in-place compaction (out_offsets == candidates) is
 * supported. The query stride is validated but does not resample candidates.
 * Apart from that exact candidate/output alias, outputs must not overlap
 * inputs or each other.
 */
vc_status vc_search_refine(const uint8_t *previous,
                           const uint8_t *current,
                           size_t size,
                           const uint32_t *candidates,
                           size_t candidate_count,
                           const vc_query *query,
                           uint32_t *out_offsets,
                           size_t capacity,
                           size_t *written,
                           size_t *total_matches);

#ifdef __cplusplus
}
#endif

#endif
