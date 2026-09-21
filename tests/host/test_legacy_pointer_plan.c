#include "vitacheat/legacy_plan.h"

#include <stdarg.h>
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

#define FIXTURE_CAPACITY ((size_t)128)
#define SOURCE_CAPACITY ((size_t)4096)
#define MEMORY_BASE UINT32_C(0x00001000)
#define MEMORY_SIZE ((size_t)0x6000)

typedef struct parsed_fixture {
    vc_psv_line lines[FIXTURE_CAPACITY];
    vc_psv_cheat cheats[FIXTURE_CAPACITY];
    vc_psv_operation operations[FIXTURE_CAPACITY];
    vc_psv_report report;
} parsed_fixture;

typedef struct fake_host {
    uint8_t memory[MEMORY_SIZE];
    size_t resolve_calls;
    size_t read_calls;
    size_t write_calls;
    size_t fail_read_call;
} fake_host;

static bool append_text(char *buffer,
                        size_t capacity,
                        size_t *length,
                        const char *format,
                        ...)
{
    va_list arguments;
    int written;

    if (*length >= capacity) {
        return false;
    }
    va_start(arguments, format);
    written = vsnprintf(buffer + *length, capacity - *length,
                        format, arguments);
    va_end(arguments);
    if (written < 0 || (size_t)written >= capacity - *length) {
        return false;
    }
    *length += (size_t)written;
    return true;
}

static bool parse_source(const char *source, parsed_fixture *fixture)
{
    const vc_psv_status status =
        vc_psv_parse(source, strlen(source),
                     fixture->lines, FIXTURE_CAPACITY,
                     fixture->cheats, FIXTURE_CAPACITY,
                     fixture->operations, FIXTURE_CAPACITY,
                     &fixture->report);

    CHECK(status == VC_PSV_STATUS_OK);
    return status == VC_PSV_STATUS_OK;
}

static vc_psv_compile_status compile_source(
    const char *source,
    const vc_psv_compile_options *options,
    parsed_fixture *fixture,
    vc_psv_plan_node *nodes,
    size_t node_capacity,
    vc_psv_plan *plan)
{
    if (!parse_source(source, fixture)) {
        return VC_PSV_COMPILE_INVALID_ARGUMENT;
    }
    CHECK(fixture->report.total_cheats == 1);
    if (fixture->report.total_cheats != 1) {
        return VC_PSV_COMPILE_INVALID_ARGUMENT;
    }
    return vc_psv_compile_cheat(
        &fixture->cheats[0], fixture->operations,
        fixture->report.total_operations, options,
        nodes, node_capacity, plan);
}

static bool fake_range(uint32_t address, size_t size, size_t *offset)
{
    const uint64_t begin = address;
    const uint64_t end = begin + size;
    const uint64_t memory_end = (uint64_t)MEMORY_BASE + MEMORY_SIZE;

    if (begin < MEMORY_BASE || end > memory_end || end < begin) {
        return false;
    }
    *offset = (size_t)(begin - MEMORY_BASE);
    return true;
}

static void reset_host(fake_host *host)
{
    memset(host, 0, sizeof(*host));
    host->fail_read_call = SIZE_MAX;
}

static void store_u32(fake_host *host, uint32_t address, uint32_t value)
{
    size_t offset;
    size_t index;

    CHECK(fake_range(address, 4, &offset));
    if (!fake_range(address, 4, &offset)) {
        return;
    }
    for (index = 0; index < 4; ++index) {
        host->memory[offset + index] =
            (uint8_t)(value >> (index * 8u));
    }
}

static bool fake_resolve(void *context,
                         uint8_t module_serial,
                         uint8_t segment_index,
                         uint32_t *base)
{
    fake_host *host = context;

    ++host->resolve_calls;
    if (module_serial == 0 && segment_index == 1) {
        *base = UINT32_C(0x1000);
        return true;
    }
    if (module_serial == 1 && segment_index == 0) {
        *base = UINT32_C(0x2000);
        return true;
    }
    return false;
}

static bool fake_read(void *context,
                      uint32_t address,
                      uint8_t *bytes,
                      size_t size)
{
    fake_host *host = context;
    size_t offset;
    const size_t call = host->read_calls++;

    if (call == host->fail_read_call ||
        !fake_range(address, size, &offset)) {
        return false;
    }
    memcpy(bytes, &host->memory[offset], size);
    return true;
}

static bool fake_write(void *context,
                       uint32_t address,
                       const uint8_t *bytes,
                       size_t size)
{
    fake_host *host = context;
    size_t offset;

    ++host->write_calls;
    if (!fake_range(address, size, &offset)) {
        return false;
    }
    memcpy(&host->memory[offset], bytes, size);
    return true;
}

static bool fake_buttons(void *context,
                         uint32_t mode,
                         uint32_t *normalized_mask)
{
    fake_host *host = context;

    (void)mode;
    (void)host;
    *normalized_mask = UINT32_C(0);
    return true;
}

static vc_psv_evaluation_callbacks evaluation_callbacks(fake_host *host)
{
    vc_psv_evaluation_callbacks callbacks;

    callbacks.context = host;
    callbacks.resolve_module_base = fake_resolve;
    callbacks.read_memory = fake_read;
    callbacks.read_buttons = fake_buttons;
    return callbacks;
}

static bool make_pointer_write(char *source,
                               size_t capacity,
                               unsigned width_index,
                               unsigned levels,
                               uint16_t terminal)
{
    size_t length = 0;
    unsigned level;

    if (!append_text(source, capacity, &length,
                     "_V0 Pointer write\n"
                     "$3%X0%X 00001000 00000004\n",
                     width_index, levels)) {
        return false;
    }
    for (level = 1; level < levels; ++level) {
        if (!append_text(source, capacity, &length,
                         "$3%X00 00000000 00000004\n",
                         width_index)) {
            return false;
        }
    }
    return append_text(source, capacity, &length,
                       "$%04X 00000000 A1B2C3D4\n", terminal);
}

static uint32_t prepare_chain(fake_host *host,
                              uint32_t base,
                              unsigned levels,
                              uint32_t offset)
{
    uint32_t cursor = base;
    unsigned level;

    for (level = 0; level < levels; ++level) {
        const uint32_t raw =
            UINT32_C(0x1800) + (uint32_t)level * UINT32_C(0x100);

        store_u32(host, cursor, raw);
        cursor = (uint32_t)(raw + offset);
    }
    return cursor;
}

static void test_pointer_write_levels_widths_offsets_and_b2(void)
{
    unsigned width_index;

    for (width_index = 0; width_index <= 2; ++width_index) {
        unsigned levels;

        for (levels = 1; levels <= 5; ++levels) {
            char source[SOURCE_CAPACITY];
            parsed_fixture fixture;
            vc_psv_plan_node node;
            vc_psv_plan plan;
            vc_psv_action action;
            vc_psv_evaluation_report report;
            vc_psv_evaluation_callbacks callbacks;
            fake_host host;
            uint32_t expected;

            CHECK(make_pointer_write(source, sizeof(source),
                                     width_index, levels,
                                     UINT16_C(0x3300)));
            CHECK(compile_source(source, NULL, &fixture,
                                 &node, 1, &plan) ==
                  VC_PSV_COMPILE_OK);
            CHECK(node.kind == VC_PSV_PLAN_POINTER_WRITE);
            CHECK(node.destination_pointer.level_count == levels);
            CHECK(node.physical_count == levels + 1u);
            CHECK(plan.maximum_actions == 1);
            CHECK(plan.maximum_memory_reads == levels);
            reset_host(&host);
            expected = prepare_chain(&host, UINT32_C(0x1000),
                                     levels, UINT32_C(4));
            callbacks = evaluation_callbacks(&host);
            CHECK(vc_psv_evaluate_plan(
                      &plan, &node, &callbacks, &action, 1, &report) ==
                  VC_PSV_EVALUATE_OK);
            CHECK(report.action_count == 1);
            CHECK(report.memory_reads == levels);
            CHECK(action.address == expected);
            CHECK((unsigned)action.width == (1u << width_index));
            CHECK(action.bytes[0] == UINT8_C(0xd4));
            if (width_index >= 1) {
                CHECK(action.bytes[1] == UINT8_C(0xc3));
            }
            if (width_index == 2) {
                CHECK(action.bytes[3] == UINT8_C(0xa1));
            }
        }
    }

    {
        static const char source[] =
            "_V0 Relative pointer\n"
            "$B200 00000001 00000000\n"
            "$3201 00000020 FFFFFFFC\n"
            "$3300 00000000 DEADBEEF\n";
        parsed_fixture fixture;
        vc_psv_plan_node node;
        vc_psv_plan plan;
        vc_psv_action action;
        vc_psv_evaluation_report report;
        vc_psv_evaluation_callbacks callbacks;
        fake_host host;

        CHECK(compile_source(source, NULL, &fixture,
                             &node, 1, &plan) == VC_PSV_COMPILE_OK);
        CHECK(node.destination_pointer.base.mode ==
              VC_PSV_ADDRESS_SELECTED_MODULE_RELATIVE);
        CHECK(node.destination_pointer.reads[0].offset ==
              UINT32_C(0xfffffffc));
        reset_host(&host);
        store_u32(&host, UINT32_C(0x1020), UINT32_C(0x1804));
        callbacks = evaluation_callbacks(&host);
        CHECK(vc_psv_evaluate_plan(
                  &plan, &node, &callbacks, &action, 1, &report) ==
              VC_PSV_EVALUATE_OK);
        CHECK(action.address == UINT32_C(0x1800));
        CHECK(host.resolve_calls == 1);
        CHECK(host.read_calls == 1);
    }
}

static void test_pointer_write_compatibility_and_failures(void)
{
    unsigned levels;

    for (levels = 6; levels <= 8; ++levels) {
        char source[SOURCE_CAPACITY];
        parsed_fixture fixture;
        vc_psv_plan_node node;
        vc_psv_plan plan;
        vc_psv_compile_options options;

        CHECK(make_pointer_write(source, sizeof(source), 0, levels,
                                 UINT16_C(0x3300)));
        CHECK(compile_source(source, NULL, &fixture,
                             &node, 1, &plan) ==
              VC_PSV_COMPILE_NONCANONICAL);
        options.compatibility =
            VC_PSV_COMPILE_ALLOW_POINTER_LEVELS_6_8;
        options.action_limit = 1;
        CHECK(compile_source(source, &options, &fixture,
                             &node, 1, &plan) == VC_PSV_COMPILE_OK);
        CHECK((node.diagnostics &
               VC_PSV_PLAN_DIAGNOSTIC_POINTER_LEVEL) != 0);
        CHECK(plan.compatibility_diagnostics == 1);
    }

    {
        const uint16_t terminals[] = {
            UINT16_C(0x3302), UINT16_C(0x9000)
        };
        size_t index;

        for (index = 0; index < 2; ++index) {
            char source[SOURCE_CAPACITY];
            parsed_fixture fixture;
            vc_psv_plan_node node;
            vc_psv_plan plan;
            vc_psv_compile_options options;

            CHECK(make_pointer_write(source, sizeof(source), 0, 1,
                                     terminals[index]));
            CHECK(compile_source(source, NULL, &fixture,
                                 &node, 1, &plan) ==
                  VC_PSV_COMPILE_NONCANONICAL);
            options.compatibility =
                VC_PSV_COMPILE_ALLOW_POINTER_TERMINAL_MARKERS;
            options.action_limit = 1;
            CHECK(compile_source(source, &options, &fixture,
                                 &node, 1, &plan) ==
                  VC_PSV_COMPILE_OK);
            CHECK((node.diagnostics &
                   VC_PSV_PLAN_DIAGNOSTIC_POINTER_TERMINAL_MARKER) != 0);
        }
    }

    {
        static const char invalid_width[] =
            "_V0 Invalid width\n"
            "$3F01 00001000 00000000\n"
            "$3300 00000000 00000001\n";
        static const char zero_levels[] =
            "_V0 Zero levels\n"
            "$3000 00001000 00000000\n";
        parsed_fixture fixture;
        vc_psv_plan_node node;
        vc_psv_plan plan;

        CHECK(compile_source(invalid_width, NULL, &fixture,
                             &node, 1, &plan) ==
              VC_PSV_COMPILE_MALFORMED);
        CHECK(compile_source(zero_levels, NULL, &fixture,
                             &node, 1, &plan) ==
              VC_PSV_COMPILE_MALFORMED);
    }
}

static bool make_pointer_repeat(char *source,
                                size_t capacity,
                                unsigned selector,
                                uint16_t count,
                                uint32_t gap,
                                uint16_t marker)
{
    size_t length = 0;

    return append_text(
        source, capacity, &length,
        "_V0 Pointer repeat\n"
        "$7002 00001000 00000000\n"
        "$7000 00000000 00000000\n"
        "$%04X 00000000 FFFFFFFE\n"
        "$%04X %08X 00000003\n",
        marker == UINT16_C(0) ? (uint16_t)(UINT16_C(0x7700) | selector)
                             : marker,
        count, gap);
}

static void prepare_repeat_paths(fake_host *host, unsigned selector)
{
    store_u32(host, UINT32_C(0x1000), UINT32_C(0x1100));
    store_u32(host, UINT32_C(0x1100), UINT32_C(0x1200));
    if (selector == 0) {
        store_u32(host, UINT32_C(0x1004), UINT32_C(0x1300));
        store_u32(host, UINT32_C(0x1300), UINT32_C(0x1400));
    } else if (selector == 1) {
        store_u32(host, UINT32_C(0x1104), UINT32_C(0x1400));
    }
}

static void test_pointer_repeat_selectors_counts_and_wrap(void)
{
    unsigned selector;

    for (selector = 0; selector <= 2; ++selector) {
        char source[SOURCE_CAPACITY];
        parsed_fixture fixture;
        vc_psv_plan_node node;
        vc_psv_plan plan;
        vc_psv_action actions[2];
        vc_psv_evaluation_report report;
        vc_psv_evaluation_callbacks callbacks;
        fake_host host;

        CHECK(make_pointer_repeat(source, sizeof(source), selector, 2,
                                  UINT32_C(4), UINT16_C(0)));
        CHECK(compile_source(source, NULL, &fixture,
                             &node, 1, &plan) == VC_PSV_COMPILE_OK);
        CHECK(node.pointer_increment_selector == selector);
        CHECK(node.physical_count == 4);
        CHECK(plan.maximum_memory_reads == 4);
        reset_host(&host);
        prepare_repeat_paths(&host, selector);
        callbacks = evaluation_callbacks(&host);
        CHECK(vc_psv_evaluate_plan(
                  &plan, &node, &callbacks, actions, 2, &report) ==
              VC_PSV_EVALUATE_OK);
        CHECK(report.action_count == 2);
        CHECK(actions[0].address == UINT32_C(0x1200));
        CHECK(actions[1].address ==
              (selector == 2 ? UINT32_C(0x1204)
                             : UINT32_C(0x1400)));
        CHECK(actions[0].bytes[0] == UINT8_C(0xfe));
        CHECK(actions[1].bytes[0] == UINT8_C(0x01));
    }

    {
        char source[SOURCE_CAPACITY];
        parsed_fixture fixture;
        vc_psv_plan_node node;
        vc_psv_plan plan;
        vc_psv_action action;
        vc_psv_evaluation_report report;
        vc_psv_evaluation_callbacks callbacks;
        vc_psv_compile_options options;
        fake_host host;

        CHECK(make_pointer_repeat(source, sizeof(source), 0, 0,
                                  UINT32_C(0), UINT16_C(0)));
        CHECK(compile_source(source, NULL, &fixture,
                             &node, 1, &plan) == VC_PSV_COMPILE_OK);
        CHECK(plan.maximum_actions == 0);
        CHECK(plan.maximum_memory_reads == 0);
        memset(&callbacks, 0, sizeof(callbacks));
        CHECK(vc_psv_evaluate_plan(
                  &plan, &node, &callbacks, NULL, 0, &report) ==
              VC_PSV_EVALUATE_OK);

        CHECK(make_pointer_repeat(source, sizeof(source), 2, 1,
                                  UINT32_C(0), UINT16_C(0)));
        CHECK(compile_source(source, NULL, &fixture,
                             &node, 1, &plan) == VC_PSV_COMPILE_OK);
        reset_host(&host);
        prepare_repeat_paths(&host, 2);
        callbacks = evaluation_callbacks(&host);
        CHECK(vc_psv_evaluate_plan(
                  &plan, &node, &callbacks, &action, 1, &report) ==
              VC_PSV_EVALUATE_OK);
        CHECK(report.action_count == 1);

        CHECK(make_pointer_repeat(source, sizeof(source), 2,
                                  UINT16_C(0xffff), UINT32_C(0),
                                  UINT16_C(0)));
        CHECK(compile_source(source, NULL, &fixture,
                             &node, 1, &plan) ==
              VC_PSV_COMPILE_ACTION_LIMIT);
        options.compatibility = VC_PSV_COMPILE_STRICT;
        options.action_limit = 65535;
        CHECK(compile_source(source, &options, &fixture,
                             &node, 1, &plan) == VC_PSV_COMPILE_OK);
        CHECK(plan.maximum_actions == (size_t)65535);
        CHECK(plan.maximum_memory_reads == (size_t)131070);
    }

    {
        char source[SOURCE_CAPACITY];
        parsed_fixture fixture;
        vc_psv_plan_node node;
        vc_psv_plan plan;
        vc_psv_compile_options options;

        CHECK(make_pointer_repeat(source, sizeof(source), 0, 1,
                                  UINT32_C(0x10000), UINT16_C(0)));
        CHECK(compile_source(source, NULL, &fixture,
                             &node, 1, &plan) ==
              VC_PSV_COMPILE_NONCANONICAL);
        options.compatibility =
            VC_PSV_COMPILE_ALLOW_POINTER_U32_GAP;
        options.action_limit = 1;
        CHECK(compile_source(source, &options, &fixture,
                             &node, 1, &plan) == VC_PSV_COMPILE_OK);
        CHECK((node.diagnostics &
               VC_PSV_PLAN_DIAGNOSTIC_POINTER_U32_GAP) != 0);

        CHECK(make_pointer_repeat(source, sizeof(source), 2, 1,
                                  UINT32_C(0), UINT16_C(0x7402)));
        CHECK(compile_source(source, NULL, &fixture,
                             &node, 1, &plan) ==
              VC_PSV_COMPILE_NONCANONICAL);
        options.compatibility =
            VC_PSV_COMPILE_ALLOW_POINTER_TERMINAL_MARKERS;
        CHECK(compile_source(source, &options, &fixture,
                             &node, 1, &plan) == VC_PSV_COMPILE_OK);
        CHECK((node.diagnostics &
               VC_PSV_PLAN_DIAGNOSTIC_POINTER_REPEAT_MARKER) != 0);

        CHECK(make_pointer_repeat(source, sizeof(source), 3, 1,
                                  UINT32_C(0), UINT16_C(0)));
        CHECK(compile_source(source, NULL, &fixture,
                             &node, 1, &plan) ==
              VC_PSV_COMPILE_MALFORMED);
    }

    {
        static const char zero_gap[] =
            "_V0 Zero gap\n"
            "$7001 00001000 00000000\n"
            "$7701 00000000 FFFFFFFE\n"
            "$0002 00000000 00000003\n";
        static const char wrap[] =
            "_V0 Address wrap\n"
            "$7001 00001000 00000000\n"
            "$7701 00000000 FFFFFFFE\n"
            "$0002 00000008 00000003\n";
        parsed_fixture fixture;
        vc_psv_plan_node node;
        vc_psv_plan plan;
        vc_psv_action actions[2];
        vc_psv_evaluation_report report;
        vc_psv_evaluation_callbacks callbacks;
        fake_host host;

        CHECK(compile_source(zero_gap, NULL, &fixture,
                             &node, 1, &plan) == VC_PSV_COMPILE_OK);
        reset_host(&host);
        store_u32(&host, UINT32_C(0x1000), UINT32_C(0x1800));
        callbacks = evaluation_callbacks(&host);
        CHECK(vc_psv_evaluate_plan(
                  &plan, &node, &callbacks, actions, 2, &report) ==
              VC_PSV_EVALUATE_OK);
        CHECK(actions[0].address == UINT32_C(0x1800));
        CHECK(actions[1].address == UINT32_C(0x1800));
        CHECK(actions[1].bytes[0] == UINT8_C(1));

        CHECK(compile_source(wrap, NULL, &fixture,
                             &node, 1, &plan) == VC_PSV_COMPILE_OK);
        reset_host(&host);
        store_u32(&host, UINT32_C(0x1000), UINT32_C(0xfffffffc));
        callbacks = evaluation_callbacks(&host);
        CHECK(vc_psv_evaluate_plan(
                  &plan, &node, &callbacks, actions, 2, &report) ==
              VC_PSV_EVALUATE_OK);
        CHECK(actions[0].address == UINT32_C(0xfffffffc));
        CHECK(actions[1].address == UINT32_C(4));
        CHECK(actions[1].bytes[0] == UINT8_C(1));
    }
}

static bool make_pointer_move(char *source,
                              size_t capacity,
                              unsigned destination_width,
                              unsigned levels,
                              unsigned source_width,
                              unsigned source_levels,
                              uint16_t destination_marker,
                              uint16_t source_marker)
{
    size_t length = 0;
    unsigned level;

    if (!append_text(source, capacity, &length,
                     "_V0 Pointer move\n"
                     "$8%X0%X 00001000 00000000\n",
                     destination_width, levels)) {
        return false;
    }
    for (level = 1; level < levels; ++level) {
        if (!append_text(source, capacity, &length,
                         "$8%X00 00000000 00000000\n",
                         destination_width)) {
            return false;
        }
    }
    if (!append_text(source, capacity, &length,
                     "$%04X 00000000 00000000\n"
                     "$8%X0%X 00001100 00000000\n",
                     destination_marker, source_width, source_levels)) {
        return false;
    }
    for (level = 1; level < levels; ++level) {
        if (!append_text(source, capacity, &length,
                         "$8%X00 00000000 00000000\n",
                         source_width)) {
            return false;
        }
    }
    return append_text(source, capacity, &length,
                       "$%04X 00000000 00000000\n", source_marker);
}

static void prepare_move_paths(fake_host *host,
                               unsigned levels,
                               uint32_t destination,
                               uint32_t source)
{
    uint32_t destination_cursor = UINT32_C(0x1000);
    uint32_t source_cursor = UINT32_C(0x1100);
    unsigned level;

    for (level = 0; level < levels; ++level) {
        const uint32_t destination_next =
            level + 1u == levels
                ? destination
                : UINT32_C(0x2000) + (uint32_t)level * UINT32_C(0x100);
        const uint32_t source_next =
            level + 1u == levels
                ? source
                : UINT32_C(0x3000) + (uint32_t)level * UINT32_C(0x100);

        store_u32(host, destination_cursor, destination_next);
        store_u32(host, source_cursor, source_next);
        destination_cursor = destination_next;
        source_cursor = source_next;
    }
}

static void test_pointer_move_matching_mismatch_and_snapshot(void)
{
    unsigned width;

    for (width = 0; width <= 2; ++width) {
        unsigned levels;

        for (levels = 1; levels <= 5; ++levels) {
            char source_text[SOURCE_CAPACITY];
            parsed_fixture fixture;
            vc_psv_plan_node node;
            vc_psv_plan plan;
            vc_psv_action action;
            vc_psv_evaluation_report report;
            vc_psv_evaluation_callbacks callbacks;
            fake_host host;

            CHECK(make_pointer_move(
                source_text, sizeof(source_text), width, levels,
                width + 4u, levels, UINT16_C(0x8800),
                UINT16_C(0x8900)));
            CHECK(compile_source(source_text, NULL, &fixture,
                                 &node, 1, &plan) ==
                  VC_PSV_COMPILE_OK);
            CHECK(node.kind == VC_PSV_PLAN_POINTER_MOVE);
            CHECK(node.physical_count == (levels + 1u) * 2u);
            reset_host(&host);
            prepare_move_paths(&host, levels, UINT32_C(0x4001),
                               UINT32_C(0x4000));
            store_u32(&host, UINT32_C(0x4000), UINT32_C(0xa1b2c3d4));
            callbacks = evaluation_callbacks(&host);
            CHECK(vc_psv_evaluate_plan(
                      &plan, &node, &callbacks, &action, 1, &report) ==
                  VC_PSV_EVALUATE_OK);
            CHECK(action.address == UINT32_C(0x4001));
            CHECK(action.bytes[0] == UINT8_C(0xd4));
            CHECK(report.memory_reads == levels * 2u + 1u);
            if (width == 2 && levels == 1) {
                vc_psv_write_callbacks writer;
                vc_psv_apply_report applied;

                writer.context = &host;
                writer.write_memory = fake_write;
                CHECK(vc_psv_apply_actions(
                          &action, 1, &writer, NULL, 0, &applied) ==
                      VC_PSV_EXECUTE_OK);
                CHECK(host.memory[UINT32_C(0x4001) - MEMORY_BASE] ==
                      UINT8_C(0xd4));
                CHECK(host.memory[UINT32_C(0x4002) - MEMORY_BASE] ==
                      UINT8_C(0xc3));
            }
        }
    }

    {
        char source[SOURCE_CAPACITY];
        parsed_fixture fixture;
        vc_psv_plan_node node;
        vc_psv_plan plan;
        vc_psv_compile_options options;

        CHECK(make_pointer_move(
            source, sizeof(source), 0, 2, 5, 1,
            UINT16_C(0x8900), UINT16_C(0x8800)));
        CHECK(compile_source(source, NULL, &fixture,
                             &node, 1, &plan) ==
              VC_PSV_COMPILE_NONCANONICAL);
        options.compatibility =
            VC_PSV_COMPILE_ALLOW_POINTER_MOV_MISMATCH;
        options.action_limit = 1;
        CHECK(compile_source(source, &options, &fixture,
                             &node, 1, &plan) == VC_PSV_COMPILE_OK);
        CHECK(node.width == VC_PSV_WIDTH_U8);
        CHECK(node.observed_source_width_index == UINT8_C(5));
        CHECK(node.observed_source_level_count == UINT8_C(1));
        CHECK((node.diagnostics &
               VC_PSV_PLAN_DIAGNOSTIC_POINTER_MOV_WIDTH) != 0);
        CHECK((node.diagnostics &
               VC_PSV_PLAN_DIAGNOSTIC_POINTER_MOV_LEVEL) != 0);
        CHECK((node.diagnostics &
               VC_PSV_PLAN_DIAGNOSTIC_POINTER_MOV_MARKER) != 0);
    }
}

static void test_b2_applies_only_to_pointer_bases(void)
{
    static const char repeat_source[] =
        "_V0 Relative pointer repeat\n"
        "$B200 00000001 00000000\n"
        "$7001 00000020 00000004\n"
        "$7700 00000000 00000001\n"
        "$0002 00000004 00000000\n";
    static const char move_source[] =
        "_V0 Relative pointer move\n"
        "$B200 00000001 00000000\n"
        "$8201 00000020 00000000\n"
        "$8800 00000000 00000000\n"
        "$8601 00000040 00000000\n"
        "$8900 00000000 00000000\n";
    parsed_fixture fixture;
    vc_psv_plan_node node;
    vc_psv_plan plan;
    vc_psv_action actions[2];
    vc_psv_evaluation_report report;
    vc_psv_evaluation_callbacks callbacks;
    fake_host host;

    CHECK(compile_source(repeat_source, NULL, &fixture,
                         &node, 1, &plan) == VC_PSV_COMPILE_OK);
    reset_host(&host);
    store_u32(&host, UINT32_C(0x1020), UINT32_C(0x1800));
    store_u32(&host, UINT32_C(0x1024), UINT32_C(0x1900));
    callbacks = evaluation_callbacks(&host);
    CHECK(vc_psv_evaluate_plan(
              &plan, &node, &callbacks, actions, 2, &report) ==
          VC_PSV_EVALUATE_OK);
    CHECK(actions[0].address == UINT32_C(0x1804));
    CHECK(actions[1].address == UINT32_C(0x1904));
    CHECK(host.resolve_calls == 2);

    CHECK(compile_source(move_source, NULL, &fixture,
                         &node, 1, &plan) == VC_PSV_COMPILE_OK);
    reset_host(&host);
    store_u32(&host, UINT32_C(0x1020), UINT32_C(0x1800));
    store_u32(&host, UINT32_C(0x1040), UINT32_C(0x1900));
    store_u32(&host, UINT32_C(0x1900), UINT32_C(0xaabbccdd));
    callbacks = evaluation_callbacks(&host);
    CHECK(vc_psv_evaluate_plan(
              &plan, &node, &callbacks, actions, 2, &report) ==
          VC_PSV_EVALUATE_OK);
    CHECK(actions[0].address == UINT32_C(0x1800));
    CHECK(actions[0].bytes[0] == UINT8_C(0xdd));
    CHECK(actions[0].bytes[3] == UINT8_C(0xaa));
    CHECK(host.resolve_calls == 2);
}

static void test_cursor_gates_and_positional_dispatch(void)
{
    static const char source[] =
        "_V0 Positional count\n"
        "$7001 00001000 00000000\n"
        "$7700 00000000 00000001\n"
        "$B200 00000001 00000000\n"
        "$0000 00001020 000000AA\n";
    static const char gated[] =
        "_V0 Gate spans\n"
        "$C202 00000001 00000001\n"
        "$3001 00001000 00000000\n"
        "$3300 00000000 00000011\n"
        "$D003 00001800 00000001\n"
        "$7001 00001100 00000000\n"
        "$7700 00000000 00000022\n"
        "$0001 00000000 00000000\n"
        "$C204 00000001 00000001\n"
        "$8001 00001200 00000000\n"
        "$8800 00000000 00000000\n"
        "$8401 00001300 00000000\n"
        "$8900 00000000 00000000\n"
        "$0000 00001400 000000AA\n";
    static const char split[] =
        "_V0 Split pointer\n"
        "$C201 00000001 00000001\n"
        "$3001 00001000 00000000\n"
        "$3300 00000000 00000001\n";
    parsed_fixture fixture;
    vc_psv_plan_node nodes[8];
    vc_psv_plan plan;
    vc_psv_action actions[4];
    vc_psv_evaluation_report report;
    vc_psv_evaluation_callbacks callbacks;
    fake_host host;

    CHECK(parse_source(source, &fixture));
    CHECK(fixture.operations[2].kind ==
          VC_PSV_OPERATION_POINTER_POSITIONAL);
    CHECK(fixture.operations[3].address_mode ==
          VC_PSV_ADDRESS_ABSOLUTE);

    CHECK(compile_source(gated, NULL, &fixture,
                         nodes, 8, &plan) == VC_PSV_COMPILE_OK);
    reset_host(&host);
    callbacks = evaluation_callbacks(&host);
    CHECK(vc_psv_evaluate_plan(
              &plan, nodes, &callbacks, actions, 4, &report) ==
          VC_PSV_EVALUATE_OK);
    CHECK(report.action_count == 1);
    CHECK(actions[0].address == UINT32_C(0x1400));
    CHECK(report.skipped_physical_records == 9);
    CHECK(host.read_calls == 1);

    CHECK(compile_source(split, NULL, &fixture,
                         nodes, 8, &plan) ==
          VC_PSV_COMPILE_MALFORMED);
}

static void test_truncation_and_read_failures_are_atomic(void)
{
    static const char *const write_records[] = {
        "$3002 00001000 00000000\n",
        "$3000 00000000 00000000\n",
        "$3300 00000000 00000001\n"
    };
    static const char *const repeat_records[] = {
        "$7002 00001000 00000000\n",
        "$7000 00000000 00000000\n",
        "$7700 00000000 00000001\n",
        "$0001 00000000 00000000\n"
    };
    static const char *const move_records[] = {
        "$8002 00001000 00000000\n",
        "$8000 00000000 00000000\n",
        "$8800 00000000 00000000\n",
        "$8402 00001100 00000000\n",
        "$8400 00000000 00000000\n",
        "$8900 00000000 00000000\n"
    };
    const char *const *families[] = {
        write_records, repeat_records, move_records
    };
    const size_t family_counts[] = {3, 4, 6};
    size_t family;

    for (family = 0; family < 3; ++family) {
        size_t prefix;

        for (prefix = 1; prefix < family_counts[family]; ++prefix) {
            char source[SOURCE_CAPACITY];
            size_t length = 0;
            size_t record;
            parsed_fixture fixture;
            vc_psv_plan_node node;
            vc_psv_plan plan;

            CHECK(append_text(source, sizeof(source), &length,
                              "_V0 Truncated\n"));
            for (record = 0; record < prefix; ++record) {
                CHECK(append_text(source, sizeof(source), &length, "%s",
                                  families[family][record]));
            }
            CHECK(compile_source(source, NULL, &fixture,
                                 &node, 1, &plan) ==
                  VC_PSV_COMPILE_MALFORMED);
        }
    }

    {
        static const char source[] =
            "_V0 Atomic read failure\n"
            "$0000 00001400 000000AA\n"
            "$3001 00001000 00000000\n"
            "$3300 00000000 000000BB\n";
        parsed_fixture fixture;
        vc_psv_plan_node nodes[2];
        vc_psv_plan plan;
        vc_psv_action actions[2];
        vc_psv_evaluation_report report;
        vc_psv_evaluation_callbacks callbacks;
        vc_psv_write_callbacks writer;
        vc_psv_apply_report applied;
        fake_host host;
        union {
            vc_psv_plan_node nodes[2];
            vc_psv_action actions[2];
        } aliased_evaluation;
        union {
            vc_psv_operation operations[FIXTURE_CAPACITY];
            vc_psv_plan_node nodes[FIXTURE_CAPACITY];
        } aliased_compile;

        CHECK(compile_source(source, NULL, &fixture,
                             NULL, 0, &plan) ==
              VC_PSV_COMPILE_OUTPUT_TOO_SMALL);
        CHECK(plan.node_count == 2);
        CHECK(compile_source(source, NULL, &fixture,
                             nodes, 2, &plan) == VC_PSV_COMPILE_OK);
        memcpy(aliased_compile.operations, fixture.operations,
               fixture.report.total_operations *
                   sizeof(*fixture.operations));
        CHECK(vc_psv_compile_cheat(
                  &fixture.cheats[0], aliased_compile.operations,
                  fixture.report.total_operations, NULL,
                  aliased_compile.nodes, 2, &plan) ==
              VC_PSV_COMPILE_INVALID_ARGUMENT);
        reset_host(&host);
        host.fail_read_call = 0;
        callbacks = evaluation_callbacks(&host);
        memset(actions, 0xa5, sizeof(actions));
        CHECK(vc_psv_evaluate_plan(
                  &plan, nodes, &callbacks, actions, 2, &report) ==
              VC_PSV_EVALUATE_CALLBACK_FAILED);
        CHECK(report.action_count == 0);
        CHECK(memcmp(actions, &(vc_psv_action[2]){{0}},
                     sizeof(actions)) == 0);
        CHECK(host.write_calls == 0);

        memcpy(aliased_evaluation.nodes, nodes, sizeof(nodes));
        CHECK(vc_psv_evaluate_plan(
                  &plan, aliased_evaluation.nodes, &callbacks,
                  aliased_evaluation.actions, 2, &report) ==
              VC_PSV_EVALUATE_INVALID_PLAN);

        writer.context = &host;
        writer.write_memory = fake_write;
        CHECK(vc_psv_apply_actions(
                  actions, report.action_count, &writer,
                  NULL, 0, &applied) == VC_PSV_EXECUTE_OK);
        CHECK(host.write_calls == 0);
    }

    {
        static const char source[] =
            "_V0 Invalid target\n"
            "$3201 00001000 00000000\n"
            "$3300 00000000 DEADBEEF\n";
        parsed_fixture fixture;
        vc_psv_plan_node node;
        vc_psv_plan plan;
        vc_psv_action action;
        vc_psv_evaluation_report report;
        vc_psv_evaluation_callbacks callbacks;
        fake_host host;

        CHECK(compile_source(source, NULL, &fixture,
                             &node, 1, &plan) == VC_PSV_COMPILE_OK);
        reset_host(&host);
        store_u32(&host, UINT32_C(0x1000), UINT32_MAX);
        callbacks = evaluation_callbacks(&host);
        CHECK(vc_psv_evaluate_plan(
                  &plan, &node, &callbacks, &action, 1, &report) ==
              VC_PSV_EVALUATE_ADDRESS_OVERFLOW);
        callbacks.read_memory = NULL;
        CHECK(vc_psv_evaluate_plan(
                  &plan, &node, &callbacks, &action, 1, &report) ==
              VC_PSV_EVALUATE_INVALID_PLAN);
    }

    {
        static const char null_pointer[] =
            "_V0 Null pointer\n"
            "$3001 00001000 00000004\n"
            "$3300 00000000 00000001\n";
        static const char bad_marker_argument[] =
            "_V0 Marker argument\n"
            "$3001 00001000 00000000\n"
            "$3300 00000001 00000001\n";
        parsed_fixture fixture;
        vc_psv_plan_node node;
        vc_psv_plan plan;
        vc_psv_action action;
        vc_psv_evaluation_report report;
        vc_psv_evaluation_callbacks callbacks;
        fake_host host;

        CHECK(compile_source(null_pointer, NULL, &fixture,
                             &node, 1, &plan) == VC_PSV_COMPILE_OK);
        reset_host(&host);
        store_u32(&host, UINT32_C(0x1000), UINT32_C(0));
        callbacks = evaluation_callbacks(&host);
        CHECK(vc_psv_evaluate_plan(
                  &plan, &node, &callbacks, &action, 1, &report) ==
              VC_PSV_EVALUATE_INVALID_POINTER);
        CHECK(report.action_count == 0);

        CHECK(compile_source(bad_marker_argument, NULL, &fixture,
                             &node, 1, &plan) ==
              VC_PSV_COMPILE_MALFORMED);
    }
}

static void test_pointer_span_property(void)
{
    unsigned family;
    unsigned levels;

    for (family = 0; family < 3; ++family) {
        for (levels = 1; levels <= 8; ++levels) {
            char source[SOURCE_CAPACITY];
            parsed_fixture fixture;
            vc_psv_plan_node node;
            vc_psv_plan plan;
            vc_psv_compile_options options;
            size_t expected_span;

            if (family == 0) {
                CHECK(make_pointer_write(source, sizeof(source), 0,
                                         levels, UINT16_C(0x3300)));
                expected_span = levels + 1u;
            } else if (family == 1) {
                size_t length = 0;
                unsigned level;
                const unsigned width = levels % 3u;

                CHECK(append_text(source, sizeof(source), &length,
                                  "_V0 Generated repeat\n"
                                  "$7%X0%X 00001000 00000000\n",
                                  width, levels));
                for (level = 1; level < levels; ++level) {
                    CHECK(append_text(
                        source, sizeof(source), &length,
                        "$7%X00 00000000 00000000\n", width));
                }
                CHECK(append_text(
                    source, sizeof(source), &length,
                    "$7700 00000000 00000001\n"
                    "$0001 00000000 00000000\n"));
                expected_span = levels + 2u;
            } else {
                CHECK(make_pointer_move(
                    source, sizeof(source), 0, levels, 4, levels,
                    UINT16_C(0x8800), UINT16_C(0x8900)));
                expected_span = (levels + 1u) * 2u;
            }
            options.compatibility =
                levels > 5
                    ? VC_PSV_COMPILE_ALLOW_POINTER_LEVELS_6_8
                    : VC_PSV_COMPILE_STRICT;
            options.action_limit = 1;
            CHECK(compile_source(source, &options, &fixture,
                                 &node, 1, &plan) == VC_PSV_COMPILE_OK);
            CHECK(node.physical_count == expected_span);
            CHECK(fixture.report.total_operations == expected_span);
        }
    }
}

int main(void)
{
    test_pointer_write_levels_widths_offsets_and_b2();
    test_pointer_write_compatibility_and_failures();
    test_pointer_repeat_selectors_counts_and_wrap();
    test_pointer_move_matching_mismatch_and_snapshot();
    test_b2_applies_only_to_pointer_bases();
    test_cursor_gates_and_positional_dispatch();
    test_truncation_and_read_failures_are_atomic();
    test_pointer_span_property();

    if (failures != 0) {
        fprintf(stderr, "%d legacy pointer plan test(s) failed\n",
                failures);
        return 1;
    }
    puts("VitaCheat legacy pointer plan tests passed");
    return 0;
}
