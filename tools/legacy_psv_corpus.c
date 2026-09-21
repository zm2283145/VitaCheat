#include "vitacheat/legacy_emit.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

#define PINNED_REVISION "bb8158a1c696914a8ea2299889d42ab9a57a3ab2"
#define PATH_BUFFER_BYTES ((size_t)4096)
#define TOKEN_COUNT ((size_t)65536)

#if defined(_WIN32)
#define PATH_SEPARATOR '\\'
#else
#define PATH_SEPARATOR '/'
#endif

typedef struct corpus_stats {
    size_t files;
    size_t records;
    size_t v0_headers;
    size_t v1_headers;
    size_t alternative_headers;
    size_t inline_comments;
    size_t trailing_whitespace_records;
    size_t width_defects;
    size_t non_crlf_lines;
    size_t unclassified_records;
    size_t utf8_files;
    size_t utf8_bom_files;
    size_t windows_1252_files;
    size_t iso_8859_1_files;
    size_t strict_decoded;
    size_t strict_noncanonical;
    size_t strict_unsupported;
    size_t strict_malformed;
    size_t strict_action_limit;
    size_t compatibility_decoded;
    size_t compatibility_unsupported;
    size_t compatibility_malformed;
    size_t compatibility_action_limit;
    bool saw_long_address;
    bool saw_short_value;
    bool saw_shorter_value;
    unsigned char tokens[TOKEN_COUNT];
    unsigned char top_level_tokens[TOKEN_COUNT];
    unsigned char continuation_tokens[TOKEN_COUNT];
} corpus_stats;

typedef struct file_list {
    char **names;
    size_t count;
    size_t capacity;
} file_list;

static bool ascii_space(char value)
{
    return value == ' ' || value == '\t';
}

static int hex_value(char value)
{
    if (value >= '0' && value <= '9') {
        return value - '0';
    }
    if (value >= 'a' && value <= 'f') {
        return value - 'a' + 10;
    }
    if (value >= 'A' && value <= 'F') {
        return value - 'A' + 10;
    }
    return -1;
}

static bool has_psv_suffix(const char *name)
{
    const size_t length = strlen(name);

    return length >= 4u &&
           name[length - 4u] == '.' &&
           (name[length - 3u] == 'p' || name[length - 3u] == 'P') &&
           (name[length - 2u] == 's' || name[length - 2u] == 'S') &&
           (name[length - 1u] == 'v' || name[length - 1u] == 'V');
}

static char *copy_string(const char *source)
{
    const size_t length = strlen(source);
    char *copy = (char *)malloc(length + 1u);

    if (copy != NULL) {
        memcpy(copy, source, length + 1u);
    }
    return copy;
}

static bool append_file(file_list *files, const char *name)
{
    char **resized;
    size_t new_capacity;
    char *copy;

    if (!has_psv_suffix(name)) {
        return true;
    }
    if (files->count == files->capacity) {
        new_capacity = files->capacity == 0 ? 64u : files->capacity * 2u;
        if (new_capacity < files->capacity ||
            new_capacity > SIZE_MAX / sizeof(*files->names)) {
            return false;
        }
        resized = (char **)realloc(
            files->names, new_capacity * sizeof(*files->names));
        if (resized == NULL) {
            return false;
        }
        files->names = resized;
        files->capacity = new_capacity;
    }
    copy = copy_string(name);
    if (copy == NULL) {
        return false;
    }
    files->names[files->count++] = copy;
    return true;
}

static void free_files(file_list *files)
{
    size_t index;

    for (index = 0; index < files->count; ++index) {
        free(files->names[index]);
    }
    free(files->names);
    memset(files, 0, sizeof(*files));
}

static int compare_names(const void *left, const void *right)
{
    const char *const *left_name = (const char *const *)left;
    const char *const *right_name = (const char *const *)right;

    return strcmp(*left_name, *right_name);
}

static bool join_path(char *output,
                      size_t capacity,
                      const char *directory,
                      const char *name)
{
    const size_t directory_length = strlen(directory);
    const bool has_separator =
        directory_length != 0 &&
        (directory[directory_length - 1u] == '/' ||
         directory[directory_length - 1u] == '\\');
    const int length =
        has_separator
            ? snprintf(output, capacity, "%s%s", directory, name)
            : snprintf(output, capacity, "%s%c%s", directory,
                       PATH_SEPARATOR, name);

    return length >= 0 && (size_t)length < capacity;
}

static bool directory_exists(const char *path)
{
#if defined(_WIN32)
    const DWORD attributes = GetFileAttributesA(path);

    return attributes != INVALID_FILE_ATTRIBUTES &&
           (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
#else
    struct stat status;

    return stat(path, &status) == 0 && S_ISDIR(status.st_mode);
#endif
}

static bool list_psv_files(const char *directory, file_list *files)
{
#if defined(_WIN32)
    char pattern[PATH_BUFFER_BYTES];
    WIN32_FIND_DATAA entry;
    HANDLE search;

    if (!join_path(pattern, sizeof(pattern), directory, "*.psv")) {
        return false;
    }
    search = FindFirstFileA(pattern, &entry);
    if (search == INVALID_HANDLE_VALUE) {
        return false;
    }
    do {
        if ((entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0 &&
            !append_file(files, entry.cFileName)) {
            FindClose(search);
            return false;
        }
    } while (FindNextFileA(search, &entry) != 0);
    FindClose(search);
#else
    DIR *handle = opendir(directory);
    struct dirent *entry;

    if (handle == NULL) {
        return false;
    }
    while ((entry = readdir(handle)) != NULL) {
        if (!append_file(files, entry->d_name)) {
            closedir(handle);
            return false;
        }
    }
    closedir(handle);
#endif
    qsort(files->names, files->count, sizeof(*files->names),
          compare_names);
    return true;
}

static bool read_file(const char *path, char **source, size_t *size)
{
    FILE *file;
    long length;
    char *buffer;

#if defined(_WIN32)
    if (fopen_s(&file, path, "rb") != 0) {
        file = NULL;
    }
#else
    file = fopen(path, "rb");
#endif
    if (file == NULL || fseek(file, 0, SEEK_END) != 0) {
        if (file != NULL) {
            fclose(file);
        }
        return false;
    }
    length = ftell(file);
    if (length < 0 || (unsigned long)length > VC_PSV_MAX_SOURCE_BYTES ||
        fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return false;
    }
    buffer = (char *)malloc(length == 0 ? 1u : (size_t)length);
    if (buffer == NULL) {
        fclose(file);
        return false;
    }
    if (length != 0 &&
        fread(buffer, 1, (size_t)length, file) != (size_t)length) {
        free(buffer);
        fclose(file);
        return false;
    }
    fclose(file);
    *source = buffer;
    *size = (size_t)length;
    return true;
}

static void trimmed_line(const char *source,
                         const vc_psv_line *line,
                         size_t *begin,
                         size_t *end)
{
    *begin = (size_t)line->content.offset;
    *end = *begin + (size_t)line->content.length;
    while (*begin < *end && ascii_space(source[*begin])) {
        ++*begin;
    }
    while (*end > *begin && ascii_space(source[*end - 1u])) {
        --*end;
    }
}

static bool line_token(const char *source,
                       const vc_psv_line *line,
                       uint16_t *token)
{
    size_t begin;
    size_t end;
    size_t index;
    uint16_t value = 0;

    trimmed_line(source, line, &begin, &end);
    if (end - begin < 5u || source[begin] != '$') {
        return false;
    }
    for (index = 1; index <= 4; ++index) {
        const int digit = hex_value(source[begin + index]);

        if (digit < 0) {
            return false;
        }
        value = (uint16_t)((value << 4u) | (uint16_t)digit);
    }
    if (begin + 5u < end && !ascii_space(source[begin + 5u])) {
        return false;
    }
    *token = value;
    return true;
}

static bool record_field_widths(const char *source,
                                const vc_psv_line *line,
                                size_t *code_width,
                                size_t *address_width,
                                size_t *value_width)
{
    size_t begin;
    size_t end;
    size_t token_begin;

    trimmed_line(source, line, &begin, &end);
    if (begin == end || source[begin++] != '$') {
        return false;
    }
    token_begin = begin;
    while (begin < end && !ascii_space(source[begin])) {
        ++begin;
    }
    *code_width = begin - token_begin;
    while (begin < end && ascii_space(source[begin])) {
        ++begin;
    }
    token_begin = begin;
    while (begin < end && !ascii_space(source[begin])) {
        ++begin;
    }
    *address_width = begin - token_begin;
    while (begin < end && ascii_space(source[begin])) {
        ++begin;
    }
    token_begin = begin;
    while (begin < end && !ascii_space(source[begin])) {
        ++begin;
    }
    *value_width = begin - token_begin;
    return *code_width != 0 && *address_width != 0 && *value_width != 0;
}

static bool token_is_structural(uint16_t token)
{
    if (token == UINT16_C(0x3000) ||
        token == UINT16_C(0x3100) ||
        token == UINT16_C(0x3200) ||
        token == UINT16_C(0x3300) ||
        token == UINT16_C(0x3302) ||
        token == UINT16_C(0x7000) ||
        token == UINT16_C(0x7100) ||
        token == UINT16_C(0x7200) ||
        token == UINT16_C(0x7402) ||
        token == UINT16_C(0x8000) ||
        token == UINT16_C(0x8100) ||
        token == UINT16_C(0x8200) ||
        token == UINT16_C(0x8800) ||
        token == UINT16_C(0x8900) ||
        token == UINT16_C(0x9000)) {
        return true;
    }
    if ((token & UINT16_C(0xff00)) == UINT16_C(0x7700)) {
        return true;
    }
    return (token & UINT16_C(0xf0f0)) == UINT16_C(0x8000) &&
           ((token >> 8u) & UINT16_C(0x000f)) >= UINT16_C(4) &&
           ((token >> 8u) & UINT16_C(0x000f)) <= UINT16_C(6) &&
           (token & UINT16_C(0x000f)) <= UINT16_C(8);
}

static bool line_has_prefix(const char *source,
                            const vc_psv_line *line,
                            const char *prefix)
{
    size_t begin;
    size_t end;
    const size_t prefix_length = strlen(prefix);

    trimmed_line(source, line, &begin, &end);
    return end - begin >= prefix_length &&
           memcmp(source + begin, prefix, prefix_length) == 0;
}

static bool line_is_header(const char *source,
                           const vc_psv_line *line,
                           char activation)
{
    size_t begin;
    size_t end;

    trimmed_line(source, line, &begin, &end);
    return end - begin >= 3u &&
           source[begin] == '_' &&
           source[begin + 1u] == 'V' &&
           source[begin + 2u] == activation &&
           (end - begin == 3u || ascii_space(source[begin + 3u]));
}

static void record_compile_status(vc_psv_compile_status status,
                                  bool compatibility,
                                  corpus_stats *stats)
{
    size_t *decoded =
        compatibility ? &stats->compatibility_decoded
                      : &stats->strict_decoded;

    if (status == VC_PSV_COMPILE_OK ||
        status == VC_PSV_COMPILE_OUTPUT_TOO_SMALL) {
        ++*decoded;
    } else if (compatibility) {
        if (status == VC_PSV_COMPILE_UNSUPPORTED) {
            ++stats->compatibility_unsupported;
        } else if (status == VC_PSV_COMPILE_MALFORMED) {
            ++stats->compatibility_malformed;
        } else if (status == VC_PSV_COMPILE_ACTION_LIMIT) {
            ++stats->compatibility_action_limit;
        }
    } else if (status == VC_PSV_COMPILE_NONCANONICAL) {
        ++stats->strict_noncanonical;
    } else if (status == VC_PSV_COMPILE_UNSUPPORTED) {
        ++stats->strict_unsupported;
    } else if (status == VC_PSV_COMPILE_MALFORMED) {
        ++stats->strict_malformed;
    } else if (status == VC_PSV_COMPILE_ACTION_LIMIT) {
        ++stats->strict_action_limit;
    }
}

static bool check_round_trips(const char *source,
                              size_t source_size,
                              const vc_psv_line *lines,
                              size_t line_count,
                              const vc_psv_cheat *cheats,
                              size_t cheat_count,
                              const vc_psv_operation *operations,
                              size_t operation_count)
{
    char *output;
    size_t required = 0;
    vc_psv_canonical_report canonical;
    vc_psv_emit_status status;

    output = (char *)malloc(source_size == 0 ? 1u : source_size);
    if (output == NULL) {
        return false;
    }
    status = vc_psv_emit_lossless(
        source, source_size, lines, line_count, output, source_size,
        &required);
    if (status != VC_PSV_EMIT_OK || required != source_size ||
        memcmp(source, output, source_size) != 0) {
        free(output);
        return false;
    }
    status = vc_psv_emit_canonical(
        source, source_size, lines, line_count, cheats, cheat_count,
        operations, operation_count, NULL, 0, &canonical);
    if (status != VC_PSV_EMIT_OUTPUT_TOO_SMALL &&
        status != VC_PSV_EMIT_OK) {
        free(output);
        return false;
    }
    if (canonical.required_size > source_size) {
        char *resized =
            (char *)realloc(output, canonical.required_size);

        if (resized == NULL) {
            free(output);
            return false;
        }
        output = resized;
    }
    status = vc_psv_emit_canonical(
        source, source_size, lines, line_count, cheats, cheat_count,
        operations, operation_count, output, canonical.required_size,
        &canonical);
    free(output);
    return status == VC_PSV_EMIT_OK &&
           canonical.written_size == canonical.required_size;
}

static bool analyze_source(const char *name,
                           const char *source,
                           size_t source_size,
                           corpus_stats *stats)
{
    vc_psv_parse_options parse_options;
    vc_psv_report sizing;
    vc_psv_report report;
    vc_psv_line *lines = NULL;
    vc_psv_cheat *cheats = NULL;
    vc_psv_operation *operations = NULL;
    vc_psv_status parse_status;
    vc_psv_compile_options compatibility_options;
    size_t index;
    bool ok = false;

    parse_options.legacy_fallback =
        VC_PSV_LEGACY_ENCODING_WINDOWS_1252;
    parse_status = vc_psv_parse_with_options(
        source, source_size, &parse_options, NULL, 0, NULL, 0, NULL, 0,
        &sizing);
    if (parse_status != VC_PSV_STATUS_OK &&
        parse_status != VC_PSV_STATUS_TRUNCATED) {
        fprintf(stderr, "%s: parse sizing failed (%d)\n",
                name, (int)parse_status);
        return false;
    }
    if (sizing.total_lines > SIZE_MAX / sizeof(*lines) ||
        sizing.total_cheats > SIZE_MAX / sizeof(*cheats) ||
        sizing.total_operations > SIZE_MAX / sizeof(*operations)) {
        fprintf(stderr, "%s: parsed counts overflow allocation\n", name);
        return false;
    }
    if (sizing.total_lines != 0) {
        lines = (vc_psv_line *)calloc(sizing.total_lines, sizeof(*lines));
    }
    if (sizing.total_cheats != 0) {
        cheats =
            (vc_psv_cheat *)calloc(sizing.total_cheats, sizeof(*cheats));
    }
    if (sizing.total_operations != 0) {
        operations = (vc_psv_operation *)calloc(
            sizing.total_operations, sizeof(*operations));
    }
    if ((sizing.total_lines != 0 && lines == NULL) ||
        (sizing.total_cheats != 0 && cheats == NULL) ||
        (sizing.total_operations != 0 && operations == NULL)) {
        fprintf(stderr, "%s: allocation failed\n", name);
        goto cleanup;
    }
    parse_status = vc_psv_parse_with_options(
        source, source_size, &parse_options, lines, sizing.total_lines,
        cheats, sizing.total_cheats, operations, sizing.total_operations,
        &report);
    if (parse_status != VC_PSV_STATUS_OK) {
        fprintf(stderr, "%s: full parse failed (%d)\n",
                name, (int)parse_status);
        goto cleanup;
    }
    if (!check_round_trips(source, source_size, lines, report.total_lines,
                           cheats, report.total_cheats, operations,
                           report.total_operations)) {
        fprintf(stderr, "%s: round-trip verification failed\n", name);
        goto cleanup;
    }
    ++stats->files;
    if (report.source_encoding == VC_PSV_SOURCE_UTF8) {
        ++stats->utf8_files;
    } else if (report.source_encoding == VC_PSV_SOURCE_UTF8_BOM) {
        ++stats->utf8_bom_files;
    } else if (report.source_encoding ==
               VC_PSV_SOURCE_WINDOWS_1252) {
        ++stats->windows_1252_files;
    } else if (report.source_encoding ==
               VC_PSV_SOURCE_ISO_8859_1) {
        ++stats->iso_8859_1_files;
    }
    stats->records += report.physical_records;
    stats->inline_comments += report.inline_comments;
    stats->trailing_whitespace_records +=
        report.trailing_whitespace_records;

    for (index = 0; index < report.total_lines; ++index) {
        const vc_psv_line *line = &lines[index];
        const size_t raw_end =
            (size_t)line->raw.offset + (size_t)line->raw.length;
        uint16_t token;

        if (raw_end >
                (size_t)line->content.offset +
                    (size_t)line->content.length &&
            (raw_end < 2u ||
             source[raw_end - 2u] != '\r' ||
             source[raw_end - 1u] != '\n')) {
            ++stats->non_crlf_lines;
        }
        if (line_is_header(source, line, '0')) {
            ++stats->v0_headers;
        } else if (line_is_header(source, line, '1')) {
            ++stats->v1_headers;
        } else if (line_has_prefix(
                       source, line, "_V0---Alternative")) {
            ++stats->alternative_headers;
        }
        if ((line->diagnostics &
             (VC_PSV_DIAGNOSTIC_LEXICAL_ERROR |
              VC_PSV_DIAGNOSTIC_NONCANONICAL_WIDTH)) ==
            (VC_PSV_DIAGNOSTIC_LEXICAL_ERROR |
             VC_PSV_DIAGNOSTIC_NONCANONICAL_WIDTH)) {
            size_t code_width = 0;
            size_t address_width = 0;
            size_t value_width = 0;

            ++stats->width_defects;
            if (strcmp(name, "PCSB00370.psv") == 0 &&
                index + 1u == 158u &&
                record_field_widths(
                    source, line, &code_width, &address_width,
                    &value_width) &&
                code_width == 4u && address_width == 9u &&
                value_width == 8u) {
                stats->saw_long_address = true;
            } else if (strcmp(name, "PCSB00497.psv") == 0 &&
                       index + 1u == 420u &&
                       record_field_widths(
                           source, line, &code_width, &address_width,
                           &value_width) &&
                       code_width == 4u && address_width == 8u &&
                       value_width == 7u) {
                stats->saw_short_value = true;
            } else if (strcmp(name, "PCSE00465-mp.psv") == 0 &&
                       index + 1u == 53u &&
                       record_field_widths(
                           source, line, &code_width, &address_width,
                           &value_width) &&
                       code_width == 4u && address_width == 8u &&
                       value_width == 6u) {
                stats->saw_shorter_value = true;
            } else {
                fprintf(stderr,
                        "%s:%zu: unexpected field-width defect\n",
                        name, index + 1u);
            }
        }
        if (!line_token(source, line, &token)) {
            if (line->record_role != VC_PSV_RECORD_NOT_APPLICABLE &&
                line->record_role != VC_PSV_RECORD_UNKNOWN) {
                ++stats->unclassified_records;
            }
            continue;
        }
        stats->tokens[token] = 1;
        if (line->record_role == VC_PSV_RECORD_TOP_LEVEL) {
            stats->top_level_tokens[token] = 1;
        } else if (line->record_role == VC_PSV_RECORD_CONTINUATION) {
            stats->continuation_tokens[token] = 1;
        } else if (line->record_role != VC_PSV_RECORD_UNKNOWN) {
            ++stats->unclassified_records;
        }
    }
    if (report.top_level_records + report.continuation_records +
            report.unknown_records !=
        report.physical_records) {
        stats->unclassified_records += report.physical_records;
    }

    compatibility_options.compatibility =
        VC_PSV_COMPILE_ALLOW_INLINE_COMMENTS |
        VC_PSV_COMPILE_ALLOW_NONCANONICAL_REPEAT |
        VC_PSV_COMPILE_ALLOW_PATCH_U8 |
        VC_PSV_COMPILE_ALLOW_POINTER_LEVELS_6_8 |
        VC_PSV_COMPILE_ALLOW_POINTER_TERMINAL_MARKERS |
        VC_PSV_COMPILE_ALLOW_POINTER_U32_GAP |
        VC_PSV_COMPILE_ALLOW_POINTER_MOV_MISMATCH |
        VC_PSV_COMPILE_ALLOW_NONCANONICAL_HEADER;
    compatibility_options.action_limit = VC_PSV_MAX_ACTION_LIMIT;
    for (index = 0; index < report.total_cheats; ++index) {
        vc_psv_plan plan;
        vc_psv_compile_options strict_options;

        strict_options.compatibility = VC_PSV_COMPILE_STRICT;
        strict_options.action_limit = VC_PSV_MAX_ACTION_LIMIT;
        record_compile_status(
            vc_psv_compile_cheat(
                &cheats[index], operations, report.total_operations,
                &strict_options, NULL, 0, &plan),
            false, stats);
        record_compile_status(
            vc_psv_compile_cheat(
                &cheats[index], operations, report.total_operations,
                &compatibility_options, NULL, 0, &plan),
            true, stats);
    }
    ok = true;

cleanup:
    free(operations);
    free(cheats);
    free(lines);
    return ok;
}

static bool analyze_file(const char *directory,
                         const char *name,
                         corpus_stats *stats)
{
    char path[PATH_BUFFER_BYTES];
    char *source;
    size_t source_size;
    bool result;

    if (!join_path(path, sizeof(path), directory, name)) {
        fprintf(stderr, "%s: path is too long\n", name);
        return false;
    }
    if (!read_file(path, &source, &source_size)) {
        fprintf(stderr, "%s: could not read file\n", path);
        return false;
    }
    result = analyze_source(name, source, source_size, stats);
    free(source);
    return result;
}

static bool expect_size(const char *label,
                        size_t actual,
                        size_t expected)
{
    if (actual == expected) {
        printf("ok %-30s %zu\n", label, actual);
        return true;
    }
    fprintf(stderr, "mismatch %-24s expected %zu, got %zu\n",
            label, expected, actual);
    return false;
}

static bool verify_pinned_invariants(const corpus_stats *stats)
{
    size_t distinct_tokens = 0;
    size_t continuation_only = 0;
    size_t candidate_tokens;
    size_t index;
    bool ok = true;

    for (index = 0; index < TOKEN_COUNT; ++index) {
        if (stats->tokens[index] != 0) {
            ++distinct_tokens;
            if (stats->continuation_tokens[index] != 0 &&
                stats->top_level_tokens[index] == 0 &&
                !token_is_structural((uint16_t)index)) {
                ++continuation_only;
            }
        }
    }
    candidate_tokens = distinct_tokens - continuation_only;
    ok &= expect_size("PSV files", stats->files, 676);
    ok &= expect_size("physical $ records", stats->records, 34839);
    ok &= expect_size("distinct first fields", distinct_tokens, 318);
    ok &= expect_size("ordinary _V0 headers", stats->v0_headers, 7867);
    ok &= expect_size("_V1 headers", stats->v1_headers, 121);
    ok &= expect_size("alternative headers",
                      stats->alternative_headers, 2);
    ok &= expect_size("inline comments", stats->inline_comments, 1343);
    ok &= expect_size("trailing whitespace records",
                      stats->trailing_whitespace_records, 18);
    ok &= expect_size("field-width defects", stats->width_defects, 3);
    ok &= expect_size("non-CRLF lines", stats->non_crlf_lines, 0);
    ok &= expect_size("continuation-only fields",
                      continuation_only, 171);
    ok &= expect_size("opcode/structural candidates",
                      candidate_tokens, 147);
    ok &= expect_size("unclassified records",
                      stats->unclassified_records, 0);
    if (!stats->saw_long_address || !stats->saw_short_value ||
        !stats->saw_shorter_value) {
        fprintf(stderr,
                "mismatch field-width defect locations: "
                "PCSB00370:158=%d PCSB00497:420=%d "
                "PCSE00465-mp:53=%d\n",
                stats->saw_long_address ? 1 : 0,
                stats->saw_short_value ? 1 : 0,
                stats->saw_shorter_value ? 1 : 0);
        ok = false;
    }
    printf("strict compile: decoded=%zu noncanonical=%zu unsupported=%zu "
           "malformed=%zu limit=%zu\n",
           stats->strict_decoded, stats->strict_noncanonical,
           stats->strict_unsupported, stats->strict_malformed,
           stats->strict_action_limit);
    printf("compat compile: decoded=%zu unsupported=%zu malformed=%zu "
           "limit=%zu\n",
           stats->compatibility_decoded,
           stats->compatibility_unsupported,
           stats->compatibility_malformed,
           stats->compatibility_action_limit);
    printf("source encoding: utf8=%zu utf8-bom=%zu windows-1252=%zu "
           "iso-8859-1=%zu\n",
           stats->utf8_files, stats->utf8_bom_files,
           stats->windows_1252_files, stats->iso_8859_1_files);
    return ok;
}

static int run_self_test(void)
{
    static const char source[] =
        "_V0 Synthetic\r\n"
        "$7001 00001000 00000000\r\n"
        "$7700 00000000 00000001\r\n"
        "$0001 00000004 00000001\r\n"
        "$0200 00002000 00000002\r\n";
    corpus_stats stats;
    size_t distinct = 0;
    size_t continuation_only = 0;
    size_t index;

    memset(&stats, 0, sizeof(stats));
    if (!analyze_source("synthetic.psv", source, sizeof(source) - 1u,
                        &stats)) {
        return 1;
    }
    for (index = 0; index < TOKEN_COUNT; ++index) {
        if (stats.tokens[index] != 0) {
            ++distinct;
            if (stats.continuation_tokens[index] != 0 &&
                stats.top_level_tokens[index] == 0 &&
                !token_is_structural((uint16_t)index)) {
                ++continuation_only;
            }
        }
    }
    if (stats.files != 1 || stats.records != 4 ||
        stats.v0_headers != 1 || stats.non_crlf_lines != 0 ||
        stats.unclassified_records != 0 || distinct != 4 ||
        continuation_only != 1) {
        fprintf(stderr, "legacy corpus self-test invariant failed\n");
        return 1;
    }
    puts("VitaCheat legacy corpus runner self-test passed");
    return 0;
}

int main(int argc, char **argv)
{
    char db_path[PATH_BUFFER_BYTES];
    file_list files;
    corpus_stats stats;
    size_t index;
    bool ok = true;

    if (argc == 2 && strcmp(argv[1], "--self-test") == 0) {
        return run_self_test();
    }
    if (argc != 2) {
        fprintf(stderr,
                "usage: %s <r0ah-vitacheat-checkout-or-db-path>\n"
                "expected revision: %s\n",
                argv[0], PINNED_REVISION);
        return 2;
    }
    if (!join_path(db_path, sizeof(db_path), argv[1], "db") ||
        !directory_exists(db_path)) {
        const int length = snprintf(db_path, sizeof(db_path), "%s",
                                    argv[1]);

        if (length < 0 || (size_t)length >= sizeof(db_path) ||
            !directory_exists(db_path)) {
            fprintf(stderr, "%s: no corpus directory found\n", argv[1]);
            return 2;
        }
    }
    memset(&files, 0, sizeof(files));
    memset(&stats, 0, sizeof(stats));
    if (!list_psv_files(db_path, &files)) {
        fprintf(stderr, "%s: could not enumerate corpus\n", db_path);
        free_files(&files);
        return 2;
    }
    for (index = 0; index < files.count; ++index) {
        if (!analyze_file(db_path, files.names[index], &stats)) {
            ok = false;
        }
    }
    if (ok) {
        ok = verify_pinned_invariants(&stats);
    }
    free_files(&files);
    if (!ok) {
        return 1;
    }
    printf("Pinned corpus invariants for %s verified\n",
           PINNED_REVISION);
    return 0;
}
