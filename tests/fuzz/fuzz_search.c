#include "vitacheat/search.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum {
    VC_FUZZ_MAX_SNAPSHOT_SIZE = 256,
    VC_FUZZ_MAX_CANDIDATES = 128
};

static void fuzz_check(int condition)
{
    if (!condition) {
        abort();
    }
}

static uint32_t read_u32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] |
           (uint32_t)bytes[1] << 8u |
           (uint32_t)bytes[2] << 16u |
           (uint32_t)bytes[3] << 24u;
}

static void check_offsets(const uint32_t *offsets,
                          size_t count,
                          size_t snapshot_size,
                          size_t width)
{
    size_t index;

    fuzz_check(count <= VC_FUZZ_MAX_CANDIDATES);
    for (index = 0; index < count; ++index) {
        const size_t offset = (size_t)offsets[index];

        fuzz_check(index == 0 || offsets[index - 1] < offsets[index]);
        fuzz_check(offset <= snapshot_size);
        fuzz_check(width <= snapshot_size - offset);
    }
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t data_size)
{
    const uint8_t *previous;
    const uint8_t *current;
    size_t snapshot_size;
    size_t capacity;
    size_t written = 0;
    size_t total = 0;
    size_t refine_written = 0;
    size_t refine_total = 0;
    size_t width;
    vc_query arbitrary_query;
    vc_query seed_query;
    vc_query refine_query;
    vc_status status;
    uint32_t candidates[VC_FUZZ_MAX_CANDIDATES];
    uint32_t in_place[VC_FUZZ_MAX_CANDIDATES];
    uint32_t offsets[VC_FUZZ_MAX_CANDIDATES];

    if (data_size < 8) {
        return 0;
    }

    snapshot_size = (data_size - 8u) / 2u;
    if (snapshot_size > VC_FUZZ_MAX_SNAPSHOT_SIZE) {
        snapshot_size = VC_FUZZ_MAX_SNAPSHOT_SIZE;
    }
    previous = data + 8;
    current = previous + snapshot_size;
    capacity = (size_t)data[7] % (VC_FUZZ_MAX_CANDIDATES + 1u);

    arbitrary_query.type = (vc_scalar_type)(data[0] & 7u);
    arbitrary_query.relation = (vc_relation)(data[1] & 7u);
    arbitrary_query.stride = (uint32_t)data[2];
    arbitrary_query.value_bits = read_u32(data + 3);

    status = vc_search_initial(current, snapshot_size, &arbitrary_query,
                               offsets, capacity, &written, &total);
    if (status == VC_STATUS_OK || status == VC_STATUS_TRUNCATED) {
        width = vc_scalar_width(arbitrary_query.type);
        fuzz_check(width != 0);
        fuzz_check(written <= capacity);
        fuzz_check(written <= total);
        fuzz_check((status == VC_STATUS_TRUNCATED) == (total > capacity));
        check_offsets(offsets, written, snapshot_size, width);
    }

    seed_query.type = (vc_scalar_type)(data[0] % 6u);
    seed_query.relation = (data[1] & 1u) != 0 ? VC_RELATION_EQUAL : VC_RELATION_NOT_EQUAL;
    seed_query.stride = (uint32_t)(data[2] % 8u) + 1u;
    seed_query.value_bits = arbitrary_query.value_bits;

    status = vc_search_initial(previous, snapshot_size, &seed_query,
                               candidates, VC_FUZZ_MAX_CANDIDATES, &written, &total);
    fuzz_check(status == VC_STATUS_OK || status == VC_STATUS_TRUNCATED);
    fuzz_check(written <= VC_FUZZ_MAX_CANDIDATES);
    check_offsets(candidates, written, snapshot_size, vc_scalar_width(seed_query.type));

    refine_query.type = seed_query.type;
    refine_query.relation = (vc_relation)(data[1] % 6u);
    refine_query.stride = seed_query.stride;
    refine_query.value_bits = seed_query.value_bits;

    status = vc_search_refine(previous, current, snapshot_size,
                              candidates, written, &refine_query,
                              offsets, capacity, &refine_written, &refine_total);
    fuzz_check(status == VC_STATUS_OK || status == VC_STATUS_TRUNCATED);
    fuzz_check(refine_written <= capacity);
    fuzz_check(refine_written <= refine_total);
    fuzz_check((status == VC_STATUS_TRUNCATED) == (refine_total > capacity));
    check_offsets(offsets, refine_written, snapshot_size, vc_scalar_width(refine_query.type));

    memcpy(in_place, candidates, written * sizeof(candidates[0]));
    {
        size_t in_place_written = 0;
        size_t in_place_total = 0;
        vc_status in_place_status;

        in_place_status = vc_search_refine(previous, current, snapshot_size,
                                           in_place, written, &refine_query,
                                           in_place, capacity,
                                           &in_place_written, &in_place_total);
        fuzz_check(in_place_status == status);
        fuzz_check(in_place_written == refine_written);
        fuzz_check(in_place_total == refine_total);
        fuzz_check(memcmp(in_place, offsets,
                          refine_written * sizeof(offsets[0])) == 0);
    }

    if (written > 1) {
        size_t invalid_written = 0;
        size_t invalid_total = 0;

        memcpy(in_place, candidates, written * sizeof(candidates[0]));
        in_place[1] = in_place[0];
        status = vc_search_refine(previous, current, snapshot_size,
                                  in_place, written, &refine_query,
                                  offsets, capacity, &invalid_written, &invalid_total);
        fuzz_check(status == VC_STATUS_INVALID_CANDIDATES);
    }

    return 0;
}
