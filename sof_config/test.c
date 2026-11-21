/**
 * @file test.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief Tests for sof_config.c.
 * @details Usage: test. Runs every test, and exits with 0 if they all pass
 * or 1 if any fails. The Makefile's check target builds and runs it.
 * @version 0.0.1
 * @date 2026-11-03
 * @copyright Copyright (c) 2026
 */

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sof_config.h"
#include "test.h"

/// @brief The file of test data, which test_parse_file() describes.
#define DATA_FILE "../test-data/config-data.ini"

/// @brief The most assignments a test records.
#define MAX_ASSIGNMENTS 1100

/// @brief The longest section, key or value a test records, with its NUL.
#define MAX_TEXT 1100

/// @brief One assignment, as the handler received it.
typedef struct assignment_t {
    int has_section;
    char section[MAX_TEXT];
    char key[MAX_TEXT];
    char value[MAX_TEXT];
} assignment_t;

/// @brief What the recording handler has received.
typedef struct record_t {
    size_t count;
    /// @brief Stop when count reaches this, if it isn't 0.
    size_t stop_at;
    /// @brief Returned to stop.
    int stop_value;
    /// @brief The config being parsed, for the nested call test.
    sof_config_t *config;
    /// @brief What nested calls into config returned.
    sof_config_error_t nested_parse;
    sof_config_error_t nested_clear;
    sof_config_error_t nested_free;
    assignment_t assignments[MAX_ASSIGNMENTS];
} record_t;

/// @brief Large enough that tests share one, rather than one on each stack.
static record_t record;

static int record_assignment(const char *section, const char *key, const char *value, void *ctx) {
    record_t *r = ctx;
    if (r->count < MAX_ASSIGNMENTS) {
        assignment_t *a = &r->assignments[r->count];
        a->has_section = section != NULL;
        snprintf(a->section, MAX_TEXT, "%s", section != NULL ? section : "");
        snprintf(a->key, MAX_TEXT, "%s", key);
        snprintf(a->value, MAX_TEXT, "%s", value);
    }
    r->count++;
    if (r->config != NULL) {
        r->nested_parse = sof_config_parse_string(r->config, "a = 1", 5);
        r->nested_clear = sof_config_clear(r->config);
        r->nested_free = sof_config_free(r->config);
    }
    if (r->stop_at != 0 && r->count == r->stop_at) {
        return r->stop_value;
    }
    return 0;
}

/// @brief Initializes config to record into the shared record, emptied.
static sof_config_error_t init_recording(sof_config_t *config) {
    memset(&record, 0, sizeof(record));
    return sof_config_init(config, record_assignment, &record, NULL);
}

/// @brief Parses a NUL-terminated string.
static sof_config_error_t parse_text(sof_config_t *config, const char *text) {
    return sof_config_parse_string(config, text, strlen(text));
}

/// @brief Returns 1 if section is the NULL or NUL-terminated name expected.
static int same_section(int has_section, const char *section, const char *expected) {
    if (expected == NULL) {
        return !has_section;
    }
    return has_section && strcmp(section, expected) == 0;
}

/// @brief Returns 1 if the handler's assignment i is section.key = value,
/// with a NULL section for none.
static int assigned(size_t i, const char *section, const char *key, const char *value) {
    const assignment_t *a = &record.assignments[i];
    return same_section(a->has_section, a->section, section) && strcmp(a->key, key) == 0 && strcmp(a->value, value) == 0;
}

/// @brief Returns 1 if entry is section.key = value.
static int entry_is(const sof_config_entry_t *entry, const char *section, const char *key, const char *value) {
    return entry != NULL && same_section(entry->section != NULL, entry->section, section) && strcmp(entry->key, key) == 0 && strcmp(entry->value, value) == 0;
}

static void test_init_free(void) {
    sof_config_t config;
    int ctx;
    CHECK(sof_config_init(&config, record_assignment, &ctx, NULL) == SOF_CONFIG_OK);
    CHECK(config.handler == record_assignment);
    CHECK(config.ctx == &ctx);
    CHECK(config.state != NULL);
    CHECK(config.count == 0);
    CHECK(sof_config_free(&config) == SOF_CONFIG_OK);
    CHECK(config.state == NULL);
    CHECK(config.handler == NULL);
    CHECK(sof_config_free(&config) == SOF_CONFIG_OK);
    // A freed config can be initialized again.
    CHECK(sof_config_init(&config, NULL, NULL, NULL) == SOF_CONFIG_OK);
    CHECK(parse_text(&config, "a = 1") == SOF_CONFIG_OK);
    CHECK(config.count == 1);
    CHECK(sof_config_free(&config) == SOF_CONFIG_OK);
}

/// @brief Every assignment in DATA_FILE, in order, but for the long value in
/// the last section, which test_parse_file() checks itself.
static const char *const data_assignments[][3] = {
    {NULL, "key1", "somevalue"},
    {NULL, "key2", "a quoted value"},
    {"section 1", "key1", "value1"},
    {"section 1", "key2", "a value with spaces"},
    {"section 1", "key3", "1234"},
    {"section 1", "key4", "3.141"},
    {"section 1", "key5", "Raw values run to the end of the line, and are trimmed: they can hold \"quotes\" and = signs."},
    {"section 1", "key6", "semi;colon and hash#tag stay, because no space comes before them"},
    {"section 1", "key7", ""},
    {"section 1", "key8", ""},
    {"section 1", "key9", "a = b = c"},
    {"section 2", "key1", "this is a quoted value"},
    {"section 2", "key2", "-3.141592"},
    {"section 2", "key3", "this is testing \t escape characters\n"},
    {"section 2", "key4", "every escape: \a\b\f\n\r\t\v \\ \" ' and in hex: A~"},
    {"section 2", "key5", "code points: \xc3\xa9 \xe6\x97\xa5\xe6\x9c\xac \xf0\x9f\x98\x80"},
    {"section 2", "key6", ""},
    {"section 2", "key7", "   leading and trailing spaces   "},
    {"section 2", "key8", "a\ttab, kept as it is"},
    {"section 2", "key9", "can you have [special characters] ; # = in a quoted value? ;;;"},
    {"section 2", "key10", "#fff"},
    {"section_3-with.punctuation", "tabbed", "tabs"},
    {"section_3-with.punctuation", "under_score", "1"},
    {"section_3-with.punctuation", "dash-ed", "2"},
    {"section_3-with.punctuation", "dotted.key", "3"},
    {"section_3-with.punctuation", "-leading-dash", "4"},
    {"section_3-with.punctuation", "_leading_underscore", "5"},
    {"section_3-with.punctuation", "123numeric", "6"},
    {"section_3-with.punctuation", "CamelCase", "7"},
    {"section_3-with.punctuation", "a_key_well_over_thirty_two_characters_long_so_its_buffer_grows", "grown"},
    {"types", "port", "8080"},
    {"types", "negative", "-42"},
    {"types", "mask", "0xff"},
    {"types", "ratio", "0.75"},
    {"types", "big", "1e300"},
    {"types", "enabled", "yes"},
    {"types", "disabled", "Off"},
    {"types", "not_a_number", "12abc"},
    {"section 5", "punctuation", "!#$%&'()*+,-./:<=>?@[\\]^_`{|}~"},
    {"section 5", "quotes_inside", "a\"b\"c"},
    {"section 5", "backslashes", "C:\\path\\to\\file"},
    {"section 5", "utf8", "caf\xc3\xa9-\xc3\xb1-\xe6\x97\xa5\xe6\x9c\xac"},
    {"section 5", "quoted_utf8", "na\xc3\xafve \xe2\x80\x93 \xe2\x80\x9cquotes\xe2\x80\x9d"},
    {"section 5", "url", "http://example.com/a?b=c&d=e#f"},
    {"padded name", "inside", "yes"},
    {"section 1", "key1", "replaced"},
    {"section 1", "key10", "back_in_section_1"},
    {"server.http", "host", "example.com"},
};

/// @brief The index in data_assignments of the assignment that replaces an
/// earlier one, and of the one it replaces.
#define DATA_REPLACING 45
#define DATA_REPLACED 2

/// @brief The long value in DATA_FILE is this many copies of
/// "0123456789abcdef".
#define LONG_COPIES 40

/// @brief Writes the long value in DATA_FILE to value, which holds
/// LONG_COPIES * 16 + 1 bytes.
static void long_value(char *value) {
    for (size_t i = 0; i < LONG_COPIES; i++) {
        memcpy(value + i * 16, "0123456789abcdef", 16);
    }
    value[LONG_COPIES * 16] = 0;
}

static void test_parse_file(void) {
    size_t count = sizeof(data_assignments) / sizeof(data_assignments[0]);
    char value[LONG_COPIES * 16 + 1];
    long_value(value);
    sof_config_t config;
    CHECK(init_recording(&config) == SOF_CONFIG_OK);
    CHECK(sof_config_parse(&config, DATA_FILE) == SOF_CONFIG_OK);
    CHECK(record.count == count + 2);
    for (size_t i = 0; i < count; i++) {
        CHECK(assigned(i, data_assignments[i][0], data_assignments[i][1], data_assignments[i][2]));
    }
    CHECK(assigned(count, "last", "long", value));
    CHECK(assigned(count + 1, "last", "no_newline", "at the end of the file"));
    CHECK(config.line == 92);
    CHECK(config.column == 38);
    CHECK(sof_config_free(&config) == SOF_CONFIG_OK);
}

static void test_file_entries(void) {
    size_t count = sizeof(data_assignments) / sizeof(data_assignments[0]);
    char value[LONG_COPIES * 16 + 1];
    long_value(value);
    sof_config_t config;
    CHECK(sof_config_init(&config, NULL, NULL, NULL) == SOF_CONFIG_OK);
    CHECK(sof_config_parse(&config, DATA_FILE) == SOF_CONFIG_OK);
    // One assignment replaced an earlier one's value.
    CHECK(config.count == count + 1);
    size_t position = 0;
    for (size_t i = 0; i < count; i++) {
        if (i == DATA_REPLACING) {
            continue;
        }
        const char *expected = i == DATA_REPLACED ? data_assignments[DATA_REPLACING][2] : data_assignments[i][2];
        const sof_config_entry_t *entry = sof_config_next(&config, &position);
        CHECK(entry_is(entry, data_assignments[i][0], data_assignments[i][1], expected));
        CHECK(sof_config_find(&config, data_assignments[i][0], data_assignments[i][1]) == entry);
    }
    CHECK(entry_is(sof_config_next(&config, &position), "last", "long", value));
    CHECK(entry_is(sof_config_next(&config, &position), "last", "no_newline", "at the end of the file"));
    CHECK(sof_config_next(&config, &position) == NULL);
    // The replaced entry has the last value's line.
    CHECK(sof_config_find(&config, "section 1", "key1")->line == 82);
    CHECK(sof_config_find(&config, NULL, "key1")->line == 9);
    CHECK(strcmp(sof_config_get(&config, "server.http", "host"), "example.com") == 0);
    CHECK(sof_config_free(&config) == SOF_CONFIG_OK);
}

static void test_sections(void) {
    sof_config_t config;
    CHECK(init_recording(&config) == SOF_CONFIG_OK);
    CHECK(parse_text(&config, "a = 1\n[ one ]\nb = 2\n[two words]\nc = 3\n[dotted.name-with_punct]\nd = 4\n[\tone\t]\ne = 5\n") == SOF_CONFIG_OK);
    CHECK(record.count == 5);
    CHECK(assigned(0, NULL, "a", "1"));
    CHECK(assigned(1, "one", "b", "2"));
    CHECK(assigned(2, "two words", "c", "3"));
    CHECK(assigned(3, "dotted.name-with_punct", "d", "4"));
    CHECK(assigned(4, "one", "e", "5"));
    // Each parse starts outside any section, at line 1.
    CHECK(parse_text(&config, "f = 6\n") == SOF_CONFIG_OK);
    CHECK(assigned(5, NULL, "f", "6"));
    CHECK(config.line == 2);
    CHECK(config.column == 1);
    CHECK(sof_config_free(&config) == SOF_CONFIG_OK);
}

static void test_raw_values(void) {
    sof_config_t config;
    CHECK(init_recording(&config) == SOF_CONFIG_OK);
    const char *text =
        "plain=value\n"
        "  spaced   =   value with  inner  spaces   ; a comment\n"
        "hash = value # comment\n"
        "semi = a;b#c\n"
        "empty =\n"
        "empty_comment = ;comment\n"
        "equals = a=b\n"
        "quote_inside = say \"hi\"\n"
        "crlf = yes\r\n"
        "last = no newline";
    CHECK(parse_text(&config, text) == SOF_CONFIG_OK);
    CHECK(record.count == 10);
    CHECK(assigned(0, NULL, "plain", "value"));
    CHECK(assigned(1, NULL, "spaced", "value with  inner  spaces"));
    CHECK(assigned(2, NULL, "hash", "value"));
    CHECK(assigned(3, NULL, "semi", "a;b#c"));
    CHECK(assigned(4, NULL, "empty", ""));
    CHECK(assigned(5, NULL, "empty_comment", ""));
    CHECK(assigned(6, NULL, "equals", "a=b"));
    CHECK(assigned(7, NULL, "quote_inside", "say \"hi\""));
    CHECK(assigned(8, NULL, "crlf", "yes"));
    CHECK(assigned(9, NULL, "last", "no newline"));
    CHECK(config.line == 10);
    CHECK(sof_config_free(&config) == SOF_CONFIG_OK);
}

static void test_quoted_values(void) {
    sof_config_t config;
    CHECK(init_recording(&config) == SOF_CONFIG_OK);
    const char *text =
        "e = \"\\a\\b\\f\\n\\r\\t\\v \\\\ \\\" \\' \\x41\\x7e\\xff\"\n"
        "u = \"\\u00e9\\u65e5\\U0001F600\\u0041\"\n"
        "c = \"a ; b # c\"   ; a comment\n"
        "h = \"a\"# a comment needs no space after a quote\n"
        "s = \"  kept  \"\n"
        "empty = \"\"\n";
    CHECK(parse_text(&config, text) == SOF_CONFIG_OK);
    CHECK(record.count == 6);
    CHECK(assigned(0, NULL, "e", "\a\b\f\n\r\t\v \\ \" ' A~\xff"));
    CHECK(assigned(1, NULL, "u", "\xc3\xa9\xe6\x97\xa5\xf0\x9f\x98\x80" "A"));
    CHECK(assigned(2, NULL, "c", "a ; b # c"));
    CHECK(assigned(3, NULL, "h", "a"));
    CHECK(assigned(4, NULL, "s", "  kept  "));
    CHECK(assigned(5, NULL, "empty", ""));
    CHECK(sof_config_free(&config) == SOF_CONFIG_OK);
}

/// @brief A text whose parse fails, the error, the line and column it stops
/// at, and the number of assignments passed to the handler first.
typedef struct bad_text_t {
    const char *text;
    size_t length;
    sof_config_error_t rc;
    size_t line;
    size_t column;
    size_t count;
} bad_text_t;

/// @brief A bad_text_t for a NUL-terminated string literal.
#define BAD(text, rc, line, column, count) {text, sizeof(text) - 1, rc, line, column, count}

static void test_errors(void) {
    static const bad_text_t bad[] = {
        BAD("key", SOF_CONFIG_PREMATURE_EOF, 1, 4, 0),
        BAD("key\n= 1", SOF_CONFIG_UNEXPECTED_EOL, 1, 4, 0),
        BAD("key value", SOF_CONFIG_UNEXPECTED_CHARACTER, 1, 5, 0),
        BAD("ke;y = 1", SOF_CONFIG_UNEXPECTED_CHARACTER, 1, 3, 0),
        BAD("k*y = 1", SOF_CONFIG_UNEXPECTED_CHARACTER, 1, 2, 0),
        BAD("caf\xc3\xa9 = 1", SOF_CONFIG_UNEXPECTED_CHARACTER, 1, 4, 0),
        BAD("= 1", SOF_CONFIG_UNEXPECTED_CHARACTER, 1, 1, 0),
        BAD("\"a\" = 1", SOF_CONFIG_UNEXPECTED_CHARACTER, 1, 1, 0),
        BAD("key = \"open", SOF_CONFIG_PREMATURE_EOF, 1, 12, 0),
        BAD("key = \"open\nmore\"", SOF_CONFIG_UNEXPECTED_EOL, 1, 12, 0),
        BAD("key = \"closed\" extra", SOF_CONFIG_UNEXPECTED_CHARACTER, 1, 16, 0),
        BAD("a = 1\n[section", SOF_CONFIG_PREMATURE_EOF, 2, 9, 1),
        BAD("[section\n]", SOF_CONFIG_UNEXPECTED_EOL, 1, 9, 0),
        BAD("[sec;tion]", SOF_CONFIG_UNEXPECTED_CHARACTER, 1, 5, 0),
        BAD("[sec*tion]", SOF_CONFIG_UNEXPECTED_CHARACTER, 1, 5, 0),
        BAD("[]", SOF_CONFIG_UNEXPECTED_CHARACTER, 1, 2, 0),
        BAD("[   ]", SOF_CONFIG_UNEXPECTED_CHARACTER, 1, 5, 0),
        BAD("[s] a = 1", SOF_CONFIG_UNEXPECTED_CHARACTER, 1, 5, 0),
        BAD("a = 1\nb = 2\n\n*", SOF_CONFIG_UNEXPECTED_CHARACTER, 4, 1, 2),
        BAD("k\0y = 1", SOF_CONFIG_UNEXPECTED_CHARACTER, 1, 2, 0),
        BAD("key = a\0b", SOF_CONFIG_UNEXPECTED_CHARACTER, 1, 8, 0),
        BAD("key = \"a\0b\"", SOF_CONFIG_UNEXPECTED_CHARACTER, 1, 9, 0),
        BAD("\xef\xbb" "a = 1", SOF_CONFIG_UNEXPECTED_CHARACTER, 1, 3, 0),
        BAD("\xef" "a = 1", SOF_CONFIG_UNEXPECTED_CHARACTER, 1, 2, 0),
        // A bad escape is reported at its backslash.
        BAD("k = \"\\q\"", SOF_CONFIG_BAD_ESCAPE, 1, 6, 0),
        BAD("k = \"\\A\"", SOF_CONFIG_BAD_ESCAPE, 1, 6, 0),
        BAD("k = \"\\x4\"", SOF_CONFIG_BAD_ESCAPE, 1, 6, 0),
        BAD("k = \"\\xg0\"", SOF_CONFIG_BAD_ESCAPE, 1, 6, 0),
        BAD("k = \"\\x00\"", SOF_CONFIG_BAD_ESCAPE, 1, 6, 0),
        BAD("k = \"\\u12\"", SOF_CONFIG_BAD_ESCAPE, 1, 6, 0),
        BAD("k = \"\\u0000\"", SOF_CONFIG_BAD_ESCAPE, 1, 6, 0),
        BAD("k = \"\\ud800\"", SOF_CONFIG_BAD_ESCAPE, 1, 6, 0),
        BAD("k = \"\\U00110000\"", SOF_CONFIG_BAD_ESCAPE, 1, 6, 0),
        BAD("a = 1\nk = \"x\\q\"", SOF_CONFIG_BAD_ESCAPE, 2, 7, 1),
        BAD("k = \"ab\\", SOF_CONFIG_PREMATURE_EOF, 1, 9, 0),
        BAD("k = \"ab\\\nc\"", SOF_CONFIG_UNEXPECTED_EOL, 1, 9, 0),
    };
    sof_config_t config;
    CHECK(init_recording(&config) == SOF_CONFIG_OK);
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        record.count = 0;
        CHECK(sof_config_parse_string(&config, bad[i].text, bad[i].length) == bad[i].rc);
        CHECK(config.line == bad[i].line);
        CHECK(config.column == bad[i].column);
        CHECK(record.count == bad[i].count);
    }
    // The length is respected: the text needn't be NUL-terminated.
    record.count = 0;
    CHECK(sof_config_parse_string(&config, "a = 1b = 2", 5) == SOF_CONFIG_OK);
    CHECK(record.count == 1);
    CHECK(assigned(0, NULL, "a", "1"));
    CHECK(sof_config_free(&config) == SOF_CONFIG_OK);
}

static void test_bom(void) {
    sof_config_t config;
    CHECK(init_recording(&config) == SOF_CONFIG_OK);
    CHECK(parse_text(&config, "\xef\xbb\xbf" "a = 1\nb = 2") == SOF_CONFIG_OK);
    CHECK(record.count == 2);
    CHECK(assigned(0, NULL, "a", "1"));
    // The mark doesn't count as columns.
    CHECK(parse_text(&config, "\xef\xbb\xbf" "key") == SOF_CONFIG_PREMATURE_EOF);
    CHECK(config.line == 1);
    CHECK(config.column == 4);
    CHECK(sof_config_free(&config) == SOF_CONFIG_OK);
}

static void test_duplicates_and_merging(void) {
    sof_config_t config;
    CHECK(init_recording(&config) == SOF_CONFIG_OK);
    CHECK(parse_text(&config, "a = 1\n[s]\nb = 2\nb = 3\n") == SOF_CONFIG_OK);
    // The handler sees every assignment; the config keeps the last.
    CHECK(record.count == 3);
    CHECK(config.count == 2);
    CHECK(strcmp(sof_config_get(&config, "s", "b"), "3") == 0);
    CHECK(sof_config_find(&config, "s", "b")->line == 4);
    // A second parse adds to and replaces what is stored.
    CHECK(parse_text(&config, "a = override\nc = new\n") == SOF_CONFIG_OK);
    CHECK(config.count == 3);
    size_t position = 0;
    CHECK(entry_is(sof_config_next(&config, &position), NULL, "a", "override"));
    CHECK(entry_is(sof_config_next(&config, &position), "s", "b", "3"));
    CHECK(entry_is(sof_config_next(&config, &position), NULL, "c", "new"));
    CHECK(sof_config_next(&config, &position) == NULL);
    CHECK(sof_config_clear(&config) == SOF_CONFIG_OK);
    CHECK(config.count == 0);
    CHECK(sof_config_get(&config, NULL, "a") == NULL);
    position = 0;
    CHECK(sof_config_next(&config, &position) == NULL);
    CHECK(parse_text(&config, "a = again\n") == SOF_CONFIG_OK);
    CHECK(strcmp(sof_config_get(&config, NULL, "a"), "again") == 0);
    CHECK(sof_config_free(&config) == SOF_CONFIG_OK);
}

static void test_lookups(void) {
    sof_config_t config;
    CHECK(sof_config_init(&config, NULL, NULL, NULL) == SOF_CONFIG_OK);
    CHECK(parse_text(&config, "k = top\n[s]\nk = inner\n") == SOF_CONFIG_OK);
    CHECK(strcmp(sof_config_get(&config, NULL, "k"), "top") == 0);
    CHECK(strcmp(sof_config_get(&config, "s", "k"), "inner") == 0);
    CHECK(sof_config_get(&config, "t", "k") == NULL);
    CHECK(sof_config_get(&config, NULL, "missing") == NULL);
    CHECK(sof_config_get(&config, "", "k") == NULL);
    const sof_config_entry_t *entry = sof_config_find(&config, NULL, "k");
    CHECK(entry_is(entry, NULL, "k", "top"));
    CHECK(entry->line == 1);
    entry = sof_config_find(&config, "s", "k");
    CHECK(entry_is(entry, "s", "k", "inner"));
    CHECK(entry->line == 3);
    CHECK(sof_config_find(&config, "s", "K") == NULL);
    CHECK(sof_config_free(&config) == SOF_CONFIG_OK);
}

static void test_typed_getters(void) {
    const char *text =
        "[t]\n"
        "int = 42\n" "neg = -42\n" "plus = +7\n" "hex = 0xFF\n" "neghex = -0x10\n"
        "zero_lead = 010\n" "max = 9223372036854775807\n" "min = -9223372036854775808\n"
        "over = 9223372036854775808\n" "hexonly = 0x\n" "spaced = \" 5\"\n"
        "trailing = 5x\n" "double_sign = --5\n" "empty =\n"
        "d = 0.75\n" "dexp = 1e300\n" "dneg = -2.5e-3\n" "dhex = 0x1p3\n"
        "dinf = 1e999\n" "dnan = nan\n" "dword = inf\n"
        "b1 = yes\n" "b2 = Off\n" "b3 = TRUE\n" "b4 = 0\n" "b5 = maybe\n"
        "b6 = on\n" "b7 = NO\n" "b8 = 1\n" "b9 = yess\n";
    sof_config_t config;
    CHECK(sof_config_init(&config, NULL, NULL, NULL) == SOF_CONFIG_OK);
    CHECK(parse_text(&config, text) == SOF_CONFIG_OK);

    int64_t n = 99;
    CHECK(sof_config_get_int(&config, "t", "int", &n) == SOF_CONFIG_OK && n == 42);
    CHECK(sof_config_get_int(&config, "t", "neg", &n) == SOF_CONFIG_OK && n == -42);
    CHECK(sof_config_get_int(&config, "t", "plus", &n) == SOF_CONFIG_OK && n == 7);
    CHECK(sof_config_get_int(&config, "t", "hex", &n) == SOF_CONFIG_OK && n == 255);
    CHECK(sof_config_get_int(&config, "t", "neghex", &n) == SOF_CONFIG_OK && n == -16);
    CHECK(sof_config_get_int(&config, "t", "zero_lead", &n) == SOF_CONFIG_OK && n == 10);
    CHECK(sof_config_get_int(&config, "t", "max", &n) == SOF_CONFIG_OK && n == INT64_MAX);
    CHECK(sof_config_get_int(&config, "t", "min", &n) == SOF_CONFIG_OK && n == INT64_MIN);
    // A failed get leaves the value as it was.
    n = 99;
    CHECK(sof_config_get_int(&config, "t", "over", &n) == SOF_CONFIG_BAD_VALUE && n == 99);
    CHECK(sof_config_get_int(&config, "t", "hexonly", &n) == SOF_CONFIG_BAD_VALUE && n == 99);
    CHECK(sof_config_get_int(&config, "t", "spaced", &n) == SOF_CONFIG_BAD_VALUE && n == 99);
    CHECK(sof_config_get_int(&config, "t", "trailing", &n) == SOF_CONFIG_BAD_VALUE && n == 99);
    CHECK(sof_config_get_int(&config, "t", "double_sign", &n) == SOF_CONFIG_BAD_VALUE && n == 99);
    CHECK(sof_config_get_int(&config, "t", "empty", &n) == SOF_CONFIG_BAD_VALUE && n == 99);
    CHECK(sof_config_get_int(&config, "t", "d", &n) == SOF_CONFIG_BAD_VALUE && n == 99);
    CHECK(sof_config_get_int(&config, "t", "missing", &n) == SOF_CONFIG_NO_MATCH && n == 99);
    CHECK(sof_config_get_int(&config, NULL, "int", &n) == SOF_CONFIG_NO_MATCH && n == 99);

    double d = 9.5;
    CHECK(sof_config_get_double(&config, "t", "d", &d) == SOF_CONFIG_OK && d == 0.75);
    CHECK(sof_config_get_double(&config, "t", "dexp", &d) == SOF_CONFIG_OK && d == 1e300);
    CHECK(sof_config_get_double(&config, "t", "dneg", &d) == SOF_CONFIG_OK && d == -2.5e-3);
    CHECK(sof_config_get_double(&config, "t", "dhex", &d) == SOF_CONFIG_OK && d == 8.0);
    CHECK(sof_config_get_double(&config, "t", "int", &d) == SOF_CONFIG_OK && d == 42.0);
    d = 9.5;
    CHECK(sof_config_get_double(&config, "t", "dinf", &d) == SOF_CONFIG_BAD_VALUE && d == 9.5);
    CHECK(sof_config_get_double(&config, "t", "dnan", &d) == SOF_CONFIG_BAD_VALUE && d == 9.5);
    CHECK(sof_config_get_double(&config, "t", "dword", &d) == SOF_CONFIG_BAD_VALUE && d == 9.5);
    CHECK(sof_config_get_double(&config, "t", "spaced", &d) == SOF_CONFIG_BAD_VALUE && d == 9.5);
    CHECK(sof_config_get_double(&config, "t", "trailing", &d) == SOF_CONFIG_BAD_VALUE && d == 9.5);
    CHECK(sof_config_get_double(&config, "t", "empty", &d) == SOF_CONFIG_BAD_VALUE && d == 9.5);
    CHECK(sof_config_get_double(&config, "t", "missing", &d) == SOF_CONFIG_NO_MATCH && d == 9.5);

    int b = 5;
    CHECK(sof_config_get_bool(&config, "t", "b1", &b) == SOF_CONFIG_OK && b == 1);
    CHECK(sof_config_get_bool(&config, "t", "b2", &b) == SOF_CONFIG_OK && b == 0);
    CHECK(sof_config_get_bool(&config, "t", "b3", &b) == SOF_CONFIG_OK && b == 1);
    CHECK(sof_config_get_bool(&config, "t", "b4", &b) == SOF_CONFIG_OK && b == 0);
    CHECK(sof_config_get_bool(&config, "t", "b6", &b) == SOF_CONFIG_OK && b == 1);
    CHECK(sof_config_get_bool(&config, "t", "b7", &b) == SOF_CONFIG_OK && b == 0);
    CHECK(sof_config_get_bool(&config, "t", "b8", &b) == SOF_CONFIG_OK && b == 1);
    b = 5;
    CHECK(sof_config_get_bool(&config, "t", "b5", &b) == SOF_CONFIG_BAD_VALUE && b == 5);
    CHECK(sof_config_get_bool(&config, "t", "b9", &b) == SOF_CONFIG_BAD_VALUE && b == 5);
    CHECK(sof_config_get_bool(&config, "t", "empty", &b) == SOF_CONFIG_BAD_VALUE && b == 5);
    CHECK(sof_config_get_bool(&config, "t", "missing", &b) == SOF_CONFIG_NO_MATCH && b == 5);

    CHECK(sof_config_get_int(&config, "t", NULL, &n) == SOF_CONFIG_ERROR);
    CHECK(sof_config_get_int(&config, "t", "int", NULL) == SOF_CONFIG_ERROR);
    CHECK(sof_config_get_double(&config, "t", "d", NULL) == SOF_CONFIG_ERROR);
    CHECK(sof_config_get_bool(&config, "t", "b1", NULL) == SOF_CONFIG_ERROR);
    CHECK(sof_config_free(&config) == SOF_CONFIG_OK);
}

static int count_entry(const sof_config_entry_t *entry, void *ctx) {
    (*(size_t *)ctx)++;
    return 0;
}

static int stop_at_second(const sof_config_entry_t *entry, void *ctx) {
    return ++(*(size_t *)ctx) == 2 ? 7 : 0;
}

static void test_next_and_traverse(void) {
    sof_config_t config;
    CHECK(sof_config_init(&config, NULL, NULL, NULL) == SOF_CONFIG_OK);
    CHECK(parse_text(&config, "a = 1\n[s]\nb = 2\nc = 3\n") == SOF_CONFIG_OK);
    size_t position = 0;
    CHECK(entry_is(sof_config_next(&config, &position), NULL, "a", "1"));
    CHECK(entry_is(sof_config_next(&config, &position), "s", "b", "2"));
    CHECK(entry_is(sof_config_next(&config, &position), "s", "c", "3"));
    CHECK(position == 3);
    CHECK(sof_config_next(&config, &position) == NULL);
    CHECK(sof_config_next(&config, &position) == NULL);
    size_t seen = 0;
    CHECK(sof_config_traverse(&config, count_entry, &seen) == 0);
    CHECK(seen == 3);
    seen = 0;
    CHECK(sof_config_traverse(&config, stop_at_second, &seen) == 7);
    CHECK(seen == 2);
    CHECK(sof_config_free(&config) == SOF_CONFIG_OK);
}

static void test_stop(void) {
    sof_config_t config;
    CHECK(init_recording(&config) == SOF_CONFIG_OK);
    record.stop_at = 2;
    record.stop_value = -7;
    CHECK(parse_text(&config, "a = 1\nb = 2\nc = 3\n") == SOF_CONFIG_STOPPED);
    CHECK(config.stop_value == -7);
    CHECK(record.count == 2);
    // The assignment is stored before the handler sees it.
    CHECK(config.count == 2);
    CHECK(sof_config_get(&config, NULL, "c") == NULL);
    CHECK(config.line == 2);
    // The config can parse again after a stop, which resets stop_value.
    record.stop_at = 0;
    CHECK(parse_text(&config, "d = 4\n") == SOF_CONFIG_OK);
    CHECK(config.stop_value == 0);
    CHECK(record.count == 3);
    CHECK(sof_config_free(&config) == SOF_CONFIG_OK);
}

static void test_no_handler(void) {
    sof_config_t config;
    CHECK(sof_config_init(&config, NULL, NULL, NULL) == SOF_CONFIG_OK);
    CHECK(parse_text(&config, "[s]\na = 1\n") == SOF_CONFIG_OK);
    CHECK(parse_text(&config, "[s]\na = \"\n") == SOF_CONFIG_UNEXPECTED_EOL);
    CHECK(sof_config_parse(&config, DATA_FILE) == SOF_CONFIG_OK);
    CHECK(sof_config_free(&config) == SOF_CONFIG_OK);
}

static void test_nested_calls(void) {
    sof_config_t config;
    CHECK(init_recording(&config) == SOF_CONFIG_OK);
    record.config = &config;
    CHECK(parse_text(&config, "a = 1\nb = 2\n") == SOF_CONFIG_OK);
    CHECK(record.count == 2);
    CHECK(record.nested_parse == SOF_CONFIG_ERROR);
    CHECK(record.nested_clear == SOF_CONFIG_ERROR);
    CHECK(record.nested_free == SOF_CONFIG_ERROR);
    CHECK(config.state != NULL);
    CHECK(config.count == 2);
    record.config = NULL;
    CHECK(sof_config_free(&config) == SOF_CONFIG_OK);
}

/// @brief Writes length random characters from chars to s, and a NUL.
static void random_text(char *s, size_t length, const char *chars, uint64_t *state) {
    size_t count = strlen(chars);
    for (size_t i = 0; i < length; i++) {
        s[i] = chars[next_random(state) % count];
    }
    s[length] = 0;
}

/// @brief Writes count random assignments to f, in sections of random names,
/// with values of up to max_length characters, and records what they should
/// parse to in expected. Every section and key is different.
static void write_random(FILE *f, record_t *expected, size_t count, size_t max_length, uint64_t *state) {
    static const char name_chars[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-.";
    static const char value_chars[] = "abcdefghijklmnopqrstuvwxyz0123456789 !$%&()*+,-./:<=>?@[]^_{|}~\"\\";
    expected->count = 0;
    for (size_t i = 0; i < count; i++) {
        assignment_t *a = &expected->assignments[i];
        a->has_section = 1;
        if (i == 0 || next_random(state) % 10 == 0) {
            int length = snprintf(a->section, MAX_TEXT, "s%zu_", i);
            random_text(a->section + length, next_random(state) % 40, name_chars, state);
            fprintf(f, "[ %s ]\n", a->section);
        } else {
            strcpy(a->section, expected->assignments[i - 1].section);
        }
        int length = snprintf(a->key, MAX_TEXT, "k%zu_", i);
        random_text(a->key + length, next_random(state) % 40, name_chars, state);
        // Values start and end with a letter, so trimming leaves them alone.
        size_t value_length = 2 + next_random(state) % (max_length - 2);
        random_text(a->value, value_length, value_chars, state);
        a->value[0] = 'v';
        a->value[value_length - 1] = 'v';
        if (next_random(state) % 2 == 0) {
            fprintf(f, "%s = %s ; comment\n", a->key, a->value);
        } else {
            fprintf(f, "%s=\"", a->key);
            for (size_t j = 0; j < value_length; j++) {
                char ch = a->value[j];
                fprintf(f, ch == '"' || ch == '\\' ? "\\%c" : "%c", ch);
            }
            fprintf(f, "\"\n");
        }
        expected->count++;
    }
}

static void test_random(void) {
    static record_t expected;
    uint64_t state = 0x9e3779b97f4a7c15ULL;
    FILE *f = tmpfile();
    CHECK(f != NULL);
    // Long values make the strings grow, and the file is several times the
    // read buffer, so tokens cross refills.
    write_random(f, &expected, 1000, 1000, &state);
    long length = ftell(f);
    CHECK(length > 100000);

    sof_config_t config;
    CHECK(init_recording(&config) == SOF_CONFIG_OK);
    rewind(f);
    CHECK(sof_config_parse_file(&config, f) == SOF_CONFIG_OK);
    CHECK(record.count == expected.count);
    CHECK(config.count == expected.count);
    for (size_t i = 0; i < expected.count; i++) {
        const assignment_t *a = &expected.assignments[i];
        CHECK(assigned(i, a->section, a->key, a->value));
        CHECK(strcmp(sof_config_get(&config, a->section, a->key), a->value) == 0);
    }

    // The same text parsed from memory gives the same assignments.
    char *text = sof_alloc_malloc(NULL, (size_t)length);
    CHECK(text != NULL);
    rewind(f);
    CHECK(fread(text, 1, (size_t)length, f) == (size_t)length);
    fclose(f);
    record.count = 0;
    CHECK(sof_config_clear(&config) == SOF_CONFIG_OK);
    CHECK(sof_config_parse_string(&config, text, (size_t)length) == SOF_CONFIG_OK);
    sof_alloc_free(NULL, text, (size_t)length);
    CHECK(record.count == expected.count);
    CHECK(config.count == expected.count);
    for (size_t i = 0; i < expected.count; i++) {
        const assignment_t *a = &expected.assignments[i];
        CHECK(assigned(i, a->section, a->key, a->value));
    }
    CHECK(sof_config_free(&config) == SOF_CONFIG_OK);
}

static void test_files(void) {
    sof_config_t config;
    CHECK(init_recording(&config) == SOF_CONFIG_OK);
    CHECK(sof_config_parse(&config, "no-such-file.ini") == SOF_CONFIG_NO_OPEN);
    CHECK(sof_config_parse(&config, "") == SOF_CONFIG_NO_OPEN);
    // A directory opens, but can't be read.
    CHECK(sof_config_parse(&config, ".") == SOF_CONFIG_NO_READ);
    // An empty file parses, with nothing in it.
    FILE *f = tmpfile();
    CHECK(f != NULL);
    CHECK(sof_config_parse_file(&config, f) == SOF_CONFIG_OK);
    CHECK(config.count == 0);
    // A file is parsed from where it is, and isn't closed.
    fputs("a = 1\nb = 2\n", f);
    fseek(f, 6, SEEK_SET);
    CHECK(sof_config_parse_file(&config, f) == SOF_CONFIG_OK);
    CHECK(record.count == 1);
    CHECK(assigned(0, NULL, "b", "2"));
    CHECK(fseek(f, 0, SEEK_SET) == 0);
    fclose(f);
    CHECK(sof_config_free(&config) == SOF_CONFIG_OK);
}

static void test_custom_allocator(void) {
    test_alloc_t state = {0};
    sof_alloc_t alloc = test_allocator(&state);
    sof_config_t config;
    CHECK(sof_config_init(&config, NULL, NULL, &alloc) == SOF_CONFIG_OK);
    // The config keeps a copy of the allocator.
    alloc.malloc = NULL;
    long initial = state.live;
    CHECK(initial > 0);
    // Everything is allocated with it, including the stored strings and the
    // map's array, and freed with the sizes it was allocated with.
    CHECK(sof_config_parse(&config, DATA_FILE) == SOF_CONFIG_OK);
    CHECK(config.count == sizeof(data_assignments) / sizeof(data_assignments[0]) + 1);
    CHECK(state.live > initial + (long)config.count);
    CHECK(strcmp(sof_config_get(&config, "server.http", "host"), "example.com") == 0);
    CHECK(sof_config_clear(&config) == SOF_CONFIG_OK);
    CHECK(sof_config_parse(&config, DATA_FILE) == SOF_CONFIG_OK);
    CHECK(sof_config_free(&config) == SOF_CONFIG_OK);
    CHECK(state.live == 0);
    CHECK(state.bad == 0);

    // An allocator with a malloc but no free is refused, without allocating.
    size_t allocations = state.allocations;
    alloc = test_allocator(&state);
    alloc.free_sized = NULL;
    CHECK(sof_config_init(&config, NULL, NULL, &alloc) == SOF_CONFIG_ERROR);
    CHECK(config.state == NULL);
    CHECK(state.allocations == allocations);
}

static void test_no_memory(void) {
    // Fail each allocation in turn, through init, a parse of the test data
    // and free, until a run makes no allocation fail.
    size_t entries = 0;
    for (size_t n = 1;; n++) {
        test_alloc_t state = {0};
        state.fail_at = n;
        sof_alloc_t alloc = test_allocator(&state);
        sof_config_t config;
        sof_config_error_t rc = sof_config_init(&config, NULL, NULL, &alloc);
        if (rc == SOF_CONFIG_OK) {
            rc = sof_config_parse(&config, DATA_FILE);
            entries = config.count;
            if (state.allocations >= n) {
                CHECK(rc == SOF_CONFIG_NO_MEM);
            }
            CHECK(sof_config_free(&config) == SOF_CONFIG_OK);
        } else {
            CHECK(rc == SOF_CONFIG_NO_MEM);
            CHECK(config.state == NULL);
            CHECK(sof_config_parse(&config, DATA_FILE) == SOF_CONFIG_ERROR);
            CHECK(sof_config_free(&config) == SOF_CONFIG_OK);
        }
        CHECK(state.live == 0);
        CHECK(state.bad == 0);
        if (state.allocations < n) {
            CHECK(rc == SOF_CONFIG_OK);
            break;
        }
    }
    CHECK(entries == sizeof(data_assignments) / sizeof(data_assignments[0]) + 1);
}

static void test_error_string(void) {
    CHECK(strcmp(sof_config_error_string(SOF_CONFIG_OK), "No error") == 0);
    CHECK(strcmp(sof_config_error_string(SOF_CONFIG_UNEXPECTED_EOL), "Unexpected end of line") == 0);
    for (int i = SOF_CONFIG_OK; i <= SOF_CONFIG_STOPPED; i++) {
        CHECK(sof_config_error_string((sof_config_error_t)i) != NULL);
        CHECK(strcmp(sof_config_error_string((sof_config_error_t)i), "Unknown error") != 0);
        for (int j = SOF_CONFIG_OK; j < i; j++) {
            CHECK(strcmp(sof_config_error_string((sof_config_error_t)i), sof_config_error_string((sof_config_error_t)j)) != 0);
        }
    }
    CHECK(strcmp(sof_config_error_string((sof_config_error_t)(SOF_CONFIG_STOPPED + 1)), "Unknown error") == 0);
    CHECK(strcmp(sof_config_error_string((sof_config_error_t)-1), "Unknown error") == 0);
}

static void test_null(void) {
    sof_config_t config;
    size_t position = 0, seen = 0;
    int64_t n = 0;
    double d = 0;
    int b = 0;
    FILE *f = tmpfile();
    CHECK(f != NULL);
    CHECK(sof_config_init(NULL, record_assignment, NULL, NULL) == SOF_CONFIG_ERROR);
    CHECK(sof_config_clear(NULL) == SOF_CONFIG_ERROR);
    CHECK(sof_config_free(NULL) == SOF_CONFIG_ERROR);
    CHECK(sof_config_parse(NULL, DATA_FILE) == SOF_CONFIG_ERROR);
    CHECK(sof_config_parse_file(NULL, f) == SOF_CONFIG_ERROR);
    CHECK(sof_config_parse_string(NULL, "a = 1", 5) == SOF_CONFIG_ERROR);
    CHECK(sof_config_find(NULL, NULL, "a") == NULL);
    CHECK(sof_config_get(NULL, NULL, "a") == NULL);
    CHECK(sof_config_get_int(NULL, NULL, "a", &n) == SOF_CONFIG_ERROR);
    CHECK(sof_config_get_double(NULL, NULL, "a", &d) == SOF_CONFIG_ERROR);
    CHECK(sof_config_get_bool(NULL, NULL, "a", &b) == SOF_CONFIG_ERROR);
    CHECK(sof_config_next(NULL, &position) == NULL);
    CHECK(sof_config_traverse(NULL, count_entry, &seen) == -1);

    CHECK(init_recording(&config) == SOF_CONFIG_OK);
    CHECK(sof_config_parse(&config, NULL) == SOF_CONFIG_ERROR);
    CHECK(sof_config_parse_file(&config, NULL) == SOF_CONFIG_ERROR);
    CHECK(sof_config_parse_string(&config, NULL, 0) == SOF_CONFIG_ERROR);
    CHECK(sof_config_parse_string(&config, "", 0) == SOF_CONFIG_OK);
    CHECK(parse_text(&config, "a = 1") == SOF_CONFIG_OK);
    CHECK(sof_config_find(&config, NULL, NULL) == NULL);
    CHECK(sof_config_get(&config, NULL, NULL) == NULL);
    CHECK(sof_config_next(&config, NULL) == NULL);
    CHECK(sof_config_traverse(&config, NULL, NULL) == -1);

    // A freed config is empty: it finds nothing and parses nothing until it
    // is initialized again.
    CHECK(sof_config_free(&config) == SOF_CONFIG_OK);
    record.count = 0;
    CHECK(sof_config_parse(&config, DATA_FILE) == SOF_CONFIG_ERROR);
    CHECK(sof_config_parse_file(&config, f) == SOF_CONFIG_ERROR);
    CHECK(sof_config_parse_string(&config, "a = 1", 5) == SOF_CONFIG_ERROR);
    CHECK(record.count == 0);
    CHECK(sof_config_find(&config, NULL, "a") == NULL);
    CHECK(sof_config_get(&config, NULL, "a") == NULL);
    CHECK(sof_config_get_int(&config, NULL, "a", &n) == SOF_CONFIG_NO_MATCH);
    position = 0;
    CHECK(sof_config_next(&config, &position) == NULL);
    CHECK(sof_config_traverse(&config, count_entry, &seen) == 0);
    CHECK(seen == 0);
    CHECK(sof_config_clear(&config) == SOF_CONFIG_OK);
    CHECK(sof_config_free(&config) == SOF_CONFIG_OK);
    fclose(f);
}

/// @brief Reader threads, the passes each makes, and the re-parses the
/// writer makes, in the threaded tests. Readers make a fixed number of
/// passes rather than reading until the writer is done: glibc's rwlock
/// prefers readers, so readers that keep taking the lock can keep the writer
/// out forever.
#define READERS 4
#define PASSES 2000
#define WRITES 500

/// @brief The sections in a generated config, the keys in each, and the
/// entries in all, counting "generation" outside any section.
#define SECTIONS 20
#define KEYS 4
#define ENTRIES (1 + SECTIONS * KEYS)

/// @brief The keys in each section of a generated config.
static const char *const thread_keys[KEYS] = {"int", "double", "bool", "name"};

/// @brief Writes the value key k of section s has in generation g.
static void generated_value(size_t g, size_t s, size_t k, char *buffer, size_t size) {
    switch (k) {
    case 0:
        snprintf(buffer, size, "%zu", g * 1000 + s);
        break;
    case 1:
        snprintf(buffer, size, "%zu.5", g * 1000 + s);
        break;
    case 2:
        snprintf(buffer, size, "%s", g % 2 ? "yes" : "off");
        break;
    default:
        snprintf(buffer, size, "gen %zu; section %zu", g, s);
        break;
    }
}

/// @brief The line key k of section s is on, in every generation.
static size_t generated_line(size_t s, size_t k) {
    return 3 + s * (KEYS + 1) + k;
}

/// @brief Writes the text of generation g of the config: "generation = g",
/// then SECTIONS sections of KEYS keys, with the names quoted. Returns its
/// length.
static size_t generate(size_t g, char *text, size_t size) {
    size_t length = (size_t)snprintf(text, size, "generation = %zu\n", g);
    for (size_t s = 0; s < SECTIONS; s++) {
        length += (size_t)snprintf(text + length, size - length, "[s%zu]\n", s);
        for (size_t k = 0; k < KEYS; k++) {
            char value[64];
            generated_value(g, s, k, value, sizeof(value));
            const char *quote = k == 3 ? "\"" : "";
            length += (size_t)snprintf(text + length, size - length, "%s = %s%s%s\n", thread_keys[k], quote, value, quote);
        }
    }
    return length;
}

/// @brief Returns 1 if entry is one of generation g's, with its value and
/// line.
static int generated_entry(const sof_config_entry_t *entry, size_t g) {
    char expected[64];
    if (entry->section == NULL) {
        snprintf(expected, sizeof(expected), "%zu", g);
        return strcmp(entry->key, "generation") == 0 && strcmp(entry->value, expected) == 0 && entry->line == 1;
    }
    char *end;
    if (entry->section[0] != 's') {
        return 0;
    }
    unsigned long s = strtoul(entry->section + 1, &end, 10);
    if (*end != 0 || s >= SECTIONS) {
        return 0;
    }
    for (size_t k = 0; k < KEYS; k++) {
        if (strcmp(entry->key, thread_keys[k]) == 0) {
            generated_value(g, s, k, expected, sizeof(expected));
            return strcmp(entry->value, expected) == 0 && entry->line == generated_line(s, k);
        }
    }
    return 0;
}

/// @brief What check_generated() checks entries against, and what it found.
typedef struct generated_t {
    size_t generation;
    size_t seen;
    size_t bad;
    /// @brief The number of visits after which the traversal stops, or 0.
    size_t stop_after;
} generated_t;

/// @brief Checks, for sof_config_traverse(), that each entry is one of the
/// generation's, and counts them.
static int check_generated(const sof_config_entry_t *entry, void *ctx) {
    generated_t *g = ctx;
    if (!generated_entry(entry, g->generation)) {
        g->bad++;
    }
    g->seen++;
    if (g->stop_after != 0 && g->seen == g->stop_after) {
        return 7;
    }
    return 0;
}

/// @brief One reader thread's state and results, checked by the main thread
/// after joining, since CHECK isn't safe on other threads.
typedef struct reader_t {
    sof_config_t *config;
    /// @brief The lock to read under, or NULL to read without one.
    pthread_rwlock_t *lock;
    /// @brief What sof_config_error_string(SOF_CONFIG_BAD_VALUE) returned on
    /// the main thread.
    const char *bad_value_string;
    uint64_t random;
    size_t passes;
    size_t errors;
    /// @brief The generation the last pass saw; generations never go back.
    size_t generation;
} reader_t;

/// @brief Reads the config once through every reading function, checking
/// that everything comes from the one generation it holds. Returns the
/// number of things that were wrong.
static size_t read_pass(reader_t *r) {
    const sof_config_t *c = r->config;
    size_t errors = 0;
    int64_t n = -1;
    if (sof_config_get_int(c, NULL, "generation", &n) != SOF_CONFIG_OK || n < 1 || (size_t)n < r->generation) {
        return 1;
    }
    size_t g = (size_t)n;
    r->generation = g;

    // One section, at random, through each lookup.
    size_t s = next_random(&r->random) % SECTIONS;
    char section[16], expected[64];
    snprintf(section, sizeof(section), "s%zu", s);
    double d = 0;
    int b = -1;
    errors += sof_config_get_int(c, section, "int", &n) != SOF_CONFIG_OK || n != (int64_t)(g * 1000 + s);
    errors += sof_config_get_double(c, section, "double", &d) != SOF_CONFIG_OK || d != (double)(g * 1000 + s) + 0.5;
    errors += sof_config_get_bool(c, section, "bool", &b) != SOF_CONFIG_OK || b != (int)(g % 2);
    generated_value(g, s, 3, expected, sizeof(expected));
    const char *name = sof_config_get(c, section, "name");
    errors += name == NULL || strcmp(name, expected) != 0;
    const sof_config_entry_t *entry = sof_config_find(c, section, "double");
    errors += entry == NULL || !generated_entry(entry, g);
    errors += sof_config_get(c, section, "missing") != NULL;
    errors += sof_config_find(c, "nowhere", "int") != NULL;
    errors += sof_config_get_int(c, "nowhere", "int", &n) != SOF_CONFIG_NO_MATCH;
    errors += sof_config_get_int(c, section, "name", &n) != SOF_CONFIG_BAD_VALUE;
    errors += sof_config_error_string(SOF_CONFIG_BAD_VALUE) != r->bad_value_string;

    // Every entry, through sof_config_next() and sof_config_traverse().
    size_t position = 0, walked = 0;
    while ((entry = sof_config_next(c, &position)) != NULL) {
        errors += !generated_entry(entry, g);
        walked++;
    }
    errors += walked != ENTRIES || c->count != ENTRIES;
    generated_t check = {g, 0, 0, 0};
    errors += sof_config_traverse(c, check_generated, &check) != 0 || check.seen != ENTRIES || check.bad != 0;
    generated_t stop = {g, 0, 0, 1 + next_random(&r->random) % ENTRIES};
    errors += sof_config_traverse(c, check_generated, &stop) != 7 || stop.seen != stop.stop_after || stop.bad != 0;
    return errors;
}

/// @brief Makes PASSES read passes, each under the read lock if there is one.
static void *read_config(void *arg) {
    reader_t *r = arg;
    for (size_t pass = 0; pass < PASSES; pass++) {
        if (r->lock != NULL) {
            pthread_rwlock_rdlock(r->lock);
        }
        r->errors += read_pass(r);
        if (r->lock != NULL) {
            pthread_rwlock_unlock(r->lock);
        }
        r->passes++;
    }
    return NULL;
}

/// @brief Starts READERS readers of config, each with its own random seed.
static int start_readers(reader_t *readers, pthread_t *threads, sof_config_t *config, pthread_rwlock_t *lock) {
    for (size_t i = 0; i < READERS; i++) {
        readers[i].config = config;
        readers[i].lock = lock;
        readers[i].bad_value_string = sof_config_error_string(SOF_CONFIG_BAD_VALUE);
        readers[i].random = 0x2545f4914f6cdd1dULL * (i + 1);
        if (pthread_create(&threads[i], NULL, read_config, &readers[i]) != 0) {
            return 0;
        }
    }
    return 1;
}

static void test_threads(void) {
    // A config parsed once and then only read needs no lock: readers on
    // several threads at once use every reading function, and must all see
    // the one generation it holds.
    static char text[4096];
    test_alloc_t state = {0};
    sof_alloc_t alloc = test_allocator(&state);
    sof_config_t config;
    CHECK(sof_config_init(&config, NULL, NULL, &alloc) == SOF_CONFIG_OK);
    size_t length = generate(1, text, sizeof(text));
    CHECK(length < sizeof(text));
    CHECK(sof_config_parse_string(&config, text, length) == SOF_CONFIG_OK);
    CHECK(config.count == ENTRIES);
    reader_t readers[READERS] = {{0}};
    pthread_t threads[READERS];
    CHECK(start_readers(readers, threads, &config, NULL));
    for (size_t i = 0; i < READERS; i++) {
        CHECK(pthread_join(threads[i], NULL) == 0);
    }
    for (size_t i = 0; i < READERS; i++) {
        CHECK(readers[i].errors == 0);
        CHECK(readers[i].passes == PASSES);
        CHECK(readers[i].generation == 1);
    }
    CHECK(sof_config_free(&config) == SOF_CONFIG_OK);
    CHECK(state.live == 0);
    CHECK(state.bad == 0);
}

static void test_threads_readers(void) {
    // One writer re-parses the config with each new generation, by turns
    // over the old one and after clearing it, while readers read it under a
    // read lock. Each pass must see one whole generation, never older than
    // the last pass saw, and nothing may leak.
    static char text[4096];
    test_alloc_t state = {0};
    sof_alloc_t alloc = test_allocator(&state);
    sof_config_t config;
    pthread_rwlock_t lock;
    CHECK(sof_config_init(&config, NULL, NULL, &alloc) == SOF_CONFIG_OK);
    CHECK(pthread_rwlock_init(&lock, NULL) == 0);
    size_t length = generate(1, text, sizeof(text));
    CHECK(sof_config_parse_string(&config, text, length) == SOF_CONFIG_OK);
    reader_t readers[READERS] = {{0}};
    pthread_t threads[READERS];
    CHECK(start_readers(readers, threads, &config, &lock));
    size_t errors = 0;
    for (size_t g = 2; g <= WRITES + 1; g++) {
        length = generate(g, text, sizeof(text));
        pthread_rwlock_wrlock(&lock);
        if (g % 3 == 0 && sof_config_clear(&config) != SOF_CONFIG_OK) {
            errors++;
        }
        if (sof_config_parse_string(&config, text, length) != SOF_CONFIG_OK || config.count != ENTRIES) {
            errors++;
        }
        pthread_rwlock_unlock(&lock);
    }
    for (size_t i = 0; i < READERS; i++) {
        CHECK(pthread_join(threads[i], NULL) == 0);
    }
    CHECK(errors == 0);
    for (size_t i = 0; i < READERS; i++) {
        CHECK(readers[i].errors == 0);
        CHECK(readers[i].passes == PASSES);
        CHECK(readers[i].generation >= 1 && readers[i].generation <= WRITES + 1);
    }
    generated_t check = {WRITES + 1, 0, 0, 0};
    CHECK(sof_config_traverse(&config, check_generated, &check) == 0);
    CHECK(check.seen == ENTRIES && check.bad == 0);
    CHECK(sof_config_free(&config) == SOF_CONFIG_OK);
    CHECK(state.live == 0);
    CHECK(state.bad == 0);
    pthread_rwlock_destroy(&lock);
}

int main(void) {
    printf("sof_config:\n");
    run("init and free", test_init_free);
    run("parse a file", test_parse_file);
    run("entries from a file", test_file_entries);
    run("sections", test_sections);
    run("raw values", test_raw_values);
    run("quoted values", test_quoted_values);
    run("syntax errors and where they are", test_errors);
    run("byte order mark", test_bom);
    run("duplicates and merging", test_duplicates_and_merging);
    run("lookups", test_lookups);
    run("typed getters", test_typed_getters);
    run("next and traverse", test_next_and_traverse);
    run("early stop by the handler", test_stop);
    run("no handler", test_no_handler);
    run("calls from inside the handler", test_nested_calls);
    run("random files and strings", test_random);
    run("files", test_files);
    run("custom allocator", test_custom_allocator);
    run("out of memory", test_no_memory);
    run("error strings", test_error_string);
    run("NULL arguments and freed configs", test_null);
    run("readers on threads without a lock", test_threads);
    run("readers alongside a writer on threads", test_threads_readers);
    return test_report();
}
