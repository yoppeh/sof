/**
 * @file sof_json_doc.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief The JSON document of sof_json, and the functions that read its
 * values.
 * @details A document reads a value with a reader, a token at a time,
 * without recursion. Each value, key and string is allocated in the arena
 * as its token is read, so its address never changes. An object or array
 * that is open is a frame on the frames stack, and its members or elements
 * are pushed on the members or elements stack as they are read; when it
 * ends, they are copied into an array in the arena, and the stack is cut
 * back to where it began. The stacks are kept from one value to the next,
 * so their memory is reused.
 *
 * Objects with more than SOF_JSON_INDEX_MIN members are put in the index,
 * one hashmap for the whole document, whose keys are the members
 * themselves, hashed and compared by their object and key, and whose data
 * is the last member with that key.
 * @version 0.0.1
 * @date 2026-11-19
 * @copyright Copyright (c) 2026
 */

#include <stdint.h>
#include <string.h>

#include "sof_hash.h"
#include "sof_json.h"

/// @brief The usable size of each block of the document's arena.
#define DOC_BLOCK 8192

/// @brief An object or array that is open while a value is read.
typedef struct frame_t {
    /// @brief The object or array.
    sof_json_value_t *value;
    /// @brief Where its members or elements begin on their stack.
    size_t start;
} frame_t;

static sof_json_error_t attach(sof_json_doc_t *d, sof_json_value_t *v, const char *key, size_t key_length, sof_json_span_t key_span);
static sof_json_error_t build(sof_json_doc_t *d, sof_json_reader_t *r);
static sof_json_error_t close_frame(sof_json_doc_t *d, size_t end);
static char *copy_text(sof_json_doc_t *d, const char *text, size_t length);
static sof_json_error_t index_object(sof_json_doc_t *d, sof_json_value_t *object);
static int member_equal(const void *a, const void *b, void *ctx);
static size_t member_hash(const void *key, void *ctx);
static void reset(sof_json_doc_t *d);
static int token_equal(const char *token, size_t token_length, const char *key, size_t key_length);
static int usable(const sof_json_doc_t *d);

/// @brief How the index hashes and compares its keys, which are members.
static const sof_hashmap_key_callbacks_t member_keys = {member_hash, member_equal, NULL, NULL};

/// @brief Puts a value read into the document: as the root, the next
/// element of the array open last, or the value of the member of the object
/// open last whose key is key, written at key_span.
static sof_json_error_t attach(sof_json_doc_t *d, sof_json_value_t *v, const char *key, size_t key_length, sof_json_span_t key_span) {
    frame_t *frame = sof_stack_peek(&d->frames);
    if (frame == NULL) {
        d->root = v;
        return SOF_JSON_OK;
    }
    sof_stack_error_t rc;
    if (frame->value->type == SOF_JSON_ARRAY) {
        const sof_json_value_t *element = v;
        rc = sof_stack_push(&d->elements, &element);
    } else {
        sof_json_member_t member = {key, key_length, key_span, v, frame->value};
        rc = sof_stack_push(&d->members, &member);
    }
    return rc == SOF_STACK_OK ? SOF_JSON_OK : SOF_JSON_NO_MEM;
}

/// @brief Reads a value from the reader into the document, which is empty.
static sof_json_error_t build(sof_json_doc_t *d, sof_json_reader_t *r) {
    const char *key = NULL;
    size_t key_length = 0;
    sof_json_span_t key_span = {0, 0};
    for (;;) {
        sof_json_token_t t;
        sof_json_error_t rc = sof_json_reader_next(r, &t);
        if (rc != SOF_JSON_OK) {
            return rc;
        }
        int outside = d->frames.count == 0;
        switch (t.type) {
        case SOF_JSON_KEY:
            if (outside) {
                return SOF_JSON_ERROR;
            }
            key_length = t.length;
            key_span = t.span;
            key = copy_text(d, t.text, t.length);
            if (key == NULL) {
                return SOF_JSON_NO_MEM;
            }
            continue;
        case SOF_JSON_ARRAY_END:
        case SOF_JSON_OBJECT_END:
            // The end of an array or object the value began in, or, outside
            // of any, of the one the reader was in.
            if (outside) {
                return SOF_JSON_NO_MATCH;
            }
            rc = close_frame(d, t.span.offset + t.span.length);
            if (rc != SOF_JSON_OK || d->frames.count == 0) {
                return rc;
            }
            continue;
        default:
            break;
        }
        // A string or number's text is allocated with its value, just
        // after it, with a NUL.
        int has_text = t.type == SOF_JSON_STRING || t.type == SOF_JSON_NUMBER;
        if (has_text && t.length > SIZE_MAX - sizeof(sof_json_value_t) - 1) {
            return SOF_JSON_NO_MEM;
        }
        sof_json_value_t *v = sof_mem_arena_alloc(&d->arena, sizeof(*v) + (has_text ? t.length + 1 : 0));
        if (v == NULL) {
            return SOF_JSON_NO_MEM;
        }
        memset(v, 0, sizeof(*v));
        v->type = t.type;
        // An array or object's span is its [ or { until close_frame()
        // stretches it to its end.
        v->span = t.span;
        if (t.type == SOF_JSON_BOOL) {
            v->as.boolean = t.boolean;
        } else if (has_text) {
            char *text = (char *)(v + 1);
            memcpy(text, t.text, t.length);
            text[t.length] = '\0';
            v->length = t.length;
            v->as.text = text;
        }
        rc = attach(d, v, key, key_length, key_span);
        if (rc != SOF_JSON_OK) {
            return rc;
        }
        if (t.type == SOF_JSON_ARRAY || t.type == SOF_JSON_OBJECT) {
            frame_t frame = {v, t.type == SOF_JSON_ARRAY ? d->elements.count : d->members.count};
            if (sof_stack_push(&d->frames, &frame) != SOF_STACK_OK) {
                return SOF_JSON_NO_MEM;
            }
        } else if (outside) {
            return SOF_JSON_OK;
        }
    }
}

/// @brief Ends the object or array open last, whose ] or } ends at end,
/// giving it its members or elements.
static sof_json_error_t close_frame(sof_json_doc_t *d, size_t end) {
    frame_t frame;
    sof_stack_pop(&d->frames, &frame);
    sof_json_value_t *v = frame.value;
    v->span.length = end - v->span.offset;
    sof_stack_t *items = v->type == SOF_JSON_ARRAY ? &d->elements : &d->members;
    size_t n = items->count - frame.start;
    if (n > 0) {
        size_t size = n * items->element_size;
        void *copy = sof_mem_arena_alloc(&d->arena, size);
        if (copy == NULL) {
            return SOF_JSON_NO_MEM;
        }
        memcpy(copy, items->elements + frame.start * items->element_size, size);
        if (v->type == SOF_JSON_ARRAY) {
            v->as.elements = copy;
        } else {
            v->as.object.members = copy;
        }
    }
    v->length = n;
    sof_stack_truncate(items, frame.start);
    if (v->type == SOF_JSON_OBJECT && n > SOF_JSON_INDEX_MIN) {
        return index_object(d, v);
    }
    return SOF_JSON_OK;
}

/// @brief Copies text, of length bytes, into the arena, with a NUL after
/// it.
/// @return The copy, or NULL if there isn't the memory.
static char *copy_text(sof_json_doc_t *d, const char *text, size_t length) {
    char *copy = sof_mem_arena_alloc(&d->arena, length + 1);
    if (copy != NULL) {
        memcpy(copy, text, length);
        copy[length] = '\0';
    }
    return copy;
}

/// @brief Puts an object's members in the index, which is made the first
/// time.
static sof_json_error_t index_object(sof_json_doc_t *d, sof_json_value_t *object) {
    if (d->index == NULL) {
        sof_hashmap_t *map = sof_mem_arena_alloc(&d->arena, sizeof(*map));
        if (map == NULL || sof_hashmap_init(map, &member_keys, NULL, NULL, object->length, &d->alloc) != SOF_HASHMAP_OK) {
            return SOF_JSON_NO_MEM;
        }
        d->index = map;
    }
    if (sof_hashmap_reserve(d->index, d->index->count + object->length) != SOF_HASHMAP_OK) {
        return SOF_JSON_NO_MEM;
    }
    // A key there more than once keeps its first member as the map's key,
    // and its last as the data, which is the one found.
    for (size_t i = 0; i < object->length; i++) {
        const sof_json_member_t *member = &object->as.object.members[i];
        sof_hashmap_element_t *e = sof_hashmap_insert(d->index, member, NULL);
        if (e == NULL) {
            return SOF_JSON_NO_MEM;
        }
        e->data = (void *)member;
    }
    object->as.object.index = d->index;
    return SOF_JSON_OK;
}

/// @brief Says whether two members are of the same object and have the
/// same key.
static int member_equal(const void *a, const void *b, void *ctx) {
    const sof_json_member_t *ma = a, *mb = b;
    (void)ctx;
    return ma->object == mb->object && ma->key_length == mb->key_length && memcmp(ma->key, mb->key, ma->key_length) == 0;
}

/// @brief Hashes a member by its key, seeded with its object's address.
static size_t member_hash(const void *key, void *ctx) {
    const sof_json_member_t *m = key;
    (void)ctx;
    return (size_t)sof_hash_bytes(m->key, m->key_length, (uint64_t)(uintptr_t)m->object);
}

/// @brief Empties the document, keeping its memory.
static void reset(sof_json_doc_t *d) {
    if (d->index != NULL) {
        sof_hashmap_free(d->index);
        d->index = NULL;
    }
    sof_mem_arena_clear(&d->arena);
    sof_stack_clear(&d->frames);
    sof_stack_clear(&d->elements);
    sof_stack_clear(&d->members);
    d->root = NULL;
    d->line = 0;
    d->column = 0;
    d->offset = 0;
}

/// @brief Says whether a JSON Pointer's token, with its ~0 and ~1 escapes,
/// stands for a key.
static int token_equal(const char *token, size_t token_length, const char *key, size_t key_length) {
    size_t i = 0, j = 0;
    while (i < token_length && j < key_length) {
        char c = token[i++];
        if (c == '~') {
            c = token[i++] == '0' ? '~' : '/';
        }
        if (c != key[j++]) {
            return 0;
        }
    }
    return i == token_length && j == key_length;
}

/// @brief Says whether d is a document that can be used: not NULL, and
/// initialized.
static int usable(const sof_json_doc_t *d) {
    return d != NULL && d->ready;
}

sof_json_error_t sof_json_doc_clear(sof_json_doc_t *d) {
    if (!usable(d)) {
        return SOF_JSON_ERROR;
    }
    reset(d);
    return SOF_JSON_OK;
}

sof_json_error_t sof_json_doc_free(sof_json_doc_t *d) {
    if (d == NULL) {
        return SOF_JSON_ERROR;
    }
    if (d->ready) {
        if (d->index != NULL) {
            sof_hashmap_free(d->index);
        }
        sof_mem_arena_free(&d->arena);
        sof_stack_free(&d->frames);
        sof_stack_free(&d->elements);
        sof_stack_free(&d->members);
    }
    memset(d, 0, sizeof(*d));
    return SOF_JSON_OK;
}

sof_json_error_t sof_json_doc_init(sof_json_doc_t *d, const sof_alloc_t *alloc) {
    if (d == NULL) {
        return SOF_JSON_ERROR;
    }
    memset(d, 0, sizeof(*d));
    if (!sof_alloc_valid(alloc)) {
        return SOF_JSON_ERROR;
    }
    // None of these allocates until it is first used.
    if (sof_mem_arena_init(&d->arena, DOC_BLOCK, alloc) != SOF_MEM_OK || sof_stack_init(&d->frames, sizeof(frame_t), 0, NULL, NULL, alloc) != SOF_STACK_OK || sof_stack_init(&d->elements, sizeof(const sof_json_value_t *), 0, NULL, NULL, alloc) != SOF_STACK_OK || sof_stack_init(&d->members, sizeof(sof_json_member_t), 0, NULL, NULL, alloc) != SOF_STACK_OK) {
        sof_mem_arena_free(&d->arena);
        sof_stack_free(&d->frames);
        sof_stack_free(&d->elements);
        memset(d, 0, sizeof(*d));
        return SOF_JSON_NO_MEM;
    }
    if (alloc != NULL) {
        d->alloc = *alloc;
    }
    d->ready = 1;
    return SOF_JSON_OK;
}

sof_json_error_t sof_json_doc_parse(sof_json_doc_t *d, const void *text, size_t length) {
    sof_json_reader_t r;
    if (!usable(d) || (text == NULL && length != 0)) {
        return SOF_JSON_ERROR;
    }
    reset(d);
    sof_json_error_t rc = sof_json_reader_init_bytes(&r, 0, text, length, &d->alloc);
    if (rc != SOF_JSON_OK) {
        return rc;
    }
    rc = sof_json_doc_read(d, &r);
    if (rc == SOF_JSON_OK) {
        // Nothing but whitespace may follow the value.
        rc = sof_json_reader_next(&r, NULL);
        if (rc == SOF_JSON_NO_MATCH) {
            rc = SOF_JSON_OK;
        } else {
            reset(d);
            d->line = r.line;
            d->column = r.column;
            d->offset = r.offset;
        }
    }
    sof_json_reader_free(&r);
    return rc;
}

sof_json_error_t sof_json_doc_read(sof_json_doc_t *d, sof_json_reader_t *r) {
    if (!usable(d) || r == NULL || !r->ready) {
        return SOF_JSON_ERROR;
    }
    reset(d);
    sof_json_error_t rc = build(d, r);
    if (rc != SOF_JSON_OK) {
        reset(d);
        if (rc == SOF_JSON_SYNTAX || rc == SOF_JSON_TOO_DEEP) {
            d->line = r->line;
            d->column = r->column;
            d->offset = r->offset;
        }
    }
    return rc;
}

const sof_json_value_t *sof_json_value_at(const sof_json_value_t *array, size_t index) {
    if (array == NULL || array->type != SOF_JSON_ARRAY || index >= array->length) {
        return NULL;
    }
    return array->as.elements[index];
}

sof_json_error_t sof_json_value_bool(const sof_json_value_t *v, int *value) {
    if (value == NULL) {
        return SOF_JSON_ERROR;
    }
    if (v == NULL || v->type != SOF_JSON_BOOL) {
        return SOF_JSON_NO_MATCH;
    }
    *value = v->as.boolean;
    return SOF_JSON_OK;
}

size_t sof_json_value_count(const sof_json_value_t *v) {
    if (v == NULL || (v->type != SOF_JSON_ARRAY && v->type != SOF_JSON_OBJECT)) {
        return 0;
    }
    return v->length;
}

sof_json_error_t sof_json_value_double(const sof_json_value_t *v, double *value) {
    if (value == NULL) {
        return SOF_JSON_ERROR;
    }
    if (v == NULL || v->type != SOF_JSON_NUMBER) {
        return SOF_JSON_NO_MATCH;
    }
    return sof_json_number_double(v->as.text, v->length, value);
}

const sof_json_value_t *sof_json_value_get(const sof_json_value_t *object, const char *key) {
    if (key == NULL) {
        return NULL;
    }
    return sof_json_value_get_bytes(object, key, strlen(key));
}

const sof_json_value_t *sof_json_value_get_bytes(const sof_json_value_t *object, const void *key, size_t length) {
    if (object == NULL || object->type != SOF_JSON_OBJECT || (key == NULL && length != 0)) {
        return NULL;
    }
    if (key == NULL) {
        key = "";
    }
    if (object->as.object.index != NULL) {
        sof_json_member_t probe = {key, length, {0, 0}, NULL, object};
        sof_hashmap_element_t *e = sof_hashmap_find(object->as.object.index, &probe);
        return e != NULL ? ((const sof_json_member_t *)e->data)->value : NULL;
    }
    // The last member with the key is the one found.
    for (size_t i = object->length; i > 0; i--) {
        const sof_json_member_t *m = &object->as.object.members[i - 1];
        if (m->key_length == length && memcmp(m->key, key, length) == 0) {
            return m->value;
        }
    }
    return NULL;
}

sof_json_error_t sof_json_value_int(const sof_json_value_t *v, int64_t *value) {
    if (value == NULL) {
        return SOF_JSON_ERROR;
    }
    if (v == NULL || v->type != SOF_JSON_NUMBER) {
        return SOF_JSON_NO_MATCH;
    }
    return sof_json_number_int(v->as.text, v->length, value);
}

const sof_json_member_t *sof_json_value_member(const sof_json_value_t *object, size_t index) {
    if (object == NULL || object->type != SOF_JSON_OBJECT || index >= object->length) {
        return NULL;
    }
    return &object->as.object.members[index];
}

const sof_json_value_t *sof_json_value_pointer(const sof_json_value_t *v, const char *pointer) {
    if (v == NULL || pointer == NULL || (pointer[0] != '\0' && pointer[0] != '/')) {
        return NULL;
    }
    while (v != NULL && *pointer == '/') {
        const char *token = pointer + 1;
        size_t length = strcspn(token, "/");
        pointer = token + length;
        int escaped = 0;
        for (size_t i = 0; i < length; i++) {
            if (token[i] == '~') {
                if (i + 1 == length || (token[i + 1] != '0' && token[i + 1] != '1')) {
                    return NULL;
                }
                escaped = 1;
                i++;
            }
        }
        if (v->type == SOF_JSON_OBJECT) {
            if (!escaped) {
                v = sof_json_value_get_bytes(v, token, length);
                continue;
            }
            // A key with escapes is compared with each key in turn, the last
            // member with it being the one found.
            const sof_json_value_t *found = NULL;
            for (size_t i = v->length; found == NULL && i > 0; i--) {
                const sof_json_member_t *m = &v->as.object.members[i - 1];
                if (token_equal(token, length, m->key, m->key_length)) {
                    found = m->value;
                }
            }
            v = found;
        } else if (v->type == SOF_JSON_ARRAY) {
            // An index is 0, or digits that don't begin with 0.
            size_t index = 0;
            if (length == 0 || (token[0] == '0' && length > 1)) {
                return NULL;
            }
            for (size_t i = 0; i < length; i++) {
                if (token[i] < '0' || token[i] > '9' || index > (SIZE_MAX - 9) / 10) {
                    return NULL;
                }
                index = index * 10 + (size_t)(token[i] - '0');
            }
            v = sof_json_value_at(v, index);
        } else {
            return NULL;
        }
    }
    return v;
}

const char *sof_json_value_string(const sof_json_value_t *v, size_t *length) {
    int is_string = v != NULL && v->type == SOF_JSON_STRING;
    if (length != NULL) {
        *length = is_string ? v->length : 0;
    }
    return is_string ? v->as.text : NULL;
}

sof_json_type_t sof_json_value_type(const sof_json_value_t *v) {
    return v != NULL ? v->type : SOF_JSON_NONE;
}

sof_json_error_t sof_json_value_uint(const sof_json_value_t *v, uint64_t *value) {
    if (value == NULL) {
        return SOF_JSON_ERROR;
    }
    if (v == NULL || v->type != SOF_JSON_NUMBER) {
        return SOF_JSON_NO_MATCH;
    }
    return sof_json_number_uint(v->as.text, v->length, value);
}
