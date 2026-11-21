/**
 * @file test.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief Tests for sof_regex.c.
 * @details Usage: test. Runs every test, and exits with 0 if they all pass
 * or 1 if any fails. The Makefile's check target builds and runs it.
 * @version 0.0.1
 * @date 2026-11-05
 * @copyright Copyright (c) 2026
 */

#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "sof_alloc.h"
#include "sof_regex.h"
#include "sof_string.h"
#include "test.h"

/// @brief The number of elements in an array.
#define LENGTH(a) (sizeof(a) / sizeof((a)[0]))

/// @brief The number of threads in the threading test.
#define THREADS 4

/// @brief Text built up by the tests to compare with what they expect.
typedef struct text_t {
    char data[2048];
    size_t used;
} text_t;

/// @brief A thread's work in the threading test.
typedef struct job_t {
    const sof_string_t *subject;
    size_t matches;
    size_t fields;
    int ok;
    test_alloc_t state;
} job_t;

static sof_regex_error_t regex_init(sof_regex_t *regex, const char *pattern, const sof_alloc_t *alloc);
static void run_all(void);
static void text_clear(text_t *text);
static void text_escaped(text_t *text, const char *bytes, size_t length);
static void text_put(text_t *text, const char *format, ...);
static sof_regex_error_t find_all(const char *bytes, size_t length, const char *pattern, text_t *out);
static sof_regex_error_t split_all(const char *bytes, size_t length, const char *pattern, text_t *out);
static void *search_thread(void *arg);

/// @brief Set while the tests run the second time, with every regex
/// compiled by PCRE2's JIT.
static int jit = 0;

/// @brief sof_regex_init(), followed by sof_regex_jit() when jit is set, so
/// the same tests check both ways of matching.
static sof_regex_error_t regex_init(sof_regex_t *regex, const char *pattern, const sof_alloc_t *alloc) {
    sof_regex_error_t rc = sof_regex_init(regex, pattern, alloc);
    if (rc == SOF_REGEX_OK && jit) {
        sof_regex_jit(regex);
    }
    return rc;
}

/// @brief Empties text.
static void text_clear(text_t *text) {
    text->used = 0;
    text->data[0] = '\0';
}

/// @brief Adds bytes to text, writing each one that isn't printable ASCII as
/// \xhh.
static void text_escaped(text_t *text, const char *bytes, size_t length) {
    for (size_t i = 0; i < length; i++) {
        unsigned char c = (unsigned char)bytes[i];
        if (c < 0x20 || c >= 0x7f) {
            text_put(text, "\\x%02x", c);
        } else {
            text_put(text, "%c", c);
        }
    }
}

/// @brief Adds formatted text, stopping at the end of the buffer.
static void text_put(text_t *text, const char *format, ...) {
    va_list args;
    size_t room = sizeof(text->data) - text->used;
    va_start(args, format);
    int n = vsnprintf(text->data + text->used, room, format, args);
    va_end(args);
    if (n > 0) {
        text->used += (size_t)n < room ? (size_t)n : room - 1;
    }
}

/// @brief Finds every match of pattern in bytes and writes them to out as
/// "[a]@0[b]@2". Returns the result that ended the search, or SOF_REGEX_OK
/// if it was still finding matches after 100, which none of the cases have.
static sof_regex_error_t find_all(const char *bytes, size_t length, const char *pattern, text_t *out) {
    sof_string_t s = sof_string_view(bytes, length);
    sof_regex_t re;
    size_t position = 0, offset, match_length;
    text_clear(out);
    sof_regex_error_t rc = regex_init(&re, pattern, NULL);
    if (rc != SOF_REGEX_OK) {
        return rc;
    }
    for (int i = 0; i < 100; i++) {
        rc = sof_regex_find(&re, &s, &position, &offset, &match_length);
        if (rc != SOF_REGEX_OK) {
            break;
        }
        text_put(out, "[");
        text_escaped(out, bytes + offset, match_length);
        text_put(out, "]@%zu", offset);
    }
    sof_regex_free(&re);
    return rc;
}

/// @brief Splits bytes on pattern and writes the fields to out as "[a][b]".
/// Returns the result that ended the split, or SOF_REGEX_OK if it was still
/// finding fields after 100.
static sof_regex_error_t split_all(const char *bytes, size_t length, const char *pattern, text_t *out) {
    sof_string_t s = sof_string_view(bytes, length);
    sof_regex_t re;
    size_t position = 0, offset, field_length;
    text_clear(out);
    sof_regex_error_t rc = regex_init(&re, pattern, NULL);
    if (rc != SOF_REGEX_OK) {
        return rc;
    }
    for (int i = 0; i < 100; i++) {
        rc = sof_regex_split(&re, &s, &position, &offset, &field_length);
        if (rc != SOF_REGEX_OK) {
            break;
        }
        text_put(out, "[");
        text_escaped(out, bytes + offset, field_length);
        text_put(out, "]");
    }
    sof_regex_free(&re);
    return rc;
}

/// @brief Counts the words and fields of a shared subject many times with
/// regexes of its own, checking each count.
static void *search_thread(void *arg) {
    job_t *job = arg;
    sof_alloc_t alloc = test_allocator(&job->state);
    sof_regex_t words, spaces;
    sof_regex_error_t words_rc = regex_init(&words, "\\w+", &alloc);
    sof_regex_error_t spaces_rc = regex_init(&spaces, " ", &alloc);
    job->ok = words_rc == SOF_REGEX_OK && spaces_rc == SOF_REGEX_OK;
    for (int pass = 0; job->ok && pass < 20; pass++) {
        size_t position = 0, matches = 0, fields = 0;
        while (sof_regex_find(&words, job->subject, &position, NULL, NULL) == SOF_REGEX_OK) {
            matches++;
        }
        position = 0;
        while (sof_regex_split(&spaces, job->subject, &position, NULL, NULL) == SOF_REGEX_OK) {
            fields++;
        }
        job->ok = matches == job->matches && fields == job->fields;
    }
    sof_regex_free(&words);
    sof_regex_free(&spaces);
    return NULL;
}

static void test_init(void) {
    sof_regex_t re;
    CHECK(regex_init(&re, "a+b", NULL) == SOF_REGEX_OK);
    CHECK(re.code != NULL && re.match_data != NULL && re.match_context != NULL && re.memory != NULL);
    CHECK(sof_regex_free(&re) == SOF_REGEX_OK);
    CHECK(re.code == NULL && re.match_data == NULL && re.match_context == NULL && re.memory == NULL);
    CHECK(sof_regex_free(&re) == SOF_REGEX_OK);
    CHECK(sof_regex_free(NULL) == SOF_REGEX_ERROR);

    // Patterns that don't compile leave the regex dead.
    static const char *bad[] = {"(", ")", "[a", "a{2,1}", "\\", "(?<n>a)(?<n>b)", "*", "(?P<"};
    for (size_t i = 0; i < LENGTH(bad); i++) {
        memset(&re, 0xa5, sizeof(re));
        CHECK(regex_init(&re, bad[i], NULL) == SOF_REGEX_BAD_PATTERN);
        CHECK(re.code == NULL && re.match_data == NULL && re.memory == NULL);
    }
    memset(&re, 0xa5, sizeof(re));
    CHECK(regex_init(&re, NULL, NULL) == SOF_REGEX_ERROR);
    CHECK(re.code == NULL);
    CHECK(regex_init(NULL, "a", NULL) == SOF_REGEX_ERROR);

    // A regex can move, since PCRE2's pointer to the allocator is to the
    // heap.
    sof_regex_t moved;
    sof_string_t s = sof_string_const("xaab");
    size_t position = 0, offset, length;
    CHECK(regex_init(&re, "a+b", NULL) == SOF_REGEX_OK);
    memcpy(&moved, &re, sizeof(re));
    memset(&re, 0, sizeof(re));
    CHECK(sof_regex_find(&moved, &s, &position, &offset, &length) == SOF_REGEX_OK);
    CHECK(offset == 1 && length == 3);
    sof_regex_free(&moved);
}

static void test_find(void) {
#define CASE(text, regex, matches) {text, sizeof(text) - 1, regex, matches}
    static const struct {
        const char *text;
        size_t length;
        const char *regex, *matches;
    } cases[] = {
        CASE("ab cd", "\\w+", "[ab]@0[cd]@3"),
        CASE("ab cd", "\\w*", "[ab]@0[]@2[cd]@3[]@5"),
        CASE("abc", "x*", "[]@0[]@1[]@2[]@3"),
        CASE("", "x*", "[]@0"),
        CASE("", "", "[]@0"),
        CASE("ab", "x", ""),
        CASE("", "x", ""),
        // After an empty match the search moves on a byte, so the "a" at 0
        // is passed over.
        CASE("a", "|a", "[]@0[]@1"),
        CASE("aaa", "a*?", "[]@0[]@1[]@2[]@3"),
        CASE("aaaa", "aa", "[aa]@0[aa]@2"),
        CASE("aaa", "aa", "[aa]@0"),
        // ^ and \b look at the whole string, not just from the position.
        CASE("aaa", "^a", "[a]@0"),
        CASE("aaa", "a$", "[a]@2"),
        CASE("ab\nab", "(?m)^a", "[a]@0[a]@3"),
        CASE("abc", "\\b", "[]@0[]@3"),
        CASE("abab", "(?<=a)b", "[b]@1[b]@3"),
        CASE("abc", "(?=b)", "[]@1"),
        CASE("aBAb", "(?i)ab", "[aB]@0[Ab]@2"),
        CASE("a1b22", "\\d", "[1]@1[2]@3[2]@4"),
        CASE("a1b22", "(\\d)(\\d)?", "[1]@1[22]@3"),
        // NULs and high bytes are matched like any other bytes.
        CASE("a\0b\0", "\\x00", "[\\x00]@1[\\x00]@3"),
        CASE("a\0b\0", "[^\\x00]+", "[a]@0[b]@2"),
        CASE("a\xff\x80z", "[\\x80-\\xff]+", "[\\xff\\x80]@1"),
        CASE("\xc3\xa9", ".", "[\\xc3]@0[\\xa9]@1"),
    };
#undef CASE
    text_t matches;
    for (size_t i = 0; i < LENGTH(cases); i++) {
        CHECK(find_all(cases[i].text, cases[i].length, cases[i].regex, &matches) == SOF_REGEX_NO_MATCH);
        if (strcmp(matches.data, cases[i].matches) != 0) {
            printf("FAILED\n    find /%s/ in case %zu: got %s, expected %s\n", cases[i].regex, i, matches.data, cases[i].matches);
            test_failure_count++;
            return;
        }
    }
}

static void test_find_position(void) {
    sof_string_t s = sof_string_const("one two three");
    sof_regex_t re;
    size_t position, offset, length;
    CHECK(regex_init(&re, "\\w+", NULL) == SOF_REGEX_OK);

    // A search can start anywhere, even inside a word.
    position = 4;
    CHECK(sof_regex_find(&re, &s, &position, &offset, &length) == SOF_REGEX_OK);
    CHECK(offset == 4 && length == 3 && position == 7);
    position = 5;
    CHECK(sof_regex_find(&re, &s, &position, &offset, &length) == SOF_REGEX_OK);
    CHECK(offset == 5 && length == 2);
    // At or past the end, nothing is found, and position stays.
    position = 13;
    CHECK(sof_regex_find(&re, &s, &position, &offset, &length) == SOF_REGEX_NO_MATCH);
    CHECK(position == 13);
    position = SIZE_MAX;
    CHECK(sof_regex_find(&re, &s, &position, &offset, &length) == SOF_REGEX_NO_MATCH);
    CHECK(position == SIZE_MAX);
    // Once the matches run out, they stay out.
    position = 8;
    CHECK(sof_regex_find(&re, &s, &position, NULL, NULL) == SOF_REGEX_OK);
    CHECK(position == 13);
    CHECK(sof_regex_find(&re, &s, &position, NULL, NULL) == SOF_REGEX_NO_MATCH);
    CHECK(sof_regex_find(&re, &s, &position, NULL, NULL) == SOF_REGEX_NO_MATCH);
    CHECK(position == 13);

    // The offset and length can be left out.
    size_t count = 0;
    position = 0;
    while (sof_regex_find(&re, &s, &position, NULL, NULL) == SOF_REGEX_OK) {
        count++;
    }
    CHECK(count == 3);

    // One regex can search two strings in turn, since the position is the
    // whole state of a search.
    sof_string_t t = sof_string_const("x y");
    size_t s_position = 0, t_position = 0, s_offset, t_offset;
    CHECK(sof_regex_find(&re, &s, &s_position, &s_offset, NULL) == SOF_REGEX_OK);
    CHECK(sof_regex_find(&re, &t, &t_position, &t_offset, NULL) == SOF_REGEX_OK);
    CHECK(sof_regex_find(&re, &s, &s_position, &s_offset, NULL) == SOF_REGEX_OK);
    CHECK(sof_regex_find(&re, &t, &t_position, &t_offset, NULL) == SOF_REGEX_OK);
    CHECK(s_offset == 4 && t_offset == 2);
    CHECK(sof_regex_find(&re, &t, &t_position, &t_offset, NULL) == SOF_REGEX_NO_MATCH);
    CHECK(sof_regex_find(&re, &s, &s_position, &s_offset, NULL) == SOF_REGEX_OK);
    CHECK(s_offset == 8);

    // A dead subject is empty.
    sof_string_t dead = {0};
    sof_regex_t empty;
    position = 0;
    CHECK(sof_regex_find(&re, &dead, &position, NULL, NULL) == SOF_REGEX_NO_MATCH);
    CHECK(regex_init(&empty, "x*", NULL) == SOF_REGEX_OK);
    CHECK(sof_regex_find(&empty, &dead, &position, &offset, &length) == SOF_REGEX_OK);
    CHECK(offset == 0 && length == 0 && position == 1);
    CHECK(sof_regex_find(&empty, &dead, &position, &offset, &length) == SOF_REGEX_NO_MATCH);
    sof_regex_free(&empty);

    // NULLs and a dead regex are errors, and leave position alone.
    position = 2;
    CHECK(sof_regex_find(&re, NULL, &position, &offset, &length) == SOF_REGEX_ERROR);
    CHECK(sof_regex_find(NULL, &s, &position, &offset, &length) == SOF_REGEX_ERROR);
    CHECK(sof_regex_find(&re, &s, NULL, &offset, &length) == SOF_REGEX_ERROR);
    CHECK(sof_regex_find(&empty, &s, &position, &offset, &length) == SOF_REGEX_ERROR);
    CHECK(position == 2);
    sof_regex_free(&re);
    CHECK(sof_regex_find(&re, &s, &position, &offset, &length) == SOF_REGEX_ERROR);
    CHECK(position == 2);
}

static void test_split(void) {
#define CASE(text, regex, fields) {text, sizeof(text) - 1, regex, fields}
    static const struct {
        const char *text;
        size_t length;
        const char *regex, *fields;
    } cases[] = {
        CASE("ab,cd,ef", ",", "[ab][cd][ef]"),
        CASE("ab, cd, ef", ", ", "[ab][cd][ef]"),
        CASE("ab, cd,ef ,  gh", "\\s*,\\s*", "[ab][cd][ef][gh]"),
        CASE("a1b22c333", "\\d+", "[a][b][c][]"),
        CASE("ab,,cd", ",", "[ab][][cd]"),
        CASE(",ab", ",", "[][ab]"),
        CASE("ab,", ",", "[ab][]"),
        CASE(",", ",", "[][]"),
        CASE(",,", ",", "[][][]"),
        CASE("ab", ",", "[ab]"),
        CASE("", ",", "[]"),
        CASE("", "x*", "[]"),
        CASE("a  b", " +", "[a][b]"),
        CASE("  ", " ", "[][][]"),
        // A delimiter that can be empty splits between bytes.
        CASE("abc", "x*", "[a][b][c][]"),
        CASE("ab", "", "[a][b][]"),
        CASE("a,b", "(?=,)", "[a][,b]"),
        CASE("a-b_c", "[-_]", "[a][b][c]"),
        CASE("a\0b", "\\x00", "[a][b]"),
        CASE("\0\0", "\\x00", "[][][]"),
        CASE("one\r\ntwo\nthree", "\\r?\\n", "[one][two][three]"),
        CASE("aXbxc", "(?i)x", "[a][b][c]"),
    };
#undef CASE
    text_t fields;
    for (size_t i = 0; i < LENGTH(cases); i++) {
        CHECK(split_all(cases[i].text, cases[i].length, cases[i].regex, &fields) == SOF_REGEX_NO_MATCH);
        if (strcmp(fields.data, cases[i].fields) != 0) {
            printf("FAILED\n    split on /%s/ in case %zu: got %s, expected %s\n", cases[i].regex, i, fields.data, cases[i].fields);
            test_failure_count++;
            return;
        }
    }
}

static void test_split_position(void) {
    sof_string_t s = sof_string_const("a,b");
    sof_regex_t re;
    size_t position = 0, offset, length;
    CHECK(regex_init(&re, ",", NULL) == SOF_REGEX_OK);
    CHECK(sof_regex_split(&re, &s, &position, &offset, &length) == SOF_REGEX_OK);
    CHECK(offset == 0 && length == 1 && position == 2);
    CHECK(sof_regex_split(&re, &s, &position, &offset, &length) == SOF_REGEX_OK);
    CHECK(offset == 2 && length == 1);
    // After the last field, position is past the end, and stays there.
    CHECK(position == 4);
    CHECK(sof_regex_split(&re, &s, &position, &offset, &length) == SOF_REGEX_NO_MATCH);
    CHECK(sof_regex_split(&re, &s, &position, &offset, &length) == SOF_REGEX_NO_MATCH);
    CHECK(position == 4);
    // Starting again is setting position back to 0.
    position = 0;
    CHECK(sof_regex_split(&re, &s, &position, NULL, NULL) == SOF_REGEX_OK);
    CHECK(position == 2);
    // At the very end there is one more field, which is empty.
    position = 3;
    CHECK(sof_regex_split(&re, &s, &position, &offset, &length) == SOF_REGEX_OK);
    CHECK(offset == 3 && length == 0 && position == 4);

    // A dead subject is one empty field.
    sof_string_t dead = {0};
    position = 0;
    CHECK(sof_regex_split(&re, &dead, &position, &offset, &length) == SOF_REGEX_OK);
    CHECK(offset == 0 && length == 0 && position == 1);
    CHECK(sof_regex_split(&re, &dead, &position, &offset, &length) == SOF_REGEX_NO_MATCH);

    position = 1;
    CHECK(sof_regex_split(&re, NULL, &position, &offset, &length) == SOF_REGEX_ERROR);
    CHECK(sof_regex_split(NULL, &s, &position, &offset, &length) == SOF_REGEX_ERROR);
    CHECK(sof_regex_split(&re, &s, NULL, &offset, &length) == SOF_REGEX_ERROR);
    CHECK(position == 1);
    sof_regex_free(&re);
    CHECK(sof_regex_split(&re, &s, &position, &offset, &length) == SOF_REGEX_ERROR);
    CHECK(position == 1);
}

static void test_random(void) {
    // Random strings of a, b and commas, with the matches of "b+" and the
    // fields between commas worked out by hand.
    uint64_t random = 0xabc;
    char bytes[64];
    sof_regex_t runs, commas;
    CHECK(regex_init(&runs, "b+", NULL) == SOF_REGEX_OK);
    CHECK(regex_init(&commas, ",", NULL) == SOF_REGEX_OK);
    for (int i = 0; i < 5000; i++) {
        size_t length = next_random(&random) % sizeof(bytes);
        for (size_t j = 0; j < length; j++) {
            bytes[j] = "ab,"[next_random(&random) % 3];
        }
        sof_string_t s = sof_string_view(bytes, length);
        size_t position = 0, offset, match_length, expected = 0;
        while (sof_regex_find(&runs, &s, &position, &offset, &match_length) == SOF_REGEX_OK) {
            while (expected < length && bytes[expected] != 'b') {
                expected++;
            }
            CHECK(offset == expected);
            while (expected < length && bytes[expected] == 'b') {
                expected++;
            }
            CHECK(match_length == expected - offset);
        }
        while (expected < length) {
            CHECK(bytes[expected++] != 'b');
        }
        // The fields and the commas between them make up the string.
        position = 0;
        expected = 0;
        size_t fields = 0, delimiters = 0;
        while (sof_regex_split(&commas, &s, &position, &offset, &match_length) == SOF_REGEX_OK) {
            CHECK(offset == expected);
            CHECK(memchr(bytes + offset, ',', match_length) == NULL);
            expected = offset + match_length;
            if (expected < length) {
                CHECK(bytes[expected] == ',');
                expected++;
            }
            fields++;
        }
        for (size_t j = 0; j < length; j++) {
            delimiters += bytes[j] == ',';
        }
        CHECK(fields == delimiters + 1);
        CHECK(expected == length);
    }
    sof_regex_free(&runs);
    sof_regex_free(&commas);
}

static void test_match_error(void) {
    // A match that runs into PCRE2's match limit, set low in the pattern,
    // with the optimizations that would see there is no "c" turned off.
    sof_string_t s = sof_string_const("abababababababababababababababababababab");
    sof_regex_t re;
    size_t position = 3, offset = 99, length = 99;
    CHECK(regex_init(&re, "(*NO_START_OPT)(*LIMIT_MATCH=10)(?:a|b)*c", NULL) == SOF_REGEX_OK);
    CHECK(sof_regex_find(&re, &s, &position, &offset, &length) == SOF_REGEX_MATCH_ERROR);
    CHECK(position == 3 && offset == 99 && length == 99);
    CHECK(sof_regex_split(&re, &s, &position, &offset, &length) == SOF_REGEX_MATCH_ERROR);
    CHECK(position == 3 && offset == 99 && length == 99);
    sof_regex_free(&re);
    // The heap limit too.
    CHECK(regex_init(&re, "(*NO_START_OPT)(*LIMIT_HEAP=1)(a|b(?:x|y))*c", NULL) == SOF_REGEX_OK);
    sof_string_t big;
    CHECK(sof_string_init(&big, 0, NULL) == SOF_STRING_OK);
    for (int i = 0; i < 2000; i++) {
        CHECK(sof_string_append_cstr(&big, "a") == SOF_STRING_OK);
    }
    position = 0;
    CHECK(sof_regex_find(&re, &big, &position, NULL, NULL) == SOF_REGEX_MATCH_ERROR);
    sof_string_free(&big);
    sof_regex_free(&re);
}

static void test_large(void) {
    // A long string of numbered words, searched and split.
    sof_string_t s;
    sof_regex_t numbers, spaces;
    char word[32];
    const size_t words = 100000;
    CHECK(sof_string_init(&s, 0, NULL) == SOF_STRING_OK);
    for (size_t i = 0; i < words; i++) {
        snprintf(word, sizeof(word), "w%zu ", i);
        CHECK(sof_string_append_cstr(&s, word) == SOF_STRING_OK);
    }
    CHECK(regex_init(&numbers, "\\d+", NULL) == SOF_REGEX_OK);
    CHECK(regex_init(&spaces, " ", NULL) == SOF_REGEX_OK);
    size_t position = 0, offset, length, count = 0;
    while (sof_regex_find(&numbers, &s, &position, &offset, &length) == SOF_REGEX_OK) {
        snprintf(word, sizeof(word), "%zu", count);
        CHECK(length == strlen(word) && memcmp(s.data + offset, word, length) == 0);
        count++;
    }
    CHECK(count == words);
    position = 0;
    count = 0;
    while (sof_regex_split(&spaces, &s, &position, &offset, &length) == SOF_REGEX_OK) {
        if (count < words) {
            snprintf(word, sizeof(word), "w%zu", count);
            CHECK(length == strlen(word) && memcmp(s.data + offset, word, length) == 0);
        } else {
            CHECK(length == 0);
        }
        count++;
    }
    CHECK(count == words + 1);
    sof_regex_free(&numbers);
    sof_regex_free(&spaces);
    sof_string_free(&s);
}

static void test_custom_allocator(void) {
    test_alloc_t state = {0};
    sof_alloc_t alloc = test_allocator(&state);
    sof_string_t s = sof_string_const("abc def");
    sof_regex_t re;
    // PCRE2 gets its memory from the regex's allocator, for compiling and
    // for matching, and the regex keeps a copy of the allocator.
    CHECK(regex_init(&re, "(\\w)+", &alloc) == SOF_REGEX_OK);
    CHECK(state.allocations > 1);
    alloc.malloc = NULL;
    size_t before = state.allocations, position = 0;
    CHECK(sof_regex_find(&re, &s, &position, NULL, NULL) == SOF_REGEX_OK);
    // The machine code from sof_regex_jit() runs on the thread's stack, and
    // allocates nothing.
    if (jit && sof_regex_jit(&re) == SOF_REGEX_OK) {
        CHECK(state.allocations == before);
    } else {
        CHECK(state.allocations > before);
    }
    CHECK(sof_regex_free(&re) == SOF_REGEX_OK);
    CHECK(state.live == 0);
    CHECK(state.bad == 0);

    // An allocator with only malloc and free, without sizes or realloc.
    alloc = test_allocator(&state);
    alloc.free_sized = NULL;
    alloc.free = test_alloc_free;
    alloc.realloc = NULL;
    CHECK(regex_init(&re, "e", &alloc) == SOF_REGEX_OK);
    position = 0;
    CHECK(sof_regex_find(&re, &s, &position, NULL, NULL) == SOF_REGEX_OK);
    CHECK(position == 6);
    sof_regex_free(&re);
    CHECK(state.live == 0);
    CHECK(state.bad == 0);

    // One with a malloc but no free is refused, without allocating.
    alloc = test_allocator(&state);
    alloc.free_sized = NULL;
    before = state.allocations;
    CHECK(regex_init(&re, "x", &alloc) == SOF_REGEX_ERROR);
    CHECK(re.code == NULL);
    CHECK(state.allocations == before);
}

static void test_no_memory(void) {
    // Fail each allocation in turn, PCRE2's included, until a run makes
    // none fail. Whatever fails must report SOF_REGEX_NO_MEM, leave things
    // as documented, and leak nothing.
    sof_string_t s = sof_string_const("one, two,three");
    size_t compile_failures = 0, match_failures = 0;
    for (size_t n = 1;; n++) {
        test_alloc_t state = {0};
        state.fail_at = n;
        sof_alloc_t alloc = test_allocator(&state);
        sof_regex_t words, commas;
        sof_regex_error_t rc;
        int failed = 0;
        size_t position, count;

        rc = regex_init(&words, "(\\w)+", &alloc);
        CHECK(rc == SOF_REGEX_OK || (rc == SOF_REGEX_NO_MEM && words.code == NULL && words.memory == NULL));
        failed |= rc != SOF_REGEX_OK;
        compile_failures += rc != SOF_REGEX_OK;
        rc = regex_init(&commas, "\\s*,\\s*", &alloc);
        CHECK(rc == SOF_REGEX_OK || (rc == SOF_REGEX_NO_MEM && commas.code == NULL && commas.memory == NULL));
        failed |= rc != SOF_REGEX_OK;
        if (words.code != NULL) {
            // A search that couldn't get memory can be tried again.
            position = 0;
            count = 0;
            while ((rc = sof_regex_find(&words, &s, &position, NULL, NULL)) != SOF_REGEX_NO_MATCH) {
                CHECK(rc == SOF_REGEX_OK || rc == SOF_REGEX_NO_MEM);
                failed |= rc != SOF_REGEX_OK;
                match_failures += rc != SOF_REGEX_OK;
                count += rc == SOF_REGEX_OK;
            }
            CHECK(count == 3);
        }
        if (commas.code != NULL) {
            position = 0;
            count = 0;
            while ((rc = sof_regex_split(&commas, &s, &position, NULL, NULL)) != SOF_REGEX_NO_MATCH) {
                CHECK(rc == SOF_REGEX_OK || rc == SOF_REGEX_NO_MEM);
                failed |= rc != SOF_REGEX_OK;
                count += rc == SOF_REGEX_OK;
            }
            CHECK(count == 3);
        }
        // Only the chosen allocation fails. sof_regex_jit() allocates while
        // compiling too, but if that fails the regex is left for the
        // interpreter, and nothing is reported.
        if (jit) {
            CHECK(!failed || state.allocations >= n);
        } else {
            CHECK(failed == (state.allocations >= n));
        }
        CHECK(sof_regex_free(&words) == SOF_REGEX_OK);
        CHECK(sof_regex_free(&commas) == SOF_REGEX_OK);
        CHECK(state.live == 0);
        CHECK(state.bad == 0);
        if (state.allocations < n) {
            break;
        }
    }
    // PCRE2's own allocations were among those that failed: several while
    // compiling, and the one for the match's frames.
    CHECK(compile_failures > 2);
    CHECK(jit || match_failures > 0);
}

static void test_jit(void) {
    // sof_regex_jit() refuses a NULL or dead regex, and can be called more
    // than once.
    sof_regex_t re, plain, fast;
    // sof_regex_jit_size() is 0 without machine code, and more with it.
    CHECK(sof_regex_jit(NULL) == SOF_REGEX_ERROR);
    CHECK(sof_regex_jit_size(NULL) == 0);
    memset(&re, 0, sizeof(re));
    CHECK(sof_regex_jit(&re) == SOF_REGEX_ERROR);
    CHECK(sof_regex_jit_size(&re) == 0);
    CHECK(sof_regex_init(&re, "a+", NULL) == SOF_REGEX_OK);
    CHECK(sof_regex_jit_size(&re) == 0);
    sof_regex_error_t rc = sof_regex_jit(&re);
    CHECK(rc == SOF_REGEX_OK || rc == SOF_REGEX_NO_MATCH);
    size_t size = sof_regex_jit_size(&re);
    CHECK(rc == SOF_REGEX_OK ? size > 0 : size == 0);
    CHECK(sof_regex_jit(&re) == rc);
    CHECK(sof_regex_jit_size(&re) == size);
    // A longer pattern takes more machine code.
    if (rc == SOF_REGEX_OK) {
        sof_regex_t big;
        CHECK(sof_regex_init(&big, "(?i)(king|queen|prince|duke|earl)\\s+of\\s+[A-Z][a-z]+", NULL) == SOF_REGEX_OK);
        CHECK(sof_regex_jit(&big) == SOF_REGEX_OK);
        CHECK(sof_regex_jit_size(&big) > size);
        sof_regex_free(&big);
    }
    CHECK(sof_regex_free(&re) == SOF_REGEX_OK);
    CHECK(sof_regex_jit(&re) == SOF_REGEX_ERROR);
    CHECK(sof_regex_jit_size(&re) == 0);

    // With and without it, every pattern finds the same matches and fields
    // in random text of letters, digits, spaces, punctuation and newlines.
    static const char *patterns[] = {
        "love", "[A-Z][a-z]+ing\\b", "\\b(\\w+)\\s+\\1\\b", "(?i)king|queen|prince", "\\d+",
        "x*", "", "(?m)^\\w", "\\s*,\\s*", "(?<=a)b|c(?=d)", "[^a-z]+$",
    };
    static const char alphabet[] = "abcdKQlove ing,\n12 Ab.";
    uint64_t random = 0x5eed;
    char bytes[300];
    for (size_t p = 0; p < LENGTH(patterns); p++) {
        CHECK(sof_regex_init(&plain, patterns[p], NULL) == SOF_REGEX_OK);
        CHECK(sof_regex_init(&fast, patterns[p], NULL) == SOF_REGEX_OK);
        rc = sof_regex_jit(&fast);
        CHECK(rc == SOF_REGEX_OK || rc == SOF_REGEX_NO_MATCH);
        for (int i = 0; i < 200; i++) {
            size_t length = next_random(&random) % sizeof(bytes);
            for (size_t j = 0; j < length; j++) {
                bytes[j] = alphabet[next_random(&random) % (sizeof(alphabet) - 1)];
            }
            sof_string_t s = sof_string_view(bytes, length);
            size_t a_position = 0, b_position = 0, a_offset = 0, b_offset = 0, a_length = 0, b_length = 0;
            sof_regex_error_t a, b;
            do {
                a = sof_regex_find(&plain, &s, &a_position, &a_offset, &a_length);
                b = sof_regex_find(&fast, &s, &b_position, &b_offset, &b_length);
                CHECK(a == b && a_position == b_position && a_offset == b_offset && a_length == b_length);
            } while (a == SOF_REGEX_OK);
            a_position = b_position = 0;
            do {
                a = sof_regex_split(&plain, &s, &a_position, &a_offset, &a_length);
                b = sof_regex_split(&fast, &s, &b_position, &b_offset, &b_length);
                CHECK(a == b && a_position == b_position && a_offset == b_offset && a_length == b_length);
            } while (a == SOF_REGEX_OK);
        }
        sof_regex_free(&plain);
        sof_regex_free(&fast);
    }

    // A match that needs more than the machine code's 32 KiB of stack is run
    // again without it, and found all the same.
    sof_string_t big;
    CHECK(sof_string_init(&big, 0, NULL) == SOF_STRING_OK);
    for (int i = 0; i < 5000; i++) {
        CHECK(sof_string_append_cstr(&big, "ab") == SOF_STRING_OK);
    }
    CHECK(sof_regex_init(&re, "((a)|(b))*$", NULL) == SOF_REGEX_OK);
    sof_regex_jit(&re);
    size_t position = 0, offset = 99, length = 0;
    CHECK(sof_regex_find(&re, &big, &position, &offset, &length) == SOF_REGEX_OK);
    CHECK(offset == 0 && length == big.length);
    sof_regex_free(&re);
    sof_string_free(&big);
}

static void test_threads(void) {
    // Threads search and split one shared string, which they only read,
    // each with regexes of its own.
    sof_string_t s;
    char word[32];
    const size_t words = 2000;
    CHECK(sof_string_init(&s, 0, NULL) == SOF_STRING_OK);
    for (size_t i = 0; i < words; i++) {
        snprintf(word, sizeof(word), i == 0 ? "w%zu" : " w%zu", i);
        CHECK(sof_string_append_cstr(&s, word) == SOF_STRING_OK);
    }
    pthread_t threads[THREADS];
    job_t jobs[THREADS];
    for (int i = 0; i < THREADS; i++) {
        memset(&jobs[i], 0, sizeof(jobs[i]));
        jobs[i].subject = &s;
        jobs[i].matches = words;
        jobs[i].fields = words;
        CHECK(pthread_create(&threads[i], NULL, search_thread, &jobs[i]) == 0);
    }
    for (int i = 0; i < THREADS; i++) {
        CHECK(pthread_join(threads[i], NULL) == 0);
    }
    for (int i = 0; i < THREADS; i++) {
        CHECK(jobs[i].ok);
        CHECK(jobs[i].state.live == 0);
        CHECK(jobs[i].state.bad == 0);
    }
    sof_string_free(&s);
}

/// @brief Runs every test once.
static void run_all(void) {
    run("init and free", test_init);
    run("find", test_find);
    run("find positions", test_find_position);
    run("split", test_split);
    run("split positions", test_split_position);
    run("random find and split", test_random);
    run("match errors", test_match_error);
    run("large string", test_large);
    run("custom allocator", test_custom_allocator);
    run("each allocation failing", test_no_memory);
    run("sof_regex_jit", test_jit);
    run("threads sharing a string", test_threads);
}

int main(void) {
    printf("sof_regex:\n");
    run_all();
    printf("sof_regex with sof_regex_jit():\n");
    jit = 1;
    run_all();
    return test_report();
}
