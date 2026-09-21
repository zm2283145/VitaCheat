#include "vitacheat/legacy_plan.h"

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

#define FIXTURE_CAPACITY ((size_t)256)
#define FAKE_MEMORY_BASE UINT32_C(0x00001000)
#define FAKE_MEMORY_SIZE ((size_t)0x400)
#define WRITE_LOG_CAPACITY ((size_t)128)

typedef struct parsed_fixture {
    vc_psv_line lines[FIXTURE_CAPACITY];
    vc_psv_cheat cheats[FIXTURE_CAPACITY];
    vc_psv_operation operations[FIXTURE_CAPACITY];
    vc_psv_report report;
} parsed_fixture;

typedef struct fake_write_log {
    uint32_t address;
    size_t size;
    uint8_t bytes[4];
} fake_write_log;

typedef struct fake_host {
    uint8_t memory[FAKE_MEMORY_SIZE];
    uint32_t button_mask;
    uint32_t last_button_mode;
    size_t resolve_calls;
    size_t read_calls;
    size_t button_calls;
    size_t write_calls;
    size_t successful_writes;
    size_t fail_write_call;
    bool force_overflow_base;
    fake_write_log write_log[WRITE_LOG_CAPACITY];
} fake_host;

static bool parse_fixture(const char *source, parsed_fixture *fixture)
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

static vc_psv_compile_status compile_fixture(
    const parsed_fixture *fixture,
    size_t cheat_index,
    const vc_psv_compile_options *options,
    vc_psv_plan_node *nodes,
    size_t node_capacity,
    vc_psv_plan *plan)
{
    CHECK(cheat_index < fixture->report.total_cheats);
    if (cheat_index >= fixture->report.total_cheats) {
        return VC_PSV_COMPILE_INVALID_ARGUMENT;
    }
    return vc_psv_compile_cheat(
        &fixture->cheats[cheat_index],
        fixture->operations,
        fixture->report.total_operations,
        options, nodes, node_capacity, plan);
}

static bool fake_range(uint32_t address, size_t size, size_t *offset)
{
    const uint64_t begin = address;
    const uint64_t end = begin + size;
    const uint64_t memory_begin = FAKE_MEMORY_BASE;
    const uint64_t memory_end = memory_begin + FAKE_MEMORY_SIZE;

    if (begin < memory_begin || end > memory_end || end < begin) {
        return false;
    }
    *offset = (size_t)(begin - memory_begin);
    return true;
}

static bool fake_resolve(void *context,
                         uint8_t module_serial,
                         uint8_t segment_index,
                         uint32_t *base)
{
    fake_host *host = context;

    ++host->resolve_calls;
    if (host->force_overflow_base) {
        *base = UINT32_C(0xfffffff0);
        return true;
    }
    if (module_serial == 0 && segment_index == 1) {
        *base = UINT32_C(0x00001000);
        return true;
    }
    if (module_serial == 1 && segment_index == 0) {
        *base = UINT32_C(0x00001100);
        return true;
    }
    if (module_serial == UINT8_C(0x0e) && segment_index == 1) {
        *base = UINT32_C(0x00001200);
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

    ++host->read_calls;
    if (!fake_range(address, size, &offset)) {
        return false;
    }
    memcpy(bytes, &host->memory[offset], size);
    return true;
}

static bool fake_buttons(void *context,
                         uint32_t mode,
                         uint32_t *normalized_mask)
{
    fake_host *host = context;

    ++host->button_calls;
    host->last_button_mode = mode;
    *normalized_mask = host->button_mask;
    return true;
}

static bool fake_write(void *context,
                       uint32_t address,
                       const uint8_t *bytes,
                       size_t size)
{
    fake_host *host = context;
    size_t offset;

    if (host->write_calls == host->fail_write_call) {
        ++host->write_calls;
        return false;
    }
    ++host->write_calls;
    if (!fake_range(address, size, &offset)) {
        return false;
    }
    if (host->successful_writes < WRITE_LOG_CAPACITY) {
        fake_write_log *entry = &host->write_log[host->successful_writes];
        entry->address = address;
        entry->size = size;
        memset(entry->bytes, 0, sizeof(entry->bytes));
        memcpy(entry->bytes, bytes, size);
    }
    ++host->successful_writes;
    memcpy(&host->memory[offset], bytes, size);
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

static vc_psv_write_callbacks write_callbacks(fake_host *host)
{
    vc_psv_write_callbacks callbacks;

    callbacks.context = host;
    callbacks.write_memory = fake_write;
    return callbacks;
}

static void reset_host(fake_host *host)
{
    memset(host, 0, sizeof(*host));
    host->fail_write_call = SIZE_MAX;
}

static void store_u32(fake_host *host,
                      uint32_t address,
                      uint32_t value,
                      size_t size)
{
    size_t offset;
    size_t index;

    CHECK(fake_range(address, size, &offset));
    if (!fake_range(address, size, &offset)) {
        return;
    }
    for (index = 0; index < size; ++index) {
        host->memory[offset + index] =
            (uint8_t)(value >> (index * 8u));
    }
}

static void test_b2_write_and_move(void)
{
    static const char source[] =
        "_V0 Relative scalar operations\n"
        "$B200 00000001 00000000\n"
        "$0000 00000010 00001234\n"
        "$5100 00000020 00000030\n";
    parsed_fixture fixture;
    vc_psv_plan_node nodes[2];
    vc_psv_plan plan;
    vc_psv_action actions[2];
    vc_psv_evaluation_report evaluation;
    vc_psv_apply_report applied;
    fake_host host;
    vc_psv_evaluation_callbacks evaluator;
    vc_psv_write_callbacks writer;

    if (!parse_fixture(source, &fixture)) {
        return;
    }
    CHECK(fixture.report.schema_version == UINT32_C(4));
    CHECK(compile_fixture(&fixture, 0, NULL, nodes, 2, &plan) ==
          VC_PSV_COMPILE_OK);
    CHECK(plan.node_count == 2);
    CHECK(plan.physical_record_count == 3);
    CHECK(nodes[0].destination.module_serial == 0);
    CHECK(nodes[0].destination.segment_index == 1);
    CHECK(nodes[1].source.module_serial == 0);
    CHECK(nodes[1].destination.module_serial == 0);

    reset_host(&host);
    store_u32(&host, UINT32_C(0x1030), UINT32_C(0x0000abcd), 2);
    evaluator = evaluation_callbacks(&host);
    CHECK(vc_psv_evaluate_plan(&plan, nodes, &evaluator,
                               actions, 2, &evaluation) ==
          VC_PSV_EVALUATE_OK);
    CHECK(evaluation.action_count == 2);
    CHECK(actions[0].address == UINT32_C(0x1010));
    CHECK(actions[0].bytes[0] == UINT8_C(0x34));
    CHECK(actions[1].address == UINT32_C(0x1020));
    CHECK(actions[1].bytes[0] == UINT8_C(0xcd));
    CHECK(actions[1].bytes[1] == UINT8_C(0xab));
    CHECK(host.read_calls == 1);

    writer = write_callbacks(&host);
    CHECK(vc_psv_apply_actions(actions, evaluation.action_count, &writer,
                               NULL, 0, &applied) == VC_PSV_EXECUTE_OK);
    CHECK(applied.applied_actions == 2);
    CHECK(applied.active_patch_entries == 0);
    CHECK(host.memory[0x10] == UINT8_C(0x34));
    CHECK(host.memory[0x20] == UINT8_C(0xcd));
    CHECK(host.memory[0x21] == UINT8_C(0xab));
}

static void test_all_direct_and_move_widths(void)
{
    static const char source[] =
        "_V0 Width coverage\n"
        "$0000 00001000 12345678\n"
        "$0100 00001002 12345678\n"
        "$0200 00001004 12345678\n"
        "$5000 00001010 00001020\n"
        "$5100 00001012 00001022\n"
        "$5200 00001014 00001024\n";
    parsed_fixture fixture;
    vc_psv_plan_node nodes[6];
    vc_psv_plan plan;
    vc_psv_action actions[6];
    vc_psv_evaluation_report evaluation;
    vc_psv_evaluation_callbacks evaluator;
    fake_host host;

    if (!parse_fixture(source, &fixture)) {
        return;
    }
    CHECK(compile_fixture(&fixture, 0, NULL, nodes, 6, &plan) ==
          VC_PSV_COMPILE_OK);
    reset_host(&host);
    store_u32(&host, UINT32_C(0x1020), UINT32_C(0xaabbccdd), 4);
    store_u32(&host, UINT32_C(0x1024), UINT32_C(0xdeadbeef), 4);
    evaluator = evaluation_callbacks(&host);
    CHECK(vc_psv_evaluate_plan(&plan, nodes, &evaluator,
                               actions, 6, &evaluation) ==
          VC_PSV_EVALUATE_OK);
    CHECK(evaluation.action_count == 6);
    CHECK(actions[0].width == VC_PSV_WIDTH_U8);
    CHECK(actions[0].bytes[0] == UINT8_C(0x78));
    CHECK(actions[1].width == VC_PSV_WIDTH_U16);
    CHECK(actions[1].bytes[0] == UINT8_C(0x78));
    CHECK(actions[1].bytes[1] == UINT8_C(0x56));
    CHECK(actions[2].width == VC_PSV_WIDTH_U32);
    CHECK(actions[2].bytes[3] == UINT8_C(0x12));
    CHECK(actions[3].width == VC_PSV_WIDTH_U8);
    CHECK(actions[3].bytes[0] == UINT8_C(0xdd));
    CHECK(actions[4].width == VC_PSV_WIDTH_U16);
    CHECK(actions[4].bytes[0] == UINT8_C(0xbb));
    CHECK(actions[4].bytes[1] == UINT8_C(0xaa));
    CHECK(actions[5].width == VC_PSV_WIDTH_U32);
    CHECK(actions[5].bytes[0] == UINT8_C(0xef));
    CHECK(actions[5].bytes[3] == UINT8_C(0xde));
}

static void test_b2_overwrite_and_descriptor_reset(void)
{
    static const char source[] =
        "_V0 Overwrite\n"
        "$B200 00000001 00000000\n"
        "$0000 00000010 00000001\n"
        "$B201 00000000 00000000\n"
        "$0000 00000010 00000002\n"
        "_V0 Reset\n"
        "$0000 00001012 00000003\n";
    parsed_fixture fixture;
    vc_psv_plan_node first_nodes[2];
    vc_psv_plan_node second_node;
    vc_psv_plan first_plan;
    vc_psv_plan second_plan;
    vc_psv_action actions[2];
    vc_psv_evaluation_report evaluation;
    vc_psv_evaluation_callbacks evaluator;
    fake_host host;

    if (!parse_fixture(source, &fixture)) {
        return;
    }
    CHECK(compile_fixture(&fixture, 0, NULL, first_nodes, 2,
                          &first_plan) == VC_PSV_COMPILE_OK);
    CHECK(first_nodes[0].destination.module_serial == 0);
    CHECK(first_nodes[0].destination.segment_index == 1);
    CHECK(first_nodes[1].destination.module_serial == 1);
    CHECK(first_nodes[1].destination.segment_index == 0);
    reset_host(&host);
    evaluator = evaluation_callbacks(&host);
    CHECK(vc_psv_evaluate_plan(&first_plan, first_nodes, &evaluator,
                               actions, 2, &evaluation) ==
          VC_PSV_EVALUATE_OK);
    CHECK(actions[0].address == UINT32_C(0x1010));
    CHECK(actions[1].address == UINT32_C(0x1110));

    CHECK(compile_fixture(&fixture, 1, NULL, &second_node, 1,
                          &second_plan) == VC_PSV_COMPILE_OK);
    CHECK(second_node.destination.mode == VC_PSV_ADDRESS_ABSOLUTE);
    CHECK(vc_psv_evaluate_plan(&second_plan, &second_node, &evaluator,
                               actions, 2, &evaluation) ==
          VC_PSV_EVALUATE_OK);
    CHECK(actions[0].address == UINT32_C(0x1012));
}

static void test_b2_snapshots_every_scalar_address_surface(void)
{
    static const char source[] =
        "_V0 B2 surfaces\n"
        "$B20E 00000001 00000000\n"
        "$A100 00000010 0000BF00\n"
        "$D000 00000020 00000001\n"
        "$5000 00000030 00000040\n"
        "$4001 00000050 00000002\n"
        "$0001 00000004 00000000\n";
    parsed_fixture fixture;
    vc_psv_plan_node nodes[4];
    vc_psv_plan plan;
    vc_psv_action actions[3];
    vc_psv_evaluation_report evaluation;
    vc_psv_evaluation_callbacks evaluator;
    fake_host host;
    size_t index;

    if (!parse_fixture(source, &fixture)) {
        return;
    }
    CHECK(compile_fixture(&fixture, 0, NULL, nodes, 4, &plan) ==
          VC_PSV_COMPILE_OK);
    CHECK(nodes[0].kind == VC_PSV_PLAN_PATCH);
    CHECK(nodes[1].kind == VC_PSV_PLAN_CONDITION_GATE);
    CHECK(nodes[2].kind == VC_PSV_PLAN_MOVE);
    CHECK(nodes[3].kind == VC_PSV_PLAN_REPEAT);
    for (index = 0; index < 4; ++index) {
        CHECK(nodes[index].destination.mode ==
              VC_PSV_ADDRESS_SELECTED_MODULE_RELATIVE);
        CHECK(nodes[index].destination.module_serial == UINT8_C(0x0e));
        CHECK(nodes[index].destination.segment_index == 1);
    }
    CHECK(nodes[2].source.mode ==
          VC_PSV_ADDRESS_SELECTED_MODULE_RELATIVE);
    CHECK(nodes[2].source.module_serial == UINT8_C(0x0e));
    CHECK(nodes[2].source.segment_index == 1);

    reset_host(&host);
    store_u32(&host, UINT32_C(0x1210), UINT32_C(0x1234), 2);
    store_u32(&host, UINT32_C(0x1220), UINT32_C(1), 1);
    store_u32(&host, UINT32_C(0x1240), UINT32_C(0xab), 1);
    evaluator = evaluation_callbacks(&host);
    CHECK(vc_psv_evaluate_plan(&plan, nodes, &evaluator,
                               actions, 3, &evaluation) ==
          VC_PSV_EVALUATE_OK);
    CHECK(evaluation.action_count == 3);
    CHECK(actions[0].address == UINT32_C(0x1210));
    CHECK(actions[1].address == UINT32_C(0x1230));
    CHECK(actions[2].address == UINT32_C(0x1250));
}

static void test_repeat_zero_wrap_truncate_and_limit(void)
{
    static const char wrapping_source[] =
        "_V0 Wrapping repeat\n"
        "$4001 FFFFFFFE 000000FE\n"
        "$0004 00000002 00000003\n";
    static const char relative_source[] =
        "_V0 Relative repeat\n"
        "$B200 00000001 00000000\n"
        "$4001 000000F0 FFFFFFFF\n"
        "$0003 FFFFFFFF 00000002\n";
    static const char zero_source[] =
        "_V0 Zero repeat\n"
        "$B200 00000001 00000000\n"
        "$4201 FFFFFFFF 12345678\n"
        "$0000 FFFFFFFF FFFFFFFF\n";
    static const char maximum_source[] =
        "_V0 Maximum symbolic repeat\n"
        "$4201 00001000 00000001\n"
        "$FFFF 00000004 00000001\n";
    parsed_fixture fixture;
    vc_psv_plan_node node;
    vc_psv_plan plan;
    vc_psv_action actions[4];
    vc_psv_evaluation_report evaluation;
    vc_psv_evaluation_callbacks empty_callbacks;
    vc_psv_evaluation_callbacks evaluator;
    vc_psv_compile_options low_limit;
    fake_host host;

    memset(&empty_callbacks, 0, sizeof(empty_callbacks));
    if (!parse_fixture(wrapping_source, &fixture)) {
        return;
    }
    CHECK(fixture.operations[1].kind ==
          VC_PSV_OPERATION_REPEAT_CONTINUATION);
    CHECK(compile_fixture(&fixture, 0, NULL, &node, 1, &plan) ==
          VC_PSV_COMPILE_OK);
    CHECK(node.repeat_count == 4);
    CHECK(plan.maximum_actions == 4);
    CHECK(vc_psv_evaluate_plan(&plan, &node, &empty_callbacks,
                               actions, 4, &evaluation) ==
          VC_PSV_EVALUATE_OK);
    CHECK(evaluation.action_count == 4);
    CHECK(actions[0].address == UINT32_C(0xfffffffe));
    CHECK(actions[1].address == UINT32_C(0x00000000));
    CHECK(actions[2].address == UINT32_C(0x00000002));
    CHECK(actions[3].address == UINT32_C(0x00000004));
    CHECK(actions[0].bytes[0] == UINT8_C(0xfe));
    CHECK(actions[1].bytes[0] == UINT8_C(0x01));
    CHECK(actions[2].bytes[0] == UINT8_C(0x04));
    CHECK(actions[3].bytes[0] == UINT8_C(0x07));

    low_limit.compatibility = VC_PSV_COMPILE_STRICT;
    low_limit.action_limit = 3;
    memset(&node, 0xa5, sizeof(node));
    CHECK(compile_fixture(&fixture, 0, &low_limit, &node, 1, &plan) ==
          VC_PSV_COMPILE_ACTION_LIMIT);
    CHECK(memcmp(&node, &(vc_psv_plan_node){0}, sizeof(node)) == 0);

    if (!parse_fixture(relative_source, &fixture)) {
        return;
    }
    CHECK(compile_fixture(&fixture, 0, NULL, &node, 1, &plan) ==
          VC_PSV_COMPILE_OK);
    reset_host(&host);
    evaluator = evaluation_callbacks(&host);
    CHECK(vc_psv_evaluate_plan(&plan, &node, &evaluator,
                               actions, 4, &evaluation) ==
          VC_PSV_EVALUATE_OK);
    CHECK(evaluation.action_count == 3);
    CHECK(actions[0].address == UINT32_C(0x10f0));
    CHECK(actions[1].address == UINT32_C(0x10ef));
    CHECK(actions[2].address == UINT32_C(0x10ee));
    CHECK(actions[0].bytes[0] == UINT8_C(0xff));
    CHECK(actions[1].bytes[0] == UINT8_C(0x01));
    CHECK(actions[2].bytes[0] == UINT8_C(0x03));

    if (!parse_fixture(zero_source, &fixture)) {
        return;
    }
    CHECK(compile_fixture(&fixture, 0, NULL, &node, 1, &plan) ==
          VC_PSV_COMPILE_OK);
    CHECK(plan.maximum_actions == 0);
    CHECK(vc_psv_evaluate_plan(&plan, &node, &empty_callbacks,
                               NULL, 0, &evaluation) ==
          VC_PSV_EVALUATE_OK);
    CHECK(evaluation.action_count == 0);

    if (!parse_fixture(maximum_source, &fixture)) {
        return;
    }
    CHECK(compile_fixture(&fixture, 0, NULL, &node, 1, &plan) ==
          VC_PSV_COMPILE_ACTION_LIMIT);
    low_limit.compatibility = VC_PSV_COMPILE_STRICT;
    low_limit.action_limit = 65535;
    CHECK(compile_fixture(&fixture, 0, &low_limit, &node, 1, &plan) ==
          VC_PSV_COMPILE_OK);
    CHECK(node.repeat_count == UINT32_C(0xffff));
    CHECK(plan.maximum_actions == (size_t)65535);
}

static void test_module_address_overflow_is_not_wrapped(void)
{
    static const char source[] =
        "_V0 Relative overflow\n"
        "$B20E 00000001 00000000\n"
        "$0200 00000020 DEADBEEF\n";
    static const char width_source[] =
        "_V0 Width overflow\n"
        "$0200 FFFFFFFE DEADBEEF\n";
    parsed_fixture fixture;
    vc_psv_plan_node node;
    vc_psv_plan plan;
    vc_psv_action action;
    vc_psv_evaluation_report evaluation;
    vc_psv_evaluation_callbacks evaluator;
    fake_host host;

    if (!parse_fixture(source, &fixture)) {
        return;
    }
    CHECK(compile_fixture(&fixture, 0, NULL, &node, 1, &plan) ==
          VC_PSV_COMPILE_OK);
    reset_host(&host);
    host.force_overflow_base = true;
    evaluator = evaluation_callbacks(&host);
    memset(&action, 0xa5, sizeof(action));
    CHECK(vc_psv_evaluate_plan(&plan, &node, &evaluator,
                               &action, 1, &evaluation) ==
          VC_PSV_EVALUATE_ADDRESS_OVERFLOW);
    CHECK(evaluation.action_count == 0);
    CHECK(memcmp(&action, &(vc_psv_action){0}, sizeof(action)) == 0);
    CHECK(host.resolve_calls == 1);
    CHECK(host.read_calls == 0);
    CHECK(host.write_calls == 0);

    if (!parse_fixture(width_source, &fixture)) {
        return;
    }
    CHECK(compile_fixture(&fixture, 0, NULL, &node, 1, &plan) ==
          VC_PSV_COMPILE_OK);
    reset_host(&host);
    evaluator = evaluation_callbacks(&host);
    CHECK(vc_psv_evaluate_plan(&plan, &node, &evaluator,
                               &action, 1, &evaluation) ==
          VC_PSV_EVALUATE_ADDRESS_OVERFLOW);
    CHECK(evaluation.action_count == 0);
    CHECK(host.resolve_calls == 0 && host.read_calls == 0 &&
          host.write_calls == 0);
}

static void test_patch_ledger_and_reverse_rollback(void)
{
    static const char source[] =
        "_V0 Restorable patches\n"
        "$A100 00001004 0000BF00\n"
        "$A200 00001008 12345678\n";
    parsed_fixture fixture;
    vc_psv_plan_node nodes[2];
    vc_psv_plan plan;
    vc_psv_action actions[2];
    vc_psv_patch_ledger_entry ledger[2];
    vc_psv_evaluation_report evaluation;
    vc_psv_apply_report applied;
    vc_psv_rollback_report rolled_back;
    vc_psv_evaluation_callbacks evaluator;
    vc_psv_write_callbacks writer;
    fake_host host;

    if (!parse_fixture(source, &fixture)) {
        return;
    }
    CHECK(compile_fixture(&fixture, 0, NULL, nodes, 2, &plan) ==
          VC_PSV_COMPILE_OK);
    reset_host(&host);
    store_u32(&host, UINT32_C(0x1004), UINT32_C(0x00001234), 2);
    store_u32(&host, UINT32_C(0x1008), UINT32_C(0xaabbccdd), 4);
    evaluator = evaluation_callbacks(&host);
    CHECK(vc_psv_evaluate_plan(&plan, nodes, &evaluator,
                               actions, 2, &evaluation) ==
          VC_PSV_EVALUATE_OK);
    CHECK(actions[0].bytes[0] == UINT8_C(0x00));
    CHECK(actions[0].bytes[1] == UINT8_C(0xbf));
    CHECK(actions[0].original_bytes[0] == UINT8_C(0x34));
    CHECK(actions[0].original_bytes[1] == UINT8_C(0x12));
    CHECK(actions[1].bytes[0] == UINT8_C(0x78));
    CHECK(actions[1].bytes[3] == UINT8_C(0x12));

    writer = write_callbacks(&host);
    CHECK(vc_psv_apply_actions(actions, 2, &writer, ledger, 2,
                               &applied) == VC_PSV_EXECUTE_OK);
    CHECK(applied.active_patch_entries == 2);
    CHECK(host.memory[4] == UINT8_C(0x00));
    CHECK(host.memory[5] == UINT8_C(0xbf));
    CHECK(host.memory[8] == UINT8_C(0x78));
    CHECK(vc_psv_rollback_patches(ledger, 2, &writer,
                                  &rolled_back) == VC_PSV_EXECUTE_OK);
    CHECK(rolled_back.restored_entries == 2);
    CHECK(rolled_back.remaining_entries == 0);
    CHECK(host.write_log[2].address == UINT32_C(0x1008));
    CHECK(host.write_log[3].address == UINT32_C(0x1004));
    CHECK(host.memory[4] == UINT8_C(0x34));
    CHECK(host.memory[5] == UINT8_C(0x12));
    CHECK(host.memory[8] == UINT8_C(0xdd));
    CHECK(host.memory[11] == UINT8_C(0xaa));
    CHECK(!ledger[0].active && !ledger[1].active);
}

static void test_rollback_failure_remains_retryable(void)
{
    vc_psv_patch_ledger_entry ledger[2];
    vc_psv_rollback_report report;
    vc_psv_write_callbacks writer;
    fake_host host;

    memset(ledger, 0, sizeof(ledger));
    ledger[0].address = UINT32_C(0x1000);
    ledger[0].width = VC_PSV_WIDTH_U8;
    ledger[0].original_bytes[0] = UINT8_C(0x11);
    ledger[0].active = true;
    ledger[1].address = UINT32_C(0x1001);
    ledger[1].width = VC_PSV_WIDTH_U8;
    ledger[1].original_bytes[0] = UINT8_C(0x22);
    ledger[1].active = true;
    reset_host(&host);
    host.fail_write_call = 0;
    writer = write_callbacks(&host);
    CHECK(vc_psv_rollback_patches(ledger, 2, &writer, &report) ==
          VC_PSV_EXECUTE_CALLBACK_FAILED);
    CHECK(report.restored_entries == 0);
    CHECK(report.remaining_entries == 2);
    CHECK(ledger[0].active && ledger[1].active);

    host.fail_write_call = SIZE_MAX;
    host.write_calls = 0;
    CHECK(vc_psv_rollback_patches(ledger, 2, &writer, &report) ==
          VC_PSV_EXECUTE_OK);
    CHECK(report.restored_entries == 2);
    CHECK(!ledger[0].active && !ledger[1].active);
}

static void test_physical_gate_counts_and_nesting(void)
{
    static const char source[] =
        "_V0 Nested gates\n"
        "$C204 00000001 00000001\n"
        "$D002 00001000 00000001\n"
        "$4001 00001020 00000001\n"
        "$0002 00000001 00000001\n"
        "$0000 00001010 000000AA\n"
        "$0000 00001011 000000BB\n";
    parsed_fixture fixture;
    vc_psv_plan_node nodes[5];
    vc_psv_plan plan;
    vc_psv_action actions[4];
    vc_psv_evaluation_report evaluation;
    vc_psv_evaluation_callbacks evaluator;
    fake_host host;

    if (!parse_fixture(source, &fixture)) {
        return;
    }
    CHECK(compile_fixture(&fixture, 0, NULL, nodes, 5, &plan) ==
          VC_PSV_COMPILE_OK);
    CHECK(plan.physical_record_count == 6);
    CHECK(plan.node_count == 5);
    CHECK(nodes[2].kind == VC_PSV_PLAN_REPEAT);
    CHECK(nodes[2].physical_first == 2);
    CHECK(nodes[2].physical_count == 2);

    reset_host(&host);
    host.button_mask = 0;
    evaluator = evaluation_callbacks(&host);
    CHECK(vc_psv_evaluate_plan(&plan, nodes, &evaluator,
                               actions, 4, &evaluation) ==
          VC_PSV_EVALUATE_OK);
    CHECK(evaluation.action_count == 1);
    CHECK(actions[0].address == UINT32_C(0x1011));
    CHECK(evaluation.skipped_physical_records == 4);
    CHECK(host.read_calls == 0);
    CHECK(host.last_button_mode == 1);

    reset_host(&host);
    host.button_mask = 1;
    store_u32(&host, UINT32_C(0x1000), UINT32_C(0), 1);
    evaluator = evaluation_callbacks(&host);
    CHECK(vc_psv_evaluate_plan(&plan, nodes, &evaluator,
                               actions, 4, &evaluation) ==
          VC_PSV_EVALUATE_OK);
    CHECK(evaluation.action_count == 2);
    CHECK(actions[0].address == UINT32_C(0x1010));
    CHECK(actions[1].address == UINT32_C(0x1011));
    CHECK(evaluation.skipped_physical_records == 2);

    reset_host(&host);
    host.button_mask = 1;
    store_u32(&host, UINT32_C(0x1000), UINT32_C(1), 1);
    evaluator = evaluation_callbacks(&host);
    CHECK(vc_psv_evaluate_plan(&plan, nodes, &evaluator,
                               actions, 4, &evaluation) ==
          VC_PSV_EVALUATE_OK);
    CHECK(evaluation.action_count == 4);
    CHECK(actions[0].address == UINT32_C(0x1020));
    CHECK(actions[1].address == UINT32_C(0x1021));
    CHECK(actions[2].address == UINT32_C(0x1010));
    CHECK(actions[3].address == UINT32_C(0x1011));

    reset_host(&host);
    host.button_mask = 3;
    evaluator = evaluation_callbacks(&host);
    CHECK(vc_psv_evaluate_plan(&plan, nodes, &evaluator,
                               actions, 4, &evaluation) ==
          VC_PSV_EVALUATE_OK);
    CHECK(evaluation.action_count == 1);
    CHECK(actions[0].address == UINT32_C(0x1011));
}

static void test_gate_zero_and_repeat_split_validation(void)
{
    static const char zero_source[] =
        "_V0 Zero skip\n"
        "$C200 00000001 00000001\n"
        "$0000 00001010 000000AA\n";
    static const char split_source[] =
        "_V0 Split repeat\n"
        "$C201 00000001 00000001\n"
        "$4001 00001020 00000001\n"
        "$0002 00000001 00000001\n";
    parsed_fixture fixture;
    vc_psv_plan_node nodes[2];
    vc_psv_plan plan;
    vc_psv_action action;
    vc_psv_evaluation_report evaluation;
    vc_psv_evaluation_callbacks evaluator;
    fake_host host;

    if (!parse_fixture(zero_source, &fixture)) {
        return;
    }
    CHECK(compile_fixture(&fixture, 0, NULL, nodes, 2, &plan) ==
          VC_PSV_COMPILE_OK);
    reset_host(&host);
    host.button_mask = 0;
    evaluator = evaluation_callbacks(&host);
    CHECK(vc_psv_evaluate_plan(&plan, nodes, &evaluator,
                               &action, 1, &evaluation) ==
          VC_PSV_EVALUATE_OK);
    CHECK(evaluation.action_count == 1);
    CHECK(action.address == UINT32_C(0x1010));

    if (!parse_fixture(split_source, &fixture)) {
        return;
    }
    memset(nodes, 0xa5, sizeof(nodes));
    CHECK(compile_fixture(&fixture, 0, NULL, nodes, 2, &plan) ==
          VC_PSV_COMPILE_MALFORMED);
    CHECK(memcmp(nodes, &(vc_psv_plan_node[2]){{0}}, sizeof(nodes)) == 0);
}

static void test_all_condition_relations_and_widths(void)
{
    unsigned condition;

    for (condition = 0; condition <= 11; ++condition) {
        char source[160];
        parsed_fixture fixture;
        vc_psv_plan_node nodes[2];
        vc_psv_plan plan;
        vc_psv_action action;
        vc_psv_evaluation_report evaluation;
        vc_psv_evaluation_callbacks evaluator;
        fake_host host;
        const size_t width =
            condition % 3u == 0 ? 1u : condition % 3u == 1 ? 2u : 4u;
        uint32_t expected = UINT32_C(5);
        uint32_t true_actual;
        uint32_t false_actual;
        int written;

        switch (condition / 3u) {
        case 0:
            true_actual = UINT32_C(5);
            false_actual = UINT32_C(4);
            break;
        case 1:
            true_actual = UINT32_C(4);
            false_actual = UINT32_C(5);
            break;
        case 2:
            true_actual = UINT32_C(6);
            false_actual = UINT32_C(5);
            break;
        default:
            true_actual = UINT32_C(4);
            false_actual = UINT32_C(5);
            break;
        }
        if (condition == 8) {
            expected = UINT32_C(0x7fffffff);
            true_actual = UINT32_C(0xffffffff);
            false_actual = UINT32_C(0x7fffffff);
        } else if (condition == 11) {
            expected = UINT32_C(0x80000000);
            true_actual = UINT32_C(0x7fffffff);
            false_actual = UINT32_C(0x80000000);
        } else if (width == 1) {
            expected |= UINT32_C(0xabcdef00);
        } else if (width == 2) {
            expected |= UINT32_C(0xabcd0000);
        }

        written = snprintf(source, sizeof(source),
                           "_V0 Condition %X\n"
                           "$D%X01 00001000 %08X\n"
                           "$0000 00001010 000000AA\n",
                           condition, condition, expected);
        CHECK(written > 0 && (size_t)written < sizeof(source));
        if (written <= 0 || (size_t)written >= sizeof(source) ||
            !parse_fixture(source, &fixture)) {
            continue;
        }
        CHECK(compile_fixture(&fixture, 0, NULL, nodes, 2, &plan) ==
              VC_PSV_COMPILE_OK);
        CHECK((size_t)nodes[0].width == width);
        CHECK((unsigned)nodes[0].relation == condition / 3u);

        reset_host(&host);
        store_u32(&host, UINT32_C(0x1000), true_actual, width);
        evaluator = evaluation_callbacks(&host);
        CHECK(vc_psv_evaluate_plan(&plan, nodes, &evaluator,
                                   &action, 1, &evaluation) ==
              VC_PSV_EVALUATE_OK);
        CHECK(evaluation.action_count == 1);

        reset_host(&host);
        store_u32(&host, UINT32_C(0x1000), false_actual, width);
        evaluator = evaluation_callbacks(&host);
        CHECK(vc_psv_evaluate_plan(&plan, nodes, &evaluator,
                                   &action, 1, &evaluation) ==
              VC_PSV_EVALUATE_OK);
        CHECK(evaluation.action_count == 0);
    }
}

static void test_unresolved_condition_is_false(void)
{
    static const char source[] =
        "_V0 Unresolved condition\n"
        "$D001 90000000 00000001\n"
        "$0000 00001010 000000AA\n";
    parsed_fixture fixture;
    vc_psv_plan_node nodes[2];
    vc_psv_plan plan;
    vc_psv_action action;
    vc_psv_evaluation_report evaluation;
    vc_psv_evaluation_callbacks evaluator;
    fake_host host;

    if (!parse_fixture(source, &fixture)) {
        return;
    }
    CHECK(compile_fixture(&fixture, 0, NULL, nodes, 2, &plan) ==
          VC_PSV_COMPILE_OK);
    reset_host(&host);
    evaluator = evaluation_callbacks(&host);
    CHECK(vc_psv_evaluate_plan(&plan, nodes, &evaluator,
                               &action, 1, &evaluation) ==
          VC_PSV_EVALUATE_OK);
    CHECK(evaluation.action_count == 0);
    CHECK(host.read_calls == 1);
}

static void test_staged_writes_feed_later_moves_and_conditions(void)
{
    static const char source[] =
        "_V0 Sequential staging\n"
        "$0000 00001000 00000007\n"
        "$5000 00001001 00001000\n"
        "$D001 00001001 00000007\n"
        "$0000 00001002 000000AA\n";
    parsed_fixture fixture;
    vc_psv_plan_node nodes[4];
    vc_psv_plan plan;
    vc_psv_action actions[3];
    vc_psv_evaluation_report evaluation;
    vc_psv_evaluation_callbacks evaluator;
    fake_host host;

    if (!parse_fixture(source, &fixture)) {
        return;
    }
    CHECK(compile_fixture(&fixture, 0, NULL, nodes, 4, &plan) ==
          VC_PSV_COMPILE_OK);
    reset_host(&host);
    evaluator = evaluation_callbacks(&host);
    CHECK(vc_psv_evaluate_plan(&plan, nodes, &evaluator,
                               actions, 3, &evaluation) ==
          VC_PSV_EVALUATE_OK);
    CHECK(evaluation.action_count == 3);
    CHECK(actions[1].address == UINT32_C(0x1001));
    CHECK(actions[1].bytes[0] == UINT8_C(0x07));
    CHECK(actions[2].address == UINT32_C(0x1002));
    CHECK(host.read_calls == 0);
}

static void test_compatibility_is_explicit_and_diagnostic(void)
{
    static const char repeat_source[] =
        "_V0 TempAR repeat\n"
        "$4000 00001000 00000001\n"
        "$0002 00000001 00000001\n";
    static const char patch_source[] =
        "_V0 A0 compatibility\n"
        "$A000 00001000 000000AB # runtime-observed\n";
    parsed_fixture fixture;
    vc_psv_plan_node node;
    vc_psv_plan plan;
    vc_psv_compile_options options;

    if (!parse_fixture(repeat_source, &fixture)) {
        return;
    }
    CHECK(fixture.report.compatibility_operations == 1);
    CHECK(fixture.cheats[0].translation_state ==
          VC_PSV_TRANSLATION_REQUIRES_COMPATIBILITY);
    CHECK(compile_fixture(&fixture, 0, NULL, &node, 1, &plan) ==
          VC_PSV_COMPILE_NONCANONICAL);
    options.compatibility = VC_PSV_COMPILE_ALLOW_NONCANONICAL_REPEAT;
    options.action_limit = 8;
    CHECK(compile_fixture(&fixture, 0, &options, &node, 1, &plan) ==
          VC_PSV_COMPILE_OK);
    CHECK(plan.compatibility_diagnostics == 1);
    CHECK(node.kind == VC_PSV_PLAN_REPEAT);

    if (!parse_fixture(patch_source, &fixture)) {
        return;
    }
    CHECK((fixture.operations[0].flags &
           VC_PSV_OPERATION_FLAG_NONCANONICAL_PATCH_U8) != 0);
    CHECK((fixture.operations[0].flags &
           VC_PSV_OPERATION_FLAG_INLINE_COMMENT) != 0);
    CHECK(compile_fixture(&fixture, 0, NULL, &node, 1, &plan) ==
          VC_PSV_COMPILE_NONCANONICAL);
    options.compatibility = VC_PSV_COMPILE_ALLOW_PATCH_U8 |
                            VC_PSV_COMPILE_ALLOW_INLINE_COMMENTS;
    options.action_limit = 1;
    CHECK(compile_fixture(&fixture, 0, &options, &node, 1, &plan) ==
          VC_PSV_COMPILE_OK);
    CHECK(plan.compatibility_diagnostics == 1);
    CHECK(node.kind == VC_PSV_PLAN_PATCH);
    CHECK(node.width == VC_PSV_WIDTH_U8);
}

static void test_unknown_fallback_tokens_fail_closed(void)
{
    static const char source[] =
        "_V0 Unknown records\n"
        "$0001 00001000 00000001\n"
        "$01F1 00001000 00000001\n"
        "$B000 00001000 00000001\n"
        "$C001 00001000 00000001\n"
        "$C007 00001000 00000001\n"
        "$C101 00001000 00000001\n";
    parsed_fixture fixture;
    vc_psv_plan_node nodes[6];
    vc_psv_plan plan;
    size_t index;

    if (!parse_fixture(source, &fixture)) {
        return;
    }
    CHECK(fixture.report.unsupported_operations == 6);
    for (index = 0; index < 6; ++index) {
        CHECK(fixture.operations[index].kind == VC_PSV_OPERATION_OPAQUE);
    }
    memset(nodes, 0xa5, sizeof(nodes));
    CHECK(compile_fixture(&fixture, 0, NULL, nodes, 6, &plan) ==
          VC_PSV_COMPILE_UNSUPPORTED);
    CHECK(memcmp(nodes, &(vc_psv_plan_node[6]){{0}}, sizeof(nodes)) == 0);
}

static void test_malformed_sequences_and_lexical_widths(void)
{
    static const char missing_continuation[] =
        "_V0 Missing continuation\n"
        "$4201 00001000 00000001\n";
    static const char lexical_defects[] =
        "_V0 Long address\n"
        "$0200 000001000 00000001\n"
        "_V0 Short value\n"
        "$0200 00001000 0000001\n"
        "_V0 Shorter value\n"
        "$0200 00001000 000001\n";
    parsed_fixture fixture;
    vc_psv_plan_node node;
    vc_psv_plan plan;

    if (!parse_fixture(missing_continuation, &fixture)) {
        return;
    }
    CHECK(fixture.report.invalid_operation_sequences == 1);
    CHECK(fixture.cheats[0].translation_state ==
          VC_PSV_TRANSLATION_MALFORMED);
    CHECK(compile_fixture(&fixture, 0, NULL, &node, 1, &plan) ==
          VC_PSV_COMPILE_MALFORMED);

    if (!parse_fixture(lexical_defects, &fixture)) {
        return;
    }
    CHECK(fixture.report.malformed_lines == 3);
    CHECK(fixture.report.total_operations == 0);
    CHECK(fixture.cheats[0].translation_state ==
          VC_PSV_TRANSLATION_MALFORMED);
    CHECK(fixture.cheats[1].translation_state ==
          VC_PSV_TRANSLATION_MALFORMED);
    CHECK(fixture.cheats[2].translation_state ==
          VC_PSV_TRANSLATION_MALFORMED);
}

static void test_validation_failure_has_no_callbacks(void)
{
    static const char source[] =
        "_V0 One write\n"
        "$0000 00001000 00000001\n";
    parsed_fixture fixture;
    vc_psv_plan_node node;
    vc_psv_plan plan;
    vc_psv_action action;
    vc_psv_evaluation_report evaluation;
    vc_psv_apply_report applied;
    vc_psv_patch_ledger_entry ledger;
    vc_psv_evaluation_callbacks evaluator;
    vc_psv_write_callbacks writer;
    fake_host host;

    if (!parse_fixture(source, &fixture)) {
        return;
    }
    CHECK(compile_fixture(&fixture, 0, NULL, NULL, 0, &plan) ==
          VC_PSV_COMPILE_OUTPUT_TOO_SMALL);
    CHECK(plan.node_count == 1);
    CHECK(compile_fixture(&fixture, 0, NULL, &node, 1, &plan) ==
          VC_PSV_COMPILE_OK);
    reset_host(&host);
    evaluator = evaluation_callbacks(&host);
    CHECK(vc_psv_evaluate_plan(&plan, &node, &evaluator,
                               NULL, 0, &evaluation) ==
          VC_PSV_EVALUATE_OUTPUT_TOO_SMALL);
    CHECK(host.resolve_calls == 0 && host.read_calls == 0 &&
          host.button_calls == 0);

    node.physical_count = 2;
    CHECK(vc_psv_evaluate_plan(&plan, &node, &evaluator,
                               &action, 1, &evaluation) ==
          VC_PSV_EVALUATE_INVALID_PLAN);
    CHECK(host.resolve_calls == 0 && host.read_calls == 0 &&
          host.button_calls == 0);

    memset(&action, 0, sizeof(action));
    action.kind = VC_PSV_ACTION_WRITE;
    action.width = (vc_psv_width)3;
    writer = write_callbacks(&host);
    CHECK(vc_psv_apply_actions(&action, 1, &writer, NULL, 0,
                               &applied) ==
          VC_PSV_EXECUTE_INVALID_ACTION);
    CHECK(host.write_calls == 0);

    action.kind = VC_PSV_ACTION_PATCH;
    action.width = VC_PSV_WIDTH_U8;
    CHECK(vc_psv_apply_actions(&action, 1, &writer, &ledger, 0,
                               &applied) ==
          VC_PSV_EXECUTE_LEDGER_TOO_SMALL);
    CHECK(host.write_calls == 0);
}

static void test_repeat_formula_property(void)
{
    uint32_t state = UINT32_C(0x6d2b79f5);
    unsigned iteration;
    vc_psv_evaluation_callbacks callbacks;

    memset(&callbacks, 0, sizeof(callbacks));
    for (iteration = 0; iteration < 256; ++iteration) {
        vc_psv_plan_node node;
        vc_psv_plan plan;
        vc_psv_action actions[16];
        vc_psv_evaluation_report evaluation;
        uint32_t count;
        uint32_t index;

        state = state * UINT32_C(1664525) + UINT32_C(1013904223);
        count = state & UINT32_C(0x0f);
        memset(&node, 0, sizeof(node));
        node.kind = VC_PSV_PLAN_REPEAT;
        node.width = VC_PSV_WIDTH_U8;
        if ((state & UINT32_C(0x30)) == UINT32_C(0x10)) {
            node.width = VC_PSV_WIDTH_U16;
        } else if ((state & UINT32_C(0x30)) >= UINT32_C(0x20)) {
            node.width = VC_PSV_WIDTH_U32;
        }
        node.physical_first = 0;
        node.physical_count = 2;
        node.destination.mode = VC_PSV_ADDRESS_ABSOLUTE;
        node.destination.value = state;
        node.value = state ^ UINT32_C(0xa5a5a5a5);
        node.repeat_count = count;
        node.address_gap = state | UINT32_C(1);
        node.value_increment = (state >> 7u) | UINT32_C(1);
        memset(&plan, 0, sizeof(plan));
        plan.schema_version = VC_PSV_PLAN_SCHEMA_VERSION;
        plan.physical_record_count = 2;
        plan.node_count = 1;
        plan.maximum_actions = count;

        CHECK(vc_psv_evaluate_plan(&plan, &node, &callbacks,
                                   actions, 16, &evaluation) ==
              VC_PSV_EVALUATE_OK);
        CHECK(evaluation.action_count == count);
        for (index = 0; index < count; ++index) {
            const uint32_t expected_address =
                (uint32_t)(node.destination.value +
                           (uint32_t)((uint64_t)index *
                                      node.address_gap));
            const uint32_t expected_value =
                (uint32_t)(node.value +
                           (uint32_t)((uint64_t)index *
                                      node.value_increment));
            const uint32_t mask =
                node.width == VC_PSV_WIDTH_U8
                    ? UINT32_C(0xff)
                    : node.width == VC_PSV_WIDTH_U16
                          ? UINT32_C(0xffff)
                          : UINT32_MAX;
            uint32_t actual = actions[index].bytes[0];

            if (node.width >= VC_PSV_WIDTH_U16) {
                actual |= (uint32_t)actions[index].bytes[1] << 8u;
            }
            if (node.width == VC_PSV_WIDTH_U32) {
                actual |= (uint32_t)actions[index].bytes[2] << 16u;
                actual |= (uint32_t)actions[index].bytes[3] << 24u;
            }
            CHECK(actions[index].address == expected_address);
            CHECK(actual == (expected_value & mask));
        }
    }
}

int main(void)
{
    test_b2_write_and_move();
    test_all_direct_and_move_widths();
    test_b2_overwrite_and_descriptor_reset();
    test_b2_snapshots_every_scalar_address_surface();
    test_repeat_zero_wrap_truncate_and_limit();
    test_module_address_overflow_is_not_wrapped();
    test_patch_ledger_and_reverse_rollback();
    test_rollback_failure_remains_retryable();
    test_physical_gate_counts_and_nesting();
    test_gate_zero_and_repeat_split_validation();
    test_all_condition_relations_and_widths();
    test_unresolved_condition_is_false();
    test_staged_writes_feed_later_moves_and_conditions();
    test_compatibility_is_explicit_and_diagnostic();
    test_unknown_fallback_tokens_fail_closed();
    test_malformed_sequences_and_lexical_widths();
    test_validation_failure_has_no_callbacks();
    test_repeat_formula_property();

    if (failures != 0) {
        fprintf(stderr, "%d legacy plan test(s) failed\n", failures);
        return 1;
    }
    puts("VitaCheat legacy scalar plan tests passed");
    return 0;
}
