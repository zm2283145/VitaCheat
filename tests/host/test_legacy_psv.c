#include "vitacheat/legacy_psv.h"

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

static int span_equals(const char *source, vc_psv_span span, const char *expected)
{
    const size_t expected_length = strlen(expected);
    return span.length == expected_length &&
           memcmp(source + span.offset, expected, expected_length) == 0;
}

static void test_mixed_legacy_file_is_lossless_and_fail_closed(void)
{
    static const char source[] =
        "\xef\xbb\xbf# ID: PCSA00147\r\n"
        "\r\n"
        "_V0 Infinite Money\r\n"
        "$0200 81000000 3B9AC9FF\r\n"
        "_V1 Byte and word writes\n"
        "$0000 81000004 00000064\n"
        "$0100 81000006 00001234\r"
        "$6200 81000008 0000008D\r\n"
        "$broken code\r\n"
        "future-extension payload";
    vc_psv_line lines[16];
    vc_psv_cheat cheats[4];
    vc_psv_operation operations[8];
    vc_psv_report report;
    size_t next_offset = 0;
    size_t index;

    CHECK(vc_psv_parse(source, sizeof(source) - 1u,
                       lines, 16, cheats, 4, operations, 8, &report) ==
          VC_PSV_STATUS_OK);
    CHECK(report.schema_version == VC_PSV_IMPORT_SCHEMA_VERSION);
    CHECK(report.total_lines == 10);
    CHECK(report.total_cheats == 2);
    CHECK(report.total_operations == 4);
    CHECK(report.unsupported_operations == 1);
    CHECK(report.non_direct_cheats == 1);
    CHECK(report.malformed_lines == 1);
    CHECK(report.legacy_limit_violations == 0);

    for (index = 0; index < report.stored_lines; ++index) {
        CHECK(lines[index].raw.offset == next_offset);
        next_offset += lines[index].raw.length;
    }
    CHECK(next_offset == sizeof(source) - 1u);
    CHECK(lines[0].kind == VC_PSV_LINE_COMMENT);
    CHECK(lines[1].kind == VC_PSV_LINE_BLANK);
    CHECK(lines[8].kind == VC_PSV_LINE_MALFORMED);
    CHECK(lines[9].kind == VC_PSV_LINE_OPAQUE);

    CHECK(cheats[0].activation == VC_PSV_ACTIVATION_MANUAL);
    CHECK(cheats[1].activation == VC_PSV_ACTIVATION_AUTOSTART);
    CHECK(cheats[0].translation_state == VC_PSV_TRANSLATION_DIRECT_ONLY);
    CHECK(cheats[1].translation_state == VC_PSV_TRANSLATION_MALFORMED);
    CHECK(span_equals(source, cheats[0].description, "Infinite Money"));
    CHECK(span_equals(source, cheats[1].description, "Byte and word writes"));
    CHECK(cheats[0].operation_count == 1);
    CHECK(cheats[1].operation_count == 3);

    CHECK(operations[0].kind == VC_PSV_OPERATION_WRITE_U32);
    CHECK(operations[0].address_mode == VC_PSV_ADDRESS_ABSOLUTE);
    CHECK(operations[0].legacy_code == UINT16_C(0x0200));
    CHECK(operations[0].address == UINT32_C(0x81000000));
    CHECK(operations[0].value == UINT32_C(0x3b9ac9ff));
    CHECK(operations[1].kind == VC_PSV_OPERATION_WRITE_U8);
    CHECK(operations[2].kind == VC_PSV_OPERATION_WRITE_U16);
    CHECK(operations[3].kind == VC_PSV_OPERATION_OPAQUE);
    CHECK(operations[3].legacy_code == UINT16_C(0x6200));
}

static void test_truncation_counts_without_guessing(void)
{
    static const char source[] =
        "$0200 81000000 00000001\n"
        "_V0 Test\n"
        "$0200 81000000 00000002\n"
        "$DEAD 81000004 00000003\n";
    vc_psv_report report;

    CHECK(vc_psv_parse(source, sizeof(source) - 1u,
                       NULL, 0, NULL, 0, NULL, 0, &report) ==
          VC_PSV_STATUS_TRUNCATED);
    CHECK(report.total_lines == 4);
    CHECK(report.total_cheats == 1);
    CHECK(report.total_operations == 2);
    CHECK(report.unsupported_operations == 1);
    CHECK(report.non_direct_cheats == 1);
    CHECK(report.malformed_lines == 1);
    CHECK(report.stored_lines == 0);
    CHECK(report.stored_cheats == 0);
    CHECK(report.stored_operations == 0);
}

static void test_modifier_taints_complete_cheat(void)
{
    /* Reduced from public Ratchet & Clank database patterns. */
    static const char source[] =
        "_V0 Untouchable\n"
        "$B200 00000001 00000000\n"
        "$0000 002D1760 00000003\n"
        "_V0 Moon Jump [hold X]\n"
        "$C201 00000000 00004000\n"
        "$0100 818D668A 00003DC1\n"
        "_V0 Walk Faster\n"
        "$0200 818D6734 3E0FACE0\n";
    vc_psv_line lines[8];
    vc_psv_cheat cheats[3];
    vc_psv_operation operations[5];
    vc_psv_report report;

    CHECK(vc_psv_parse(source, sizeof(source) - 1u, lines, 8, cheats, 3,
                       operations, 5, &report) == VC_PSV_STATUS_OK);
    CHECK(report.non_direct_cheats == 1);
    CHECK(report.unsupported_operations == 0);
    CHECK(report.invalid_operation_sequences == 0);
    CHECK(cheats[0].translation_state == VC_PSV_TRANSLATION_MODULE_RELATIVE);
    CHECK(cheats[1].translation_state == VC_PSV_TRANSLATION_DIRECT_ONLY);
    CHECK(cheats[2].translation_state == VC_PSV_TRANSLATION_DIRECT_ONLY);
    CHECK(operations[0].kind == VC_PSV_OPERATION_SELECT_MODULE_BASE);
    CHECK(operations[0].address_mode == VC_PSV_ADDRESS_NOT_APPLICABLE);
    CHECK(operations[1].kind == VC_PSV_OPERATION_WRITE_U8);
    CHECK(operations[1].address_mode ==
          VC_PSV_ADDRESS_SELECTED_MODULE_RELATIVE);
    CHECK(operations[2].kind == VC_PSV_OPERATION_BUTTON_GATE);
    CHECK(operations[3].kind == VC_PSV_OPERATION_WRITE_U16);
    CHECK(operations[3].address_mode == VC_PSV_ADDRESS_ABSOLUTE);
    CHECK(operations[4].kind == VC_PSV_OPERATION_WRITE_U32);
    CHECK(operations[4].address_mode == VC_PSV_ADDRESS_ABSOLUTE);
}

static void test_pcsa00133_module_relative_bolts(void)
{
    static const char source[] =
        "# ID: PCSA00133\n"
        "# Region: US\n"
        "# Version: 1.00\n"
        "_V0 max.Schrauben\n"
        "$B200 00000001 00000000\n"
        "$0100 002D1560 0000270F\n";
    vc_psv_line lines[6];
    vc_psv_cheat cheat;
    vc_psv_operation operations[2];
    vc_psv_report report;

    CHECK(vc_psv_parse(source, sizeof(source) - 1u, lines, 6, &cheat, 1,
                       operations, 2, &report) == VC_PSV_STATUS_OK);
    CHECK(report.schema_version == UINT32_C(5));
    CHECK(report.total_cheats == 1);
    CHECK(report.total_operations == 2);
    CHECK(report.unsupported_operations == 0);
    CHECK(report.non_direct_cheats == 1);
    CHECK(report.invalid_operation_sequences == 0);
    CHECK(cheat.translation_state == VC_PSV_TRANSLATION_MODULE_RELATIVE);
    CHECK(operations[0].kind == VC_PSV_OPERATION_SELECT_MODULE_BASE);
    CHECK(operations[0].legacy_code == UINT16_C(0xb200));
    CHECK(operations[0].address_mode == VC_PSV_ADDRESS_NOT_APPLICABLE);
    CHECK(operations[0].address == UINT32_C(1));
    CHECK(operations[0].value == UINT32_C(0));
    CHECK(operations[1].kind == VC_PSV_OPERATION_WRITE_U16);
    CHECK(operations[1].address_mode ==
          VC_PSV_ADDRESS_SELECTED_MODULE_RELATIVE);
    CHECK(operations[1].module_serial == 0);
    CHECK(operations[1].segment_index == 1);
    CHECK(operations[1].address == UINT32_C(0x002d1560));
    CHECK(operations[1].value == UINT32_C(0x0000270f));
}

static void test_module_base_overwrite_reset_and_validation(void)
{
    static const char source[] =
        "_V0 Selector overwrite\n"
        "$0000 00000010 00000001\n"
        "$B200 00000001 00000000\n"
        "$0100 00000020 00000002\n"
        "$B201 00000000 00000000\n"
        "$5200 00000030 00000040\n"
        "_V0 Selector reset\n"
        "$0200 00001000 00000003\n"
        "_V0 Invalid segment\n"
        "$B20E 00000002 00000000\n"
        "_V0 Invalid third operand\n"
        "$B229 00000001 00000001\n"
        "_V0 State only\n"
        "$B229 00000001 00000000\n";
    vc_psv_line lines[14];
    vc_psv_cheat cheats[5];
    vc_psv_operation operations[9];
    vc_psv_report report;

    CHECK(vc_psv_parse(source, sizeof(source) - 1u, lines, 14, cheats, 5,
                       operations, 9, &report) == VC_PSV_STATUS_OK);
    CHECK(report.total_cheats == 5);
    CHECK(report.total_operations == 9);
    CHECK(report.unsupported_operations == 0);
    CHECK(report.non_direct_cheats == 4);
    CHECK(report.invalid_operation_sequences == 2);
    CHECK(cheats[0].translation_state == VC_PSV_TRANSLATION_MODULE_RELATIVE);
    CHECK(cheats[1].translation_state == VC_PSV_TRANSLATION_DIRECT_ONLY);
    CHECK(cheats[2].translation_state == VC_PSV_TRANSLATION_MALFORMED);
    CHECK(cheats[3].translation_state == VC_PSV_TRANSLATION_MALFORMED);
    CHECK(cheats[4].translation_state == VC_PSV_TRANSLATION_MODULE_RELATIVE);
    CHECK(operations[0].address_mode == VC_PSV_ADDRESS_ABSOLUTE);
    CHECK(operations[1].kind == VC_PSV_OPERATION_SELECT_MODULE_BASE);
    CHECK(operations[2].address_mode ==
          VC_PSV_ADDRESS_SELECTED_MODULE_RELATIVE);
    CHECK(operations[2].module_serial == 0);
    CHECK(operations[2].segment_index == 1);
    CHECK(operations[3].kind == VC_PSV_OPERATION_SELECT_MODULE_BASE);
    CHECK(operations[4].kind == VC_PSV_OPERATION_MOVE);
    CHECK(operations[4].module_serial == 1);
    CHECK(operations[4].segment_index == 0);
    CHECK((operations[4].diagnostics &
           VC_PSV_DIAGNOSTIC_RUNTIME_DEPENDENT_ADDRESS) != 0);
    CHECK(operations[5].address_mode == VC_PSV_ADDRESS_ABSOLUTE);
    CHECK((operations[6].diagnostics &
           VC_PSV_DIAGNOSTIC_INVALID_MODULE_SELECTOR) != 0);
    CHECK((operations[7].diagnostics &
           VC_PSV_DIAGNOSTIC_INVALID_MODULE_SELECTOR) != 0);
    CHECK((operations[8].diagnostics &
           VC_PSV_DIAGNOSTIC_INVALID_MODULE_SELECTOR) == 0);
    CHECK(operations[8].legacy_code == UINT16_C(0xb229));
}

static void test_truncation_discards_non_closed_prefixes(void)
{
    static const char source[] =
        "_V0 Relative write\n"
        "$B200 00000001 00000000\n"
        "$0000 002D1760 00000003\n"
        "_V0 Absolute write\n"
        "$0200 818D6734 3E0FACE0\n";
    vc_psv_line lines[5];
    vc_psv_cheat cheats[2];
    vc_psv_operation operations[3];
    vc_psv_line zero_lines[5];
    vc_psv_cheat zero_cheats[2];
    vc_psv_operation zero_operations[3];
    vc_psv_report report;

    memset(zero_lines, 0, sizeof(zero_lines));
    memset(zero_cheats, 0, sizeof(zero_cheats));
    memset(zero_operations, 0, sizeof(zero_operations));
    memset(lines, 0xa5, sizeof(lines));
    memset(cheats, 0xa5, sizeof(cheats));
    memset(operations, 0xa5, sizeof(operations));
    CHECK(vc_psv_parse(source, sizeof(source) - 1u, lines, 5, cheats, 1,
                       operations, 3, &report) == VC_PSV_STATUS_TRUNCATED);
    CHECK(report.total_lines == 5 && report.total_cheats == 2 &&
          report.total_operations == 3);
    CHECK(report.stored_lines == 0 && report.stored_cheats == 0 &&
          report.stored_operations == 0);
    CHECK(memcmp(lines, zero_lines, sizeof(lines)) == 0);
    CHECK(memcmp(&cheats[0], &zero_cheats[0], sizeof(cheats[0])) == 0);
    CHECK(memcmp(operations, zero_operations, sizeof(operations)) == 0);

    memset(lines, 0xa5, sizeof(lines));
    memset(cheats, 0xa5, sizeof(cheats));
    memset(operations, 0xa5, sizeof(operations));
    CHECK(vc_psv_parse(source, sizeof(source) - 1u, lines, 5, cheats, 2,
                       operations, 1, &report) == VC_PSV_STATUS_TRUNCATED);
    CHECK(report.stored_lines == 0 && report.stored_cheats == 0 &&
          report.stored_operations == 0);
    CHECK(memcmp(lines, zero_lines, sizeof(lines)) == 0);
    CHECK(memcmp(cheats, zero_cheats, sizeof(cheats)) == 0);
    CHECK(memcmp(&operations[0], &zero_operations[0], sizeof(operations[0])) == 0);
}

static void test_invalid_header_taints_previous_entry(void)
{
    static const char source[] =
        "_V0 Direct\n"
        "$0200 81000000 00000001\n"
        "_V2 Invalid header\n";
    vc_psv_line lines[3];
    vc_psv_cheat cheats[1];
    vc_psv_operation operation;
    vc_psv_report report;

    CHECK(vc_psv_parse(source, sizeof(source) - 1u, lines, 3, cheats, 1,
                       &operation, 1, &report) == VC_PSV_STATUS_OK);
    CHECK(report.malformed_lines == 1);
    CHECK(report.non_direct_cheats == 1);
    CHECK(lines[2].kind == VC_PSV_LINE_MALFORMED);
    CHECK(cheats[0].translation_state == VC_PSV_TRANSLATION_MALFORMED);
}

static void test_invalid_and_bounded_inputs(void)
{
    static const char byte = 'x';
    vc_psv_line line;
    vc_psv_report report;
    union {
        char source[128];
        vc_psv_line lines[2];
    } source_alias;
    union {
        vc_psv_line lines[4];
        vc_psv_report report;
    } output_alias;

    CHECK(vc_psv_parse(NULL, 0, NULL, 0, NULL, 0, NULL, 0, &report) ==
          VC_PSV_STATUS_OK);
    CHECK(report.total_lines == 0);
    CHECK(vc_psv_parse(NULL, 1, NULL, 0, NULL, 0, NULL, 0, &report) ==
          VC_PSV_STATUS_INVALID_ARGUMENT);
    CHECK(vc_psv_parse(&byte, (size_t)VC_PSV_MAX_SOURCE_BYTES + 1u,
                       NULL, 0, NULL, 0, NULL, 0, &report) ==
          VC_PSV_STATUS_SOURCE_TOO_LARGE);
    CHECK(vc_psv_parse(&byte, 1, NULL, 1, NULL, 0, NULL, 0, &report) ==
          VC_PSV_STATUS_INVALID_ARGUMENT);
    CHECK(vc_psv_parse(&byte, 1, NULL, 0, NULL, 0, NULL, 0, NULL) ==
          VC_PSV_STATUS_INVALID_ARGUMENT);

    memcpy(source_alias.source, "_V0 Alias\n", sizeof("_V0 Alias\n") - 1u);
    CHECK(vc_psv_parse(source_alias.source, sizeof("_V0 Alias\n") - 1u,
                       source_alias.lines, 1, NULL, 0, NULL, 0, &report) ==
          VC_PSV_STATUS_INVALID_ARGUMENT);
    CHECK(vc_psv_parse(&byte, 1, output_alias.lines, 4, NULL, 0, NULL, 0,
                       &output_alias.report) == VC_PSV_STATUS_INVALID_ARGUMENT);
    CHECK(vc_psv_parse(NULL, 0, &line,
                       SIZE_MAX / sizeof(line) + 1u,
                       NULL, 0, NULL, 0, &report) ==
          VC_PSV_STATUS_INVALID_ARGUMENT);
}

static void test_legacy_code_limit_without_output_buffers(void)
{
    char source[8192];
    size_t used = 0;
    size_t index;
    vc_psv_report report;

    memcpy(source + used, "_V0 Oversized\n", sizeof("_V0 Oversized\n") - 1u);
    used += sizeof("_V0 Oversized\n") - 1u;
    for (index = 0; index < 200; ++index) {
        static const char code[] = "$0200 81000000 00000001\n";
        memcpy(source + used, code, sizeof(code) - 1u);
        used += sizeof(code) - 1u;
    }

    CHECK(vc_psv_parse(source, used, NULL, 0, NULL, 0, NULL, 0, &report) ==
          VC_PSV_STATUS_TRUNCATED);
    CHECK(report.total_operations == 200);
    CHECK(report.legacy_limit_violations == 0);

    memcpy(source + used, "$0200 81000000 00000001\n",
           sizeof("$0200 81000000 00000001\n") - 1u);
    used += sizeof("$0200 81000000 00000001\n") - 1u;
    CHECK(vc_psv_parse(source, used, NULL, 0, NULL, 0, NULL, 0, &report) ==
          VC_PSV_STATUS_TRUNCATED);
    CHECK(report.total_operations == 201);
    CHECK(report.legacy_limit_violations == 1);
}

static void test_legacy_descriptor_limit_boundary(void)
{
    char source[1024];
    vc_psv_line lines[51];
    vc_psv_cheat cheats[51];
    vc_psv_report report;
    size_t used = 0;
    size_t index;

    for (index = 0; index < 50; ++index) {
        static const char header[] = "_V0 Test\n";

        memcpy(source + used, header, sizeof(header) - 1u);
        used += sizeof(header) - 1u;
    }
    CHECK(vc_psv_parse(source, used, lines, 51, cheats, 51,
                       NULL, 0, &report) == VC_PSV_STATUS_OK);
    CHECK(report.total_cheats == 50);
    CHECK(report.legacy_limit_violations == 0);

    memcpy(source + used, "_V0 Extra\n", sizeof("_V0 Extra\n") - 1u);
    used += sizeof("_V0 Extra\n") - 1u;
    CHECK(vc_psv_parse(source, used, lines, 51, cheats, 51,
                       NULL, 0, &report) == VC_PSV_STATUS_OK);
    CHECK(report.total_cheats == 51);
    CHECK(report.legacy_limit_violations == 1);
    CHECK((cheats[50].diagnostics &
           VC_PSV_DIAGNOSTIC_AUTHORING_LIMIT) != 0);
}

int main(void)
{
    test_mixed_legacy_file_is_lossless_and_fail_closed();
    test_truncation_counts_without_guessing();
    test_invalid_and_bounded_inputs();
    test_legacy_code_limit_without_output_buffers();
    test_legacy_descriptor_limit_boundary();
    test_modifier_taints_complete_cheat();
    test_pcsa00133_module_relative_bolts();
    test_module_base_overwrite_reset_and_validation();
    test_truncation_discards_non_closed_prefixes();
    test_invalid_header_taints_previous_entry();

    if (failures != 0) {
        fprintf(stderr, "%d legacy PSV test(s) failed\n", failures);
        return 1;
    }
    puts("VitaCheat legacy PSV tests passed");
    return 0;
}
