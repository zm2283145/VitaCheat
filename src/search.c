#include "vitacheat/search.h"

#include <limits.h>
#include <stdbool.h>
#include <stdint.h>

static bool vc_type_is_signed(vc_scalar_type type)
{
    return type == VC_SCALAR_S8 || type == VC_SCALAR_S16 || type == VC_SCALAR_S32;
}

size_t vc_scalar_width(vc_scalar_type type)
{
    switch (type) {
    case VC_SCALAR_U8:
    case VC_SCALAR_S8:
        return 1;
    case VC_SCALAR_U16:
    case VC_SCALAR_S16:
        return 2;
    case VC_SCALAR_U32:
    case VC_SCALAR_S32:
        return 4;
    default:
        return 0;
    }
}

static bool vc_relation_is_valid(vc_relation relation)
{
    /* Some ARM EABI compilers choose an unsigned representation for this
     * enum. Casting once makes negative/corrupt callers fail the same way on
     * every target without a tautological lower-bound comparison. */
    return (unsigned int)relation <= (unsigned int)VC_RELATION_DECREASED;
}

static uint32_t vc_width_mask(size_t width)
{
    switch (width) {
    case 1:
        return UINT32_C(0xff);
    case 2:
        return UINT32_C(0xffff);
    default:
        return UINT32_MAX;
    }
}

static uint32_t vc_read_little_endian(const uint8_t *bytes, size_t width)
{
    uint32_t value = 0;
    size_t index;

    for (index = 0; index < width; ++index) {
        value |= (uint32_t)bytes[index] << (index * 8u);
    }

    return value;
}

static int64_t vc_signed_value(uint32_t bits, size_t width)
{
    const uint64_t mask = (uint64_t)vc_width_mask(width);
    const uint64_t value = (uint64_t)bits & mask;
    const uint64_t sign_bit = UINT64_C(1) << (width * 8u - 1u);

    if ((value & sign_bit) != 0) {
        return -(int64_t)((mask - value) + UINT64_C(1));
    }

    return (int64_t)value;
}

static bool vc_matches(vc_scalar_type type,
                       vc_relation relation,
                       uint32_t previous,
                       uint32_t current,
                       uint32_t constant,
                       size_t width)
{
    const uint32_t mask = vc_width_mask(width);

    previous &= mask;
    current &= mask;
    constant &= mask;

    switch (relation) {
    case VC_RELATION_EQUAL:
        return current == constant;
    case VC_RELATION_NOT_EQUAL:
        return current != constant;
    case VC_RELATION_CHANGED:
        return current != previous;
    case VC_RELATION_UNCHANGED:
        return current == previous;
    case VC_RELATION_INCREASED:
        if (vc_type_is_signed(type)) {
            return vc_signed_value(current, width) > vc_signed_value(previous, width);
        }
        return current > previous;
    case VC_RELATION_DECREASED:
        if (vc_type_is_signed(type)) {
            return vc_signed_value(current, width) < vc_signed_value(previous, width);
        }
        return current < previous;
    default:
        return false;
    }
}

static bool vc_size_is_representable(size_t size)
{
#if SIZE_MAX > UINT32_MAX
    return size <= (size_t)UINT32_MAX + (size_t)1;
#else
    (void)size;
    return true;
#endif
}

static bool vc_multiply_size(size_t left, size_t right, size_t *result)
{
    if (left != 0 && right > SIZE_MAX / left) {
        return false;
    }

    *result = left * right;
    return true;
}

static bool vc_ranges_overlap(const void *left,
                              size_t left_size,
                              const void *right,
                              size_t right_size)
{
    uintptr_t left_start;
    uintptr_t right_start;

    if (left_size == 0 || right_size == 0 || left == NULL || right == NULL) {
        return false;
    }

    left_start = (uintptr_t)left;
    right_start = (uintptr_t)right;

    if (left_start > UINTPTR_MAX - left_size || right_start > UINTPTR_MAX - right_size) {
        return true;
    }

    return left_start < right_start + right_size && right_start < left_start + left_size;
}

static vc_status vc_validate_common(const uint8_t *current,
                                    size_t size,
                                    const vc_query *query,
                                    const uint32_t *out_offsets,
                                    size_t capacity,
                                    size_t *written,
                                    size_t *total_matches,
                                    vc_query *validated_query,
                                    size_t *width,
                                    size_t *output_bytes)
{
    if (written == NULL || total_matches == NULL || validated_query == NULL ||
        width == NULL || output_bytes == NULL) {
        return VC_STATUS_INVALID_ARGUMENT;
    }

    if (query == NULL || (current == NULL && size != 0) ||
        (out_offsets == NULL && capacity != 0) || !vc_size_is_representable(size)) {
        return VC_STATUS_INVALID_ARGUMENT;
    }

    *validated_query = *query;
    *width = vc_scalar_width(validated_query->type);
    if (*width == 0 || validated_query->stride == 0 ||
        !vc_relation_is_valid(validated_query->relation)) {
        return VC_STATUS_INVALID_QUERY;
    }
#if SIZE_MAX < UINT32_MAX
    if (validated_query->stride > (uint32_t)SIZE_MAX) {
        return VC_STATUS_INVALID_QUERY;
    }
#endif

    if (!vc_multiply_size(capacity, sizeof(*out_offsets), output_bytes)) {
        return VC_STATUS_INVALID_ARGUMENT;
    }

    if (vc_ranges_overlap(written, sizeof(*written), total_matches, sizeof(*total_matches)) ||
        vc_ranges_overlap(out_offsets, *output_bytes, current, size) ||
        vc_ranges_overlap(out_offsets, *output_bytes, query, sizeof(*query)) ||
        vc_ranges_overlap(out_offsets, *output_bytes, written, sizeof(*written)) ||
        vc_ranges_overlap(out_offsets, *output_bytes, total_matches, sizeof(*total_matches)) ||
        vc_ranges_overlap(written, sizeof(*written), current, size) ||
        vc_ranges_overlap(total_matches, sizeof(*total_matches), current, size) ||
        vc_ranges_overlap(written, sizeof(*written), query, sizeof(*query)) ||
        vc_ranges_overlap(total_matches, sizeof(*total_matches), query, sizeof(*query))) {
        return VC_STATUS_INVALID_ARGUMENT;
    }

    return VC_STATUS_OK;
}

vc_status vc_search_initial(const uint8_t *current,
                            size_t size,
                            const vc_query *query,
                            uint32_t *out_offsets,
                            size_t capacity,
                            size_t *written,
                            size_t *total_matches)
{
    vc_query validated_query;
    size_t width;
    size_t output_bytes;
    size_t offset = 0;
    size_t last_offset;
    size_t local_written = 0;
    size_t local_total = 0;
    vc_status status;

    status = vc_validate_common(current,
                                size,
                                query,
                                out_offsets,
                                capacity,
                                written,
                                total_matches,
                                &validated_query,
                                &width,
                                &output_bytes);
    (void)output_bytes;
    if (status != VC_STATUS_OK) {
        return status;
    }

    if (validated_query.relation != VC_RELATION_EQUAL &&
        validated_query.relation != VC_RELATION_NOT_EQUAL) {
        return VC_STATUS_INVALID_QUERY;
    }

    if (size < width) {
        *written = 0;
        *total_matches = 0;
        return VC_STATUS_OK;
    }

    last_offset = size - width;
    for (;;) {
        const uint32_t current_value = vc_read_little_endian(current + offset, width);

        if (vc_matches(validated_query.type,
                       validated_query.relation,
                       0,
                       current_value,
                       validated_query.value_bits,
                       width)) {
            if (local_written < capacity) {
                out_offsets[local_written] = (uint32_t)offset;
                ++local_written;
            }
            ++local_total;
        }

        if ((size_t)validated_query.stride > last_offset - offset) {
            break;
        }
        offset += (size_t)validated_query.stride;
    }

    *written = local_written;
    *total_matches = local_total;
    return local_total > capacity ? VC_STATUS_TRUNCATED : VC_STATUS_OK;
}

static vc_status vc_validate_candidates(const uint32_t *candidates,
                                        size_t candidate_count,
                                        size_t size,
                                        size_t width)
{
    size_t index;

    if (candidates == NULL && candidate_count != 0) {
        return VC_STATUS_INVALID_ARGUMENT;
    }

    for (index = 0; index < candidate_count; ++index) {
        const size_t offset = (size_t)candidates[index];

        if (index != 0 && candidates[index - 1] >= candidates[index]) {
            return VC_STATUS_INVALID_CANDIDATES;
        }
        if (offset > size || width > size - offset) {
            return VC_STATUS_INVALID_CANDIDATES;
        }
    }

    return VC_STATUS_OK;
}

vc_status vc_search_refine(const uint8_t *previous,
                           const uint8_t *current,
                           size_t size,
                           const uint32_t *candidates,
                           size_t candidate_count,
                           const vc_query *query,
                           uint32_t *out_offsets,
                           size_t capacity,
                           size_t *written,
                           size_t *total_matches)
{
    vc_query validated_query;
    size_t width;
    size_t output_bytes;
    size_t candidate_bytes;
    size_t index;
    size_t local_written = 0;
    size_t local_total = 0;
    vc_status status;

    status = vc_validate_common(current,
                                size,
                                query,
                                out_offsets,
                                capacity,
                                written,
                                total_matches,
                                &validated_query,
                                &width,
                                &output_bytes);
    if (status != VC_STATUS_OK) {
        return status;
    }

    if (previous == NULL && size != 0) {
        return VC_STATUS_INVALID_ARGUMENT;
    }

    status = vc_validate_candidates(candidates, candidate_count, size, width);
    if (status != VC_STATUS_OK) {
        return status;
    }

    if (!vc_multiply_size(candidate_count, sizeof(*candidates), &candidate_bytes)) {
        return VC_STATUS_INVALID_ARGUMENT;
    }

    if (vc_ranges_overlap(previous, size, out_offsets, output_bytes) ||
        vc_ranges_overlap(written, sizeof(*written), previous, size) ||
        vc_ranges_overlap(total_matches, sizeof(*total_matches), previous, size) ||
        vc_ranges_overlap(written, sizeof(*written), candidates, candidate_bytes) ||
        vc_ranges_overlap(total_matches, sizeof(*total_matches), candidates, candidate_bytes) ||
        (out_offsets != candidates &&
         vc_ranges_overlap(candidates, candidate_bytes, out_offsets, output_bytes))) {
        return VC_STATUS_INVALID_ARGUMENT;
    }

    for (index = 0; index < candidate_count; ++index) {
        const size_t offset = (size_t)candidates[index];
        const uint32_t previous_value = vc_read_little_endian(previous + offset, width);
        const uint32_t current_value = vc_read_little_endian(current + offset, width);

        if (vc_matches(validated_query.type,
                       validated_query.relation,
                       previous_value,
                       current_value,
                       validated_query.value_bits,
                       width)) {
            if (local_written < capacity) {
                out_offsets[local_written] = candidates[index];
                ++local_written;
            }
            ++local_total;
        }
    }

    *written = local_written;
    *total_matches = local_total;
    return local_total > capacity ? VC_STATUS_TRUNCATED : VC_STATUS_OK;
}
