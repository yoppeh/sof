/**
 * @file test.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief Tests for the sof_intern library.
 * @details Usage: test. Runs every test, and exits with 0 if they all pass
 * or 1 if any fails. The Makefile's check target builds and runs it.
 * @version 0.0.1
 * @date 2026-11-04
 * @copyright Copyright (c) 2026
 */

#include <stdint.h>
#include <string.h>

#include "sof_alloc.h"
#include "sof_hash.h"
#include "sof_hashmap.h"
#include "sof_intern.h"

#include "test.h"

/// @brief Longer than a block, so it gets a block of its own.
#define LONG_LENGTH (100 * 1000)

/// @brief How many random strings test_many() interns.
#define MANY 20000

/// @brief The counts a traversal callback keeps through its ctx.
typedef struct visit_t {
    size_t calls;
    size_t bytes;
    size_t stop_at;
    const sof_intern_t *pool;
    int wrong;
} visit_t;

/// @brief Counts each string visited, checks the pool holds it, and stops
/// at the stop_at'th call if stop_at isn't 0.
static int visit(const char *s, size_t length, void *ctx) {
    visit_t *v = ctx;
    v->calls++;
    v->bytes += length;
    if (sof_intern_find(v->pool, s, length) != s || sof_intern_length(s) != length || s[length] != '\0' || sof_intern_hash(s) != sof_hash_bytes(s, length, 0)) {
        v->wrong++;
    }
    return v->stop_at != 0 && v->calls == v->stop_at ? 7 : 0;
}

/// @brief Fills buffer with a random string of 0 to 5 of the letters a, b
/// and c, so many repeat, and returns its length.
static size_t random_string(uint64_t *state, char *buffer) {
    size_t length = next_random(state) % 6;
    for (size_t i = 0; i < length; i++) {
        buffer[i] = (char)('a' + next_random(state) % 3);
    }
    return length;
}

static void test_basic(void) {
    sof_intern_t pool;
    CHECK(sof_intern_init(&pool, 0, NULL) == SOF_INTERN_OK);
    int created = -1;
    char buffer[] = "hello";
    const char *a = sof_intern_string(&pool, buffer, 5, &created);
    CHECK(a != NULL && created == 1);
    CHECK(a != buffer && strcmp(a, "hello") == 0 && sof_intern_length(a) == 5);
    // The same bytes from another place give the same pointer.
    const char *b = sof_intern_cstr(&pool, "hello", &created);
    CHECK(b == a && created == 0);
    // A prefix is a different string, and needn't be NUL-terminated.
    const char *c = sof_intern_string(&pool, "help", 3, &created);
    CHECK(c != NULL && c != a && created == 1 && strcmp(c, "hel") == 0 && sof_intern_length(c) == 3);
    CHECK(sof_intern_count(&pool) == 2);
    // The hash is kept with the string, and is the same as sof_hash's.
    CHECK(sof_intern_hash(a) == sof_hash_bytes("hello", 5, 0));
    CHECK(sof_intern_hash(c) == sof_hash_bytes("hel", 3, 0));
    // The caller's buffer can change without changing the pool's copy.
    buffer[0] = 'j';
    CHECK(strcmp(a, "hello") == 0);
    CHECK(sof_intern_string(&pool, "hello", 5, NULL) == a);
    CHECK(sof_intern_free(&pool) == SOF_INTERN_OK);
}

static void test_bytes(void) {
    sof_intern_t pool;
    CHECK(sof_intern_init(&pool, 0, NULL) == SOF_INTERN_OK);
    int created = -1;
    // NULs are part of the string.
    const char *ab = sof_intern_string(&pool, "a\0b", 3, &created);
    CHECK(ab != NULL && created == 1 && sof_intern_length(ab) == 3 && memcmp(ab, "a\0b", 4) == 0);
    const char *a = sof_intern_string(&pool, "a\0c", 1, NULL);
    CHECK(a != NULL && a != ab && sof_intern_length(a) == 1);
    CHECK(sof_intern_string(&pool, "a\0b", 3, NULL) == ab);
    CHECK(sof_intern_string(&pool, "a\0c", 3, NULL) != ab);
    // The empty string is a string like any other.
    const char *empty = sof_intern_string(&pool, "", 0, &created);
    CHECK(empty != NULL && created == 1 && empty[0] == '\0' && sof_intern_length(empty) == 0);
    CHECK(sof_intern_string(&pool, "xyz", 0, &created) == empty && created == 0);
    CHECK(sof_intern_cstr(&pool, "", NULL) == empty);
    CHECK(sof_intern_hash(empty) == sof_hash_bytes("", 0, 0));
    CHECK(sof_intern_hash(ab) == sof_hash_bytes("a\0b", 3, 0) && sof_intern_hash(ab) != sof_intern_hash(a));
    CHECK(sof_intern_count(&pool) == 4);
    // Every byte value.
    char all[256];
    for (int i = 0; i < 256; i++) {
        all[i] = (char)i;
    }
    const char *s = sof_intern_string(&pool, all, sizeof(all), NULL);
    CHECK(s != NULL && sof_intern_length(s) == 256 && memcmp(s, all, 256) == 0 && s[256] == '\0');
    CHECK(sof_intern_find(&pool, all, sizeof(all)) == s);
    // Interned strings are aligned for their entries.
    CHECK((uintptr_t)s % _Alignof(sof_intern_entry_t) == 0);
    sof_intern_free(&pool);
}

static void test_find(void) {
    sof_intern_t pool;
    CHECK(sof_intern_init(&pool, 0, NULL) == SOF_INTERN_OK);
    CHECK(sof_intern_find(&pool, "word", 4) == NULL);
    CHECK(sof_intern_count(&pool) == 0);
    const char *w = sof_intern_cstr(&pool, "word", NULL);
    CHECK(sof_intern_find(&pool, "word", 4) == w);
    CHECK(sof_intern_find(&pool, "words", 4) == w);
    CHECK(sof_intern_find(&pool, "wor", 3) == NULL);
    CHECK(sof_intern_find(&pool, "words", 5) == NULL);
    CHECK(sof_intern_count(&pool) == 1);
    sof_intern_free(&pool);
}

static void test_long(void) {
    static char text[LONG_LENGTH];
    uint64_t state = 0x10e6;
    for (size_t i = 0; i < sizeof(text); i++) {
        text[i] = (char)('a' + next_random(&state) % 26);
    }
    sof_intern_t pool;
    CHECK(sof_intern_init(&pool, 0, NULL) == SOF_INTERN_OK);
    // A long string first, then short ones, then another long one, which
    // goes after the block the short ones are in.
    const char *first = sof_intern_string(&pool, text, sizeof(text), NULL);
    CHECK(first != NULL && sof_intern_length(first) == sizeof(text) && memcmp(first, text, sizeof(text)) == 0);
    CHECK(sof_intern_hash(first) == sof_hash_bytes(text, sizeof(text), 0));
    const char *short1 = sof_intern_cstr(&pool, "short", NULL);
    CHECK(short1 != NULL);
    const char *second = sof_intern_string(&pool, text + 1, sizeof(text) - 1, NULL);
    CHECK(second != NULL && second != first && sof_intern_length(second) == sizeof(text) - 1);
    const char *short2 = sof_intern_cstr(&pool, "shorter", NULL);
    CHECK(short2 != NULL);
    // The short strings share the first block, since the long one went after
    // it.
    CHECK(short2 > short1 && short2 - short1 < 64);
    CHECK(sof_intern_string(&pool, text, sizeof(text), NULL) == first);
    CHECK(sof_intern_string(&pool, text + 1, sizeof(text) - 1, NULL) == second);
    CHECK(memcmp(first, text, sizeof(text)) == 0 && memcmp(second, text + 1, sizeof(text) - 1) == 0);
    CHECK(sof_intern_count(&pool) == 4);
    sof_intern_free(&pool);
}

static void test_many(void) {
    // Random strings of 1 to 40 characters from a few letters, so many
    // repeat. Each is checked against the pointer it got first, which must
    // not move as the pool grows.
    static char texts[MANY][41];
    static const char *pointers[MANY];
    uint64_t state = 0x5eed;
    sof_intern_t pool;
    CHECK(sof_intern_init(&pool, 0, NULL) == SOF_INTERN_OK);
    size_t created_count = 0;
    for (size_t i = 0; i < MANY; i++) {
        size_t length = 1 + next_random(&state) % 40;
        // Short strings from two letters repeat often; long ones rarely.
        for (size_t k = 0; k < length; k++) {
            texts[i][k] = (char)('a' + next_random(&state) % (length < 8 ? 2 : 26));
        }
        texts[i][length] = '\0';
        int created;
        pointers[i] = sof_intern_cstr(&pool, texts[i], &created);
        CHECK(pointers[i] != NULL && strcmp(pointers[i], texts[i]) == 0);
        created_count += created;
    }
    CHECK(sof_intern_count(&pool) == created_count);
    CHECK(created_count < MANY && created_count > MANY / 2);
    for (size_t i = 0; i < MANY; i++) {
        CHECK(strcmp(pointers[i], texts[i]) == 0);
        CHECK(sof_intern_cstr(&pool, texts[i], NULL) == pointers[i]);
        CHECK(sof_intern_find(&pool, texts[i], strlen(texts[i])) == pointers[i]);
        CHECK(sof_intern_length(pointers[i]) == strlen(texts[i]));
        CHECK(sof_intern_hash(pointers[i]) == sof_hash_string(texts[i], 0));
    }
    // Equal strings, and only those, have equal pointers.
    for (size_t i = 0; i + 1 < MANY; i++) {
        CHECK((strcmp(texts[i], texts[i + 1]) == 0) == (pointers[i] == pointers[i + 1]));
    }
    CHECK(sof_intern_count(&pool) == created_count);
    sof_intern_free(&pool);
}

static void test_traverse(void) {
    sof_intern_t pool;
    CHECK(sof_intern_init(&pool, 0, NULL) == SOF_INTERN_OK);
    visit_t v = {0, 0, 0, &pool, 0};
    CHECK(sof_intern_traverse(&pool, visit, &v) == 0 && v.calls == 0);
    uint64_t state = 0x7a5e;
    char buffer[8];
    size_t bytes = 0;
    for (int i = 0; i < 500; i++) {
        size_t length = random_string(&state, buffer);
        int created;
        CHECK(sof_intern_string(&pool, buffer, length, &created) != NULL);
        if (created) {
            bytes += length;
        }
    }
    size_t count = sof_intern_count(&pool);
    CHECK(count > 10 && count < 500);
    CHECK(sof_intern_traverse(&pool, visit, &v) == 0);
    CHECK(v.calls == count && v.bytes == bytes && v.wrong == 0);
    // An early stop returns the callback's value.
    visit_t stop = {0, 0, 3, &pool, 0};
    CHECK(sof_intern_traverse(&pool, visit, &stop) == 7 && stop.calls == 3);
    CHECK(sof_intern_traverse(&pool, NULL, NULL) == 0);
    // sof_intern_next() visits the same strings, in the same order.
    size_t position = 0, seen = 0, seen_bytes = 0;
    const char *s;
    while ((s = sof_intern_next(&pool, &position)) != NULL) {
        CHECK(sof_intern_find(&pool, s, sof_intern_length(s)) == s);
        seen++;
        seen_bytes += sof_intern_length(s);
    }
    CHECK(seen == count && seen_bytes == bytes);
    CHECK(sof_intern_next(&pool, &position) == NULL);
    CHECK(sof_intern_next(&pool, NULL) == NULL);
    sof_intern_free(&pool);
}

static void test_clear(void) {
    sof_intern_t pool;
    CHECK(sof_intern_init(&pool, 0, NULL) == SOF_INTERN_OK);
    static char text[LONG_LENGTH];
    memset(text, 'x', sizeof(text));
    CHECK(sof_intern_cstr(&pool, "one", NULL) != NULL);
    CHECK(sof_intern_string(&pool, text, sizeof(text), NULL) != NULL);
    CHECK(sof_intern_clear(&pool) == SOF_INTERN_OK);
    CHECK(sof_intern_count(&pool) == 0 && pool.blocks == NULL);
    CHECK(sof_intern_find(&pool, "one", 3) == NULL);
    visit_t v = {0, 0, 0, &pool, 0};
    CHECK(sof_intern_traverse(&pool, visit, &v) == 0 && v.calls == 0);
    // It is still usable.
    int created;
    const char *one = sof_intern_cstr(&pool, "one", &created);
    CHECK(one != NULL && created == 1 && sof_intern_count(&pool) == 1);
    CHECK(sof_intern_clear(&pool) == SOF_INTERN_OK);
    CHECK(sof_intern_clear(&pool) == SOF_INTERN_OK);
    sof_intern_free(&pool);
}

static void test_null(void) {
    int created = -1;
    size_t position = 0;
    CHECK(sof_intern_init(NULL, 0, NULL) == SOF_INTERN_ERROR);
    CHECK(sof_intern_free(NULL) == SOF_INTERN_ERROR);
    CHECK(sof_intern_clear(NULL) == SOF_INTERN_ERROR);
    CHECK(sof_intern_count(NULL) == 0);
    CHECK(sof_intern_string(NULL, "a", 1, &created) == NULL && created == 0);
    created = -1;
    CHECK(sof_intern_cstr(NULL, "a", &created) == NULL && created == 0);
    CHECK(sof_intern_find(NULL, "a", 1) == NULL);
    CHECK(sof_intern_next(NULL, &position) == NULL);
    CHECK(sof_intern_traverse(NULL, visit, NULL) == -1);
    CHECK(sof_intern_length(NULL) == 0);
    CHECK(sof_intern_hash(NULL) == sof_hash_bytes("", 0, 0));
    // NULL data, in a working pool.
    sof_intern_t pool;
    CHECK(sof_intern_init(&pool, 0, NULL) == SOF_INTERN_OK);
    created = -1;
    CHECK(sof_intern_string(&pool, NULL, 0, &created) == NULL && created == 0);
    CHECK(sof_intern_string(&pool, NULL, 5, NULL) == NULL);
    created = -1;
    CHECK(sof_intern_cstr(&pool, NULL, &created) == NULL && created == 0);
    CHECK(sof_intern_find(&pool, NULL, 0) == NULL);
    CHECK(sof_intern_count(&pool) == 0);
    sof_intern_free(&pool);
}

/// @brief Checks that a pool acts as empty and refuses new strings.
static int dead(sof_intern_t *pool) {
    int created = -1;
    size_t position = 0;
    visit_t v = {0, 0, 0, pool, 0};
    return sof_intern_string(pool, "a", 1, &created) == NULL && created == 0 && sof_intern_cstr(pool, "a", NULL) == NULL && sof_intern_find(pool, "a", 1) == NULL && sof_intern_count(pool) == 0 && sof_intern_next(pool, &position) == NULL && sof_intern_traverse(pool, visit, &v) == 0 && v.calls == 0 && sof_intern_clear(pool) == SOF_INTERN_OK;
}

static void test_dead(void) {
    sof_intern_t pool;
    CHECK(sof_intern_init(&pool, 0, NULL) == SOF_INTERN_OK);
    CHECK(sof_intern_cstr(&pool, "a", NULL) != NULL);
    CHECK(sof_intern_free(&pool) == SOF_INTERN_OK);
    CHECK(dead(&pool));
    CHECK(sof_intern_free(&pool) == SOF_INTERN_OK);
    // An allocator with no way to free is refused.
    test_alloc_t state = {0};
    sof_alloc_t bad = test_allocator(&state);
    bad.free_sized = NULL;
    CHECK(sof_intern_init(&pool, 0, &bad) == SOF_INTERN_ERROR);
    CHECK(dead(&pool));
    CHECK(state.allocations == 0);
    // A pool whose init ran out of memory.
    sof_alloc_t alloc = test_allocator(&state);
    state.fail_at = 1;
    CHECK(sof_intern_init(&pool, 0, &alloc) == SOF_INTERN_NO_MEM);
    CHECK(dead(&pool));
    CHECK(sof_intern_free(&pool) == SOF_INTERN_OK);
    CHECK(state.live == 0 && state.bad == 0);
    // It can be initialized again.
    state.fail_at = 0;
    CHECK(sof_intern_init(&pool, 0, &alloc) == SOF_INTERN_OK);
    CHECK(sof_intern_cstr(&pool, "a", NULL) != NULL);
    sof_intern_free(&pool);
    CHECK(state.live == 0 && state.bad == 0);
}

/// @brief Interns the strings of test_alloc_failures() in order, with each
/// string longer than a block now and then. Returns 1 if every one was
/// interned.
static int intern_all(sof_intern_t *pool, const char *text, const char **pointers, size_t count) {
    int all = 1;
    for (size_t i = 0; i < count; i++) {
        size_t length = i % 50 == 49 ? LONG_LENGTH : 1 + i % 30;
        pointers[i] = sof_intern_string(pool, text + i % 7, length, NULL);
        all = all && pointers[i] != NULL;
    }
    return all;
}

static void test_alloc_failures(void) {
    // Fail each allocation in turn: the hashmap's array as it grows, and the
    // blocks, ordinary and long. Whatever fails, nothing leaks, every size
    // is right, and the pool is left as if the failed string was never
    // given, so it can be given again.
    enum { COUNT = 400 };
    static char text[LONG_LENGTH + 8];
    static const char *pointers[COUNT];
    uint64_t seed = 0xfa11;
    for (size_t i = 0; i < sizeof(text); i++) {
        text[i] = (char)('a' + next_random(&seed) % 26);
    }
    int finished = 0;
    for (size_t fail_at = 1; !finished; fail_at++) {
        test_alloc_t state = {0};
        state.fail_at = fail_at;
        sof_alloc_t alloc = test_allocator(&state);
        sof_intern_t pool;
        if (sof_intern_init(&pool, 0, &alloc) == SOF_INTERN_OK) {
            int all = intern_all(&pool, text, pointers, COUNT);
            finished = all && state.allocations < fail_at;
            // With no more failures, every string can be interned, and
            // those interned before keep their pointers.
            state.fail_at = 0;
            const char *again[COUNT];
            CHECK(intern_all(&pool, text, again, COUNT));
            for (size_t i = 0; i < COUNT; i++) {
                CHECK(pointers[i] == NULL || pointers[i] == again[i]);
                size_t length = i % 50 == 49 ? LONG_LENGTH : 1 + i % 30;
                CHECK(sof_intern_length(again[i]) == length && memcmp(again[i], text + i % 7, length) == 0);
                CHECK(sof_intern_hash(again[i]) == sof_hash_bytes(text + i % 7, length, 0));
            }
        } else {
            CHECK(dead(&pool));
        }
        sof_intern_free(&pool);
        CHECK(state.live == 0 && state.bad == 0);
        CHECK(fail_at < 10000);
    }
}

static void test_near_misses(void) {
    // The pool compares lengths and bytes itself: every prefix of a string,
    // and every string one byte away from it, is a different string.
    sof_intern_t pool;
    CHECK(sof_intern_init(&pool, 0, NULL) == SOF_INTERN_OK);
    char s[40];
    memset(s, 'a', sizeof(s));
    const char *prefix[41];
    for (size_t n = 0; n <= sizeof(s); n++) {
        int created = 0;
        prefix[n] = sof_intern_string(&pool, s, n, &created);
        CHECK(prefix[n] != NULL && created == 1 && sof_intern_length(prefix[n]) == n);
    }
    for (size_t i = 0; i < sizeof(s); i++) {
        s[i] = 'b';
        CHECK(sof_intern_find(&pool, s, sizeof(s)) == NULL);
        const char *one = sof_intern_string(&pool, s, sizeof(s), NULL);
        CHECK(one != NULL && one != prefix[sizeof(s)] && memcmp(one, s, sizeof(s)) == 0);
        CHECK(sof_intern_find(&pool, s, sizeof(s)) == one);
        s[i] = 'a';
    }
    for (size_t n = 0; n <= sizeof(s); n++) {
        CHECK(sof_intern_find(&pool, s, n) == prefix[n]);
        CHECK(sof_intern_string(&pool, s, n, NULL) == prefix[n]);
    }
    CHECK(sof_intern_count(&pool) == 2 * sizeof(s) + 1);
    CHECK(sof_intern_free(&pool) == SOF_INTERN_OK);
}

int main(void) {
    printf("sof_intern:\n");
    run("intern and compare", test_basic);
    run("NULs, the empty string and every byte", test_bytes);
    run("find without interning", test_find);
    run("prefixes and strings differing in one byte", test_near_misses);
    run("strings longer than a block", test_long);
    run("many strings keep their pointers", test_many);
    run("traverse and next", test_traverse);
    run("clear", test_clear);
    run("NULL pool and data", test_null);
    run("freed and failed pools", test_dead);
    run("every allocation failing in turn", test_alloc_failures);
    return test_report();
}
