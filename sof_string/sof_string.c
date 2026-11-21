/**
 * @file sof_string.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief Fat strings in C.
 * @version 0.0.1
 * @date 2026-11-04
 * @copyright Copyright (c) 2026
 */

#include <stdint.h>
#include <string.h>

#include "sof_string.h"

/// @brief Strings shorter than this are compared 8 bytes at a time by
/// sof_string_cmp() itself; longer ones by memcmp(), whose call costs more
/// than the comparison of a short string. At 32 bytes glibc's memcmp() is
/// already the faster.
#define CMP_INLINE 32

static sof_string_error_t append_bytes(sof_string_t *target, const char *bytes, size_t length);
static void case_flip(char *data, size_t length, unsigned char first);
static sof_string_error_t copy_bytes(sof_string_t *target, const char *bytes, size_t length, size_t capacity, const sof_alloc_t *alloc);
static unsigned char fold(unsigned char c);
static uint64_t fold64(uint64_t x);
static uint64_t load_be64(const unsigned char *p);
static sof_string_error_t grow(sof_string_t *target, size_t length, const char **bytes);
static sof_string_error_t insert_bytes(sof_string_t *target, size_t offset, const char *bytes, size_t length);
static int writable(const sof_string_t *string);

/// @brief Appends length bytes to a writable target, growing it to at least
/// twice its size if it must grow. bytes can lie in target's data.
static sof_string_error_t append_bytes(sof_string_t *target, const char *bytes, size_t length) {
    sof_string_error_t rc = grow(target, length, &bytes);
    if (rc != SOF_STRING_OK) {
        return rc;
    }
    memmove(target->data + target->length, bytes, length);
    target->length += length;
    target->data[target->length] = '\0';
    return SOF_STRING_OK;
}

/// @brief Flips the case of the ASCII letters from first to first + 25, 'A'
/// or 'a', in length bytes of data, which differ from the other case's only
/// in bit 5.
/// @details Every byte is written, the others with what they were, so the
/// loop has no branch and the compiler can vectorize it; the pointer and
/// length are locals, since a char store could otherwise change them.
static void case_flip(char *data, size_t length, unsigned char first) {
    unsigned char *p = (unsigned char *)data;
    for (size_t i = 0; i < length; i++) {
        unsigned char c = p[i];
        p[i] = (unsigned char)(c ^ (((unsigned char)(c - first) < 26) << 5));
    }
}

/// @brief Initializes target with room for capacity bytes, holding a copy of
/// the first length of them from bytes. Leaves it dead on failure.
static sof_string_error_t copy_bytes(sof_string_t *target, const char *bytes, size_t length, size_t capacity, const sof_alloc_t *alloc) {
    memset(target, 0, sizeof(*target));
    if (!sof_alloc_valid(alloc)) {
        return SOF_STRING_ERROR;
    }
    if (capacity == SIZE_MAX) {
        return SOF_STRING_NO_MEM;
    }
    char *data = sof_alloc_malloc(alloc, capacity + 1);
    if (data == NULL) {
        return SOF_STRING_NO_MEM;
    }
    if (length != 0) {
        memcpy(data, bytes, length);
    }
    data[length] = '\0';
    target->data = data;
    target->length = length;
    target->size = capacity + 1;
    if (alloc != NULL) {
        target->alloc = *alloc;
    }
    return SOF_STRING_OK;
}

/// @brief Folds an ASCII upper case letter to lower case, as
/// sof_string_tolower() does, whatever the locale; other bytes are returned
/// as they are.
static unsigned char fold(unsigned char c) {
    return c >= 'A' && c <= 'Z' ? (unsigned char)(c + ('a' - 'A')) : c;
}

/// @brief Makes room in a writable target for length more bytes and its NUL,
/// growing it to at least twice its size if it must grow.
/// @details *bytes, which is to be added, may lie in target's data, which
/// can move; it is moved with it.
/// @return SOF_STRING_OK, or SOF_STRING_NO_MEM, leaving target as it was.
static sof_string_error_t grow(sof_string_t *target, size_t length, const char **bytes) {
    if (length > SIZE_MAX - 1 - target->length) {
        return SOF_STRING_NO_MEM;
    }
    size_t needed = target->length + length;
    if (needed < target->size) {
        return SOF_STRING_OK;
    }
    size_t size = target->size <= SIZE_MAX / 2 ? target->size * 2 : SIZE_MAX;
    if (size < needed + 1) {
        size = needed + 1;
    }
    uintptr_t from = (uintptr_t)*bytes, start = (uintptr_t)target->data;
    int own = from >= start && from < start + target->size;
    char *data = sof_alloc_realloc(&target->alloc, target->data, target->size, size);
    if (data == NULL) {
        return SOF_STRING_NO_MEM;
    }
    if (own) {
        *bytes = data + (from - start);
    }
    target->data = data;
    target->size = size;
    return SOF_STRING_OK;
}

/// @brief Inserts length bytes, at least one, into a writable target at
/// offset, which is within it. bytes can lie in target's data, even where
/// the bytes after offset move to.
static sof_string_error_t insert_bytes(sof_string_t *target, size_t offset, const char *bytes, size_t length) {
    sof_string_error_t rc = grow(target, length, &bytes);
    if (rc != SOF_STRING_OK) {
        return rc;
    }
    char *data = target->data;
    uintptr_t from = (uintptr_t)bytes, start = (uintptr_t)data;
    int own = from >= start && from < start + target->size;
    // Make room, moving the NUL up with the rest.
    memmove(data + offset + length, data + offset, target->length - offset + 1);
    if (!own) {
        memcpy(data + offset, bytes, length);
    } else {
        // The target's own bytes: those before offset are where they were,
        // and those from offset on have moved up by length.
        size_t source = from - start;
        size_t before = source < offset ? offset - source : 0;
        if (before > length) {
            before = length;
        }
        memmove(data + offset, data + source, before);
        memmove(data + offset + before, data + source + before + length, length - before);
    }
    target->length += length;
    return SOF_STRING_OK;
}

/// @brief Reads 8 bytes as a big-endian number, so that two such numbers
/// compare as their bytes do with memcmp().
/// @details Compilers make this one load, and a byte swap on a
/// little-endian machine, whatever the machine's byte order.
static uint64_t load_be64(const unsigned char *p) {
    return ((uint64_t)p[0] << 56) | ((uint64_t)p[1] << 48) | ((uint64_t)p[2] << 40) | ((uint64_t)p[3] << 32) | ((uint64_t)p[4] << 24) | ((uint64_t)p[5] << 16) | ((uint64_t)p[6] << 8) | (uint64_t)p[7];
}

/// @brief Folds the ASCII upper case letters among 8 bytes to lower case,
/// as fold() does each, all at once.
/// @details For each byte below 0x80, adding 0x3f sets its top bit if it is
/// 'A' or more, and adding 0x25 if it is past 'Z', without carrying into the
/// next byte; a letter has the first and not the second. Bytes from 0x80 up
/// are left out, and bit 5 is set in the letters.
static uint64_t fold64(uint64_t x) {
    const uint64_t ones = 0x0101010101010101u, high = 0x8080808080808080u;
    uint64_t low = x & ~high;
    uint64_t from_a = low + (0x80 - 'A') * ones, past_z = low + (0x80 - 'Z' - 1) * ones;
    uint64_t upper = (from_a ^ past_z) & ~x & high;
    return x | (upper >> 2);
}

/// @brief Returns 1 if string can be changed: not NULL, dead or a
/// sof_string_const().
static int writable(const sof_string_t *string) {
    return string != NULL && string->data != NULL && string->size != 0;
}

sof_string_error_t sof_string_append(sof_string_t *target, const sof_string_t *suffix) {
    if (!writable(target) || suffix == NULL) {
        return SOF_STRING_ERROR;
    }
    if (suffix->data == NULL) {
        return SOF_STRING_OK;
    }
    return append_bytes(target, suffix->data, suffix->length);
}

sof_string_error_t sof_string_append_bytes(sof_string_t *target, const void *bytes, size_t length) {
    if (!writable(target) || (bytes == NULL && length != 0)) {
        return SOF_STRING_ERROR;
    }
    // memmove() mustn't be given a NULL pointer, even for no bytes.
    if (length == 0) {
        return SOF_STRING_OK;
    }
    return append_bytes(target, bytes, length);
}

sof_string_error_t sof_string_append_cstr(sof_string_t *target, const char *suffix) {
    if (!writable(target) || suffix == NULL) {
        return SOF_STRING_ERROR;
    }
    return append_bytes(target, suffix, strlen(suffix));
}

int sof_string_casecmp(const sof_string_t *a, const sof_string_t *b) {
    size_t a_length = a != NULL && a->data != NULL ? a->length : 0;
    size_t b_length = b != NULL && b->data != NULL ? b->length : 0;
    size_t length = a_length < b_length ? a_length : b_length;
    if (length >= 8) {
        // 8 bytes at a time, folded, as big-endian numbers, which compare
        // as the folded bytes do; the last 8 overlap those already found
        // equal.
        const unsigned char *p = (const unsigned char *)a->data, *q = (const unsigned char *)b->data;
        for (size_t i = 0;; i += 8) {
            if (i + 8 > length) {
                i = length - 8;
            }
            uint64_t x = load_be64(p + i), y = load_be64(q + i);
            if (x != y) {
                x = fold64(x);
                y = fold64(y);
                if (x != y) {
                    return x < y ? -1 : 1;
                }
            }
            if (i + 8 == length) {
                break;
            }
        }
    } else {
        for (size_t i = 0; i < length; i++) {
            unsigned char ca = fold((unsigned char)a->data[i]), cb = fold((unsigned char)b->data[i]);
            if (ca != cb) {
                return ca < cb ? -1 : 1;
            }
        }
    }
    return (a_length > b_length) - (a_length < b_length);
}

sof_string_error_t sof_string_clear(sof_string_t *string) {
    if (!writable(string)) {
        return SOF_STRING_ERROR;
    }
    string->length = 0;
    string->data[0] = '\0';
    return SOF_STRING_OK;
}

int sof_string_cmp(const sof_string_t *a, const sof_string_t *b) {
    size_t a_length = a != NULL && a->data != NULL ? a->length : 0;
    size_t b_length = b != NULL && b->data != NULL ? b->length : 0;
    size_t length = a_length < b_length ? a_length : b_length;
    if (length >= CMP_INLINE) {
        int cmp = memcmp(a->data, b->data, length);
        if (cmp != 0) {
            return cmp < 0 ? -1 : 1;
        }
    } else if (length >= 8) {
        // 8 bytes at a time, as big-endian numbers, which compare as the
        // bytes do; the last 8 overlap those already found equal.
        const unsigned char *p = (const unsigned char *)a->data, *q = (const unsigned char *)b->data;
        for (size_t i = 0;; i += 8) {
            if (i + 8 > length) {
                i = length - 8;
            }
            uint64_t x = load_be64(p + i), y = load_be64(q + i);
            if (x != y) {
                return x < y ? -1 : 1;
            }
            if (i + 8 == length) {
                break;
            }
        }
    } else {
        for (size_t i = 0; i < length; i++) {
            unsigned char x = (unsigned char)a->data[i], y = (unsigned char)b->data[i];
            if (x != y) {
                return x < y ? -1 : 1;
            }
        }
    }
    return (a_length > b_length) - (a_length < b_length);
}

const char *sof_string_cstr(const sof_string_t *string) {
    return string == NULL ? NULL : string->data;
}

sof_string_error_t sof_string_delete(sof_string_t *string, size_t offset, size_t length) {
    if (!writable(string) || offset > string->length || length > string->length - offset) {
        return SOF_STRING_ERROR;
    }
    // The NUL moves down with the rest.
    memmove(string->data + offset, string->data + offset + length, string->length - offset - length + 1);
    string->length -= length;
    return SOF_STRING_OK;
}

sof_string_error_t sof_string_dup(sof_string_t *target, const sof_string_t *source, const sof_alloc_t *alloc) {
    if (target == NULL || target == source) {
        return SOF_STRING_ERROR;
    }
    if (source == NULL || source->data == NULL) {
        memset(target, 0, sizeof(*target));
        return SOF_STRING_ERROR;
    }
    return copy_bytes(target, source->data, source->length, source->length, alloc);
}

sof_string_error_t sof_string_free(sof_string_t *string) {
    if (string == NULL) {
        return SOF_STRING_ERROR;
    }
    if (string->size != 0) {
        sof_alloc_free(&string->alloc, string->data, string->size);
    }
    memset(string, 0, sizeof(*string));
    return SOF_STRING_OK;
}

sof_string_error_t sof_string_from_bytes(sof_string_t *string, const void *bytes, size_t length, const sof_alloc_t *alloc) {
    if (string == NULL) {
        return SOF_STRING_ERROR;
    }
    if (bytes == NULL && length != 0) {
        memset(string, 0, sizeof(*string));
        return SOF_STRING_ERROR;
    }
    return copy_bytes(string, bytes, length, length, alloc);
}

sof_string_error_t sof_string_from_cstr(sof_string_t *string, const char *cstring, const sof_alloc_t *alloc) {
    if (string == NULL) {
        return SOF_STRING_ERROR;
    }
    if (cstring == NULL) {
        memset(string, 0, sizeof(*string));
        return SOF_STRING_ERROR;
    }
    size_t length = strlen(cstring);
    return copy_bytes(string, cstring, length, length, alloc);
}

sof_string_error_t sof_string_init(sof_string_t *string, size_t capacity, const sof_alloc_t *alloc) {
    if (string == NULL) {
        return SOF_STRING_ERROR;
    }
    return copy_bytes(string, NULL, 0, capacity, alloc);
}

sof_string_error_t sof_string_insert(sof_string_t *target, size_t offset, const sof_string_t *s) {
    if (!writable(target) || s == NULL || offset > target->length) {
        return SOF_STRING_ERROR;
    }
    if (s->data == NULL || s->length == 0) {
        return SOF_STRING_OK;
    }
    return insert_bytes(target, offset, s->data, s->length);
}

sof_string_error_t sof_string_insert_bytes(sof_string_t *target, size_t offset, const void *bytes, size_t length) {
    if (!writable(target) || offset > target->length || (bytes == NULL && length != 0)) {
        return SOF_STRING_ERROR;
    }
    if (length == 0) {
        return SOF_STRING_OK;
    }
    return insert_bytes(target, offset, bytes, length);
}

sof_string_error_t sof_string_insert_cstr(sof_string_t *target, size_t offset, const char *s) {
    if (!writable(target) || s == NULL || offset > target->length) {
        return SOF_STRING_ERROR;
    }
    size_t length = strlen(s);
    if (length == 0) {
        return SOF_STRING_OK;
    }
    return insert_bytes(target, offset, s, length);
}

sof_string_error_t sof_string_position(const sof_string_t *string, size_t offset, size_t *line, size_t *column) {
    if (string == NULL || offset > string->length) {
        return SOF_STRING_ERROR;
    }
    const char *s = string->data;
    const char *nl;
    size_t lines = 1, start = 0;
    while (start < offset && (nl = memchr(s + start, '\n', offset - start)) != NULL) {
        lines++;
        start = (size_t)(nl - s) + 1;
    }
    if (line != NULL) {
        *line = lines;
    }
    if (column != NULL) {
        *column = offset - start + 1;
    }
    return SOF_STRING_OK;
}

char *sof_string_reserve(sof_string_t *target, size_t length) {
    if (!writable(target)) {
        return NULL;
    }
    const char *none = NULL;
    if (grow(target, length, &none) != SOF_STRING_OK) {
        return NULL;
    }
    return target->data + target->length;
}

sof_string_error_t sof_string_substr(sof_string_t *target, const sof_string_t *source, size_t offset, size_t length, const sof_alloc_t *alloc) {
    if (target == NULL || target == source) {
        return SOF_STRING_ERROR;
    }
    if (source == NULL || source->data == NULL || offset > source->length || length > source->length - offset) {
        memset(target, 0, sizeof(*target));
        return SOF_STRING_ERROR;
    }
    return copy_bytes(target, source->data + offset, length, length, alloc);
}

sof_string_error_t sof_string_tolower(sof_string_t *string) {
    if (!writable(string)) {
        return SOF_STRING_ERROR;
    }
    case_flip(string->data, string->length, 'A');
    return SOF_STRING_OK;
}

sof_string_error_t sof_string_toupper(sof_string_t *string) {
    if (!writable(string)) {
        return SOF_STRING_ERROR;
    }
    case_flip(string->data, string->length, 'a');
    return SOF_STRING_OK;
}

sof_string_t sof_string_view(const void *bytes, size_t length) {
    sof_string_t string = sof_string_const("");
    if (bytes != NULL) {
        // The size of 0 keeps the bytes from being changed or freed.
        string.data = (char *)bytes;
        string.length = length;
    }
    return string;
}
