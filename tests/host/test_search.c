#include "vitacheat/search.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(expression)                                                        \
    do {                                                                         \
        if (!(expression)) {                                                     \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expression); \
            ++failures;                                                          \
        }                                                                        \
    } while (0)

static vc_query query(vc_scalar_type type, vc_relation relation, uint32_t value, uint32_t stride)
{
    vc_query result;

    result.type = type;
    result.relation = relation;
    result.value_bits = value;
    result.stride = stride;
    return result;
}

static void test_initial_little_endian_and_stride(void)
{
    const uint8_t bytes[] = {0x34, 0x12, 0x00, 0x34, 0x12, 0x34, 0x12};
    uint32_t offsets[8] = {UINT32_MAX};
    size_t written = 99;
    size_t total = 99;
    vc_query q = query(VC_SCALAR_U16, VC_RELATION_EQUAL, UINT32_C(0x1234), 1);

    CHECK(vc_search_initial(bytes, sizeof(bytes), &q, offsets, 8, &written, &total) == VC_STATUS_OK);
    CHECK(written == 3);
    CHECK(total == 3);
    CHECK(offsets[0] == 0);
    CHECK(offsets[1] == 3);
    CHECK(offsets[2] == 5);

    q.stride = 2;
    CHECK(vc_search_initial(bytes, sizeof(bytes), &q, offsets, 8, &written, &total) == VC_STATUS_OK);
    CHECK(written == 1);
    CHECK(total == 1);
    CHECK(offsets[0] == 0);

    q = query(VC_SCALAR_U8, VC_RELATION_EQUAL, UINT32_C(0x12), 1);
    CHECK(vc_search_initial(bytes, sizeof(bytes), &q, offsets, 8, &written, &total) == VC_STATUS_OK);
    CHECK(written == 3);
    CHECK(offsets[2] == sizeof(bytes) - 1);
}

static void test_u32_and_truncation(void)
{
    const uint8_t bytes[] = {
        0x78, 0x56, 0x34, 0x12,
        0x00, 0x00, 0x00, 0x00,
        0x78, 0x56, 0x34, 0x12
    };
    uint32_t guarded[3] = {UINT32_C(0xaaaaaaaa), UINT32_C(0xbbbbbbbb), UINT32_C(0xcccccccc)};
    size_t written;
    size_t total;
    vc_query q = query(VC_SCALAR_U32, VC_RELATION_EQUAL, UINT32_C(0x12345678), 4);

    CHECK(vc_search_initial(bytes, sizeof(bytes), &q, &guarded[1], 1, &written, &total) == VC_STATUS_TRUNCATED);
    CHECK(written == 1);
    CHECK(total == 2);
    CHECK(guarded[0] == UINT32_C(0xaaaaaaaa));
    CHECK(guarded[1] == 0);
    CHECK(guarded[2] == UINT32_C(0xcccccccc));
}

static void test_signed_refinement(void)
{
    const uint8_t previous[] = {0xfe, 0xff, 0x7f}; /* -2, -1, 127 */
    const uint8_t current[] = {0xff, 0xfe, 0x80};  /* -1, -2, -128 */
    const uint32_t candidates[] = {0, 1, 2};
    uint32_t offsets[3];
    size_t written;
    size_t total;
    vc_query q = query(VC_SCALAR_S8, VC_RELATION_INCREASED, 0, 1);

    CHECK(vc_search_refine(previous, current, sizeof(current), candidates, 3, &q,
                           offsets, 3, &written, &total) == VC_STATUS_OK);
    CHECK(written == 1 && total == 1 && offsets[0] == 0);

    q.relation = VC_RELATION_DECREASED;
    CHECK(vc_search_refine(previous, current, sizeof(current), candidates, 3, &q,
                           offsets, 3, &written, &total) == VC_STATUS_OK);
    CHECK(written == 2 && total == 2);
    CHECK(offsets[0] == 1 && offsets[1] == 2);
}

static void write_u16_le(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8u);
}

static void write_u32_le(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8u);
    bytes[2] = (uint8_t)(value >> 16u);
    bytes[3] = (uint8_t)(value >> 24u);
}

static void test_signed_wide_edges(void)
{
    uint8_t previous16[4];
    uint8_t current16[4];
    uint8_t previous32[8];
    uint8_t current32[8];
    const uint32_t candidates16[] = {0, 2};
    const uint32_t candidates32[] = {0, 4};
    uint32_t offsets[2];
    size_t written;
    size_t total;
    vc_query q;

    write_u16_le(previous16, UINT16_C(0xffff));
    write_u16_le(previous16 + 2, UINT16_C(0x7fff));
    write_u16_le(current16, 0);
    write_u16_le(current16 + 2, UINT16_C(0x8000));
    q = query(VC_SCALAR_S16, VC_RELATION_INCREASED, 0, 1);
    CHECK(vc_search_refine(previous16, current16, sizeof(current16), candidates16, 2,
                           &q, offsets, 2, &written, &total) == VC_STATUS_OK);
    CHECK(written == 1 && offsets[0] == 0);
    q.relation = VC_RELATION_DECREASED;
    CHECK(vc_search_refine(previous16, current16, sizeof(current16), candidates16, 2,
                           &q, offsets, 2, &written, &total) == VC_STATUS_OK);
    CHECK(written == 1 && offsets[0] == 2);

    write_u32_le(previous32, UINT32_MAX);
    write_u32_le(previous32 + 4, UINT32_C(0x7fffffff));
    write_u32_le(current32, 0);
    write_u32_le(current32 + 4, UINT32_C(0x80000000));
    q = query(VC_SCALAR_S32, VC_RELATION_INCREASED, 0, 1);
    CHECK(vc_search_refine(previous32, current32, sizeof(current32), candidates32, 2,
                           &q, offsets, 2, &written, &total) == VC_STATUS_OK);
    CHECK(written == 1 && offsets[0] == 0);
    q.relation = VC_RELATION_DECREASED;
    CHECK(vc_search_refine(previous32, current32, sizeof(current32), candidates32, 2,
                           &q, offsets, 2, &written, &total) == VC_STATUS_OK);
    CHECK(written == 1 && offsets[0] == 4);
}

static void test_unsigned_relations_and_not_equal(void)
{
    const uint8_t previous[] = {0, 9, 3};
    const uint8_t current[] = {1, 4, 3};
    const uint32_t candidates[] = {0, 1, 2};
    uint32_t offsets[3];
    size_t written;
    size_t total;
    vc_query q = query(VC_SCALAR_U8, VC_RELATION_INCREASED, 0, 1);

    CHECK(vc_search_refine(previous, current, sizeof(current), candidates, 3, &q,
                           offsets, 3, &written, &total) == VC_STATUS_OK);
    CHECK(written == 1 && offsets[0] == 0);
    q.relation = VC_RELATION_DECREASED;
    CHECK(vc_search_refine(previous, current, sizeof(current), candidates, 3, &q,
                           offsets, 3, &written, &total) == VC_STATUS_OK);
    CHECK(written == 1 && offsets[0] == 1);
    q.relation = VC_RELATION_NOT_EQUAL;
    q.value_bits = 3;
    CHECK(vc_search_initial(current, sizeof(current), &q, offsets, 3,
                            &written, &total) == VC_STATUS_OK);
    CHECK(written == 2 && offsets[0] == 0 && offsets[1] == 1);
}

static void test_change_exact_and_in_place_refinement(void)
{
    const uint8_t previous[] = {1, 2, 3, 4};
    const uint8_t current[] = {1, 9, 3, 9};
    uint32_t candidates[] = {0, 1, 2, 3};
    uint32_t offsets[4];
    size_t written;
    size_t total;
    vc_query q = query(VC_SCALAR_U8, VC_RELATION_CHANGED, 0, 1);

    CHECK(vc_search_refine(previous, current, sizeof(current), candidates, 4, &q,
                           offsets, 4, &written, &total) == VC_STATUS_OK);
    CHECK(written == 2 && offsets[0] == 1 && offsets[1] == 3);

    q.relation = VC_RELATION_UNCHANGED;
    CHECK(vc_search_refine(previous, current, sizeof(current), candidates, 4, &q,
                           offsets, 4, &written, &total) == VC_STATUS_OK);
    CHECK(written == 2 && offsets[0] == 0 && offsets[1] == 2);

    q.relation = VC_RELATION_EQUAL;
    q.value_bits = 9;
    CHECK(vc_search_refine(previous, current, sizeof(current), candidates, 4, &q,
                           candidates, 4, &written, &total) == VC_STATUS_OK);
    CHECK(written == 2 && candidates[0] == 1 && candidates[1] == 3);
}

static void test_empty_and_invalid_inputs(void)
{
    const uint8_t bytes[] = {1, 2, 3, 4};
    const uint8_t unchanged[] = {1, 2, 3, 4};
    const uint32_t duplicate[] = {1, 1};
    const uint32_t descending[] = {2, 1};
    const uint32_t out_of_range[] = {4};
    uint32_t offsets[4] = {0};
    size_t written = 7;
    size_t total = 7;
    vc_query q = query(VC_SCALAR_U32, VC_RELATION_EQUAL, 0, 1);

    CHECK(vc_search_initial(NULL, 0, &q, NULL, 0, &written, &total) == VC_STATUS_OK);
    CHECK(written == 0 && total == 0);
    CHECK(vc_search_initial(bytes, 3, &q, offsets, 4, &written, &total) == VC_STATUS_OK);
    CHECK(written == 0 && total == 0);

    q.stride = 0;
    CHECK(vc_search_initial(bytes, sizeof(bytes), &q, offsets, 4, &written, &total) == VC_STATUS_INVALID_QUERY);
    q.stride = 1;
    q.type = (vc_scalar_type)99;
    CHECK(vc_search_initial(bytes, sizeof(bytes), &q, offsets, 4, &written, &total) == VC_STATUS_INVALID_QUERY);
    q.type = VC_SCALAR_U8;
    q.relation = VC_RELATION_CHANGED;
    CHECK(vc_search_initial(bytes, sizeof(bytes), &q, offsets, 4, &written, &total) == VC_STATUS_INVALID_QUERY);
    q.relation = (vc_relation)-1;
    CHECK(vc_search_initial(bytes, sizeof(bytes), &q, offsets, 4, &written, &total) == VC_STATUS_INVALID_QUERY);
    q.relation = (vc_relation)99;
    CHECK(vc_search_initial(bytes, sizeof(bytes), &q, offsets, 4, &written, &total) == VC_STATUS_INVALID_QUERY);

    q.relation = VC_RELATION_UNCHANGED;
    CHECK(vc_search_refine(bytes, unchanged, sizeof(bytes), duplicate, 2, &q,
                           offsets, 4, &written, &total) == VC_STATUS_INVALID_CANDIDATES);
    CHECK(vc_search_refine(bytes, unchanged, sizeof(bytes), descending, 2, &q,
                           offsets, 4, &written, &total) == VC_STATUS_INVALID_CANDIDATES);
    q.type = VC_SCALAR_U16;
    CHECK(vc_search_refine(bytes, unchanged, sizeof(bytes), out_of_range, 1, &q,
                           offsets, 4, &written, &total) == VC_STATUS_INVALID_CANDIDATES);
}

static void test_alias_and_zero_capacity_guards(void)
{
    const uint8_t bytes[] = {7, 7};
    uint32_t candidates[] = {0, 1};
    uint32_t offsets[2] = {UINT32_C(0xaaaaaaaa), UINT32_C(0xbbbbbbbb)};
    size_t written = 10;
    size_t total = 20;
    size_t both = 30;
    vc_query q = query(VC_SCALAR_U8, VC_RELATION_EQUAL, 7, 1);
    union {
        vc_query query_value;
        uint32_t output[4];
    } query_alias;

    CHECK(vc_search_initial(bytes, sizeof(bytes), &q, NULL, 0,
                            &written, &total) == VC_STATUS_TRUNCATED);
    CHECK(written == 0 && total == 2);

    both = 30;
    CHECK(vc_search_initial(bytes, sizeof(bytes), &q, offsets, 2,
                            &both, &both) == VC_STATUS_INVALID_ARGUMENT);
    CHECK(both == 30);

    query_alias.query_value = q;
    written = 10;
    total = 20;
    CHECK(vc_search_initial(bytes, sizeof(bytes), &query_alias.query_value,
                            query_alias.output, 2, &written, &total) ==
          VC_STATUS_INVALID_ARGUMENT);
    CHECK(written == 10 && total == 20);

    q.relation = VC_RELATION_UNCHANGED;
    written = 10;
    total = 20;
    CHECK(vc_search_refine(bytes, bytes, sizeof(bytes), candidates, 2, &q,
                           candidates + 1, 1, &written, &total) ==
          VC_STATUS_INVALID_ARGUMENT);
    CHECK(written == 10 && total == 20);
}

static void test_snapshots_are_immutable(void)
{
    uint8_t previous[] = {1, 2, 3, 4, 5};
    uint8_t current[] = {1, 3, 3, 8, 5};
    uint8_t previous_copy[sizeof(previous)];
    uint8_t current_copy[sizeof(current)];
    uint32_t offsets[5];
    const uint32_t candidates[] = {0, 1, 2, 3, 4};
    size_t written;
    size_t total;
    vc_query q = query(VC_SCALAR_U8, VC_RELATION_CHANGED, 0, 1);

    memcpy(previous_copy, previous, sizeof(previous));
    memcpy(current_copy, current, sizeof(current));
    CHECK(vc_search_refine(previous, current, sizeof(current), candidates, 5, &q,
                           offsets, 5, &written, &total) == VC_STATUS_OK);
    CHECK(memcmp(previous, previous_copy, sizeof(previous)) == 0);
    CHECK(memcmp(current, current_copy, sizeof(current)) == 0);
}

static uint32_t next_random(uint32_t *state)
{
    uint32_t value = *state;

    value ^= value << 13u;
    value ^= value >> 17u;
    value ^= value << 5u;
    *state = value;
    return value;
}

static uint32_t reference_read(const uint8_t *bytes, size_t width)
{
    uint32_t value = 0;
    size_t index;

    for (index = 0; index < width; ++index) {
        value |= (uint32_t)bytes[index] << (index * 8u);
    }

    return value;
}

static uint32_t reference_mask(size_t width)
{
    if (width == 1) {
        return UINT32_C(0xff);
    }
    if (width == 2) {
        return UINT32_C(0xffff);
    }
    return UINT32_MAX;
}

static int64_t reference_signed(uint32_t bits, size_t width)
{
    const uint64_t mask = (uint64_t)reference_mask(width);
    const uint64_t value = (uint64_t)bits & mask;
    const uint64_t sign = UINT64_C(1) << (width * 8u - 1u);

    if ((value & sign) != 0) {
        return -(int64_t)((mask - value) + UINT64_C(1));
    }
    return (int64_t)value;
}

static int reference_matches(vc_scalar_type type,
                             vc_relation relation,
                             uint32_t previous,
                             uint32_t current,
                             uint32_t constant,
                             size_t width)
{
    const uint32_t mask = reference_mask(width);

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
        if (type == VC_SCALAR_S8 || type == VC_SCALAR_S16 || type == VC_SCALAR_S32) {
            return reference_signed(current, width) > reference_signed(previous, width);
        }
        return current > previous;
    case VC_RELATION_DECREASED:
        if (type == VC_SCALAR_S8 || type == VC_SCALAR_S16 || type == VC_SCALAR_S32) {
            return reference_signed(current, width) < reference_signed(previous, width);
        }
        return current < previous;
    default:
        return 0;
    }
}

static void test_initial_search_properties(void)
{
    uint32_t random_state = UINT32_C(0x8b5ad4ce);
    uint8_t bytes[64];
    uint8_t original[64];
    uint32_t guarded[66];
    uint32_t expected[64];
    size_t iteration;

    for (iteration = 0; iteration < 2000; ++iteration) {
        vc_query q;
        size_t size;
        size_t width;
        size_t capacity;
        size_t expected_total = 0;
        size_t expected_written = 0;
        size_t written = SIZE_MAX;
        size_t total = SIZE_MAX;
        size_t index;
        vc_status status;

        for (index = 0; index < sizeof(bytes); ++index) {
            bytes[index] = (uint8_t)next_random(&random_state);
        }
        memcpy(original, bytes, sizeof(bytes));

        q.type = (vc_scalar_type)(next_random(&random_state) % 6u);
        q.relation = (next_random(&random_state) & 1u) != 0
                         ? VC_RELATION_EQUAL
                         : VC_RELATION_NOT_EQUAL;
        q.value_bits = next_random(&random_state);
        q.stride = next_random(&random_state) % 8u + 1u;
        size = (size_t)(next_random(&random_state) % (sizeof(bytes) + 1u));
        width = vc_scalar_width(q.type);
        capacity = (size_t)(next_random(&random_state) % (sizeof(expected) / sizeof(expected[0]) + 1u));

        for (index = 0; index < sizeof(guarded) / sizeof(guarded[0]); ++index) {
            guarded[index] = UINT32_C(0xa5a5a5a5);
        }

        if (size >= width) {
            const size_t last_offset = size - width;
            size_t offset = 0;

            for (;;) {
                const uint32_t current = reference_read(bytes + offset, width);

                if (reference_matches(q.type, q.relation, 0, current, q.value_bits, width)) {
                    if (expected_written < capacity) {
                        expected[expected_written++] = (uint32_t)offset;
                    }
                    ++expected_total;
                }
                if ((size_t)q.stride > last_offset - offset) {
                    break;
                }
                offset += (size_t)q.stride;
            }
        }

        status = vc_search_initial(bytes, size, &q, guarded + 1, capacity, &written, &total);
        CHECK(status == (expected_total > capacity ? VC_STATUS_TRUNCATED : VC_STATUS_OK));
        CHECK(written == expected_written);
        CHECK(total == expected_total);
        CHECK(memcmp(guarded + 1, expected, expected_written * sizeof(expected[0])) == 0);
        CHECK(guarded[0] == UINT32_C(0xa5a5a5a5));
        CHECK(guarded[capacity + 1] == UINT32_C(0xa5a5a5a5));
        CHECK(memcmp(bytes, original, sizeof(bytes)) == 0);
    }
}

static void test_refinement_properties(void)
{
    uint32_t random_state = UINT32_C(0x16c3a72d);
    uint8_t previous[64];
    uint8_t current[64];
    uint8_t previous_copy[64];
    uint8_t current_copy[64];
    uint32_t candidates[64];
    uint32_t candidates_copy[64];
    uint32_t guarded[66];
    uint32_t expected[64];
    size_t iteration;

    for (iteration = 0; iteration < 2000; ++iteration) {
        vc_query q;
        size_t size;
        size_t width;
        size_t capacity;
        size_t candidate_count = 0;
        size_t expected_total = 0;
        size_t expected_written = 0;
        size_t written = SIZE_MAX;
        size_t total = SIZE_MAX;
        size_t index;
        vc_status status;

        for (index = 0; index < sizeof(previous); ++index) {
            previous[index] = (uint8_t)next_random(&random_state);
            current[index] = (uint8_t)next_random(&random_state);
        }
        memcpy(previous_copy, previous, sizeof(previous));
        memcpy(current_copy, current, sizeof(current));

        q.type = (vc_scalar_type)(next_random(&random_state) % 6u);
        q.relation = (vc_relation)(next_random(&random_state) % 6u);
        q.value_bits = next_random(&random_state);
        q.stride = next_random(&random_state) % 8u + 1u;
        size = (size_t)(next_random(&random_state) % (sizeof(previous) + 1u));
        width = vc_scalar_width(q.type);
        capacity = (size_t)(next_random(&random_state) % (sizeof(expected) / sizeof(expected[0]) + 1u));

        if (size >= width) {
            const size_t last_offset = size - width;

            for (index = 0; index <= last_offset; ++index) {
                if ((next_random(&random_state) & 1u) != 0) {
                    candidates[candidate_count++] = (uint32_t)index;
                }
            }
        }
        memcpy(candidates_copy, candidates, candidate_count * sizeof(candidates[0]));

        for (index = 0; index < candidate_count; ++index) {
            const size_t offset = (size_t)candidates[index];
            const uint32_t before = reference_read(previous + offset, width);
            const uint32_t after = reference_read(current + offset, width);

            if (reference_matches(q.type, q.relation, before, after, q.value_bits, width)) {
                if (expected_written < capacity) {
                    expected[expected_written++] = candidates[index];
                }
                ++expected_total;
            }
        }

        for (index = 0; index < sizeof(guarded) / sizeof(guarded[0]); ++index) {
            guarded[index] = UINT32_C(0x5a5a5a5a);
        }

        status = vc_search_refine(previous, current, size, candidates, candidate_count,
                                  &q, guarded + 1, capacity, &written, &total);
        CHECK(status == (expected_total > capacity ? VC_STATUS_TRUNCATED : VC_STATUS_OK));
        CHECK(written == expected_written);
        CHECK(total == expected_total);
        CHECK(memcmp(guarded + 1, expected, expected_written * sizeof(expected[0])) == 0);
        CHECK(guarded[0] == UINT32_C(0x5a5a5a5a));
        CHECK(guarded[capacity + 1] == UINT32_C(0x5a5a5a5a));
        CHECK(memcmp(previous, previous_copy, sizeof(previous)) == 0);
        CHECK(memcmp(current, current_copy, sizeof(current)) == 0);
        CHECK(memcmp(candidates, candidates_copy, candidate_count * sizeof(candidates[0])) == 0);

        written = SIZE_MAX;
        total = SIZE_MAX;
        status = vc_search_refine(previous, current, size, candidates_copy, candidate_count,
                                  &q, candidates_copy, capacity, &written, &total);
        CHECK(status == (expected_total > capacity ? VC_STATUS_TRUNCATED : VC_STATUS_OK));
        CHECK(written == expected_written);
        CHECK(total == expected_total);
        CHECK(memcmp(candidates_copy, expected, expected_written * sizeof(expected[0])) == 0);
    }
}

int main(void)
{
    test_initial_little_endian_and_stride();
    test_u32_and_truncation();
    test_signed_refinement();
    test_signed_wide_edges();
    test_unsigned_relations_and_not_equal();
    test_change_exact_and_in_place_refinement();
    test_empty_and_invalid_inputs();
    test_alias_and_zero_capacity_guards();
    test_snapshots_are_immutable();
    test_initial_search_properties();
    test_refinement_properties();

    if (failures != 0) {
        fprintf(stderr, "%d VitaCheat host test(s) failed\n", failures);
        return 1;
    }

    puts("VitaCheat host tests passed");
    return 0;
}
