/**
 * @file test.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief Tests for sof_string.c.
 * @details Usage: test. Runs every test, and exits with 0 if they all pass
 * or 1 if any fails. The Makefile's check target builds and runs it.
 * @version 0.0.1
 * @date 2026-11-04
 * @copyright Copyright (c) 2026
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "sof_alloc.h"
#include "sof_string.h"
#include "test.h"

/// @brief The number of elements in an array.
#define LENGTH(a) (sizeof(a) / sizeof((a)[0]))

/// @brief The most bytes the random edits test lets its string reach.
#define MODEL_MAX 4096

static int casecmp_is(const sof_string_t *a, const sof_string_t *b, int expected);
static int fold_byte(int c);

static void test_init(void) {
    sof_string_t s;
    CHECK(sof_string_init(&s, 0, NULL) == SOF_STRING_OK);
    CHECK(s.data != NULL && s.data[0] == '\0');
    CHECK(s.length == 0 && s.size == 1);
    CHECK(strcmp(sof_string_cstr(&s), "") == 0);
    CHECK(sof_string_free(&s) == SOF_STRING_OK);
    CHECK(s.data == NULL && s.length == 0 && s.size == 0);

    CHECK(sof_string_init(&s, 100, NULL) == SOF_STRING_OK);
    CHECK(s.length == 0 && s.size == 101);
    // Filling the capacity doesn't grow it.
    for (int i = 0; i < 100; i++) {
        CHECK(sof_string_append_cstr(&s, "x") == SOF_STRING_OK);
    }
    CHECK(s.length == 100 && s.size == 101);
    CHECK(sof_string_append_cstr(&s, "x") == SOF_STRING_OK);
    CHECK(s.length == 101 && s.size > 101);
    sof_string_free(&s);

    // A capacity with no room for the NUL fails, leaving the string dead.
    memset(&s, 0xa5, sizeof(s));
    CHECK(sof_string_init(&s, SIZE_MAX, NULL) == SOF_STRING_NO_MEM);
    CHECK(s.data == NULL && s.length == 0 && s.size == 0);
    CHECK(sof_string_init(NULL, 10, NULL) == SOF_STRING_ERROR);
}

static void test_from_cstr(void) {
    sof_string_t s;
    CHECK(sof_string_from_cstr(&s, "hello", NULL) == SOF_STRING_OK);
    CHECK(s.length == 5 && s.size == 6);
    CHECK(memcmp(s.data, "hello", 6) == 0);
    sof_string_free(&s);
    CHECK(sof_string_from_cstr(&s, "", NULL) == SOF_STRING_OK);
    CHECK(s.length == 0 && s.size == 1 && s.data[0] == '\0');
    sof_string_free(&s);
    // Only the bytes up to the first NUL are copied.
    CHECK(sof_string_from_cstr(&s, "ab\0cd", NULL) == SOF_STRING_OK);
    CHECK(s.length == 2);
    sof_string_free(&s);

    memset(&s, 0xa5, sizeof(s));
    CHECK(sof_string_from_cstr(&s, NULL, NULL) == SOF_STRING_ERROR);
    CHECK(s.data == NULL && s.size == 0);
    CHECK(sof_string_from_cstr(NULL, "x", NULL) == SOF_STRING_ERROR);
}

static void test_from_bytes(void) {
    sof_string_t s;
    // Any bytes, NULs included, and only as many as asked for.
    CHECK(sof_string_from_bytes(&s, "a\0b\xff" "cd", 4, NULL) == SOF_STRING_OK);
    CHECK(s.length == 4 && s.size == 5);
    CHECK(memcmp(s.data, "a\0b\xff", 5) == 0);
    sof_string_free(&s);
    // No bytes, from NULL or not, make an empty string.
    CHECK(sof_string_from_bytes(&s, NULL, 0, NULL) == SOF_STRING_OK);
    CHECK(s.length == 0 && s.size == 1 && s.data[0] == '\0');
    CHECK(sof_string_append_cstr(&s, "more") == SOF_STRING_OK);
    CHECK(strcmp(s.data, "more") == 0);
    sof_string_free(&s);
    CHECK(sof_string_from_bytes(&s, "x", 0, NULL) == SOF_STRING_OK);
    CHECK(s.length == 0 && s.data[0] == '\0');
    sof_string_free(&s);

    memset(&s, 0xa5, sizeof(s));
    CHECK(sof_string_from_bytes(&s, NULL, 1, NULL) == SOF_STRING_ERROR);
    CHECK(s.data == NULL && s.length == 0 && s.size == 0);
    CHECK(sof_string_from_bytes(NULL, "x", 1, NULL) == SOF_STRING_ERROR);
}

static void test_view(void) {
    // Part of a buffer, with no NUL after it: only its length bytes count.
    char buffer[] = {'W', 'o', 'r', 'd', 's', '!'};
    sof_string_t s = sof_string_view(buffer, 4);
    CHECK(s.data == buffer && s.length == 4 && s.size == 0);
    sof_string_t word = sof_string_const("word");
    CHECK(sof_string_cmp(&s, &word) == -1);
    CHECK(sof_string_casecmp(&s, &word) == 0);

    // Read-only: every change is refused, and the bytes are untouched.
    CHECK(sof_string_append_cstr(&s, "x") == SOF_STRING_ERROR);
    CHECK(sof_string_append_bytes(&s, "x", 1) == SOF_STRING_ERROR);
    CHECK(sof_string_clear(&s) == SOF_STRING_ERROR);
    CHECK(sof_string_delete(&s, 0, 1) == SOF_STRING_ERROR);
    CHECK(sof_string_tolower(&s) == SOF_STRING_ERROR);
    CHECK(sof_string_toupper(&s) == SOF_STRING_ERROR);
    CHECK(memcmp(buffer, "Words!", 6) == 0);

    // It can be copied and appended, and the copies end in a NUL.
    sof_string_t copy, part, target;
    CHECK(sof_string_dup(&copy, &s, NULL) == SOF_STRING_OK);
    CHECK(strcmp(copy.data, "Word") == 0);
    CHECK(sof_string_substr(&part, &s, 1, 3, NULL) == SOF_STRING_OK);
    CHECK(strcmp(part.data, "ord") == 0);
    CHECK(sof_string_init(&target, 0, NULL) == SOF_STRING_OK);
    CHECK(sof_string_append(&target, &s) == SOF_STRING_OK);
    CHECK(strcmp(target.data, "Word") == 0);
    sof_string_free(&copy);
    sof_string_free(&part);
    sof_string_free(&target);

    // Freeing empties it without freeing the bytes.
    CHECK(sof_string_free(&s) == SOF_STRING_OK);
    CHECK(s.data == NULL && s.length == 0);
    CHECK(buffer[0] == 'W');

    // NULL is empty, whatever the length, and can still be copied.
    sof_string_t none = sof_string_view(NULL, 5);
    CHECK(none.data != NULL && none.length == 0 && none.size == 0);
    CHECK(strcmp(sof_string_cstr(&none), "") == 0);
    CHECK(sof_string_dup(&copy, &none, NULL) == SOF_STRING_OK);
    CHECK(copy.length == 0);
    sof_string_free(&copy);
    sof_string_t empty = sof_string_view(buffer, 0);
    CHECK(empty.length == 0 && sof_string_cmp(&empty, &none) == 0);
}

static void test_const(void) {
    sof_string_t s = sof_string_const("a\0b");
    CHECK(s.length == 3 && s.size == 0);
    CHECK(memcmp(s.data, "a\0b", 4) == 0);
    sof_string_t empty = sof_string_const("");
    CHECK(empty.length == 0 && empty.data[0] == '\0');

    // Read-only: every change is refused, and the literal is untouched.
    sof_string_t word = sof_string_const("Word");
    CHECK(sof_string_append(&word, &s) == SOF_STRING_ERROR);
    CHECK(sof_string_append_cstr(&word, "x") == SOF_STRING_ERROR);
    CHECK(sof_string_clear(&word) == SOF_STRING_ERROR);
    CHECK(sof_string_delete(&word, 0, 1) == SOF_STRING_ERROR);
    CHECK(sof_string_tolower(&word) == SOF_STRING_ERROR);
    CHECK(sof_string_toupper(&word) == SOF_STRING_ERROR);
    CHECK(word.length == 4 && strcmp(word.data, "Word") == 0);

    // It can be read, copied and appended to another string.
    sof_string_t copy, part, target;
    CHECK(strcmp(sof_string_cstr(&word), "Word") == 0);
    CHECK(sof_string_dup(&copy, &s, NULL) == SOF_STRING_OK);
    CHECK(copy.length == 3 && memcmp(copy.data, "a\0b", 4) == 0 && copy.size == 4);
    CHECK(sof_string_cmp(&copy, &s) == 0);
    CHECK(sof_string_substr(&part, &word, 1, 2, NULL) == SOF_STRING_OK);
    CHECK(strcmp(part.data, "or") == 0);
    CHECK(sof_string_init(&target, 0, NULL) == SOF_STRING_OK);
    CHECK(sof_string_append(&target, &word) == SOF_STRING_OK);
    CHECK(sof_string_append(&target, &s) == SOF_STRING_OK);
    CHECK(target.length == 7 && memcmp(target.data, "Worda\0b", 8) == 0);
    sof_string_free(&copy);
    sof_string_free(&part);
    sof_string_free(&target);

    // Freeing empties it without freeing the literal.
    CHECK(sof_string_free(&word) == SOF_STRING_OK);
    CHECK(word.data == NULL && word.length == 0);
}

static void test_dup(void) {
    sof_string_t a = sof_string_const("x\0y\xff");
    sof_string_t s, copy;
    CHECK(sof_string_init(&s, 50, NULL) == SOF_STRING_OK);
    CHECK(sof_string_append(&s, &a) == SOF_STRING_OK);
    CHECK(sof_string_dup(&copy, &s, NULL) == SOF_STRING_OK);
    CHECK(copy.length == 4 && copy.size == 5);
    CHECK(copy.data != s.data);
    CHECK(memcmp(copy.data, s.data, 5) == 0);
    // The two don't share memory.
    copy.data[0] = 'z';
    CHECK(s.data[0] == 'x');
    sof_string_free(&copy);

    sof_string_t empty;
    CHECK(sof_string_init(&empty, 0, NULL) == SOF_STRING_OK);
    CHECK(sof_string_dup(&copy, &empty, NULL) == SOF_STRING_OK);
    CHECK(copy.length == 0 && copy.data[0] == '\0');
    sof_string_free(&copy);
    sof_string_free(&empty);

    // A string can't be duplicated into itself, and is left alone.
    CHECK(sof_string_dup(&s, &s, NULL) == SOF_STRING_ERROR);
    CHECK(s.length == 4 && memcmp(s.data, "x\0y\xff", 5) == 0);
    sof_string_free(&s);

    memset(&copy, 0xa5, sizeof(copy));
    CHECK(sof_string_dup(&copy, NULL, NULL) == SOF_STRING_ERROR);
    CHECK(copy.data == NULL);
    memset(&copy, 0xa5, sizeof(copy));
    CHECK(sof_string_dup(&copy, &s, NULL) == SOF_STRING_ERROR);
    CHECK(copy.data == NULL);
    CHECK(sof_string_dup(NULL, &a, NULL) == SOF_STRING_ERROR);
}

static void test_substr(void) {
    sof_string_t s = sof_string_const("0123\0" "56789");
    sof_string_t part;
    // Every part that lies within the string.
    for (size_t offset = 0; offset <= s.length; offset++) {
        for (size_t length = 0; offset + length <= s.length; length++) {
            CHECK(sof_string_substr(&part, &s, offset, length, NULL) == SOF_STRING_OK);
            CHECK(part.length == length && part.size == length + 1);
            CHECK(memcmp(part.data, s.data + offset, length) == 0);
            CHECK(part.data[length] == '\0');
            sof_string_free(&part);
        }
        // And every one that runs past its end.
        for (size_t length = s.length - offset + 1; length <= s.length + 2; length++) {
            memset(&part, 0xa5, sizeof(part));
            CHECK(sof_string_substr(&part, &s, offset, length, NULL) == SOF_STRING_ERROR);
            CHECK(part.data == NULL);
        }
    }
    CHECK(sof_string_substr(&part, &s, s.length + 1, 0, NULL) == SOF_STRING_ERROR);
    // Sizes whose sum overflows.
    CHECK(sof_string_substr(&part, &s, 1, SIZE_MAX, NULL) == SOF_STRING_ERROR);
    CHECK(sof_string_substr(&part, &s, SIZE_MAX, 2, NULL) == SOF_STRING_ERROR);
    CHECK(sof_string_substr(&part, &s, SIZE_MAX, SIZE_MAX, NULL) == SOF_STRING_ERROR);

    sof_string_t own;
    CHECK(sof_string_from_cstr(&own, "abc", NULL) == SOF_STRING_OK);
    CHECK(sof_string_substr(&own, &own, 0, 1, NULL) == SOF_STRING_ERROR);
    CHECK(strcmp(own.data, "abc") == 0);
    sof_string_free(&own);
    CHECK(sof_string_substr(&part, NULL, 0, 0, NULL) == SOF_STRING_ERROR);
    CHECK(sof_string_substr(&part, &own, 0, 0, NULL) == SOF_STRING_ERROR);
    CHECK(sof_string_substr(NULL, &s, 0, 0, NULL) == SOF_STRING_ERROR);
}

static void test_append(void) {
    sof_string_t s, other;
    CHECK(sof_string_init(&s, 0, NULL) == SOF_STRING_OK);
    CHECK(sof_string_append_cstr(&s, "ab") == SOF_STRING_OK);
    CHECK(sof_string_append_cstr(&s, "") == SOF_STRING_OK);
    sof_string_t nul = sof_string_const("\0c");
    CHECK(sof_string_append(&s, &nul) == SOF_STRING_OK);
    CHECK(s.length == 4 && memcmp(s.data, "ab\0c", 5) == 0);

    // Appending a string to itself doubles it, again and again.
    for (int i = 0; i < 10; i++) {
        CHECK(sof_string_append(&s, &s) == SOF_STRING_OK);
    }
    CHECK(s.length == 4096);
    for (size_t i = 0; i < s.length; i += 4) {
        CHECK(memcmp(s.data + i, "ab\0c", 4) == 0);
    }
    CHECK(s.data[s.length] == '\0');
    sof_string_free(&s);

    // So does a copy of the struct, which shares the data and is left
    // pointing at the old block once the string grows: only s is used after.
    CHECK(sof_string_from_cstr(&s, "xyz", NULL) == SOF_STRING_OK);
    other = s;
    CHECK(sof_string_append(&s, &other) == SOF_STRING_OK);
    CHECK(strcmp(s.data, "xyzxyz") == 0);
    // A c string inside the string's own data, which moves as it grows:
    // each append adds all but the first byte, 6 to 11 to 21...
    for (int i = 0; i < 8; i++) {
        CHECK(sof_string_append_cstr(&s, s.data + 1) == SOF_STRING_OK);
    }
    CHECK(s.length == 5 * 256 + 1);
    CHECK(strlen(s.data) == s.length);
    sof_string_free(&s);

    // A dead suffix is empty; a NULL one, a dead target or a NULL target
    // are errors.
    sof_string_t dead = {0};
    CHECK(sof_string_from_cstr(&s, "ok", NULL) == SOF_STRING_OK);
    CHECK(sof_string_append(&s, &dead) == SOF_STRING_OK);
    CHECK(s.length == 2);
    CHECK(sof_string_append(&s, NULL) == SOF_STRING_ERROR);
    CHECK(sof_string_append_cstr(&s, NULL) == SOF_STRING_ERROR);
    CHECK(sof_string_append(&dead, &s) == SOF_STRING_ERROR);
    CHECK(sof_string_append_cstr(&dead, "x") == SOF_STRING_ERROR);
    CHECK(dead.data == NULL);
    CHECK(sof_string_append(NULL, &s) == SOF_STRING_ERROR);
    CHECK(sof_string_append_cstr(NULL, "x") == SOF_STRING_ERROR);
    CHECK(strcmp(s.data, "ok") == 0);
    sof_string_free(&s);
}

static void test_append_bytes(void) {
    sof_string_t s;
    CHECK(sof_string_init(&s, 0, NULL) == SOF_STRING_OK);
    // Any bytes, NULs included, and only as many as asked for.
    CHECK(sof_string_append_bytes(&s, "a\0b\xff", 4) == SOF_STRING_OK);
    CHECK(sof_string_append_bytes(&s, "cdef", 1) == SOF_STRING_OK);
    CHECK(s.length == 5 && memcmp(s.data, "a\0b\xff" "c", 6) == 0);
    // No bytes, from NULL or not, change nothing.
    CHECK(sof_string_append_bytes(&s, NULL, 0) == SOF_STRING_OK);
    CHECK(sof_string_append_bytes(&s, "x", 0) == SOF_STRING_OK);
    CHECK(s.length == 5 && s.data[5] == '\0');

    // The string's own bytes, which move as it grows: each append adds all
    // but the first byte, 5 to 9 to 17...
    for (int i = 0; i < 8; i++) {
        CHECK(sof_string_append_bytes(&s, s.data + 1, s.length - 1) == SOF_STRING_OK);
    }
    CHECK(s.length == 4 * 256 + 1);
    CHECK(s.data[0] == 'a' && s.data[s.length] == '\0');
    for (size_t i = 1; i < s.length; i += 4) {
        CHECK(memcmp(s.data + i, "\0b\xff" "c", 4) == 0);
    }
    sof_string_free(&s);

    // NULL bytes with a length, a dead target, a read-only target or a NULL
    // target are errors.
    sof_string_t dead = {0}, word = sof_string_const("Word");
    CHECK(sof_string_from_cstr(&s, "ok", NULL) == SOF_STRING_OK);
    CHECK(sof_string_append_bytes(&s, NULL, 1) == SOF_STRING_ERROR);
    CHECK(sof_string_append_bytes(&dead, "x", 1) == SOF_STRING_ERROR);
    CHECK(sof_string_append_bytes(&dead, NULL, 0) == SOF_STRING_ERROR);
    CHECK(dead.data == NULL);
    CHECK(sof_string_append_bytes(&word, "x", 1) == SOF_STRING_ERROR);
    CHECK(word.length == 4 && strcmp(word.data, "Word") == 0);
    CHECK(sof_string_append_bytes(NULL, "x", 1) == SOF_STRING_ERROR);
    CHECK(strcmp(s.data, "ok") == 0);
    sof_string_free(&s);
}

static void test_append_growth(void) {
    test_alloc_t state = {0};
    sof_alloc_t alloc = test_allocator(&state);
    sof_string_t s;
    CHECK(sof_string_init(&s, 0, &alloc) == SOF_STRING_OK);
    // The string doubles as it grows, so a byte at a time makes only a few
    // dozen allocations.
    for (int i = 0; i < 100000; i++) {
        CHECK(sof_string_append_cstr(&s, "a") == SOF_STRING_OK);
    }
    CHECK(s.length == 100000 && s.size > s.length);
    CHECK(state.allocations <= 20);
    CHECK(state.live == 1);
    // Clearing keeps the memory.
    size_t size = s.size;
    CHECK(sof_string_clear(&s) == SOF_STRING_OK);
    CHECK(s.length == 0 && s.size == size && s.data[0] == '\0');
    CHECK(sof_string_append_cstr(&s, "again") == SOF_STRING_OK);
    CHECK(strcmp(s.data, "again") == 0 && s.size == size);
    CHECK(sof_string_free(&s) == SOF_STRING_OK);
    CHECK(state.live == 0);
    CHECK(state.bad == 0);
}

static void test_append_char(void) {
    sof_string_t s;
    CHECK(sof_string_init(&s, 0, NULL) == SOF_STRING_OK);
    // Byte by byte, through growth, NULs included.
    for (int i = 0; i < 1000; i++) {
        CHECK(sof_string_append_char(&s, (char)(i % 7 == 0 ? '\0' : 'a' + i % 26)) == SOF_STRING_OK);
        CHECK(s.length == (size_t)i + 1 && s.data[s.length] == '\0' && s.length < s.size);
    }
    for (int i = 0; i < 1000; i++) {
        CHECK(s.data[i] == (char)(i % 7 == 0 ? '\0' : 'a' + i % 26));
    }
    CHECK(sof_string_free(&s) == SOF_STRING_OK);
    // A dead, constant or NULL string can't be appended to.
    CHECK(sof_string_append_char(&s, 'x') == SOF_STRING_ERROR);
    sof_string_t k = sof_string_const("k");
    CHECK(sof_string_append_char(&k, 'x') == SOF_STRING_ERROR && k.length == 1);
    sof_string_t v = sof_string_view("vw", 1);
    CHECK(sof_string_append_char(&v, 'x') == SOF_STRING_ERROR && v.length == 1);
    CHECK(sof_string_append_char(NULL, 'x') == SOF_STRING_ERROR);
}

static void test_reserve(void) {
    sof_string_t s;
    CHECK(sof_string_from_cstr(&s, "head", NULL) == SOF_STRING_OK);
    // Room is made after the bytes, and nothing changes until the commit.
    char *p = sof_string_reserve(&s, 100);
    CHECK(p == s.data + 4 && s.length == 4 && s.size >= 105);
    CHECK(strcmp(s.data, "head") == 0);
    memcpy(p, "-tail", 5);
    CHECK(sof_string_commit(&s, 5) == SOF_STRING_OK);
    CHECK(s.length == 9 && strcmp(s.data, "head-tail") == 0);
    // Committing nothing is fine; committing past the room isn't.
    CHECK(sof_string_commit(&s, 0) == SOF_STRING_OK && s.length == 9);
    CHECK(sof_string_commit(&s, s.size - s.length) == SOF_STRING_ERROR && s.length == 9);
    size_t room = s.size - s.length - 1;
    p = sof_string_reserve(&s, room);
    CHECK(p == s.data + 9);
    memset(p, 'z', room);
    CHECK(sof_string_commit(&s, room) == SOF_STRING_OK);
    CHECK(s.length == 9 + room && s.data[s.length] == '\0' && s.length + 1 == s.size);
    // Reserving nothing at all is a pointer to the NUL.
    p = sof_string_reserve(&s, 0);
    CHECK(p == s.data + s.length && *p == '\0');
    // A huge reservation fails, leaving the string alone.
    size_t length = s.length;
    CHECK(sof_string_reserve(&s, SIZE_MAX) == NULL && s.length == length);
    CHECK(sof_string_free(&s) == SOF_STRING_OK);
    // Dead, constant, view and NULL strings have no room.
    CHECK(sof_string_reserve(&s, 1) == NULL);
    CHECK(sof_string_commit(&s, 0) == SOF_STRING_ERROR);
    sof_string_t k = sof_string_const("k");
    CHECK(sof_string_reserve(&k, 1) == NULL);
    CHECK(sof_string_commit(&k, 0) == SOF_STRING_ERROR);
    sof_string_t v = sof_string_view("vw", 1);
    CHECK(sof_string_reserve(&v, 1) == NULL);
    CHECK(sof_string_commit(&v, 0) == SOF_STRING_ERROR);
    CHECK(sof_string_reserve(NULL, 1) == NULL);
    CHECK(sof_string_commit(NULL, 0) == SOF_STRING_ERROR);
}

static void test_clear(void) {
    sof_string_t s, dead = {0};
    CHECK(sof_string_from_cstr(&s, "abc", NULL) == SOF_STRING_OK);
    CHECK(sof_string_clear(&s) == SOF_STRING_OK);
    CHECK(s.length == 0 && s.size == 4 && s.data[0] == '\0');
    CHECK(sof_string_clear(&s) == SOF_STRING_OK);
    sof_string_free(&s);
    CHECK(sof_string_clear(&s) == SOF_STRING_ERROR);
    CHECK(sof_string_clear(&dead) == SOF_STRING_ERROR);
    CHECK(sof_string_clear(NULL) == SOF_STRING_ERROR);
}

static void test_delete(void) {
    static const char original[] = "0123\0" "56789";
    const size_t length = sizeof(original) - 1;
    sof_string_t s;
    char expected[sizeof(original)];
    // Every range within the string, checked against memmove.
    for (size_t offset = 0; offset <= length; offset++) {
        for (size_t count = 0; offset + count <= length; count++) {
            sof_string_t source = sof_string_view(original, length);
            CHECK(sof_string_dup(&s, &source, NULL) == SOF_STRING_OK);
            CHECK(sof_string_delete(&s, offset, count) == SOF_STRING_OK);
            memcpy(expected, original, sizeof(original));
            memmove(expected + offset, expected + offset + count, length - offset - count + 1);
            CHECK(s.length == length - count);
            CHECK(memcmp(s.data, expected, s.length + 1) == 0);
            CHECK(s.size == length + 1);
            sof_string_free(&s);
        }
    }
    // Ranges that don't lie within it change nothing.
    CHECK(sof_string_from_cstr(&s, "abc", NULL) == SOF_STRING_OK);
    CHECK(sof_string_delete(&s, 0, 4) == SOF_STRING_ERROR);
    CHECK(sof_string_delete(&s, 3, 1) == SOF_STRING_ERROR);
    CHECK(sof_string_delete(&s, 4, 0) == SOF_STRING_ERROR);
    CHECK(sof_string_delete(&s, 1, SIZE_MAX) == SOF_STRING_ERROR);
    CHECK(sof_string_delete(&s, SIZE_MAX, 2) == SOF_STRING_ERROR);
    CHECK(sof_string_delete(&s, 3, 0) == SOF_STRING_OK);
    CHECK(strcmp(s.data, "abc") == 0 && s.length == 3);
    sof_string_free(&s);
    CHECK(sof_string_delete(&s, 0, 0) == SOF_STRING_ERROR);
    CHECK(sof_string_delete(NULL, 0, 0) == SOF_STRING_ERROR);
}

static void test_insert(void) {
    static const char original[] = "0123\0" "56789";
    const size_t length = sizeof(original) - 1;
    char expected[3 * sizeof(original)];
    sof_string_t s;
    // Every place, with every part of the string's own bytes: before the
    // place, after it and across it. With room to spare, and growing, when
    // the bytes move with the data.
    for (int roomy = 0; roomy < 2; roomy++) {
        for (size_t offset = 0; offset <= length; offset++) {
            for (size_t from = 0; from <= length; from++) {
                for (size_t count = 0; from + count <= length; count++) {
                    CHECK(sof_string_init(&s, roomy ? 64 : length, NULL) == SOF_STRING_OK);
                    CHECK(sof_string_append_bytes(&s, original, length) == SOF_STRING_OK);
                    size_t size = s.size;
                    CHECK(sof_string_insert_bytes(&s, offset, s.data + from, count) == SOF_STRING_OK);
                    memcpy(expected, original, offset);
                    memcpy(expected + offset, original + from, count);
                    memcpy(expected + offset + count, original + offset, length - offset);
                    expected[length + count] = '\0';
                    CHECK(s.length == length + count);
                    CHECK(memcmp(s.data, expected, s.length + 1) == 0);
                    CHECK(roomy ? s.size == size : count == 0 || s.size >= 2 * size);
                    sof_string_free(&s);
                }
            }
        }
    }
    // Other strings, c strings and the string itself, at the start, the
    // middle and the end.
    sof_string_t other = sof_string_const("<>"), dead = {0};
    CHECK(sof_string_from_cstr(&s, "abc", NULL) == SOF_STRING_OK);
    CHECK(sof_string_insert(&s, 1, &other) == SOF_STRING_OK && strcmp(s.data, "a<>bc") == 0);
    CHECK(sof_string_insert_cstr(&s, 0, "[") == SOF_STRING_OK && strcmp(s.data, "[a<>bc") == 0);
    CHECK(sof_string_insert_cstr(&s, s.length, "]") == SOF_STRING_OK && strcmp(s.data, "[a<>bc]") == 0);
    CHECK(sof_string_insert(&s, 3, &s) == SOF_STRING_OK && strcmp(s.data, "[a<[a<>bc]>bc]") == 0);
    CHECK(sof_string_insert_cstr(&s, 2, s.data + 9) == SOF_STRING_OK && strcmp(s.data, "[a]>bc]<[a<>bc]>bc]") == 0);
    // Nothing to insert changes nothing, even at the end.
    size_t before = s.length;
    CHECK(sof_string_insert(&s, 0, &dead) == SOF_STRING_OK && s.length == before);
    CHECK(sof_string_insert_bytes(&s, s.length, NULL, 0) == SOF_STRING_OK && s.length == before);
    CHECK(sof_string_insert_cstr(&s, 1, "") == SOF_STRING_OK && s.length == before);
    // Out of range, NULL, read-only and dead strings are refused.
    CHECK(sof_string_insert(&s, s.length + 1, &other) == SOF_STRING_ERROR);
    CHECK(sof_string_insert_bytes(&s, s.length + 1, "x", 1) == SOF_STRING_ERROR);
    CHECK(sof_string_insert_cstr(&s, SIZE_MAX, "x") == SOF_STRING_ERROR);
    CHECK(sof_string_insert_bytes(&s, 0, NULL, 1) == SOF_STRING_ERROR);
    CHECK(sof_string_insert(&s, 0, NULL) == SOF_STRING_ERROR);
    CHECK(sof_string_insert_cstr(&s, 0, NULL) == SOF_STRING_ERROR);
    CHECK(s.length == before && strcmp(s.data, "[a]>bc]<[a<>bc]>bc]") == 0);
    CHECK(sof_string_insert(&other, 0, &s) == SOF_STRING_ERROR && strcmp(other.data, "<>") == 0);
    sof_string_t view = sof_string_view("view", 4);
    CHECK(sof_string_insert_cstr(&view, 0, "x") == SOF_STRING_ERROR);
    sof_string_free(&s);
    CHECK(sof_string_insert_cstr(&s, 0, "x") == SOF_STRING_ERROR);
    CHECK(sof_string_insert(NULL, 0, &other) == SOF_STRING_ERROR);
    CHECK(sof_string_insert_bytes(NULL, 0, "x", 1) == SOF_STRING_ERROR);
    CHECK(sof_string_insert_cstr(NULL, 0, "x") == SOF_STRING_ERROR);
}

static void test_cmp(void) {
    static const struct {
        const char *a;
        size_t a_length;
        const char *b;
        size_t b_length;
        int expected;
    } cases[] = {
        {"", 0, "", 0, 0},
        {"a", 1, "", 0, 1},
        {"ab", 2, "abc", 3, -1},
        {"abc", 3, "abd", 3, -1},
        {"b", 1, "abc", 3, 1},
        // Bytes compare unsigned, so 0xff is greater than 0x7f.
        {"\xff", 1, "\x7f", 1, 1},
        {"\x80", 1, "a", 1, 1},
        // NULs are ordinary bytes, inside or at the end.
        {"a\0b", 3, "a\0c", 3, -1},
        {"a", 1, "a\0", 2, -1},
        {"a\0", 2, "a\0", 2, 0},
    };
    for (size_t i = 0; i < LENGTH(cases); i++) {
        sof_string_t a = sof_string_view(cases[i].a, cases[i].a_length);
        sof_string_t b = sof_string_view(cases[i].b, cases[i].b_length);
        CHECK(sof_string_cmp(&a, &b) == cases[i].expected);
        CHECK(sof_string_cmp(&b, &a) == -cases[i].expected);
        CHECK(sof_string_cmp(&a, &a) == 0);
    }

    // Random strings over a small alphabet, checked against memcmp.
    uint64_t random = 0xc0ffee;
    char a_bytes[8], b_bytes[8];
    for (int i = 0; i < 20000; i++) {
        size_t a_length = next_random(&random) % 8, b_length = next_random(&random) % 8;
        for (size_t j = 0; j < 8; j++) {
            a_bytes[j] = "\0a\xff"[next_random(&random) % 3];
            b_bytes[j] = "\0a\xff"[next_random(&random) % 3];
        }
        size_t common = a_length < b_length ? a_length : b_length;
        int expected = memcmp(a_bytes, b_bytes, common);
        expected = expected != 0 ? (expected < 0 ? -1 : 1) : (a_length > b_length) - (a_length < b_length);
        sof_string_t a = sof_string_view(a_bytes, a_length), b = sof_string_view(b_bytes, b_length);
        CHECK(sof_string_cmp(&a, &b) == expected);
    }

    // One byte different, at every place, in strings of every length to past
    // where memcmp() takes over, at every alignment, with bytes above 0x7f
    // on either side, and prefixes of each other.
    char long_a[160], long_b[160];
    for (size_t length = 1; length <= 140; length++) {
        for (size_t align = 0; align < 8; align += 3) {
            for (size_t k = 0; k < length + align; k++) {
                long_a[k] = (char)('a' + next_random(&random) % 26);
            }
            memcpy(long_b, long_a, length + align);
            for (size_t at = 0; at < length; at++) {
                long_b[align + at] = (char)(0x80 | (unsigned char)long_a[align + at]);
                sof_string_t a = sof_string_view(long_a + align, length), b = sof_string_view(long_b + align, length);
                CHECK(sof_string_cmp(&a, &b) == -1 && sof_string_cmp(&b, &a) == 1);
                long_b[align + at] = long_a[align + at];
            }
            sof_string_t a = sof_string_view(long_a + align, length), b = sof_string_view(long_b + align, length);
            sof_string_t shorter = sof_string_view(long_b + align, length - 1);
            CHECK(sof_string_cmp(&a, &b) == 0 && sof_string_cmp(&shorter, &a) == -1 && sof_string_cmp(&a, &shorter) == 1);
        }
    }

    // NULL and dead strings are empty.
    sof_string_t dead = {0}, empty = sof_string_const(""), x = sof_string_const("x");
    CHECK(sof_string_cmp(NULL, NULL) == 0);
    CHECK(sof_string_cmp(NULL, &dead) == 0);
    CHECK(sof_string_cmp(&dead, &empty) == 0);
    CHECK(sof_string_cmp(NULL, &x) == -1);
    CHECK(sof_string_cmp(&x, &dead) == 1);
}

/// @brief Folds an ASCII upper case letter to lower case, for checking
/// sof_string_casecmp() without it.
static int fold_byte(int c) {
    return c >= 'A' && c <= 'Z' ? c + 32 : c;
}

/// @brief Returns 1 if sof_string_casecmp() gives expected for a and b,
/// its negation for b and a, and 0 for each with itself. The tests call
/// sof_string_casecmp() only from here.
static int casecmp_is(const sof_string_t *a, const sof_string_t *b, int expected) {
    return sof_string_casecmp(a, b) == expected && sof_string_casecmp(b, a) == -expected && sof_string_casecmp(a, a) == 0 && sof_string_casecmp(b, b) == 0;
}

static void test_casecmp(void) {
    static const struct {
        const char *a;
        size_t a_length;
        const char *b;
        size_t b_length;
        int expected;
    } cases[] = {
        {"", 0, "", 0, 0},
        {"a", 1, "A", 1, 0},
        {"Hello, World", 12, "hELLO, wORLD", 12, 0},
        {"AZ", 2, "az", 2, 0},
        {"a", 1, "", 0, 1},
        {"Ab", 2, "abC", 3, -1},
        {"ABC", 3, "abd", 3, -1},
        {"b", 1, "ABC", 3, 1},
        // Letters fold to lower case before comparing, so '_' (0x5f), which
        // lies between the upper and lower case letters, is less than 'A'.
        {"_", 1, "A", 1, -1},
        {"[", 1, "z", 1, -1},
        {"@", 1, "a", 1, -1},
        {"`", 1, "A", 1, -1},
        {"{", 1, "Z", 1, 1},
        // Bytes other than ASCII letters compare as they are, unsigned.
        {"\xff", 1, "\x7f", 1, 1},
        {"\xc3\x89", 2, "\xc3\xa9", 2, -1},
        {"Caf\xc3\xa9", 5, "CAF\xc3\xa9", 5, 0},
        // NULs are ordinary bytes, inside or at the end.
        {"A\0b", 3, "a\0B", 3, 0},
        {"a\0b", 3, "A\0C", 3, -1},
        {"A", 1, "a\0", 2, -1},
        // Dead strings (NULL data) are empty.
        {NULL, 0, "", 0, 0},
        {NULL, 0, NULL, 0, 0},
        {"X", 1, NULL, 0, 1},
    };
    for (size_t i = 0; i < LENGTH(cases); i++) {
        sof_string_t a = sof_string_view(cases[i].a, cases[i].a_length);
        sof_string_t b = sof_string_view(cases[i].b, cases[i].b_length);
        CHECK(casecmp_is(&a, &b, cases[i].expected));
    }

    // Every pair of bytes, and every pair of two-byte strings of one byte
    // and a letter: equal only when they fold to the same letters, and
    // otherwise ordered as their lower case forms, compared unsigned.
    for (int i = 0; i < 256 * 256; i++) {
        char ab[2] = {(char)(i >> 8), 'Q'}, bb[2] = {(char)i, 'q'};
        int la = fold_byte(i >> 8), lb = fold_byte(i & 0xff);
        sof_string_t a = sof_string_view(ab, 1), b = sof_string_view(bb, 1);
        sof_string_t a2 = sof_string_view(ab, 2), b2 = sof_string_view(bb, 2);
        CHECK(casecmp_is(&a, &b, (la > lb) - (la < lb)) && casecmp_is(&a2, &b2, (la > lb) - (la < lb)));
    }

    // Random strings of mixed case, checked against sof_string_cmp() of
    // lower case copies.
    uint64_t random = 0xcafe;
    char a_bytes[8], b_bytes[8], a_lower[8], b_lower[8];
    for (int i = 0; i < 20000; i++) {
        size_t a_length = next_random(&random) % 8, b_length = next_random(&random) % 8;
        for (size_t j = 0; j < 8; j++) {
            a_bytes[j] = "\0aAbB_\xff"[next_random(&random) % 7];
            b_bytes[j] = "\0aAbB_\xff"[next_random(&random) % 7];
            a_lower[j] = (char)fold_byte((unsigned char)a_bytes[j]);
            b_lower[j] = (char)fold_byte((unsigned char)b_bytes[j]);
        }
        sof_string_t a = sof_string_view(a_bytes, a_length), b = sof_string_view(b_bytes, b_length);
        sof_string_t la = sof_string_view(a_lower, a_length), lb = sof_string_view(b_lower, b_length);
        CHECK(casecmp_is(&a, &b, sof_string_cmp(&la, &lb)));
    }

    // Every pair of bytes at each of 16 places in strings long enough to be
    // compared 8 bytes at a time, of letters in mixed case otherwise the
    // same, so that each byte is folded in every place of a word and of the
    // last word, which overlaps the one before.
    char long_a[160], long_b[160], long_la[160], long_lb[160];
    for (size_t at = 0; at < 16; at++) {
        size_t length = 9 + at;
        for (size_t k = 0; k < length; k++) {
            long_a[k] = (char)("qQ"[k & 1]);
            long_b[k] = (char)("Qq"[k & 1]);
        }
        for (int i = 0; i < 256 * 256; i++) {
            long_a[at] = (char)(i >> 8);
            long_b[at] = (char)i;
            int la = fold_byte(i >> 8), lb = fold_byte(i & 0xff);
            sof_string_t a = sof_string_view(long_a, length), b = sof_string_view(long_b, length);
            CHECK(casecmp_is(&a, &b, (la > lb) - (la < lb)));
        }
    }

    // Random strings of any bytes to 140 long, the second the first with
    // its letters' case changed at random and, half the time, one byte
    // changed, checked against sof_string_cmp() of lower case copies.
    for (int i = 0; i < 20000; i++) {
        size_t length = next_random(&random) % 141, b_length = length;
        for (size_t k = 0; k < length; k++) {
            unsigned char c = (unsigned char)(next_random(&random) >> 56);
            long_a[k] = (char)c;
            long_b[k] = (char)(fold_byte(c) != c || (c >= 'a' && c <= 'z') ? c ^ (next_random(&random) & 0x20) : c);
        }
        if (length > 0 && next_random(&random) % 2 == 0) {
            long_b[next_random(&random) % length] = (char)(next_random(&random) >> 56);
        }
        if (length > 0 && next_random(&random) % 8 == 0) {
            b_length = next_random(&random) % length;
        }
        for (size_t k = 0; k < length; k++) {
            long_la[k] = (char)fold_byte((unsigned char)long_a[k]);
            long_lb[k] = (char)fold_byte((unsigned char)long_b[k]);
        }
        sof_string_t a = sof_string_view(long_a, length), b = sof_string_view(long_b, b_length);
        sof_string_t la = sof_string_view(long_la, length), lb = sof_string_view(long_lb, b_length);
        CHECK(casecmp_is(&a, &b, sof_string_cmp(&la, &lb)));
    }

    // The strings are only read, a sof_string_const() included. A NULL
    // string is empty.
    sof_string_t s;
    const sof_string_t word = sof_string_const("WoRd"), x = sof_string_const("X");
    CHECK(sof_string_from_cstr(&s, "wOrD", NULL) == SOF_STRING_OK);
    CHECK(casecmp_is(&s, &word, 0) && strcmp(s.data, "wOrD") == 0 && strcmp(word.data, "WoRd") == 0);
    sof_string_free(&s);
    CHECK(casecmp_is(NULL, NULL, 0) && casecmp_is(NULL, &s, 0) && casecmp_is(NULL, &x, -1));
}

static void test_cstr(void) {
    sof_string_t s, dead = {0};
    CHECK(sof_string_from_cstr(&s, "text", NULL) == SOF_STRING_OK);
    CHECK(sof_string_cstr(&s) == s.data);
    CHECK(strcmp(sof_string_cstr(&s), "text") == 0);
    sof_string_free(&s);
    CHECK(sof_string_cstr(&s) == NULL);
    CHECK(sof_string_cstr(&dead) == NULL);
    CHECK(sof_string_cstr(NULL) == NULL);
}

static void test_position(void) {
    sof_string_t s = sof_string_const("ab\ncd\r\n\nef");
    sof_string_t dead = {0};
    size_t line = 0, column = 0;
    // Each offset's line and column, the end of the text included.
    static const size_t want[][2] = {{1, 1}, {1, 2}, {1, 3}, {2, 1}, {2, 2}, {2, 3}, {2, 4}, {3, 1}, {4, 1}, {4, 2}, {4, 3}};
    for (size_t i = 0; i <= s.length; i++) {
        CHECK(sof_string_position(&s, i, &line, &column) == SOF_STRING_OK);
        CHECK(line == want[i][0] && column == want[i][1]);
    }
    // Either result can be left out.
    line = column = 0;
    CHECK(sof_string_position(&s, 4, &line, NULL) == SOF_STRING_OK && line == 2);
    CHECK(sof_string_position(&s, 4, NULL, &column) == SOF_STRING_OK && column == 2);
    CHECK(sof_string_position(&s, 4, NULL, NULL) == SOF_STRING_OK);
    // A view works, a NUL is an ordinary byte, and a string ending in a
    // line feed has an empty last line.
    sof_string_t v = sof_string_view("x\0\ny\n", 5);
    CHECK(sof_string_position(&v, 3, &line, &column) == SOF_STRING_OK && line == 2 && column == 1);
    CHECK(sof_string_position(&v, 5, &line, &column) == SOF_STRING_OK && line == 3 && column == 1);
    // Past the end, NULL, and a dead string, which is empty.
    line = column = 7;
    CHECK(sof_string_position(&s, s.length + 1, &line, &column) == SOF_STRING_ERROR && line == 7 && column == 7);
    CHECK(sof_string_position(NULL, 0, &line, &column) == SOF_STRING_ERROR && line == 7 && column == 7);
    CHECK(sof_string_position(&dead, 0, &line, &column) == SOF_STRING_OK && line == 1 && column == 1);
    CHECK(sof_string_position(&dead, 1, &line, &column) == SOF_STRING_ERROR);
}

static void test_case(void) {
    char bytes[256];
    sof_string_t s;
    for (int i = 0; i < 256; i++) {
        bytes[i] = (char)i;
    }
    sof_string_t all = sof_string_view(bytes, sizeof(bytes));
    // Only the ASCII letters change, whatever else the bytes are.
    CHECK(sof_string_dup(&s, &all, NULL) == SOF_STRING_OK);
    CHECK(sof_string_tolower(&s) == SOF_STRING_OK);
    for (int i = 0; i < 256; i++) {
        CHECK((unsigned char)s.data[i] == (i >= 'A' && i <= 'Z' ? i + 32 : i));
    }
    CHECK(s.length == 256 && s.data[256] == '\0');
    sof_string_free(&s);
    CHECK(sof_string_dup(&s, &all, NULL) == SOF_STRING_OK);
    CHECK(sof_string_toupper(&s) == SOF_STRING_OK);
    for (int i = 0; i < 256; i++) {
        CHECK((unsigned char)s.data[i] == (i >= 'a' && i <= 'z' ? i - 32 : i));
    }
    sof_string_free(&s);

    // Random bytes, every value many times, in strings of each length to
    // past what several vectors hold, so that each byte falls in the
    // vectorized part and in the part after it, against a byte at a time.
    char buffer[160], expected[160];
    uint64_t random = 0xca5e;
    for (size_t length = 0; length <= 130; length++) {
        for (int round = 0; round < 8; round++) {
            int upper = round & 1;
            for (size_t k = 0; k < length; k++) {
                buffer[k] = (char)(next_random(&random) >> 56);
                unsigned char c = (unsigned char)buffer[k];
                int change = upper ? c >= 'a' && c <= 'z' : c >= 'A' && c <= 'Z';
                expected[k] = change ? (char)(c ^ 0x20) : (char)c;
            }
            sof_string_t view = sof_string_view(buffer, length);
            CHECK(sof_string_dup(&s, &view, NULL) == SOF_STRING_OK);
            CHECK((upper ? sof_string_toupper(&s) : sof_string_tolower(&s)) == SOF_STRING_OK);
            CHECK(s.length == length && memcmp(s.data, expected, length) == 0 && s.data[length] == '\0');
            sof_string_free(&s);
        }
    }

    CHECK(sof_string_from_cstr(&s, "Caf\xc3\xa9 \xc3\x89t\xc3\xa9", NULL) == SOF_STRING_OK);
    CHECK(sof_string_toupper(&s) == SOF_STRING_OK);
    CHECK(strcmp(s.data, "CAF\xc3\xa9 \xc3\x89T\xc3\xa9") == 0);
    CHECK(sof_string_tolower(&s) == SOF_STRING_OK);
    CHECK(strcmp(s.data, "caf\xc3\xa9 \xc3\x89t\xc3\xa9") == 0);
    sof_string_free(&s);

    sof_string_t dead = {0};
    CHECK(sof_string_tolower(&dead) == SOF_STRING_ERROR);
    CHECK(sof_string_toupper(&dead) == SOF_STRING_ERROR);
    CHECK(sof_string_tolower(NULL) == SOF_STRING_ERROR);
    CHECK(sof_string_toupper(NULL) == SOF_STRING_ERROR);
}

static void test_free(void) {
    sof_string_t s;
    CHECK(sof_string_from_cstr(&s, "abc", NULL) == SOF_STRING_OK);
    CHECK(sof_string_free(&s) == SOF_STRING_OK);
    // Freeing again is harmless.
    CHECK(sof_string_free(&s) == SOF_STRING_OK);
    CHECK(s.data == NULL && s.length == 0 && s.size == 0);
    CHECK(sof_string_free(NULL) == SOF_STRING_ERROR);
    // A freed string can be initialized again.
    CHECK(sof_string_from_cstr(&s, "again", NULL) == SOF_STRING_OK);
    CHECK(strcmp(s.data, "again") == 0);
    sof_string_free(&s);
}

static void test_random_edits(void) {
    // Random edits of a string, made to a plain array too, which the string
    // must match after each one.
    test_alloc_t state = {0};
    sof_alloc_t alloc = test_allocator(&state);
    static char model[2 * MODEL_MAX + 512], part[MODEL_MAX + 512];
    size_t model_length = 0;
    char bytes[256];
    uint64_t random = 0x5eed;
    sof_string_t s, other;
    CHECK(sof_string_init(&s, 0, &alloc) == SOF_STRING_OK);
    model[0] = '\0';
    for (int step = 0; step < 20000; step++) {
        uint64_t r = next_random(&random);
        size_t n = r % 17, offset, count;
        switch ((r >> 16) % 13) {
        case 0:
            // Any bytes, NULs included.
            for (size_t i = 0; i < n; i++) {
                bytes[i] = (char)(next_random(&random) >> 56);
            }
            other = sof_string_view(bytes, n);
            CHECK(sof_string_append(&s, &other) == SOF_STRING_OK);
            memcpy(model + model_length, bytes, n);
            model_length += n;
            break;
        case 1:
            // A c string of letters and high bytes.
            for (size_t i = 0; i < n; i++) {
                bytes[i] = "aZ\x80\xff"[next_random(&random) % 4];
            }
            bytes[n] = '\0';
            CHECK(sof_string_append_cstr(&s, bytes) == SOF_STRING_OK);
            memcpy(model + model_length, bytes, n);
            model_length += n;
            break;
        case 2:
            CHECK(sof_string_append(&s, &s) == SOF_STRING_OK);
            memcpy(model + model_length, model, model_length);
            model_length *= 2;
            break;
        case 3:
            // The string's own bytes, from a place to the next NUL.
            offset = next_random(&random) % (model_length + 1);
            count = strlen(model + offset);
            CHECK(sof_string_append_cstr(&s, s.data + offset) == SOF_STRING_OK);
            memmove(model + model_length, model + offset, count);
            model_length += count;
            break;
        case 4:
            offset = next_random(&random) % (model_length + 1);
            count = next_random(&random) % (model_length - offset + 1);
            CHECK(sof_string_delete(&s, offset, count) == SOF_STRING_OK);
            memmove(model + offset, model + offset + count, model_length - offset - count);
            model_length -= count;
            break;
        case 5:
            CHECK(sof_string_tolower(&s) == SOF_STRING_OK);
            for (size_t i = 0; i < model_length; i++) {
                if (model[i] >= 'A' && model[i] <= 'Z') {
                    model[i] += 32;
                }
            }
            break;
        case 6:
            CHECK(sof_string_toupper(&s) == SOF_STRING_OK);
            for (size_t i = 0; i < model_length; i++) {
                if (model[i] >= 'a' && model[i] <= 'z') {
                    model[i] -= 32;
                }
            }
            break;
        case 7:
            // A part of itself, through a copy.
            offset = next_random(&random) % (model_length + 1);
            count = next_random(&random) % (model_length - offset + 1);
            CHECK(sof_string_substr(&other, &s, offset, count, &alloc) == SOF_STRING_OK);
            CHECK(sof_string_append(&s, &other) == SOF_STRING_OK);
            sof_string_free(&other);
            memmove(model + model_length, model + offset, count);
            model_length += count;
            break;
        case 8:
            CHECK(sof_string_dup(&other, &s, &alloc) == SOF_STRING_OK);
            CHECK(sof_string_cmp(&other, &s) == 0);
            CHECK(other.length == model_length && memcmp(other.data, model, model_length) == 0);
            sof_string_free(&other);
            break;
        case 9:
            // Edits out of range, which change nothing.
            CHECK(sof_string_delete(&s, model_length, 1) == SOF_STRING_ERROR);
            CHECK(sof_string_delete(&s, model_length + 1, 0) == SOF_STRING_ERROR);
            CHECK(sof_string_substr(&other, &s, 0, model_length + 1, &alloc) == SOF_STRING_ERROR);
            CHECK(sof_string_insert_bytes(&s, model_length + 1, "x", 1) == SOF_STRING_ERROR);
            break;
        case 10:
            // Any bytes, anywhere.
            for (size_t i = 0; i < n; i++) {
                bytes[i] = (char)(next_random(&random) >> 56);
            }
            offset = next_random(&random) % (model_length + 1);
            CHECK(sof_string_insert_bytes(&s, offset, bytes, n) == SOF_STRING_OK);
            memmove(model + offset + n, model + offset, model_length - offset);
            memcpy(model + offset, bytes, n);
            model_length += n;
            break;
        case 11: {
            // A part of itself, anywhere, which may be on both sides of
            // where it goes.
            offset = next_random(&random) % (model_length + 1);
            size_t from = next_random(&random) % (model_length + 1);
            count = next_random(&random) % (model_length - from + 1);
            CHECK(sof_string_insert_bytes(&s, offset, s.data + from, count) == SOF_STRING_OK);
            memcpy(part, model + from, count);
            memmove(model + offset + count, model + offset, model_length - offset);
            memcpy(model + offset, part, count);
            model_length += count;
            break;
        }
        default:
            if (n == 0) {
                CHECK(sof_string_clear(&s) == SOF_STRING_OK);
                model_length = 0;
            }
            break;
        }
        model[model_length] = '\0';
        if (model_length > MODEL_MAX) {
            CHECK(sof_string_delete(&s, MODEL_MAX / 2, model_length - MODEL_MAX / 2) == SOF_STRING_OK);
            model_length = MODEL_MAX / 2;
            model[model_length] = '\0';
        }
        CHECK(s.length == model_length);
        CHECK(memcmp(s.data, model, model_length + 1) == 0);
        CHECK(s.size > s.length);
    }
    sof_string_free(&s);
    CHECK(state.live == 0);
    CHECK(state.bad == 0);
}

static void test_custom_allocator(void) {
    test_alloc_t state = {0};
    sof_alloc_t alloc = test_allocator(&state);
    sof_string_t s, copy, part;
    CHECK(sof_string_from_cstr(&s, "abc def", &alloc) == SOF_STRING_OK);
    // The string keeps a copy of the allocator.
    alloc.malloc = NULL;
    CHECK(s.alloc.ctx == &state && s.alloc.malloc != NULL);
    alloc = test_allocator(&state);
    CHECK(sof_string_dup(&copy, &s, &alloc) == SOF_STRING_OK);
    CHECK(sof_string_substr(&part, &s, 4, 3, &alloc) == SOF_STRING_OK);
    CHECK(state.live == 3);
    sof_string_free(&s);
    sof_string_free(&copy);
    sof_string_free(&part);
    CHECK(sof_string_from_bytes(&s, "a\0b", 3, &alloc) == SOF_STRING_OK);
    CHECK(s.alloc.ctx == &state && state.live == 1);
    sof_string_free(&s);
    CHECK(state.live == 0);
    CHECK(state.bad == 0);

    // An allocator with only malloc and free, without sizes or realloc.
    alloc.free_sized = NULL;
    alloc.free = test_alloc_free;
    alloc.realloc = NULL;
    CHECK(sof_string_init(&s, 1, &alloc) == SOF_STRING_OK);
    for (int i = 0; i < 100; i++) {
        CHECK(sof_string_append_cstr(&s, "xyz") == SOF_STRING_OK);
    }
    CHECK(s.length == 300);
    sof_string_free(&s);
    CHECK(state.live == 0);
    CHECK(state.bad == 0);

    // One with a malloc but no free is refused, without allocating.
    alloc = test_allocator(&state);
    alloc.free_sized = NULL;
    size_t before = state.allocations;
    sof_string_t source = sof_string_const("x");
    CHECK(sof_string_init(&s, 1, &alloc) == SOF_STRING_ERROR);
    CHECK(s.data == NULL);
    CHECK(sof_string_from_cstr(&s, "x", &alloc) == SOF_STRING_ERROR);
    CHECK(sof_string_from_bytes(&s, "x", 1, &alloc) == SOF_STRING_ERROR);
    CHECK(sof_string_dup(&s, &source, &alloc) == SOF_STRING_ERROR);
    CHECK(sof_string_substr(&s, &source, 0, 1, &alloc) == SOF_STRING_ERROR);
    CHECK(s.data == NULL);
    CHECK(state.allocations == before);
}

static void test_no_memory(void) {
    // Fail each allocation in turn until a run makes none fail. Whatever
    // fails must report SOF_STRING_NO_MEM, leave things as documented, and
    // leak nothing.
    for (size_t n = 1;; n++) {
        test_alloc_t state = {0};
        state.fail_at = n;
        sof_alloc_t alloc = test_allocator(&state);
        sof_string_t a, b, c, d, e;
        sof_string_error_t rc;
        int failed = 0;

        rc = sof_string_init(&a, 2, &alloc);
        CHECK(rc == SOF_STRING_OK || (rc == SOF_STRING_NO_MEM && a.data == NULL));
        failed |= rc != SOF_STRING_OK;
        rc = sof_string_from_cstr(&b, "one, two,three", &alloc);
        CHECK(rc == SOF_STRING_OK || (rc == SOF_STRING_NO_MEM && b.data == NULL));
        failed |= rc != SOF_STRING_OK;
        rc = sof_string_from_bytes(&e, "x\0y", 3, &alloc);
        CHECK(rc == SOF_STRING_OK || (rc == SOF_STRING_NO_MEM && e.data == NULL));
        failed |= rc != SOF_STRING_OK;
        for (int i = 0; a.data != NULL && i < 40; i++) {
            size_t length = a.length;
            rc = sof_string_append_cstr(&a, "ab");
            // A failed append leaves the string as it was.
            CHECK(rc == SOF_STRING_OK || (rc == SOF_STRING_NO_MEM && a.length == length && a.data[length] == '\0'));
            failed |= rc != SOF_STRING_OK;
        }
        for (int i = 0; a.data != NULL && i < 300; i++) {
            size_t length = a.length;
            rc = sof_string_append_char(&a, 'c');
            CHECK(rc == SOF_STRING_OK || (rc == SOF_STRING_NO_MEM && a.length == length && a.data[length] == '\0'));
            failed |= rc != SOF_STRING_OK;
        }
        if (a.data != NULL) {
            size_t length = a.length;
            char *p = sof_string_reserve(&a, 2 * a.size);
            CHECK(p != NULL || (a.length == length && a.data[length] == '\0'));
            failed |= p == NULL;
            if (p != NULL) {
                memset(p, 'r', 3);
                CHECK(sof_string_commit(&a, 3) == SOF_STRING_OK && a.length == length + 3);
            }
        }
        for (int i = 0; a.data != NULL && i < 8; i++) {
            size_t length = a.length;
            rc = sof_string_insert(&a, length / 2, &a);
            // A failed insert leaves the string as it was.
            CHECK(rc == SOF_STRING_OK || (rc == SOF_STRING_NO_MEM && a.length == length && a.data[length] == '\0'));
            failed |= rc != SOF_STRING_OK;
        }
        if (b.data != NULL) {
            rc = sof_string_dup(&c, &b, &alloc);
            CHECK(rc == SOF_STRING_OK || (rc == SOF_STRING_NO_MEM && c.data == NULL));
            failed |= rc != SOF_STRING_OK;
            rc = sof_string_substr(&d, &b, 5, 3, &alloc);
            CHECK(rc == SOF_STRING_OK || (rc == SOF_STRING_NO_MEM && d.data == NULL));
            failed |= rc != SOF_STRING_OK;
        } else {
            memset(&c, 0, sizeof(c));
            memset(&d, 0, sizeof(d));
        }
        // Only the chosen allocation fails.
        CHECK(failed == (state.allocations >= n));
        sof_string_free(&a);
        sof_string_free(&b);
        sof_string_free(&c);
        sof_string_free(&d);
        sof_string_free(&e);
        CHECK(state.live == 0);
        CHECK(state.bad == 0);
        if (state.allocations < n) {
            break;
        }
    }
}

int main(void) {
    printf("sof_string:\n");
    run("init", test_init);
    run("from bytes", test_from_bytes);
    run("from cstr", test_from_cstr);
    run("const", test_const);
    run("view", test_view);
    run("dup", test_dup);
    run("substr", test_substr);
    run("append", test_append);
    run("append bytes", test_append_bytes);
    run("append growth", test_append_growth);
    run("append char", test_append_char);
    run("reserve and commit", test_reserve);
    run("clear", test_clear);
    run("delete", test_delete);
    run("insert", test_insert);
    run("cmp", test_cmp);
    run("casecmp", test_casecmp);
    run("cstr", test_cstr);
    run("position", test_position);
    run("tolower and toupper", test_case);
    run("free", test_free);
    run("random edits", test_random_edits);
    run("custom allocator", test_custom_allocator);
    run("each allocation failing", test_no_memory);
    return test_report();
}
