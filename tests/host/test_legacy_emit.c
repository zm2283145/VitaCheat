#include "vitacheat/legacy_emit.h"

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

typedef struct parsed_fixture {
    vc_psv_line lines[32];
    vc_psv_cheat cheats[8];
    vc_psv_operation operations[32];
    vc_psv_report report;
} parsed_fixture;

static int parse_source(const char *source, parsed_fixture *fixture)
{
    const vc_psv_status status = vc_psv_parse(
        source, strlen(source), fixture->lines, 32, fixture->cheats, 8,
        fixture->operations, 32, &fixture->report);

    CHECK(status == VC_PSV_STATUS_OK);
    return status == VC_PSV_STATUS_OK;
}

static void test_checked_lossless_round_trip(void)
{
    static const char source[] =
        "\xef\xbb\xbf# metadata\r\n"
        "\r\n"
        "_V0 Mixed\r\n"
        "$0200 00001000 00000001 # note\r\n"
        "$0200 000001000 00000002\r\n";
    parsed_fixture fixture;
    char output[sizeof(source)];
    size_t required = 0;

    if (!parse_source(source, &fixture)) {
        return;
    }
    CHECK(fixture.report.source_encoding == VC_PSV_SOURCE_UTF8_BOM);
    CHECK(fixture.report.physical_records == 2);
    CHECK(fixture.report.inline_comments == 1);
    CHECK(fixture.report.lexical_errors == 1);
    CHECK((fixture.lines[4].diagnostics &
           VC_PSV_DIAGNOSTIC_NONCANONICAL_WIDTH) != 0);
    CHECK(vc_psv_emit_lossless(
              source, sizeof(source) - 1u, fixture.lines,
              fixture.report.total_lines, NULL, 0, &required) ==
          VC_PSV_EMIT_OUTPUT_TOO_SMALL);
    CHECK(required == sizeof(source) - 1u);
    CHECK(vc_psv_emit_lossless(
              source, sizeof(source) - 1u, fixture.lines,
              fixture.report.total_lines, output, sizeof(output),
              &required) == VC_PSV_EMIT_OK);
    CHECK(memcmp(source, output, sizeof(source) - 1u) == 0);

    fixture.lines[1].raw.offset++;
    CHECK(vc_psv_emit_lossless(
              source, sizeof(source) - 1u, fixture.lines,
              fixture.report.total_lines, output, sizeof(output),
              &required) == VC_PSV_EMIT_INVALID_SPANS);
}

static void test_explicit_legacy_encoding_fallback(void)
{
    static const char windows_source[] = "_V0 Caf\xe9\r\n";
    static const char undefined_windows_byte[] = "_V0 Bad\x81\r\n";
    vc_psv_parse_options options;
    vc_psv_line line;
    vc_psv_cheat cheat;
    vc_psv_report report;

    CHECK(vc_psv_parse(
              windows_source, sizeof(windows_source) - 1u, &line, 1,
              &cheat, 1, NULL, 0, &report) ==
          VC_PSV_STATUS_INVALID_ENCODING);
    options.legacy_fallback = VC_PSV_LEGACY_ENCODING_WINDOWS_1252;
    CHECK(vc_psv_parse_with_options(
              windows_source, sizeof(windows_source) - 1u, &options,
              &line, 1, &cheat, 1, NULL, 0, &report) ==
          VC_PSV_STATUS_OK);
    CHECK(report.source_encoding == VC_PSV_SOURCE_WINDOWS_1252);
    CHECK(vc_psv_parse_with_options(
              undefined_windows_byte,
              sizeof(undefined_windows_byte) - 1u, &options, &line, 1,
              &cheat, 1, NULL, 0, &report) ==
          VC_PSV_STATUS_INVALID_ENCODING);
    options.legacy_fallback = VC_PSV_LEGACY_ENCODING_ISO_8859_1;
    CHECK(vc_psv_parse_with_options(
              undefined_windows_byte,
              sizeof(undefined_windows_byte) - 1u, &options, &line, 1,
              &cheat, 1, NULL, 0, &report) == VC_PSV_STATUS_OK);
    CHECK(report.source_encoding == VC_PSV_SOURCE_ISO_8859_1);
    options.legacy_fallback = (vc_psv_legacy_encoding)99;
    CHECK(vc_psv_parse_with_options(
              windows_source, sizeof(windows_source) - 1u, &options,
              &line, 1, &cheat, 1, NULL, 0, &report) ==
          VC_PSV_STATUS_INVALID_ARGUMENT);
}

static void test_contextual_classification_survives_bad_child(void)
{
    static const char source[] =
        "_V0 Context\n"
        "$7002 00001000 00000000\n"
        "$7000 00000000 00000000\n"
        "$7700 00000000 00000001\n"
        "$0001 00000000 0000000\n"
        "$0200 00002000 00000002\n";
    parsed_fixture fixture;

    if (!parse_source(source, &fixture)) {
        return;
    }
    CHECK(fixture.report.physical_records == 5);
    CHECK(fixture.report.top_level_records == 2);
    CHECK(fixture.report.continuation_records == 3);
    CHECK(fixture.report.unknown_records == 0);
    CHECK(fixture.report.invalid_operation_sequences == 1);
    CHECK(fixture.operations[0].kind == VC_PSV_OPERATION_POINTER_REPEAT);
    CHECK(fixture.operations[1].kind ==
          VC_PSV_OPERATION_POINTER_POSITIONAL);
    CHECK(fixture.operations[2].kind ==
          VC_PSV_OPERATION_POINTER_POSITIONAL);
    CHECK(fixture.operations[3].kind == VC_PSV_OPERATION_WRITE_U32);
    CHECK(fixture.operations[3].record_role == VC_PSV_RECORD_TOP_LEVEL);
    CHECK((fixture.operations[0].diagnostics &
           VC_PSV_DIAGNOSTIC_LEXICAL_ERROR) != 0);
    CHECK(fixture.cheats[0].translation_state ==
          VC_PSV_TRANSLATION_MALFORMED);
}

static void test_observed_tokens_remain_contextual_children(void)
{
    static const char source[] =
        "_V0 Repeat count\r\n"
        "$4001 00001000 00000001\r\n"
        "$0100 00000004 00000001\r\n"
        "_V0 Pointer markers\r\n"
        "$3001 00002000 00000000\r\n"
        "$9000 00000000 00000002\r\n"
        "$3001 00003000 00000000\r\n"
        "$3302 00000000 00000003\r\n"
        "$7001 00004000 00000000\r\n"
        "$7402 00000000 00000004\r\n"
        "$0001 00000004 00000001\r\n";
    parsed_fixture fixture;
    const size_t child_indexes[] = {1, 3, 5, 7, 8};
    size_t index;

    if (!parse_source(source, &fixture)) {
        return;
    }
    CHECK(fixture.report.total_operations == 9);
    for (index = 0;
         index < sizeof(child_indexes) / sizeof(child_indexes[0]);
         ++index) {
        const vc_psv_operation *operation =
            &fixture.operations[child_indexes[index]];

        CHECK(operation->record_role == VC_PSV_RECORD_CONTINUATION);
        CHECK(operation->kind == VC_PSV_OPERATION_REPEAT_CONTINUATION ||
              operation->kind == VC_PSV_OPERATION_POINTER_POSITIONAL);
    }
    CHECK(fixture.operations[1].legacy_code == UINT16_C(0x0100));
    CHECK(fixture.operations[3].legacy_code == UINT16_C(0x9000));
    CHECK(fixture.operations[5].legacy_code == UINT16_C(0x3302));
    CHECK(fixture.operations[7].legacy_code == UINT16_C(0x7402));
    CHECK(fixture.operations[8].legacy_code == UINT16_C(0x0001));
}

static void test_noncanonical_header_is_explicit_compatibility(void)
{
    static const char source[] =
        "_V0---Alternative Codes [B]---\r\n"
        "$0200 00001000 00000001\r\n";
    parsed_fixture fixture;
    vc_psv_plan_node node;
    vc_psv_plan plan;
    vc_psv_compile_options options;

    if (!parse_source(source, &fixture)) {
        return;
    }
    CHECK(fixture.report.noncanonical_headers == 1);
    CHECK(fixture.report.total_cheats == 1);
    CHECK((fixture.cheats[0].diagnostics &
           VC_PSV_DIAGNOSTIC_NONCANONICAL_HEADER) != 0);
    CHECK(vc_psv_compile_cheat(
              &fixture.cheats[0], fixture.operations,
              fixture.report.total_operations, NULL, &node, 1, &plan) ==
          VC_PSV_COMPILE_NONCANONICAL);
    options.compatibility =
        VC_PSV_COMPILE_ALLOW_NONCANONICAL_HEADER;
    options.action_limit = 1;
    CHECK(vc_psv_compile_cheat(
              &fixture.cheats[0], fixture.operations,
              fixture.report.total_operations, &options, &node, 1,
              &plan) == VC_PSV_COMPILE_OK);
    CHECK((plan.diagnostics &
           VC_PSV_PLAN_DIAGNOSTIC_NONCANONICAL_HEADER) != 0);
}

static void test_truncated_and_mismatched_spans_stay_lossless(void)
{
    static const char truncated[] =
        "_V0 Truncated pointer\r\n"
        "$3002 00001000 00000000\r\n"
        "$3000 00000000 00000004\r\n";
    static const char mismatched[] =
        "_V0 Mismatched marker\r\n"
        "$3001 00001000 00000000\r\n"
        "$1234 00000000 00000001\r\n";
    parsed_fixture fixture;
    vc_psv_plan_node node;
    vc_psv_plan plan;
    vc_psv_canonical_report report;
    char output[sizeof(mismatched)];

    if (!parse_source(truncated, &fixture)) {
        return;
    }
    CHECK(fixture.report.truncated_spans == 1);
    CHECK((fixture.operations[0].diagnostics &
           VC_PSV_DIAGNOSTIC_TRUNCATED_SPAN) != 0);
    CHECK(fixture.cheats[0].translation_state ==
          VC_PSV_TRANSLATION_MALFORMED);

    if (!parse_source(mismatched, &fixture)) {
        return;
    }
    CHECK(fixture.operations[1].record_role ==
          VC_PSV_RECORD_CONTINUATION);
    CHECK(fixture.operations[1].kind ==
          VC_PSV_OPERATION_POINTER_POSITIONAL);
    CHECK(vc_psv_compile_cheat(
              &fixture.cheats[0], fixture.operations,
              fixture.report.total_operations, NULL, &node, 1, &plan) ==
          VC_PSV_COMPILE_MALFORMED);
    CHECK(vc_psv_emit_canonical(
              mismatched, sizeof(mismatched) - 1u, fixture.lines,
              fixture.report.total_lines, fixture.cheats,
              fixture.report.total_cheats, fixture.operations,
              fixture.report.total_operations, output, sizeof(output),
              &report) == VC_PSV_EMIT_OK);
    CHECK(report.canonicalized_records == 0);
    CHECK(report.preserved_records == 2);
    CHECK(memcmp(output, mismatched, sizeof(mismatched) - 1u) == 0);
}

static void test_canonical_output_is_conservative(void)
{
    static const char source[] =
        "_V0 Canonicalizable\r\n"
        "$0200 0000abcd 000000ef\r\n"
        "_V0 Noncanonical repeat\r\n"
        "$4000 00001000 00000001\r\n"
        "$0001 00000004 00000001\r\n"
        "_V0 Unknown\r\n"
        "$C001 00000000 00000000\r\n";
    static const char expected[] =
        "_V0 Canonicalizable\r\n"
        "$0200 0000ABCD 000000EF\r\n"
        "_V0 Noncanonical repeat\r\n"
        "$4000 00001000 00000001\r\n"
        "$0001 00000004 00000001\r\n"
        "_V0 Unknown\r\n"
        "$C001 00000000 00000000\r\n";
    parsed_fixture fixture;
    vc_psv_canonical_report report;
    char output[sizeof(source)];
    char too_small[sizeof(source)];

    if (!parse_source(source, &fixture)) {
        return;
    }
    CHECK((fixture.operations[0].flags &
           VC_PSV_OPERATION_FLAG_LOWERCASE_HEX) != 0);
    memset(too_small, 0xa5, sizeof(too_small));
    CHECK(vc_psv_emit_canonical(
              source, sizeof(source) - 1u, fixture.lines,
              fixture.report.total_lines, fixture.cheats,
              fixture.report.total_cheats, fixture.operations,
              fixture.report.total_operations, too_small, 1,
              &report) == VC_PSV_EMIT_OUTPUT_TOO_SMALL);
    CHECK((unsigned char)too_small[0] == 0xa5u);
    CHECK(vc_psv_emit_canonical(
              source, sizeof(source) - 1u, fixture.lines,
              fixture.report.total_lines, fixture.cheats,
              fixture.report.total_cheats, fixture.operations,
              fixture.report.total_operations, output, sizeof(output),
              &report) == VC_PSV_EMIT_OK);
    CHECK(report.required_size == sizeof(expected) - 1u);
    CHECK(report.written_size == sizeof(expected) - 1u);
    CHECK(report.canonicalized_records == 1);
    CHECK(report.preserved_records == 3);
    CHECK(report.eligible_cheats == 1);
    CHECK(report.blocked_cheats == 2);
    CHECK(memcmp(output, expected, sizeof(expected) - 1u) == 0);
}

int main(void)
{
    test_checked_lossless_round_trip();
    test_explicit_legacy_encoding_fallback();
    test_contextual_classification_survives_bad_child();
    test_observed_tokens_remain_contextual_children();
    test_noncanonical_header_is_explicit_compatibility();
    test_truncated_and_mismatched_spans_stay_lossless();
    test_canonical_output_is_conservative();

    if (failures != 0) {
        fprintf(stderr, "%d legacy emit test(s) failed\n", failures);
        return 1;
    }
    puts("VitaCheat legacy emit tests passed");
    return 0;
}
