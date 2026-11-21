/**
 * @file sof_regex.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief Regular expression search and split of sof_string strings in C.
 * @version 0.0.1
 * @date 2026-11-05
 * @copyright Copyright (c) 2026
 */

#include <stdint.h>
#include <string.h>

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

#include "sof_regex.h"

/// @brief The hidden header of a block given to PCRE2, which frees blocks
/// without saying their size. It is as large as the strictest alignment, so
/// the block after it stays aligned.
typedef union block_header_t {
    size_t size;
    max_align_t align;
} block_header_t;

static int match(const sof_regex_t *regex, PCRE2_SPTR data, size_t length, size_t start, uint32_t options);
static sof_regex_error_t match_error(int rc);
static void *regex_malloc(PCRE2_SIZE size, void *memory);
static void regex_release(void *p, void *memory);
static void subject_bytes(const sof_string_t *subject, PCRE2_SPTR *data, size_t *length);

/// @brief Runs pcre2_match() for regex. Where the pattern was compiled by
/// the JIT, which runs on 32 KiB of the machine's stack, a match that needs
/// more stack is run again by the interpreter, which gets what it needs from
/// the regex's allocator, so the JIT never gives a result the interpreter
/// wouldn't for want of stack.
static int match(const sof_regex_t *regex, PCRE2_SPTR data, size_t length, size_t start, uint32_t options) {
    int rc = pcre2_match(regex->code, data, length, start, options, regex->match_data, regex->match_context);
    if (rc == PCRE2_ERROR_JIT_STACKLIMIT) {
        rc = pcre2_match(regex->code, data, length, start, options | PCRE2_NO_JIT, regex->match_data, regex->match_context);
    }
    return rc;
}

/// @brief Turns a failed pcre2_match() result into an error code.
static sof_regex_error_t match_error(int rc) {
    if (rc == PCRE2_ERROR_NOMATCH) {
        return SOF_REGEX_NO_MATCH;
    }
    if (rc == PCRE2_ERROR_NOMEMORY) {
        return SOF_REGEX_NO_MEM;
    }
    return SOF_REGEX_MATCH_ERROR;
}

/// @brief PCRE2's malloc: a block from the regex's allocator, after a header
/// holding its size.
static void *regex_malloc(PCRE2_SIZE size, void *memory) {
    if (size > SIZE_MAX - sizeof(block_header_t)) {
        return NULL;
    }
    block_header_t *header = sof_alloc_malloc(memory, sizeof(block_header_t) + size);
    if (header == NULL) {
        return NULL;
    }
    header->size = sizeof(block_header_t) + size;
    return header + 1;
}

/// @brief PCRE2's free, for a block from regex_malloc().
static void regex_release(void *p, void *memory) {
    if (p != NULL) {
        block_header_t *header = (block_header_t *)p - 1;
        sof_alloc_free(memory, header, header->size);
    }
}

/// @brief Gives the bytes to search in subject, which are empty if it is
/// dead.
static void subject_bytes(const sof_string_t *subject, PCRE2_SPTR *data, size_t *length) {
    if (subject->data == NULL) {
        *data = (PCRE2_SPTR) "";
        *length = 0;
    } else {
        *data = (PCRE2_SPTR)subject->data;
        *length = subject->length;
    }
}

sof_regex_error_t sof_regex_find(sof_regex_t *regex, const sof_string_t *subject, size_t *position, size_t *match_offset, size_t *match_length) {
    PCRE2_SPTR data;
    size_t length;
    if (regex == NULL || regex->code == NULL || subject == NULL || position == NULL) {
        return SOF_REGEX_ERROR;
    }
    subject_bytes(subject, &data, &length);
    if (*position > length) {
        return SOF_REGEX_NO_MATCH;
    }
    int rc = match(regex, data, length, *position, 0);
    if (rc < 0) {
        return match_error(rc);
    }
    PCRE2_SIZE *ovector = pcre2_get_ovector_pointer(regex->match_data);
    if (match_offset != NULL) {
        *match_offset = ovector[0];
    }
    if (match_length != NULL) {
        *match_length = ovector[1] - ovector[0];
    }
    // Past an empty match by a byte, so the next search can't find it again.
    *position = ovector[1] > ovector[0] ? ovector[1] : ovector[0] + 1;
    return SOF_REGEX_OK;
}

sof_regex_error_t sof_regex_free(sof_regex_t *regex) {
    if (regex == NULL) {
        return SOF_REGEX_ERROR;
    }
    pcre2_match_data_free(regex->match_data);
    pcre2_code_free(regex->code);
    pcre2_match_context_free(regex->match_context);
    if (regex->memory != NULL) {
        // The block holds the allocator it is freed with.
        sof_alloc_t alloc = *(sof_alloc_t *)regex->memory;
        sof_alloc_free(&alloc, regex->memory, sizeof(sof_alloc_t));
    }
    memset(regex, 0, sizeof(*regex));
    return SOF_REGEX_OK;
}

sof_regex_error_t sof_regex_init(sof_regex_t *regex, const char *pattern, const sof_alloc_t *alloc) {
    int error_number;
    PCRE2_SIZE error_offset;
    if (regex == NULL) {
        return SOF_REGEX_ERROR;
    }
    memset(regex, 0, sizeof(*regex));
    if (pattern == NULL || !sof_alloc_valid(alloc)) {
        return SOF_REGEX_ERROR;
    }
    // PCRE2 keeps a pointer to the allocator, which lives on the heap so the
    // sof_regex_t can move.
    sof_alloc_t *memory = sof_alloc_malloc(alloc, sizeof(sof_alloc_t));
    if (memory == NULL) {
        return SOF_REGEX_NO_MEM;
    }
    memset(memory, 0, sizeof(*memory));
    if (alloc != NULL) {
        *memory = *alloc;
    }
    regex->memory = memory;
    // The contexts copy the general context's memory functions, so it is
    // needed only while they, the code and the match data are made.
    // Given a NULL general context, PCRE2 would use the standard functions.
    pcre2_general_context *general = pcre2_general_context_create(regex_malloc, regex_release, memory);
    if (general == NULL) {
        sof_regex_free(regex);
        return SOF_REGEX_NO_MEM;
    }
    pcre2_compile_context *compile = pcre2_compile_context_create(general);
    regex->match_context = pcre2_match_context_create(general);
    if (compile == NULL || regex->match_context == NULL) {
        pcre2_compile_context_free(compile);
        pcre2_general_context_free(general);
        sof_regex_free(regex);
        return SOF_REGEX_NO_MEM;
    }
    regex->code = pcre2_compile((PCRE2_SPTR)pattern, PCRE2_ZERO_TERMINATED, 0, &error_number, &error_offset, compile);
    pcre2_compile_context_free(compile);
    if (regex->code == NULL) {
        pcre2_general_context_free(general);
        sof_regex_free(regex);
        return error_number == PCRE2_ERROR_HEAP_FAILED ? SOF_REGEX_NO_MEM : SOF_REGEX_BAD_PATTERN;
    }
    regex->match_data = pcre2_match_data_create_from_pattern(regex->code, general);
    pcre2_general_context_free(general);
    if (regex->match_data == NULL) {
        sof_regex_free(regex);
        return SOF_REGEX_NO_MEM;
    }
    return SOF_REGEX_OK;
}

sof_regex_error_t sof_regex_jit(sof_regex_t *regex) {
    if (regex == NULL || regex->code == NULL) {
        return SOF_REGEX_ERROR;
    }
    // The machine code goes in executable memory PCRE2 maps itself, not
    // through the regex's allocator; pcre2_code_free() unmaps it. If it
    // can't be made, the pattern is left as it was, for the interpreter.
    return pcre2_jit_compile(regex->code, PCRE2_JIT_COMPLETE) == 0 ? SOF_REGEX_OK : SOF_REGEX_NO_MATCH;
}

size_t sof_regex_jit_size(const sof_regex_t *regex) {
    if (regex == NULL || regex->code == NULL) {
        return 0;
    }
    // 0 for a pattern with no machine code.
    size_t size = 0;
    if (pcre2_pattern_info(regex->code, PCRE2_INFO_JITSIZE, &size) != 0) {
        return 0;
    }
    return size;
}

sof_regex_error_t sof_regex_split(sof_regex_t *regex, const sof_string_t *subject, size_t *position, size_t *field_offset, size_t *field_length) {
    PCRE2_SPTR data;
    size_t length, end, next;
    if (regex == NULL || regex->code == NULL || subject == NULL || position == NULL) {
        return SOF_REGEX_ERROR;
    }
    subject_bytes(subject, &data, &length);
    if (*position > length) {
        return SOF_REGEX_NO_MATCH;
    }
    // PCRE2_NOTEMPTY_ATSTART keeps a delimiter that can match the empty
    // string from matching at the start of the field again and again.
    int rc = match(regex, data, length, *position, PCRE2_NOTEMPTY_ATSTART);
    if (rc == PCRE2_ERROR_NOMATCH) {
        // No more delimiters: the rest of the string is the last field.
        end = length;
        next = length + 1;
    } else if (rc < 0) {
        return match_error(rc);
    } else {
        PCRE2_SIZE *ovector = pcre2_get_ovector_pointer(regex->match_data);
        end = ovector[0];
        next = ovector[1];
    }
    if (field_offset != NULL) {
        *field_offset = *position;
    }
    if (field_length != NULL) {
        *field_length = end - *position;
    }
    *position = next;
    return SOF_REGEX_OK;
}
