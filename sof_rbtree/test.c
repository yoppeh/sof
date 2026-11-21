/**
 * @file test.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief Tests for sof_rbtree.c.
 * @details Usage: test. Runs every test, and exits with 0 if they all pass
 * or 1 if any fails. The Makefile's check target builds and runs it.
 *
 * The word-list tests read
 * ../test-data/words.txt; the rest use generated keys. The threaded tests
 * share a tree between readers and a writer under a pthread_rwlock_t, and
 * use separate trees on separate threads with no lock.
 * @version 0.0.1
 * @date 2026-11-03
 * @copyright Copyright (c) 2026
 */

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sof_rbtree.h"
#include "test.h"

/// @brief The number of words in ../test-data/words.txt.
#define WORD_COUNT 263533

/// @brief The number of generated keys used by the larger tests.
#define KEY_COUNT 10000

/// @brief The threaded tests: reader threads and the walks each makes,
/// threads with a tree of their own, the keys 1..THREAD_KEYS they use, the
/// writes each makes, and the finds a reader makes each pass.
#define READERS 4
#define PASSES 1000
#define TREE_THREADS 4
#define THREAD_KEYS 512
#define WRITES 20000
#define FINDS 64

/// @brief Counts calls to the compare and delete callbacks through ctx.
typedef struct counts_t {
    size_t compares;
    size_t deletes;
} counts_t;

/// @brief Lets a traversal callback check order, count visits and stop.
typedef struct visit_t {
    /// @brief Nodes visited so far.
    size_t visited;
    /// @brief Stop, returning stop_value, after this many visits; 0 never.
    size_t stop_after;
    int stop_value;
    /// @brief The previous node visited, for checking the order.
    sof_rbtree_node_t *last;
    /// @brief 1 for ascending order, -1 for descending.
    int direction;
    /// @brief Set if a node came out of order.
    int out_of_order;
    sof_rbtree_t *tree;
} visit_t;

/// @brief The word list, loaded once by load_words(), with the sizes its two
/// blocks were allocated with.
static char *word_buffer = NULL;
static size_t word_buffer_size = 0;
static char **words = NULL;
static size_t words_size = 0;
static size_t word_count = 0;

static int check_subtree(sof_rbtree_t *tree, sof_rbtree_node_t *node, size_t *count);
static int check_tree(sof_rbtree_t *tree);
static int check_visit(sof_rbtree_node_t *node, void *ctx);
static size_t check_walk(sof_rbtree_t *tree, int direction);
static int compare_int(const void *a, const void *b, void *ctx);
static int compare_int_counting(const void *a, const void *b, void *ctx);
static int compare_str(const void *a, const void *b, void *ctx);
static void count_delete(sof_rbtree_node_t *node, void *ctx);
static void *data_for(uintptr_t key);
static void free_key_delete(sof_rbtree_node_t *node, void *ctx);
static int insert_range(sof_rbtree_t *tree, uintptr_t first, uintptr_t last);
static int load_words(void);
static void locked_free_sized(void *p, size_t size, void *ctx);
static void *locked_malloc(size_t size, void *ctx);
static void *read_shared(void *arg);
static void shuffle(char **v, size_t n, uint64_t seed);
static void *use_own_tree(void *arg);
static int visit(sof_rbtree_node_t *node, void *ctx);

/// @brief Checks the red-black properties, parent links and key order of the
/// subtree at node, counting its nodes; returns its black height, or -1 if a
/// property doesn't hold.
static int check_subtree(sof_rbtree_t *tree, sof_rbtree_node_t *node, size_t *count) {
    sof_rbtree_node_t *nil = &tree->nil_node;
    int left;
    int right;
    if (node == nil) {
        return 1;
    }
    (*count)++;
    if (SOF_RBTREE_COLOR_IS_RED(node) && (SOF_RBTREE_COLOR_IS_RED(node->left) || SOF_RBTREE_COLOR_IS_RED(node->right))) {
        return -1;
    }
    if (node->left != nil && (node->left->parent != node || tree->cmp_func(node->left->key, node->key, tree->ctx) >= 0)) {
        return -1;
    }
    if (node->right != nil && (node->right->parent != node || tree->cmp_func(node->right->key, node->key, tree->ctx) <= 0)) {
        return -1;
    }
    left = check_subtree(tree, node->left, count);
    right = check_subtree(tree, node->right, count);
    if (left < 0 || left != right) {
        return -1;
    }
    return left + (SOF_RBTREE_COLOR_IS_BLACK(node) ? 1 : 0);
}

/// @brief Returns whether tree is a valid red-black tree whose count is right.
static int check_tree(sof_rbtree_t *tree) {
    size_t count = 0;
    if (tree->root != &tree->nil_node && (!SOF_RBTREE_COLOR_IS_BLACK(tree->root) || tree->root->parent != &tree->nil_node)) {
        return 0;
    }
    if (check_subtree(tree, tree->root, &count) < 0) {
        return 0;
    }
    return count == tree->count;
}

static int compare_int(const void *a, const void *b, void *ctx) {
    uintptr_t x = (uintptr_t)a, y = (uintptr_t)b;
    return x < y ? -1 : x > y;
}

/// @brief compare_int that counts its calls in the counts_t ctx points to.
static int compare_int_counting(const void *a, const void *b, void *ctx) {
    ((counts_t *)ctx)->compares++;
    return compare_int(a, b, NULL);
}

static int compare_str(const void *a, const void *b, void *ctx) {
    return strcmp(a, b);
}

/// @brief Counts deleted nodes in the counts_t ctx points to.
static void count_delete(sof_rbtree_node_t *node, void *ctx) {
    ((counts_t *)ctx)->deletes++;
}

/// @brief Frees a node's key, a string from sof_alloc_strndup(), counting it
/// in the counts_t ctx points to.
static void free_key_delete(sof_rbtree_node_t *node, void *ctx) {
    ((counts_t *)ctx)->deletes++;
    sof_alloc_free(NULL, node->key, strlen(node->key) + 1);
}

/// @brief Inserts the keys first..last; returns 0 if any insert failed.
static int insert_range(sof_rbtree_t *tree, uintptr_t first, uintptr_t last) {
    for (uintptr_t k = first; k <= last; k++) {
        if (sof_rbtree_insert(tree, (void *)k, NULL) == NULL) {
            return 0;
        }
    }
    return 1;
}

/// @brief Loads ../test-data/words.txt into words, once; returns 0 if it
/// couldn't be read.
static int load_words(void) {
    FILE *fp;
    long length;
    size_t lines = 0;
    char *p;
    if (words != NULL) {
        return 1;
    }
    fp = fopen("../test-data/words.txt", "rb");
    if (fp == NULL) {
        return 0;
    }
    if (fseek(fp, 0, SEEK_END) != 0 || (length = ftell(fp)) < 0 || fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return 0;
    }
    word_buffer_size = (size_t)length + 1;
    word_buffer = sof_alloc_malloc(NULL, word_buffer_size);
    if (word_buffer == NULL || fread(word_buffer, 1, (size_t)length, fp) != (size_t)length) {
        fclose(fp);
        return 0;
    }
    fclose(fp);
    word_buffer[length] = '\0';
    for (p = word_buffer; *p != '\0'; p++) {
        lines += *p == '\n';
    }
    words_size = (lines + 1) * sizeof(char *);
    words = sof_alloc_malloc(NULL, words_size);
    if (words == NULL) {
        return 0;
    }
    // Split in place; the last word needn't end with a newline.
    for (p = strtok(word_buffer, "\r\n"); p != NULL; p = strtok(NULL, "\r\n")) {
        words[word_count++] = p;
    }
    return 1;
}

/// @brief Puts the n strings in v in a random order set by seed.
static void shuffle(char **v, size_t n, uint64_t seed) {
    uint64_t state = seed;
    for (size_t i = n; i > 1; i--) {
        size_t j = next_random(&state) % i;
        char *t = v[i - 1];
        v[i - 1] = v[j];
        v[j] = t;
    }
}

/// @brief A traversal callback that checks the order of the nodes and stops
/// as the visit_t ctx points to says.
static int visit(sof_rbtree_node_t *node, void *ctx) {
    visit_t *v = ctx;
    if (v->last != NULL && v->tree->cmp_func(v->last->key, node->key, v->tree->ctx) * v->direction >= 0) {
        v->out_of_order = 1;
    }
    v->last = node;
    v->visited++;
    if (v->stop_after != 0 && v->visited == v->stop_after) {
        return v->stop_value;
    }
    return 0;
}

static void test_init(void) {
    sof_rbtree_t tree;
    counts_t counts = {0};
    CHECK(sof_rbtree_init(&tree, compare_int_counting, count_delete, &counts, NULL) == SOF_RBTREE_OK);
    CHECK(tree.count == 0 && tree.root == &tree.nil_node);
    CHECK(tree.cmp_func == compare_int_counting && tree.del_func == count_delete && tree.ctx == &counts);
    CHECK(check_tree(&tree));
    CHECK(sof_rbtree_free(&tree) == SOF_RBTREE_OK);
}

static void test_empty(void) {
    sof_rbtree_t tree;
    visit_t v = {0};
    size_t count = 1;
    void **keys;
    CHECK(sof_rbtree_init(&tree, compare_int, NULL, NULL, NULL) == SOF_RBTREE_OK);
    v.tree = &tree;
    v.direction = 1;
    CHECK(sof_rbtree_find(&tree, (void *)1) == NULL);
    CHECK(sof_rbtree_minimum(&tree, NULL) == NULL && sof_rbtree_maximum(&tree, NULL) == NULL);
    CHECK(sof_rbtree_remove(&tree, (void *)1) == SOF_RBTREE_NO_MATCH);
    CHECK(sof_rbtree_traverse_ascending(&tree, NULL, visit, &v) == 0);
    CHECK(sof_rbtree_traverse_descending(&tree, NULL, visit, &v) == 0 && v.visited == 0);
    keys = sof_rbtree_get_keys(&tree, &count);
    CHECK(keys != NULL && keys[0] == NULL && count == 0);
    sof_alloc_free(&tree.alloc, keys, (count + 1) * sizeof(void *));
    CHECK(sof_rbtree_clear(&tree, NULL) == SOF_RBTREE_OK && tree.count == 0);
    CHECK(sof_rbtree_free(&tree) == SOF_RBTREE_OK);
}

static void test_insert_find(void) {
    sof_rbtree_t tree;
    counts_t counts = {0};
    uint64_t state = 0x9e3779b97f4a7c15ULL;
    uintptr_t keys[KEY_COUNT];
    size_t inserted = 0;
    int created;
    CHECK(sof_rbtree_init(&tree, compare_int_counting, NULL, &counts, NULL) == SOF_RBTREE_OK);
    for (size_t i = 0; i < KEY_COUNT; i++) {
        keys[i] = (uintptr_t)next_random(&state);
        sof_rbtree_node_t *node = sof_rbtree_insert(&tree, (void *)keys[i], &created);
        CHECK(node != NULL && node->key == (void *)keys[i] && node->data == NULL);
        CHECK(SOF_RBTREE_GET_USER(node) == 0);
        inserted += created;
    }
    CHECK(inserted == KEY_COUNT && tree.count == KEY_COUNT);
    CHECK(counts.compares > 0);
    CHECK(check_tree(&tree));
    for (size_t i = 0; i < KEY_COUNT; i++) {
        sof_rbtree_node_t *node = sof_rbtree_find(&tree, (void *)keys[i]);
        CHECK(node != NULL && node->key == (void *)keys[i]);
    }
    state = 12345;
    for (size_t i = 0; i < 100; i++) {
        // Keys from another seed are, in all likelihood, not in the tree.
        CHECK(sof_rbtree_find(&tree, (void *)(uintptr_t)next_random(&state)) == NULL);
    }
    CHECK(sof_rbtree_free(&tree) == SOF_RBTREE_OK);
}

static void test_insert_existing(void) {
    sof_rbtree_t tree;
    sof_rbtree_node_t *node;
    int created = 0;
    CHECK(sof_rbtree_init(&tree, compare_int, NULL, NULL, NULL) == SOF_RBTREE_OK);
    node = sof_rbtree_insert(&tree, (void *)7, &created);
    CHECK(node != NULL && created == 1 && tree.count == 1);
    node->data = &created;
    CHECK(sof_rbtree_insert(&tree, (void *)7, &created) == node && created == 0);
    CHECK(tree.count == 1 && node->data == &created);
    CHECK(sof_rbtree_insert(&tree, (void *)8, NULL) != NULL && tree.count == 2);
    CHECK(sof_rbtree_free(&tree) == SOF_RBTREE_OK);
}

static void test_get_keys(void) {
    sof_rbtree_t tree;
    void **keys;
    size_t count;
    CHECK(sof_rbtree_init(&tree, compare_int, NULL, NULL, NULL) == SOF_RBTREE_OK);
    // Insert in descending order, so the array's order comes from the tree.
    for (uintptr_t k = 9; k >= 1; k--) {
        CHECK(sof_rbtree_insert(&tree, (void *)k, NULL) != NULL);
        keys = sof_rbtree_get_keys(&tree, &count);
        CHECK(keys != NULL && count == 10 - k && keys[count] == NULL);
        for (size_t i = 0; i < count; i++) {
            CHECK((uintptr_t)keys[i] == k + i);
        }
        sof_alloc_free(&tree.alloc, keys, (count + 1) * sizeof(void *));
    }
    // A NULL count is allowed.
    keys = sof_rbtree_get_keys(&tree, NULL);
    CHECK(keys != NULL && (uintptr_t)keys[0] == 1);
    sof_alloc_free(&tree.alloc, keys, (tree.count + 1) * sizeof(void *));
    CHECK(sof_rbtree_free(&tree) == SOF_RBTREE_OK);

    // A large tree, built in ascending and in random order: the walk's path
    // stays on the c stack, so the array is the only allocation.
    for (int order = 0; order < 2; order++) {
        test_alloc_t state = {0};
        sof_alloc_t alloc = test_allocator(&state);
        uint64_t seed = 0x853c49e6748fea9bULL;
        CHECK(sof_rbtree_init(&tree, compare_int, NULL, NULL, &alloc) == SOF_RBTREE_OK);
        for (uintptr_t k = 1; k <= 200000; k++) {
            uintptr_t key = order == 0 ? k : (uintptr_t)(next_random(&seed) >> 16) + 1;
            CHECK(sof_rbtree_insert(&tree, (void *)key, NULL) != NULL);
        }
        size_t before = state.allocations;
        keys = sof_rbtree_get_keys(&tree, &count);
        CHECK(keys != NULL && count == tree.count && keys[count] == NULL);
        CHECK(state.allocations == before + 1);
        for (size_t i = 1; i < count; i++) {
            CHECK((uintptr_t)keys[i - 1] < (uintptr_t)keys[i]);
        }
        sof_alloc_free(&tree.alloc, keys, (count + 1) * sizeof(void *));
        CHECK(sof_rbtree_free(&tree) == SOF_RBTREE_OK);
        CHECK(state.live == 0);
        CHECK(state.bad == 0);
    }
}

static void test_null_key(void) {
    sof_rbtree_t tree;
    sof_rbtree_node_t *node;
    void **keys;
    size_t count;
    CHECK(sof_rbtree_init(&tree, compare_int, NULL, NULL, NULL) == SOF_RBTREE_OK);
    CHECK(insert_range(&tree, 1, 9));
    node = sof_rbtree_insert(&tree, NULL, NULL);
    CHECK(node != NULL && sof_rbtree_find(&tree, NULL) == node);
    keys = sof_rbtree_get_keys(&tree, &count);
    CHECK(keys != NULL && count == 10 && keys[0] == NULL && (uintptr_t)keys[9] == 9);
    sof_alloc_free(&tree.alloc, keys, (count + 1) * sizeof(void *));
    CHECK(sof_rbtree_minimum(&tree, NULL) == node);
    CHECK(sof_rbtree_remove(&tree, NULL) == SOF_RBTREE_OK && tree.count == 9);
    CHECK(sof_rbtree_find(&tree, NULL) == NULL);
    CHECK(sof_rbtree_free(&tree) == SOF_RBTREE_OK);
}

static void test_minimum_maximum(void) {
    sof_rbtree_t tree;
    sof_rbtree_node_t *root;
    CHECK(sof_rbtree_init(&tree, compare_int, NULL, NULL, NULL) == SOF_RBTREE_OK);
    CHECK(insert_range(&tree, 1, 1000));
    CHECK((uintptr_t)sof_rbtree_minimum(&tree, NULL)->key == 1);
    CHECK((uintptr_t)sof_rbtree_maximum(&tree, NULL)->key == 1000);
    // A subtree's extremes lie on either side of its root.
    root = tree.root;
    CHECK(sof_rbtree_maximum(&tree, root->left) == sof_rbtree_prev(&tree, root));
    CHECK(sof_rbtree_minimum(&tree, root->right) == sof_rbtree_next(&tree, root));
    CHECK(sof_rbtree_minimum(&tree, root) == sof_rbtree_minimum(&tree, NULL));
    // An empty subtree has none.
    root = sof_rbtree_minimum(&tree, NULL);
    CHECK(sof_rbtree_minimum(&tree, root->left) == NULL && sof_rbtree_maximum(&tree, root->left) == NULL);
    CHECK(sof_rbtree_free(&tree) == SOF_RBTREE_OK);
}

static void test_next_prev(void) {
    sof_rbtree_t tree;
    sof_rbtree_node_t *node;
    uintptr_t k;
    CHECK(sof_rbtree_init(&tree, compare_int, NULL, NULL, NULL) == SOF_RBTREE_OK);
    CHECK(insert_range(&tree, 1, 1000));
    k = 1;
    for (node = sof_rbtree_minimum(&tree, NULL); node != NULL; node = sof_rbtree_next(&tree, node)) {
        CHECK((uintptr_t)node->key == k);
        k++;
    }
    CHECK(k == 1001);
    for (node = sof_rbtree_maximum(&tree, NULL); node != NULL; node = sof_rbtree_prev(&tree, node)) {
        k--;
        CHECK((uintptr_t)node->key == k);
    }
    CHECK(k == 1);
    CHECK(sof_rbtree_next(&tree, NULL) == NULL && sof_rbtree_prev(&tree, NULL) == NULL);
    CHECK(sof_rbtree_next(&tree, &tree.nil_node) == NULL && sof_rbtree_prev(&tree, &tree.nil_node) == NULL);
    // Removing while walking works if the next node is taken first.
    for (node = sof_rbtree_minimum(&tree, NULL); node != NULL;) {
        sof_rbtree_node_t *next = sof_rbtree_next(&tree, node);
        if ((uintptr_t)node->key % 2 == 0) {
            CHECK(sof_rbtree_remove_node(&tree, node) == SOF_RBTREE_OK);
        }
        node = next;
    }
    CHECK(tree.count == 500 && check_tree(&tree));
    CHECK(sof_rbtree_free(&tree) == SOF_RBTREE_OK);
}

static void test_traverse(void) {
    sof_rbtree_t tree;
    visit_t v = {0};
    CHECK(sof_rbtree_init(&tree, compare_int, NULL, NULL, NULL) == SOF_RBTREE_OK);
    CHECK(insert_range(&tree, 1, 1000));
    v.tree = &tree;
    v.direction = 1;
    CHECK(sof_rbtree_traverse_ascending(&tree, NULL, visit, &v) == 0);
    CHECK(v.visited == 1000 && !v.out_of_order && (uintptr_t)v.last->key == 1000);
    memset(&v, 0, sizeof(v));
    v.tree = &tree;
    v.direction = -1;
    CHECK(sof_rbtree_traverse_descending(&tree, NULL, visit, &v) == 0);
    CHECK(v.visited == 1000 && !v.out_of_order && (uintptr_t)v.last->key == 1);
    // A NULL callback visits nothing, but isn't an error.
    CHECK(sof_rbtree_traverse_ascending(&tree, NULL, NULL, NULL) == 0);
    CHECK(sof_rbtree_traverse_descending(&tree, NULL, NULL, NULL) == 0);
    CHECK(sof_rbtree_free(&tree) == SOF_RBTREE_OK);
}

static void test_traverse_stop(void) {
    sof_rbtree_t tree;
    visit_t v = {0};
    CHECK(sof_rbtree_init(&tree, compare_int, NULL, NULL, NULL) == SOF_RBTREE_OK);
    CHECK(insert_range(&tree, 1, 1000));
    v.tree = &tree;
    v.direction = 1;
    v.stop_after = 10;
    v.stop_value = 42;
    CHECK(sof_rbtree_traverse_ascending(&tree, NULL, visit, &v) == 42);
    CHECK(v.visited == 10 && (uintptr_t)v.last->key == 10);
    memset(&v, 0, sizeof(v));
    v.tree = &tree;
    v.direction = -1;
    v.stop_after = 1;
    v.stop_value = -7;
    CHECK(sof_rbtree_traverse_descending(&tree, NULL, visit, &v) == -7);
    CHECK(v.visited == 1 && (uintptr_t)v.last->key == 1000);
    CHECK(sof_rbtree_free(&tree) == SOF_RBTREE_OK);
}

static void test_traverse_subtree(void) {
    sof_rbtree_t tree;
    sof_rbtree_node_t *sub;
    visit_t v = {0};
    uintptr_t lo;
    uintptr_t hi;
    CHECK(sof_rbtree_init(&tree, compare_int, NULL, NULL, NULL) == SOF_RBTREE_OK);
    CHECK(insert_range(&tree, 1, 1000));
    sub = tree.root->right;
    lo = (uintptr_t)sof_rbtree_minimum(&tree, sub)->key;
    hi = (uintptr_t)sof_rbtree_maximum(&tree, sub)->key;
    v.tree = &tree;
    v.direction = 1;
    CHECK(sof_rbtree_traverse_ascending(&tree, sub, visit, &v) == 0);
    // A subtree holds a contiguous range of keys.
    CHECK(v.visited == hi - lo + 1 && !v.out_of_order && (uintptr_t)v.last->key == hi);
    memset(&v, 0, sizeof(v));
    v.tree = &tree;
    v.direction = -1;
    CHECK(sof_rbtree_traverse_descending(&tree, sub, visit, &v) == 0);
    CHECK(v.visited == hi - lo + 1 && !v.out_of_order && (uintptr_t)v.last->key == lo);
    // An empty subtree visits nothing.
    memset(&v, 0, sizeof(v));
    sub = sof_rbtree_minimum(&tree, NULL);
    CHECK(sof_rbtree_traverse_ascending(&tree, sub->left, visit, &v) == 0);
    CHECK(sof_rbtree_traverse_descending(&tree, sub->left, visit, &v) == 0 && v.visited == 0);
    CHECK(sof_rbtree_free(&tree) == SOF_RBTREE_OK);
}

static void test_remove(void) {
    sof_rbtree_t tree;
    counts_t counts = {0};
    CHECK(sof_rbtree_init(&tree, compare_int, count_delete, &counts, NULL) == SOF_RBTREE_OK);
    CHECK(insert_range(&tree, 1, 1000));
    for (uintptr_t k = 1; k <= 1000; k += 3) {
        CHECK(sof_rbtree_remove(&tree, (void *)k) == SOF_RBTREE_OK);
    }
    CHECK(counts.deletes == 334 && tree.count == 666 && check_tree(&tree));
    for (uintptr_t k = 1; k <= 1000; k++) {
        CHECK((sof_rbtree_find(&tree, (void *)k) == NULL) == (k % 3 == 1));
    }
    CHECK(sof_rbtree_remove(&tree, (void *)1) == SOF_RBTREE_NO_MATCH);
    CHECK(sof_rbtree_remove(&tree, (void *)5000) == SOF_RBTREE_NO_MATCH);
    CHECK(counts.deletes == 334 && tree.count == 666);
    CHECK(sof_rbtree_free(&tree) == SOF_RBTREE_OK && counts.deletes == 1000);
}

static void test_remove_node(void) {
    sof_rbtree_t tree;
    counts_t counts = {0};
    sof_rbtree_node_t *node;
    sof_rbtree_node_t *other;
    CHECK(sof_rbtree_init(&tree, compare_int, count_delete, &counts, NULL) == SOF_RBTREE_OK);
    CHECK(insert_range(&tree, 1, 100));
    other = sof_rbtree_find(&tree, (void *)99);
    node = sof_rbtree_find(&tree, (void *)50);
    CHECK(node != NULL && sof_rbtree_remove_node(&tree, node) == SOF_RBTREE_OK);
    CHECK(counts.deletes == 1 && tree.count == 99 && sof_rbtree_find(&tree, (void *)50) == NULL);
    // Other nodes don't move.
    CHECK(sof_rbtree_find(&tree, (void *)99) == other);
    CHECK(sof_rbtree_remove_node(&tree, NULL) == SOF_RBTREE_ERROR);
    CHECK(sof_rbtree_remove_node(&tree, &tree.nil_node) == SOF_RBTREE_ERROR);
    CHECK(counts.deletes == 1 && tree.count == 99 && check_tree(&tree));
    // Removing the root over and over empties the tree.
    while (tree.count > 0) {
        CHECK(sof_rbtree_remove_node(&tree, tree.root) == SOF_RBTREE_OK && check_tree(&tree));
    }
    CHECK(counts.deletes == 100 && tree.root == &tree.nil_node);
    CHECK(sof_rbtree_remove_node(&tree, tree.root) == SOF_RBTREE_ERROR);
    CHECK(sof_rbtree_free(&tree) == SOF_RBTREE_OK);
}

static void test_delete_func(void) {
    sof_rbtree_t tree;
    counts_t counts = {0};
    char buf[32];
    int created;
    // Keys the tree holds are allocated here and freed by the del_func; a leak
    // checker shows whether every removal hands the node over.
    CHECK(sof_rbtree_init(&tree, compare_str, free_key_delete, &counts, NULL) == SOF_RBTREE_OK);
    for (int i = 0; i < 100; i++) {
        snprintf(buf, sizeof(buf), "key%03d", i);
        char *key = sof_alloc_strndup(NULL, buf, strlen(buf));
        CHECK(key != NULL);
        CHECK(sof_rbtree_insert(&tree, key, &created) != NULL && created == 1);
    }
    CHECK(sof_rbtree_remove(&tree, "key000") == SOF_RBTREE_OK && counts.deletes == 1);
    CHECK(sof_rbtree_remove_node(&tree, sof_rbtree_find(&tree, "key001")) == SOF_RBTREE_OK && counts.deletes == 2);
    CHECK(sof_rbtree_clear(&tree, tree.root->left) == SOF_RBTREE_OK);
    CHECK(counts.deletes == 100 - tree.count);
    CHECK(sof_rbtree_free(&tree) == SOF_RBTREE_OK && counts.deletes == 100);
}

static void test_clear_subtree(void) {
    sof_rbtree_t tree;
    counts_t counts = {0};
    sof_rbtree_node_t *sub;
    visit_t v = {0};
    uintptr_t lo;
    uintptr_t hi;
    size_t before;
    CHECK(sof_rbtree_init(&tree, compare_int, count_delete, &counts, NULL) == SOF_RBTREE_OK);
    CHECK(insert_range(&tree, 1, 1000));
    sub = tree.root->left;
    v.tree = &tree;
    v.direction = 1;
    CHECK(sof_rbtree_traverse_ascending(&tree, sub, visit, &v) == 0);
    lo = (uintptr_t)sof_rbtree_minimum(&tree, sub)->key;
    hi = (uintptr_t)sof_rbtree_maximum(&tree, sub)->key;
    CHECK(sof_rbtree_clear(&tree, sub) == SOF_RBTREE_OK);
    // Exactly lo..hi are gone, and the rest stays balanced.
    for (uintptr_t k = 1; k <= 1000; k++) {
        CHECK((sof_rbtree_find(&tree, (void *)k) == NULL) == (k >= lo && k <= hi));
    }
    CHECK(counts.deletes == v.visited && tree.count == 1000 - v.visited && check_tree(&tree));
    // An empty subtree removes nothing.
    before = tree.count;
    sub = sof_rbtree_minimum(&tree, NULL);
    CHECK(sof_rbtree_clear(&tree, sub->left) == SOF_RBTREE_OK);
    CHECK(tree.count == before && counts.deletes == v.visited && check_tree(&tree));
    CHECK(sof_rbtree_free(&tree) == SOF_RBTREE_OK && counts.deletes == 1000);
}

static void test_clear(void) {
    sof_rbtree_t tree;
    counts_t counts = {0};
    CHECK(sof_rbtree_init(&tree, compare_int, count_delete, &counts, NULL) == SOF_RBTREE_OK);
    CHECK(insert_range(&tree, 1, 1000));
    CHECK(sof_rbtree_clear(&tree, NULL) == SOF_RBTREE_OK);
    CHECK(counts.deletes == 1000 && tree.count == 0 && sof_rbtree_minimum(&tree, NULL) == NULL);
    // The tree can be used again.
    CHECK(insert_range(&tree, 1, 5) && tree.count == 5 && check_tree(&tree));
    // Clearing from the root is the same as clearing the whole tree.
    CHECK(sof_rbtree_clear(&tree, tree.root) == SOF_RBTREE_OK);
    CHECK(counts.deletes == 1005 && tree.count == 0 && tree.root == &tree.nil_node);
    CHECK(sof_rbtree_free(&tree) == SOF_RBTREE_OK && counts.deletes == 1005);
}

static void test_user_bits(void) {
    sof_rbtree_t tree;
    sof_rbtree_node_t *node;
    int set = 1;
    CHECK(sof_rbtree_init(&tree, compare_int, NULL, NULL, NULL) == SOF_RBTREE_OK);
    CHECK(insert_range(&tree, 1, 100));
    node = tree.root;
    // An expression argument must be masked as a whole, not just its first
    // operand, or it could set the color bit.
    SOF_RBTREE_SET_USER(node, set ? 0xffffffffu : 0);
    CHECK(SOF_RBTREE_COLOR_IS_BLACK(node) && SOF_RBTREE_GET_USER(node) == SOF_RBTREE_USER_MASK);
    // The tree leaves them alone while it rebalances.
    CHECK(insert_range(&tree, 101, 1000) && check_tree(&tree));
    CHECK(SOF_RBTREE_GET_USER(node) == SOF_RBTREE_USER_MASK);
    SOF_RBTREE_SET_USER(node, 0);
    CHECK(SOF_RBTREE_GET_USER(node) == 0 && check_tree(&tree));
    CHECK(sof_rbtree_free(&tree) == SOF_RBTREE_OK);
}

static void test_null(void) {
    int created = 1;
    size_t count = 1;
    CHECK(sof_rbtree_init(NULL, compare_int, NULL, NULL, NULL) == SOF_RBTREE_ERROR);
    CHECK(sof_rbtree_insert(NULL, (void *)1, &created) == NULL && created == 0);
    CHECK(sof_rbtree_find(NULL, (void *)1) == NULL);
    CHECK(sof_rbtree_remove(NULL, (void *)1) == SOF_RBTREE_ERROR);
    CHECK(sof_rbtree_remove_node(NULL, NULL) == SOF_RBTREE_ERROR);
    CHECK(sof_rbtree_minimum(NULL, NULL) == NULL && sof_rbtree_maximum(NULL, NULL) == NULL);
    CHECK(sof_rbtree_next(NULL, NULL) == NULL && sof_rbtree_prev(NULL, NULL) == NULL);
    CHECK(sof_rbtree_get_keys(NULL, &count) == NULL);
    CHECK(sof_rbtree_traverse_ascending(NULL, NULL, visit, NULL) == -1);
    CHECK(sof_rbtree_traverse_descending(NULL, NULL, visit, NULL) == -1);
    CHECK(sof_rbtree_clear(NULL, NULL) == SOF_RBTREE_ERROR);
    CHECK(sof_rbtree_free(NULL) == SOF_RBTREE_ERROR);
}

/// @brief Checks that tree, freed or never initialized, acts empty.
static int acts_empty(sof_rbtree_t *tree) {
    visit_t v = {0};
    int created = 1;
    size_t count = 1;
    void **keys;
    int ok;
    ok = sof_rbtree_insert(tree, (void *)1, &created) == NULL && created == 0;
    ok = ok && sof_rbtree_find(tree, (void *)1) == NULL;
    ok = ok && sof_rbtree_remove(tree, (void *)1) == SOF_RBTREE_NO_MATCH;
    ok = ok && sof_rbtree_minimum(tree, NULL) == NULL && sof_rbtree_maximum(tree, NULL) == NULL;
    ok = ok && sof_rbtree_traverse_ascending(tree, NULL, visit, &v) == 0;
    ok = ok && sof_rbtree_traverse_descending(tree, NULL, visit, &v) == 0 && v.visited == 0;
    keys = sof_rbtree_get_keys(tree, &count);
    ok = ok && keys != NULL && count == 0;
    if (keys != NULL) {
        sof_alloc_free(&tree->alloc, keys, (count + 1) * sizeof(void *));
    }
    ok = ok && sof_rbtree_clear(tree, NULL) == SOF_RBTREE_OK && sof_rbtree_free(tree) == SOF_RBTREE_OK;
    return ok;
}

static void test_failed_init(void) {
    sof_rbtree_t tree;
    CHECK(sof_rbtree_init(&tree, NULL, NULL, NULL, NULL) == SOF_RBTREE_ERROR && tree.root == NULL);
    CHECK(acts_empty(&tree));
    CHECK(sof_rbtree_init(&tree, compare_int, NULL, NULL, NULL) == SOF_RBTREE_OK);
    CHECK(sof_rbtree_free(&tree) == SOF_RBTREE_OK);
}

static void test_freed(void) {
    sof_rbtree_t tree;
    counts_t counts = {0};
    CHECK(sof_rbtree_init(&tree, compare_int, count_delete, &counts, NULL) == SOF_RBTREE_OK);
    CHECK(insert_range(&tree, 1, 5));
    CHECK(sof_rbtree_free(&tree) == SOF_RBTREE_OK && counts.deletes == 5);
    CHECK(tree.root == NULL && tree.count == 0);
    CHECK(acts_empty(&tree) && counts.deletes == 5);
    // It can be initialized again.
    CHECK(sof_rbtree_init(&tree, compare_int, NULL, NULL, NULL) == SOF_RBTREE_OK);
    CHECK(sof_rbtree_insert(&tree, (void *)1, NULL) != NULL && tree.count == 1);
    CHECK(sof_rbtree_free(&tree) == SOF_RBTREE_OK);
}

static void test_words(void) {
    sof_rbtree_t sorted;
    sof_rbtree_t shuffled;
    visit_t v = {0};
    sof_rbtree_node_t *a;
    sof_rbtree_node_t *b;
    CHECK(load_words());
    CHECK(word_count == WORD_COUNT);
    // Insert in file order into one tree and random order into the other;
    // both must come out the same.
    CHECK(sof_rbtree_init(&sorted, compare_str, NULL, NULL, NULL) == SOF_RBTREE_OK);
    CHECK(sof_rbtree_init(&shuffled, compare_str, NULL, NULL, NULL) == SOF_RBTREE_OK);
    for (size_t i = 0; i < word_count; i++) {
        CHECK(sof_rbtree_insert(&sorted, words[i], NULL) != NULL);
    }
    shuffle(words, word_count, 0x2545f4914f6cdd1dULL);
    for (size_t i = 0; i < word_count; i++) {
        CHECK(sof_rbtree_insert(&shuffled, words[i], NULL) != NULL);
    }
    CHECK(sorted.count == word_count && shuffled.count == word_count);
    CHECK(check_tree(&sorted) && check_tree(&shuffled));
    v.tree = &shuffled;
    v.direction = 1;
    CHECK(sof_rbtree_traverse_ascending(&shuffled, NULL, visit, &v) == 0);
    CHECK(v.visited == word_count && !v.out_of_order);
    a = sof_rbtree_minimum(&sorted, NULL);
    b = sof_rbtree_minimum(&shuffled, NULL);
    while (a != NULL && b != NULL) {
        CHECK(strcmp(a->key, b->key) == 0);
        a = sof_rbtree_next(&sorted, a);
        b = sof_rbtree_next(&shuffled, b);
    }
    CHECK(a == NULL && b == NULL);
    for (size_t i = 0; i < word_count; i++) {
        CHECK(sof_rbtree_find(&sorted, words[i]) != NULL);
    }
    // Remove a random half of the words; exactly those are gone.
    for (size_t i = 0; i < word_count / 2; i++) {
        CHECK(sof_rbtree_remove(&shuffled, words[i]) == SOF_RBTREE_OK);
    }
    CHECK(shuffled.count == word_count - word_count / 2 && check_tree(&shuffled));
    for (size_t i = 0; i < word_count; i++) {
        CHECK((sof_rbtree_find(&shuffled, words[i]) == NULL) == (i < word_count / 2));
    }
    CHECK(sof_rbtree_free(&sorted) == SOF_RBTREE_OK && sof_rbtree_free(&shuffled) == SOF_RBTREE_OK);
}

static void test_custom_allocator(void) {
    test_alloc_t state = {0};
    sof_alloc_t alloc = test_allocator(&state);
    counts_t counts = {0};
    sof_rbtree_t tree;
    size_t count = 0;
    CHECK(sof_rbtree_init(&tree, compare_int, count_delete, &counts, &alloc) == SOF_RBTREE_OK);
    // The tree keeps a copy of the allocator.
    alloc.malloc = NULL;
    CHECK(tree.alloc.ctx == &state);
    CHECK(insert_range(&tree, 1, 1000));
    CHECK(state.live == 1000);
    // The key array comes from the tree's allocator.
    void **keys = sof_rbtree_get_keys(&tree, &count);
    CHECK(keys != NULL && count == 1000);
    CHECK(state.live == 1001);
    sof_alloc_free(&tree.alloc, keys, (count + 1) * sizeof(void *));
    CHECK(sof_rbtree_remove(&tree, (void *)(uintptr_t)500) == SOF_RBTREE_OK);
    CHECK(state.live == 999);
    // Clearing part of the tree uses a temporary array, which is freed.
    CHECK(sof_rbtree_clear(&tree, tree.root->left) == SOF_RBTREE_OK);
    CHECK(state.live == (long)tree.count);
    CHECK(check_tree(&tree));
    CHECK(sof_rbtree_free(&tree) == SOF_RBTREE_OK);
    CHECK(state.live == 0);
    CHECK(state.bad == 0);
    CHECK(counts.deletes == 1000);

    // An allocator with a malloc but no free is refused, without allocating.
    size_t allocations = state.allocations;
    alloc = test_allocator(&state);
    alloc.free_sized = NULL;
    CHECK(sof_rbtree_init(&tree, compare_int, NULL, NULL, &alloc) == SOF_RBTREE_ERROR);
    CHECK(tree.root == NULL);
    CHECK(state.allocations == allocations);
}

static void test_no_memory(void) {
    // Fail each allocation in turn, through inserts, getting the keys and
    // clearing a subtree, until a run makes none fail.
    for (size_t n = 1;; n++) {
        test_alloc_t state = {0};
        state.fail_at = n;
        sof_alloc_t alloc = test_allocator(&state);
        sof_rbtree_t tree;
        size_t count = 0;
        int failed = 0;
        CHECK(sof_rbtree_init(&tree, compare_int, NULL, NULL, &alloc) == SOF_RBTREE_OK);
        for (uintptr_t k = 1; k <= 100; k++) {
            failed += sof_rbtree_insert(&tree, (void *)k, NULL) == NULL;
        }
        CHECK(check_tree(&tree));
        CHECK(tree.count == 100 - (size_t)failed);
        void **keys = sof_rbtree_get_keys(&tree, &count);
        if (keys == NULL) {
            failed++;
        } else {
            CHECK(count == tree.count);
            sof_alloc_free(&tree.alloc, keys, (count + 1) * sizeof(void *));
        }
        size_t before = tree.count;
        sof_rbtree_error_t rc = sof_rbtree_clear(&tree, tree.root->right);
        if (rc == SOF_RBTREE_NO_MEM) {
            // A failed clear leaves the tree as it was.
            failed++;
            CHECK(tree.count == before);
        } else {
            CHECK(rc == SOF_RBTREE_OK);
        }
        CHECK(check_tree(&tree));
        // Only the chosen allocation fails.
        CHECK(failed == (state.allocations >= n));
        CHECK(sof_rbtree_free(&tree) == SOF_RBTREE_OK);
        CHECK(state.live == 0);
        CHECK(state.bad == 0);
        if (state.allocations < n) {
            break;
        }
    }
}

/// @brief The data the threaded tests give the node with key, never NULL.
static void *data_for(uintptr_t key) {
    return (void *)(key * 3 + 1);
}

/// @brief A traversal callback for the threaded tests: visit(), and also
/// marks the visit_t out of order if a node's data or user bits aren't what
/// the writer set.
static int check_visit(sof_rbtree_node_t *node, void *ctx) {
    visit_t *v = ctx;
    uintptr_t key = (uintptr_t)node->key;
    if (node->data != data_for(key) || SOF_RBTREE_GET_USER(node) != key) {
        v->out_of_order = 1;
    }
    return visit(node, ctx);
}

/// @brief Walks tree with sof_rbtree_minimum() and sof_rbtree_next() (direction 1)
/// or sof_rbtree_maximum() and sof_rbtree_prev() (-1), checking the order, each
/// node's data and user bits, that the walk is count long and that it ends
/// at the other extreme; returns the number of problems found.
static size_t check_walk(sof_rbtree_t *tree, int direction) {
    size_t walked = 0, errors = 0;
    sof_rbtree_node_t *last = NULL;
    sof_rbtree_node_t *n = direction > 0 ? sof_rbtree_minimum(tree, NULL) : sof_rbtree_maximum(tree, NULL);
    while (n != NULL) {
        uintptr_t key = (uintptr_t)n->key;
        if (last != NULL && compare_int(last->key, n->key, NULL) * direction >= 0) {
            errors++;
        }
        if (n->data != data_for(key) || SOF_RBTREE_GET_USER(n) != key) {
            errors++;
        }
        last = n;
        if (++walked > tree->count) {
            // A loop in a broken tree; stop rather than walk forever.
            return errors + 1;
        }
        n = direction > 0 ? sof_rbtree_next(tree, n) : sof_rbtree_prev(tree, n);
    }
    if (walked != tree->count) {
        errors++;
    }
    if (last != (direction > 0 ? sof_rbtree_maximum(tree, NULL) : sof_rbtree_minimum(tree, NULL))) {
        errors++;
    }
    return errors;
}

/// @brief A test allocator behind a mutex, so that readers holding only the
/// read lock can call sof_rbtree_get_keys() on a shared tree.
typedef struct locked_alloc_t {
    pthread_mutex_t lock;
    test_alloc_t state;
} locked_alloc_t;

static void locked_free_sized(void *p, size_t size, void *ctx) {
    locked_alloc_t *l = ctx;
    pthread_mutex_lock(&l->lock);
    test_alloc_free_sized(p, size, &l->state);
    pthread_mutex_unlock(&l->lock);
}

static void *locked_malloc(size_t size, void *ctx) {
    locked_alloc_t *l = ctx;
    pthread_mutex_lock(&l->lock);
    void *p = test_alloc_malloc(size, &l->state);
    pthread_mutex_unlock(&l->lock);
    return p;
}

/// @brief A tree shared by one writer and several readers under a
/// readers-writer lock, as in the README's Threading section.
typedef struct rw_shared_t {
    sof_rbtree_t tree;
    pthread_rwlock_t lock;
} rw_shared_t;

/// @brief One reader thread's state and results, checked by the main thread
/// after joining, since CHECK isn't safe on other threads. Readers make a
/// fixed number of passes rather than reading until the writer is done:
/// glibc's rwlock prefers readers, so readers that keep taking the lock can
/// keep the writer out forever.
typedef struct reader_t {
    rw_shared_t *shared;
    uint64_t seed;
    size_t passes;
    /// @brief Finds that found a node, and calls of sof_rbtree_get_keys().
    size_t found;
    size_t key_arrays;
    /// @brief Anything wrong the reader saw.
    size_t errors;
} reader_t;

/// @brief Makes PASSES passes over the shared tree, each under one read
/// lock, checking the tree with every read-only function.
static void *read_shared(void *arg) {
    reader_t *r = arg;
    rw_shared_t *s = r->shared;
    sof_rbtree_t *tree = &s->tree;
    uint64_t random = r->seed;
    for (size_t pass = 0; pass < PASSES; pass++) {
        pthread_rwlock_rdlock(&s->lock);
        if (!check_tree(tree)) {
            r->errors++;
        }
        // Walks with sof_rbtree_next() and sof_rbtree_prev(), the lock held throughout.
        r->errors += check_walk(tree, 1) + check_walk(tree, -1);
        for (int direction = 1; direction >= -1; direction -= 2) {
            visit_t v = {0};
            v.tree = tree;
            v.direction = direction;
            int rc = direction > 0 ? sof_rbtree_traverse_ascending(tree, NULL, check_visit, &v) : sof_rbtree_traverse_descending(tree, NULL, check_visit, &v);
            if (rc != 0 || v.out_of_order || v.visited != tree->count) {
                r->errors++;
            }
        }
        for (size_t i = 0; i < FINDS; i++) {
            uintptr_t key = 1 + next_random(&random) % THREAD_KEYS;
            sof_rbtree_node_t *n = sof_rbtree_find(tree, (void *)key);
            if (n != NULL) {
                r->found++;
                if (n->key != (void *)key || n->data != data_for(key) || SOF_RBTREE_GET_USER(n) != key) {
                    r->errors++;
                }
            }
        }
        sof_rbtree_node_t *min = sof_rbtree_minimum(tree, NULL);
        if (min != NULL && sof_rbtree_find(tree, min->key) != min) {
            r->errors++;
        }
        if (pass % 16 == 0) {
            size_t count = 0;
            void **keys = sof_rbtree_get_keys(tree, &count);
            if (keys == NULL || count != tree->count) {
                r->errors++;
            } else {
                for (size_t i = 1; i < count; i++) {
                    if ((uintptr_t)keys[i - 1] >= (uintptr_t)keys[i]) {
                        r->errors++;
                    }
                }
            }
            if (keys != NULL) {
                sof_alloc_free(&tree->alloc, keys, (count + 1) * sizeof(void *));
                r->key_arrays++;
            }
        }
        pthread_rwlock_unlock(&s->lock);
        r->passes++;
    }
    return NULL;
}

static void test_threads_readers(void) {
    // One writer inserts and removes random keys, setting each new node's
    // data and user bits before releasing the write lock, while readers
    // check the tree under a read lock with every read-only function. They
    // must always see a valid red-black tree, sorted, with walks as long as
    // count and no node without its data.
    locked_alloc_t locked = {0};
    CHECK(pthread_mutex_init(&locked.lock, NULL) == 0);
    sof_alloc_t alloc = {0};
    alloc.malloc = locked_malloc;
    alloc.free_sized = locked_free_sized;
    alloc.ctx = &locked;
    counts_t counts = {0};
    rw_shared_t s;
    CHECK(sof_rbtree_init(&s.tree, compare_int, count_delete, &counts, &alloc) == SOF_RBTREE_OK);
    CHECK(pthread_rwlock_init(&s.lock, NULL) == 0);
    reader_t readers[READERS] = {{0}};
    pthread_t threads[READERS];
    for (size_t i = 0; i < READERS; i++) {
        readers[i].shared = &s;
        readers[i].seed = 0x9e3779b97f4a7c15ULL * (i + 1);
        CHECK(pthread_create(&threads[i], NULL, read_shared, &readers[i]) == 0);
    }
    uint8_t present[THREAD_KEYS + 1] = {0};
    uint64_t random = 0x2545f4914f6cdd1dULL;
    size_t errors = 0, inserts = 0, removes = 0;
    for (size_t i = 0; i < WRITES; i++) {
        uintptr_t key = 1 + next_random(&random) % THREAD_KEYS;
        pthread_rwlock_wrlock(&s.lock);
        if (present[key]) {
            sof_rbtree_error_t rc;
            if (i % 2 == 0) {
                rc = sof_rbtree_remove(&s.tree, (void *)key);
            } else {
                rc = sof_rbtree_remove_node(&s.tree, sof_rbtree_find(&s.tree, (void *)key));
            }
            errors += rc != SOF_RBTREE_OK;
            present[key] = 0;
            removes++;
        } else {
            int created = 0;
            sof_rbtree_node_t *n = sof_rbtree_insert(&s.tree, (void *)key, &created);
            if (n == NULL || !created) {
                errors++;
            } else {
                n->data = data_for(key);
                SOF_RBTREE_SET_USER(n, key);
                // Inserting a key already there is a write, and changes nothing.
                if (sof_rbtree_insert(&s.tree, (void *)key, &created) != n || created) {
                    errors++;
                }
            }
            present[key] = 1;
            inserts++;
        }
        pthread_rwlock_unlock(&s.lock);
    }
    for (size_t i = 0; i < READERS; i++) {
        CHECK(pthread_join(threads[i], NULL) == 0);
    }
    CHECK(errors == 0);
    size_t key_arrays = 0;
    for (size_t i = 0; i < READERS; i++) {
        CHECK(readers[i].errors == 0);
        CHECK(readers[i].passes == PASSES);
        CHECK(readers[i].key_arrays == (PASSES + 15) / 16);
        key_arrays += readers[i].key_arrays;
    }
    // The tree ends up holding exactly the keys the writer left in it.
    size_t expected = 0;
    for (uintptr_t key = 1; key <= THREAD_KEYS; key++) {
        sof_rbtree_node_t *n = sof_rbtree_find(&s.tree, (void *)key);
        CHECK((n != NULL) == present[key]);
        expected += present[key];
    }
    CHECK(s.tree.count == expected && check_tree(&s.tree));
    CHECK(check_walk(&s.tree, 1) == 0 && check_walk(&s.tree, -1) == 0);
    CHECK(counts.deletes == removes);
    CHECK(sof_rbtree_free(&s.tree) == SOF_RBTREE_OK);
    CHECK(counts.deletes == inserts);
    // A node per insert, and a key array per sof_rbtree_get_keys().
    CHECK(locked.state.allocations == inserts + key_arrays);
    CHECK(locked.state.live == 0);
    CHECK(locked.state.bad == 0);
    pthread_rwlock_destroy(&s.lock);
    pthread_mutex_destroy(&locked.lock);
}

/// @brief One thread's own tree, made from seed, and what the thread found,
/// checked by the main thread after joining.
typedef struct own_tree_t {
    uint64_t seed;
    test_alloc_t state;
    size_t inserts;
    size_t removes;
    size_t deletes;
    /// @brief The tree's count at the end, and the sum of its keys.
    size_t count;
    uintptr_t key_sum;
    /// @brief Anything wrong the thread saw.
    size_t errors;
} own_tree_t;

/// @brief Makes WRITES random inserts and removes in a tree of the thread's
/// own, with its own allocator and ctx and no lock, then checks the tree
/// against what it should hold and frees it.
static void *use_own_tree(void *arg) {
    own_tree_t *o = arg;
    counts_t counts = {0};
    sof_alloc_t alloc = test_allocator(&o->state);
    sof_rbtree_t tree;
    uint8_t present[THREAD_KEYS + 1] = {0};
    uint64_t random = o->seed;
    if (sof_rbtree_init(&tree, compare_int_counting, count_delete, &counts, &alloc) != SOF_RBTREE_OK) {
        o->errors++;
        return NULL;
    }
    for (size_t i = 0; i < WRITES; i++) {
        uintptr_t key = 1 + next_random(&random) % THREAD_KEYS;
        if (present[key]) {
            o->errors += sof_rbtree_remove(&tree, (void *)key) != SOF_RBTREE_OK;
            present[key] = 0;
            o->removes++;
        } else {
            sof_rbtree_node_t *n = sof_rbtree_insert(&tree, (void *)key, NULL);
            if (n == NULL) {
                o->errors++;
            } else {
                n->data = data_for(key);
                SOF_RBTREE_SET_USER(n, key);
            }
            present[key] = 1;
            o->inserts++;
        }
    }
    if (!check_tree(&tree) || check_walk(&tree, 1) != 0 || check_walk(&tree, -1) != 0) {
        o->errors++;
    }
    for (uintptr_t key = 1; key <= THREAD_KEYS; key++) {
        sof_rbtree_node_t *n = sof_rbtree_find(&tree, (void *)key);
        if ((n != NULL) != present[key] || (n != NULL && n->data != data_for(key))) {
            o->errors++;
        }
    }
    size_t count = 0;
    void **keys = sof_rbtree_get_keys(&tree, &count);
    if (keys == NULL || count != tree.count) {
        o->errors++;
    } else {
        for (size_t i = 0; i < count; i++) {
            if (i > 0 && (uintptr_t)keys[i - 1] >= (uintptr_t)keys[i]) {
                o->errors++;
            }
            o->key_sum += (uintptr_t)keys[i];
        }
    }
    if (keys != NULL) {
        sof_alloc_free(&tree.alloc, keys, (count + 1) * sizeof(void *));
    }
    o->count = tree.count;
    if (counts.compares == 0) {
        o->errors++;
    }
    if (sof_rbtree_free(&tree) != SOF_RBTREE_OK) {
        o->errors++;
    }
    o->deletes = counts.deletes;
    return NULL;
}

static void test_threads_separate(void) {
    // Each thread has a tree of its own, with its own allocator and ctx, and
    // no lock. Replaying each thread's random keys here says what its tree
    // should have held.
    own_tree_t own[TREE_THREADS] = {{0}};
    pthread_t threads[TREE_THREADS];
    for (size_t i = 0; i < TREE_THREADS; i++) {
        own[i].seed = 0x2545f4914f6cdd1dULL * (i + 1);
        CHECK(pthread_create(&threads[i], NULL, use_own_tree, &own[i]) == 0);
    }
    for (size_t i = 0; i < TREE_THREADS; i++) {
        CHECK(pthread_join(threads[i], NULL) == 0);
    }
    for (size_t i = 0; i < TREE_THREADS; i++) {
        uint8_t present[THREAD_KEYS + 1] = {0};
        uint64_t random = own[i].seed;
        size_t inserts = 0, removes = 0, count = 0;
        uintptr_t key_sum = 0;
        for (size_t j = 0; j < WRITES; j++) {
            uintptr_t key = 1 + next_random(&random) % THREAD_KEYS;
            if (present[key]) {
                removes++;
            } else {
                inserts++;
            }
            present[key] = !present[key];
        }
        for (uintptr_t key = 1; key <= THREAD_KEYS; key++) {
            count += present[key];
            key_sum += present[key] ? key : 0;
        }
        CHECK(own[i].errors == 0);
        CHECK(own[i].inserts == inserts && own[i].removes == removes);
        CHECK(own[i].count == count && own[i].key_sum == key_sum);
        CHECK(own[i].deletes == inserts);
        // A node per insert, and sof_rbtree_get_keys()'s array.
        CHECK(own[i].state.allocations == inserts + 1);
        CHECK(own[i].state.live == 0);
        CHECK(own[i].state.bad == 0);
    }
}

/// @brief The functions that only read take a const tree and const nodes,
/// and hand back nodes whose data can still be changed.
static void test_const_tree(void) {
    sof_rbtree_t tree;
    visit_t v = {0};
    size_t count = 0;
    uintptr_t k = 0;
    CHECK(sof_rbtree_init(&tree, compare_int, NULL, NULL, NULL) == SOF_RBTREE_OK);
    CHECK(insert_range(&tree, 1, 100));
    const sof_rbtree_t *ctree = &tree;
    sof_rbtree_node_t *n = sof_rbtree_find(ctree, (void *)(uintptr_t)50);
    CHECK(n != NULL && (uintptr_t)n->key == 50);
    n->data = data_for(50);
    CHECK(sof_rbtree_find(ctree, (void *)(uintptr_t)50)->data == data_for(50));
    CHECK(sof_rbtree_find(ctree, (void *)(uintptr_t)101) == NULL);
    const sof_rbtree_node_t *cn = n;
    CHECK((uintptr_t)sof_rbtree_next(ctree, cn)->key == 51);
    CHECK((uintptr_t)sof_rbtree_prev(ctree, cn)->key == 49);
    CHECK((uintptr_t)sof_rbtree_minimum(ctree, NULL)->key == 1);
    CHECK((uintptr_t)sof_rbtree_maximum(ctree, NULL)->key == 100);
    CHECK(sof_rbtree_minimum(ctree, cn) == sof_rbtree_minimum(&tree, n));
    CHECK(sof_rbtree_maximum(ctree, cn) == sof_rbtree_maximum(&tree, n));
    for (n = sof_rbtree_minimum(ctree, NULL); n != NULL; n = sof_rbtree_next(ctree, n)) {
        CHECK((uintptr_t)n->key == ++k);
    }
    CHECK(k == 100);
    v.tree = &tree;
    v.direction = 1;
    CHECK(sof_rbtree_traverse_ascending(ctree, NULL, visit, &v) == 0);
    CHECK(v.visited == 100 && !v.out_of_order);
    memset(&v, 0, sizeof(v));
    v.tree = &tree;
    v.direction = -1;
    CHECK(sof_rbtree_traverse_descending(ctree, ctree->root, visit, &v) == 0);
    CHECK(v.visited == 100 && !v.out_of_order);
    void **keys = sof_rbtree_get_keys(ctree, &count);
    CHECK(keys != NULL && count == 100 && (uintptr_t)keys[0] == 1 && (uintptr_t)keys[99] == 100);
    sof_alloc_free(&ctree->alloc, keys, (count + 1) * sizeof(void *));
    CHECK(check_tree(&tree));
    CHECK(sof_rbtree_free(&tree) == SOF_RBTREE_OK);
}

int main(void) {
    printf("sof_rbtree:\n");
    run("init", test_init);
    run("empty tree", test_empty);
    run("insert and find", test_insert_find);
    run("insert an existing key", test_insert_existing);
    run("get keys", test_get_keys);
    run("NULL key", test_null_key);
    run("minimum and maximum", test_minimum_maximum);
    run("next and prev", test_next_prev);
    run("traverse", test_traverse);
    run("traversals stop early", test_traverse_stop);
    run("traverse a subtree", test_traverse_subtree);
    run("read through a const tree", test_const_tree);
    run("remove", test_remove);
    run("remove a node", test_remove_node);
    run("del_func sees every removal", test_delete_func);
    run("clear a subtree", test_clear_subtree);
    run("clear the whole tree", test_clear);
    run("user bits", test_user_bits);
    run("NULL arguments", test_null);
    run("failed init", test_failed_init);
    run("freed tree", test_freed);
    run("word list", test_words);
    run("custom allocator", test_custom_allocator);
    run("out of memory", test_no_memory);
    run("readers alongside a writer on threads", test_threads_readers);
    run("separate trees on separate threads", test_threads_separate);
    sof_alloc_free(NULL, words, words_size);
    sof_alloc_free(NULL, word_buffer, word_buffer_size);
    return test_report();
}
