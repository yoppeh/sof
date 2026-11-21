/**
 * @file sof_config.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief Configuration file parser in C.
 * @version 0.0.1
 * @date 2026-11-03
 * @copyright Copyright (c) 2026
 */

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sof_alloc.h"
#include "sof_config.h"
#include "sof_hash.h"
#include "sof_hashmap.h"
#include "sof_utf.h"

/// @brief Character that opens a section declaration.
static const int section_open_char = '[';

/// @brief Character that closes a section declaration.
static const int section_close_char = ']';

/// @brief Characters that start a comment.
static const int comment_char = ';';
static const int comment_alt_char = '#';

/// @brief Character that ends a line.
static const int newline_char = '\n';

/// @brief Character that separates a key from its value.
static const int assignment_char = '=';

/// @brief Character that encloses quoted values.
static const int quote_char = '"';

/// @brief Character that starts an escape sequence in a quoted value.
static const int escape_char = '\\';

/// @brief Characters other than ASCII letters and digits allowed in keys and
/// section names.
static const char name_chars[] = "_-.";

/// @brief Returned by io_next() and io_peek() at the end of the input.
static const int io_eof = -1;

/// @brief Size of the buffer a file is read into.
static const size_t buffer_size = 4096;

/// @brief Initial capacity of the key, value and section strings.
static const size_t text_size = 32;

/// @brief Initial capacity of the entry list.
static const size_t entries_size = 16;

static const char *error_strings[] = {
    [SOF_CONFIG_OK] = "No error",
    [SOF_CONFIG_NO_MEM] = "Unable to allocate memory",
    [SOF_CONFIG_NO_MATCH] = "No such section and key",
    [SOF_CONFIG_ERROR] = "Invalid argument",
    [SOF_CONFIG_NO_OPEN] = "Unable to open file",
    [SOF_CONFIG_NO_READ] = "Unable to read file",
    [SOF_CONFIG_UNEXPECTED_CHARACTER] = "Unexpected character",
    [SOF_CONFIG_UNEXPECTED_EOL] = "Unexpected end of line",
    [SOF_CONFIG_PREMATURE_EOF] = "Unexpected end of file",
    [SOF_CONFIG_BAD_ESCAPE] = "Invalid escape sequence",
    [SOF_CONFIG_BAD_VALUE] = "Invalid value",
    [SOF_CONFIG_STOPPED] = "Stopped by the handler",
};

/// @brief A growable NUL-terminated string.
typedef struct text_t {
    char *data;
    size_t length;
    size_t capacity;
    /// @brief The allocator, in the config's state, which doesn't move.
    const sof_alloc_t *alloc;
} text_t;

/// @brief The parser's state and the stored entries.
struct sof_config_state_t {
    /// @brief The memory functions everything here is allocated with.
    sof_alloc_t alloc;
    /// @brief The buffer a file is read into.
    char *buffer;
    /// @brief The file being parsed, or NULL for a string.
    FILE *file;
    /// @brief The next character of the input, and the end of what is read.
    const char *next;
    const char *end;
    /// @brief What the parser reads the current key, value and section into.
    text_t key;
    text_t value;
    text_t section;
    /// @brief Non-zero once a section has been declared.
    int in_section;
    /// @brief Non-zero while a parse is running.
    int parsing;
    /// @brief The entries, in the order they were first assigned.
    sof_config_entry_t **entries;
    size_t capacity;
    /// @brief The entries, keyed by section and key.
    sof_hashmap_t map;
};

static sof_config_error_t end_of_line(sof_config_t *config);
static int entry_equal(const void *a, const void *b, void *ctx);
static void entry_free(const sof_alloc_t *alloc, sof_config_entry_t *entry);
static size_t entry_hash(const void *key, void *ctx);
static sof_config_error_t find_value(const sof_config_t *config, const char *section, const char *key, const void *value, const char **text);
static int hex_value(int ch);
static int io_next(sof_config_t *config);
static int io_peek(sof_config_t *config);
static int is_blank(int ch);
static int is_comment(int ch);
static int is_name_character(int ch);
static sof_config_error_t parse(sof_config_t *config);
static sof_config_error_t parse_assignment(sof_config_t *config);
static sof_config_error_t parse_escape(sof_config_t *config);
static sof_config_error_t parse_key(sof_config_t *config);
static sof_config_error_t parse_quoted_value(sof_config_t *config);
static sof_config_error_t parse_raw_value(sof_config_t *config);
static sof_config_error_t parse_section(sof_config_t *config);
static sof_config_error_t parse_value(sof_config_t *config);
static int ready(const sof_config_t *config);
static int same_word(const char *s, const char *word);
static void skip_blanks(sof_config_t *config);
static sof_config_error_t skip_bom(sof_config_t *config);
static void skip_comment(sof_config_t *config);
static sof_config_error_t store(sof_config_t *config, size_t line);
static sof_config_error_t text_append(text_t *text, int ch);
static sof_config_error_t text_append_utf8(text_t *text, uint32_t code);
static void string_free(const sof_alloc_t *alloc, const char *s);
static sof_config_error_t text_init(text_t *text, const sof_alloc_t *alloc);
static void text_reset(text_t *text);
static void text_trim(text_t *text);

/// @brief Hashes and compares entries by section and key, for the map.
static const sof_hashmap_key_callbacks_t entry_keys = {entry_hash, entry_equal, NULL, NULL};

/// @brief Skips whitespace and a comment after a section declaration or a
/// value, and returns SOF_CONFIG_UNEXPECTED_CHARACTER if anything else comes
/// before the end of the line.
static sof_config_error_t end_of_line(sof_config_t *config) {
    skip_blanks(config);
    if (is_comment(io_peek(config))) {
        skip_comment(config);
    }
    int ch = io_peek(config);
    if (ch != io_eof && ch != newline_char) {
        return SOF_CONFIG_UNEXPECTED_CHARACTER;
    }
    return SOF_CONFIG_OK;
}

/// @brief Returns 1 if entries a and b have the same section and key.
static int entry_equal(const void *a, const void *b, void *ctx) {
    const sof_config_entry_t *ea = a;
    const sof_config_entry_t *eb = b;
    if ((ea->section == NULL) != (eb->section == NULL)) {
        return 0;
    }
    if (ea->section != NULL && strcmp(ea->section, eb->section) != 0) {
        return 0;
    }
    return strcmp(ea->key, eb->key) == 0;
}

/// @brief Frees an entry and its strings.
static void entry_free(const sof_alloc_t *alloc, sof_config_entry_t *entry) {
    if (entry != NULL) {
        string_free(alloc, entry->section);
        string_free(alloc, entry->key);
        string_free(alloc, entry->value);
        sof_alloc_free(alloc, entry, sizeof(sof_config_entry_t));
    }
}

/// @brief Hashes an entry's section and key.
static size_t entry_hash(const void *key, void *ctx) {
    const sof_config_entry_t *entry = key;
    uint64_t seed = entry->section != NULL ? sof_hash_string(entry->section, 1) : 0;
    return (size_t)sof_hash_string(entry->key, seed);
}

/// @brief Finds the value for section and key for a typed getter, and
/// returns an error code.
static sof_config_error_t find_value(const sof_config_t *config, const char *section, const char *key, const void *value, const char **text) {
    if (config == NULL || key == NULL || value == NULL) {
        return SOF_CONFIG_ERROR;
    }
    *text = sof_config_get(config, section, key);
    return *text != NULL ? SOF_CONFIG_OK : SOF_CONFIG_NO_MATCH;
}

/// @brief Returns the value of hexadecimal digit ch, or -1.
static int hex_value(int ch) {
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    } else if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    } else if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }
    return -1;
}

/// @brief Returns the current character of the input and advances past it,
/// counting lines and columns, or io_eof at the end of the input.
static int io_next(sof_config_t *config) {
    int ch = io_peek(config);
    if (ch != io_eof) {
        config->state->next++;
        if (ch == newline_char) {
            config->line++;
            config->column = 1;
        } else {
            config->column++;
        }
    }
    return ch;
}

/// @brief Returns the current character of the input without advancing past
/// it, or io_eof at the end of the input. Refills the buffer from the file,
/// if there is one, when it runs out.
static int io_peek(sof_config_t *config) {
    struct sof_config_state_t *state = config->state;
    if (state->next == state->end) {
        if (state->file == NULL) {
            return io_eof;
        }
        size_t length = fread(state->buffer, 1, buffer_size, state->file);
        if (length == 0) {
            return io_eof;
        }
        state->next = state->buffer;
        state->end = state->buffer + length;
    }
    return (unsigned char)*state->next;
}

/// @brief Returns 1 if ch is whitespace other than a newline.
static int is_blank(int ch) {
    return ch == ' ' || ch == '\t' || ch == '\r' || ch == '\f' || ch == '\v';
}

/// @brief Returns 1 if ch starts a comment.
static int is_comment(int ch) {
    return ch == comment_char || ch == comment_alt_char;
}

/// @brief Returns 1 if ch may appear in a key or section name.
static int is_name_character(int ch) {
    if (ch <= 0 || ch >= 128) {
        return 0;
    }
    return isalnum(ch) || strchr(name_chars, ch) != NULL;
}

/// @brief Parses the input set up by the caller to its end, and returns an
/// error code.
static sof_config_error_t parse(sof_config_t *config) {
    struct sof_config_state_t *state = config->state;
    config->line = 1;
    config->column = 1;
    config->stop_value = 0;
    state->in_section = 0;
    state->parsing = 1;
    sof_config_error_t rc = skip_bom(config);
    while (rc == SOF_CONFIG_OK) {
        skip_blanks(config);
        int ch = io_peek(config);
        if (ch == io_eof) {
            break;
        } else if (ch == newline_char) {
            io_next(config);
        } else if (is_comment(ch)) {
            skip_comment(config);
        } else if (ch == section_open_char) {
            rc = parse_section(config);
        } else if (is_name_character(ch)) {
            rc = parse_assignment(config);
        } else {
            rc = SOF_CONFIG_UNEXPECTED_CHARACTER;
        }
    }
    state->parsing = 0;
    state->file = NULL;
    state->next = state->end = NULL;
    return rc;
}

/// @brief Parses an assignment, stores it, and passes it to the handler, if
/// there is one, and returns an error code.
static sof_config_error_t parse_assignment(sof_config_t *config) {
    struct sof_config_state_t *state = config->state;
    size_t line = config->line;
    sof_config_error_t rc = parse_key(config);
    if (rc == SOF_CONFIG_OK) {
        rc = parse_value(config);
    }
    if (rc == SOF_CONFIG_OK) {
        rc = end_of_line(config);
    }
    if (rc == SOF_CONFIG_OK) {
        rc = store(config, line);
    }
    if (rc != SOF_CONFIG_OK || config->handler == NULL) {
        return rc;
    }
    const char *section = state->in_section ? state->section.data : NULL;
    int value = config->handler(section, state->key.data, state->value.data, config->ctx);
    if (value != 0) {
        config->stop_value = value;
        return SOF_CONFIG_STOPPED;
    }
    return SOF_CONFIG_OK;
}

/// @brief Parses an escape sequence in a quoted value, appending what it
/// stands for to the value, and returns an error code. For
/// SOF_CONFIG_BAD_ESCAPE, the column is left at the backslash.
static sof_config_error_t parse_escape(sof_config_t *config) {
    text_t *value = &config->state->value;
    size_t column = config->column;
    io_next(config);
    int ch = io_peek(config);
    if (ch == io_eof) {
        return SOF_CONFIG_PREMATURE_EOF;
    } else if (ch == newline_char) {
        return SOF_CONFIG_UNEXPECTED_EOL;
    }
    io_next(config);
    int digits = 0;
    switch (ch) {
    case 'a':
        return text_append(value, '\a');
    case 'b':
        return text_append(value, '\b');
    case 'f':
        return text_append(value, '\f');
    case 'n':
        return text_append(value, '\n');
    case 'r':
        return text_append(value, '\r');
    case 't':
        return text_append(value, '\t');
    case 'v':
        return text_append(value, '\v');
    case '\\':
    case '"':
    case '\'':
        return text_append(value, ch);
    case 'x':
        digits = 2;
        break;
    case 'u':
        digits = 4;
        break;
    case 'U':
        digits = 8;
        break;
    default:
        config->column = column;
        return SOF_CONFIG_BAD_ESCAPE;
    }
    uint32_t code = 0;
    for (int i = 0; i < digits; i++) {
        int digit = hex_value(io_peek(config));
        if (digit < 0) {
            config->column = column;
            return SOF_CONFIG_BAD_ESCAPE;
        }
        code = code * 16 + (uint32_t)digit;
        io_next(config);
    }
    if (ch == 'x') {
        if (code == 0) {
            config->column = column;
            return SOF_CONFIG_BAD_ESCAPE;
        }
        return text_append(value, (int)code);
    }
    // A code point other than NUL.
    if (code == 0 || !sof_utf_valid(code)) {
        config->column = column;
        return SOF_CONFIG_BAD_ESCAPE;
    }
    return text_append_utf8(value, code);
}

/// @brief Parses a key, and the = after it on the same line, and returns an
/// error code.
static sof_config_error_t parse_key(sof_config_t *config) {
    text_t *key = &config->state->key;
    text_reset(key);
    while (is_name_character(io_peek(config))) {
        if (text_append(key, io_next(config)) != SOF_CONFIG_OK) {
            return SOF_CONFIG_NO_MEM;
        }
    }
    skip_blanks(config);
    int ch = io_peek(config);
    if (ch == assignment_char) {
        io_next(config);
        return SOF_CONFIG_OK;
    } else if (ch == io_eof) {
        return SOF_CONFIG_PREMATURE_EOF;
    } else if (ch == newline_char) {
        return SOF_CONFIG_UNEXPECTED_EOL;
    }
    return SOF_CONFIG_UNEXPECTED_CHARACTER;
}

/// @brief Parses a value enclosed in quotes, replacing escape sequences, and
/// returns an error code.
static sof_config_error_t parse_quoted_value(sof_config_t *config) {
    text_t *value = &config->state->value;
    io_next(config);
    while (1) {
        int ch = io_peek(config);
        if (ch == io_eof) {
            return SOF_CONFIG_PREMATURE_EOF;
        } else if (ch == newline_char) {
            return SOF_CONFIG_UNEXPECTED_EOL;
        } else if (ch == 0) {
            return SOF_CONFIG_UNEXPECTED_CHARACTER;
        } else if (ch == quote_char) {
            io_next(config);
            return SOF_CONFIG_OK;
        } else if (ch == escape_char) {
            sof_config_error_t rc = parse_escape(config);
            if (rc != SOF_CONFIG_OK) {
                return rc;
            }
        } else {
            if (text_append(value, ch) != SOF_CONFIG_OK) {
                return SOF_CONFIG_NO_MEM;
            }
            io_next(config);
        }
    }
}

/// @brief Parses a value not enclosed in quotes, which runs to the end of the
/// line or to a comment that follows whitespace, and trims it. Returns an
/// error code.
static sof_config_error_t parse_raw_value(sof_config_t *config) {
    text_t *value = &config->state->value;
    // The leading whitespace was skipped, so a comment can start at once.
    int after_blank = 1;
    while (1) {
        int ch = io_peek(config);
        if (ch == io_eof || ch == newline_char || (after_blank && is_comment(ch))) {
            break;
        } else if (ch == 0) {
            return SOF_CONFIG_UNEXPECTED_CHARACTER;
        }
        if (text_append(value, ch) != SOF_CONFIG_OK) {
            return SOF_CONFIG_NO_MEM;
        }
        io_next(config);
        after_blank = is_blank(ch);
    }
    text_trim(value);
    return SOF_CONFIG_OK;
}

/// @brief Parses a section declaration, trimming the name, and makes it the
/// current section. Returns an error code.
static sof_config_error_t parse_section(sof_config_t *config) {
    struct sof_config_state_t *state = config->state;
    text_reset(&state->section);
    // Until the declaration is complete, there is no current section.
    state->in_section = 0;
    io_next(config);
    skip_blanks(config);
    while (1) {
        int ch = io_peek(config);
        if (ch == section_close_char) {
            break;
        } else if (ch == io_eof) {
            return SOF_CONFIG_PREMATURE_EOF;
        } else if (ch == newline_char) {
            return SOF_CONFIG_UNEXPECTED_EOL;
        } else if (!is_name_character(ch) && ch != ' ' && ch != '\t') {
            return SOF_CONFIG_UNEXPECTED_CHARACTER;
        }
        if (text_append(&state->section, ch) != SOF_CONFIG_OK) {
            return SOF_CONFIG_NO_MEM;
        }
        io_next(config);
    }
    text_trim(&state->section);
    if (state->section.length == 0) {
        return SOF_CONFIG_UNEXPECTED_CHARACTER;
    }
    io_next(config);
    state->in_section = 1;
    return end_of_line(config);
}

/// @brief Parses the value of an assignment, quoted or not, and returns an
/// error code.
static sof_config_error_t parse_value(sof_config_t *config) {
    text_reset(&config->state->value);
    skip_blanks(config);
    if (io_peek(config) == quote_char) {
        return parse_quoted_value(config);
    }
    return parse_raw_value(config);
}

/// @brief Returns 1 if config is initialized and not already parsing, so it
/// can be parsed into, cleared or freed.
static int ready(const sof_config_t *config) {
    return config != NULL && config->state != NULL && !config->state->parsing;
}

/// @brief Returns 1 if s is word, which is in lower case, in any case.
static int same_word(const char *s, const char *word) {
    while (*s != 0 && tolower((unsigned char)*s) == *word) {
        s++;
        word++;
    }
    return *s == 0 && *word == 0;
}

/// @brief Skips whitespace other than newlines.
static void skip_blanks(sof_config_t *config) {
    while (is_blank(io_peek(config))) {
        io_next(config);
    }
}

/// @brief Skips a UTF-8 byte order mark at the start of the input, and
/// returns an error code if the input starts with part of one.
static sof_config_error_t skip_bom(sof_config_t *config) {
    static const int bom[] = {0xef, 0xbb, 0xbf};
    if (io_peek(config) != bom[0]) {
        return SOF_CONFIG_OK;
    }
    for (size_t i = 0; i < 3; i++) {
        if (io_peek(config) != bom[i]) {
            return SOF_CONFIG_UNEXPECTED_CHARACTER;
        }
        io_next(config);
    }
    // The mark isn't part of the first line's columns.
    config->column = 1;
    return SOF_CONFIG_OK;
}

/// @brief Skips a comment up to the end of the line. The newline is left for
/// parse().
static void skip_comment(sof_config_t *config) {
    int ch = io_peek(config);
    while (ch != io_eof && ch != newline_char) {
        io_next(config);
        ch = io_peek(config);
    }
}

/// @brief Stores the assignment just parsed, which started on line, replacing
/// the value of an entry with the same section and key. Returns an error
/// code.
static sof_config_error_t store(sof_config_t *config, size_t line) {
    struct sof_config_state_t *state = config->state;
    const char *section = state->in_section ? state->section.data : NULL;
    sof_config_entry_t probe = {section, state->key.data, NULL, 0};
    sof_hashmap_element_t *element = sof_hashmap_find(&state->map, &probe);
    if (element != NULL) {
        sof_config_entry_t *entry = element->key;
        char *value = sof_alloc_strndup(&state->alloc, state->value.data, state->value.length);
        if (value == NULL) {
            return SOF_CONFIG_NO_MEM;
        }
        string_free(&state->alloc, entry->value);
        entry->value = value;
        entry->line = line;
        return SOF_CONFIG_OK;
    }
    if (config->count == state->capacity) {
        size_t capacity = state->capacity == 0 ? entries_size : state->capacity * 2;
        sof_config_entry_t **entries = sof_alloc_realloc(&state->alloc, state->entries, state->capacity * sizeof(sof_config_entry_t *), capacity * sizeof(sof_config_entry_t *));
        if (entries == NULL) {
            return SOF_CONFIG_NO_MEM;
        }
        state->entries = entries;
        state->capacity = capacity;
    }
    sof_config_entry_t *entry = sof_alloc_malloc(&state->alloc, sizeof(sof_config_entry_t));
    if (entry == NULL) {
        return SOF_CONFIG_NO_MEM;
    }
    entry->section = section != NULL ? sof_alloc_strndup(&state->alloc, section, state->section.length) : NULL;
    entry->key = sof_alloc_strndup(&state->alloc, state->key.data, state->key.length);
    entry->value = sof_alloc_strndup(&state->alloc, state->value.data, state->value.length);
    entry->line = line;
    if ((section != NULL && entry->section == NULL) || entry->key == NULL || entry->value == NULL || sof_hashmap_insert(&state->map, entry, NULL) == NULL) {
        entry_free(&state->alloc, entry);
        return SOF_CONFIG_NO_MEM;
    }
    state->entries[config->count++] = entry;
    return SOF_CONFIG_OK;
}

/// @brief Appends ch to text, growing it as needed, and returns an error code.
static sof_config_error_t text_append(text_t *text, int ch) {
    // Keep room for the NUL.
    if (text->length + 1 == text->capacity) {
        char *data = sof_alloc_realloc(text->alloc, text->data, text->capacity, text->capacity * 2);
        if (data == NULL) {
            return SOF_CONFIG_NO_MEM;
        }
        text->data = data;
        text->capacity *= 2;
    }
    text->data[text->length++] = (char)ch;
    text->data[text->length] = 0;
    return SOF_CONFIG_OK;
}

/// @brief Appends code point code to text as UTF-8, and returns an error
/// code.
static sof_config_error_t text_append_utf8(text_t *text, uint32_t code) {
    unsigned char bytes[SOF_UTF8_SIZE_MAX];
    size_t length = sof_utf8_encode(code, bytes, sizeof(bytes));
    for (size_t i = 0; i < length; i++) {
        if (text_append(text, bytes[i]) != SOF_CONFIG_OK) {
            return SOF_CONFIG_NO_MEM;
        }
    }
    return SOF_CONFIG_OK;
}

/// @brief Frees a NUL-terminated string allocated with alloc, if it isn't
/// NULL.
static void string_free(const sof_alloc_t *alloc, const char *s) {
    if (s != NULL) {
        sof_alloc_free(alloc, (char *)s, strlen(s) + 1);
    }
}

/// @brief Allocates an empty text with alloc, and returns an error code.
static sof_config_error_t text_init(text_t *text, const sof_alloc_t *alloc) {
    text->alloc = alloc;
    text->data = sof_alloc_malloc(alloc, text_size);
    if (text->data == NULL) {
        return SOF_CONFIG_NO_MEM;
    }
    text->data[0] = 0;
    text->length = 0;
    text->capacity = text_size;
    return SOF_CONFIG_OK;
}

/// @brief Empties text.
static void text_reset(text_t *text) {
    text->length = 0;
    text->data[0] = 0;
}

/// @brief Removes whitespace from the end of text.
static void text_trim(text_t *text) {
    while (text->length > 0 && is_blank((unsigned char)text->data[text->length - 1])) {
        text->length--;
    }
    text->data[text->length] = 0;
}

sof_config_error_t sof_config_clear(sof_config_t *config) {
    if (config == NULL || (config->state != NULL && config->state->parsing)) {
        return SOF_CONFIG_ERROR;
    }
    struct sof_config_state_t *state = config->state;
    if (state == NULL) {
        return SOF_CONFIG_OK;
    }
    sof_hashmap_clear(&state->map);
    for (size_t i = 0; i < config->count; i++) {
        entry_free(&state->alloc, state->entries[i]);
    }
    config->count = 0;
    return SOF_CONFIG_OK;
}

const char *sof_config_error_string(sof_config_error_t error) {
    size_t count = sizeof(error_strings) / sizeof(error_strings[0]);
    if ((int)error < 0 || (size_t)error >= count) {
        return "Unknown error";
    }
    return error_strings[error];
}

const sof_config_entry_t *sof_config_find(const sof_config_t *config, const char *section, const char *key) {
    if (config == NULL || config->state == NULL || key == NULL) {
        return NULL;
    }
    sof_config_entry_t probe = {section, key, NULL, 0};
    sof_hashmap_element_t *element = sof_hashmap_find(&config->state->map, &probe);
    return element != NULL ? element->key : NULL;
}

sof_config_error_t sof_config_free(sof_config_t *config) {
    if (config == NULL || sof_config_clear(config) != SOF_CONFIG_OK) {
        return SOF_CONFIG_ERROR;
    }
    struct sof_config_state_t *state = config->state;
    if (state != NULL) {
        // The state holds the allocator, so copy it before freeing the state.
        sof_alloc_t alloc = state->alloc;
        sof_hashmap_free(&state->map);
        sof_alloc_free(&alloc, state->entries, state->capacity * sizeof(sof_config_entry_t *));
        sof_alloc_free(&alloc, state->key.data, state->key.capacity);
        sof_alloc_free(&alloc, state->value.data, state->value.capacity);
        sof_alloc_free(&alloc, state->section.data, state->section.capacity);
        sof_alloc_free(&alloc, state->buffer, buffer_size);
        sof_alloc_free(&alloc, state, sizeof(struct sof_config_state_t));
    }
    memset(config, 0, sizeof(sof_config_t));
    return SOF_CONFIG_OK;
}

const char *sof_config_get(const sof_config_t *config, const char *section, const char *key) {
    const sof_config_entry_t *entry = sof_config_find(config, section, key);
    return entry != NULL ? entry->value : NULL;
}

sof_config_error_t sof_config_get_bool(const sof_config_t *config, const char *section, const char *key, int *value) {
    static const char *const true_words[] = {"true", "yes", "on", "1"};
    static const char *const false_words[] = {"false", "no", "off", "0"};
    const char *text;
    sof_config_error_t rc = find_value(config, section, key, value, &text);
    if (rc != SOF_CONFIG_OK) {
        return rc;
    }
    for (size_t i = 0; i < sizeof(true_words) / sizeof(true_words[0]); i++) {
        if (same_word(text, true_words[i])) {
            *value = 1;
            return SOF_CONFIG_OK;
        }
        if (same_word(text, false_words[i])) {
            *value = 0;
            return SOF_CONFIG_OK;
        }
    }
    return SOF_CONFIG_BAD_VALUE;
}

sof_config_error_t sof_config_get_double(const sof_config_t *config, const char *section, const char *key, double *value) {
    const char *text;
    sof_config_error_t rc = find_value(config, section, key, value, &text);
    if (rc != SOF_CONFIG_OK) {
        return rc;
    }
    // strtod() skips leading whitespace, which the value mustn't have.
    if (text[0] == 0 || isspace((unsigned char)text[0])) {
        return SOF_CONFIG_BAD_VALUE;
    }
    char *end;
    double number = strtod(text, &end);
    if (*end != 0 || !isfinite(number)) {
        return SOF_CONFIG_BAD_VALUE;
    }
    *value = number;
    return SOF_CONFIG_OK;
}

sof_config_error_t sof_config_get_int(const sof_config_t *config, const char *section, const char *key, int64_t *value) {
    const char *text;
    sof_config_error_t rc = find_value(config, section, key, value, &text);
    if (rc != SOF_CONFIG_OK) {
        return rc;
    }
    const char *digits = text + (text[0] == '+' || text[0] == '-');
    int base = digits[0] == '0' && (digits[1] == 'x' || digits[1] == 'X') ? 16 : 10;
    // strtoll() would skip whitespace and accept a second sign, or "0x" alone.
    if (!isdigit((unsigned char)digits[0]) || (base == 16 && !isxdigit((unsigned char)digits[2]))) {
        return SOF_CONFIG_BAD_VALUE;
    }
    char *end;
    errno = 0;
    long long number = strtoll(text, &end, base);
    if (errno != 0 || *end != 0) {
        return SOF_CONFIG_BAD_VALUE;
    }
#if LLONG_MAX > INT64_MAX
    if (number < INT64_MIN || number > INT64_MAX) {
        return SOF_CONFIG_BAD_VALUE;
    }
#endif
    *value = (int64_t)number;
    return SOF_CONFIG_OK;
}

sof_config_error_t sof_config_init(sof_config_t *config, sof_config_assignment_func_t handler, void *ctx, const sof_alloc_t *alloc) {
    if (config == NULL) {
        return SOF_CONFIG_ERROR;
    }
    memset(config, 0, sizeof(sof_config_t));
    if (!sof_alloc_valid(alloc)) {
        return SOF_CONFIG_ERROR;
    }
    config->handler = handler;
    config->ctx = ctx;
    config->state = sof_alloc_calloc(alloc, 1, sizeof(struct sof_config_state_t));
    if (config->state == NULL) {
        return SOF_CONFIG_NO_MEM;
    }
    struct sof_config_state_t *state = config->state;
    if (alloc != NULL) {
        state->alloc = *alloc;
    }
    state->buffer = sof_alloc_malloc(&state->alloc, buffer_size);
    if (state->buffer == NULL || text_init(&state->key, &state->alloc) != SOF_CONFIG_OK || text_init(&state->value, &state->alloc) != SOF_CONFIG_OK || text_init(&state->section, &state->alloc) != SOF_CONFIG_OK || sof_hashmap_init(&state->map, &entry_keys, NULL, NULL, 0, &state->alloc) != SOF_HASHMAP_OK) {
        sof_config_free(config);
        return SOF_CONFIG_NO_MEM;
    }
    return SOF_CONFIG_OK;
}

const sof_config_entry_t *sof_config_next(const sof_config_t *config, size_t *position) {
    if (config == NULL || position == NULL || config->state == NULL || *position >= config->count) {
        return NULL;
    }
    return config->state->entries[(*position)++];
}

sof_config_error_t sof_config_parse(sof_config_t *config, const char *file_name) {
    if (!ready(config) || file_name == NULL) {
        return SOF_CONFIG_ERROR;
    }
    FILE *file = fopen(file_name, "rb");
    if (file == NULL) {
        return SOF_CONFIG_NO_OPEN;
    }
    sof_config_error_t rc = sof_config_parse_file(config, file);
    fclose(file);
    return rc;
}

sof_config_error_t sof_config_parse_file(sof_config_t *config, FILE *file) {
    if (!ready(config) || file == NULL) {
        return SOF_CONFIG_ERROR;
    }
    struct sof_config_state_t *state = config->state;
    clearerr(file);
    state->file = file;
    state->next = state->end = state->buffer;
    sof_config_error_t rc = parse(config);
    if (ferror(file)) {
        return SOF_CONFIG_NO_READ;
    }
    return rc;
}

sof_config_error_t sof_config_parse_string(sof_config_t *config, const char *text, size_t length) {
    if (!ready(config) || text == NULL) {
        return SOF_CONFIG_ERROR;
    }
    struct sof_config_state_t *state = config->state;
    state->file = NULL;
    state->next = text;
    state->end = text + length;
    return parse(config);
}

int sof_config_traverse(const sof_config_t *config, sof_config_traverse_func_t cb, void *ctx) {
    if (config == NULL || cb == NULL) {
        return -1;
    }
    for (size_t i = 0; i < config->count; i++) {
        int rc = cb(config->state->entries[i], ctx);
        if (rc != 0) {
            return rc;
        }
    }
    return 0;
}
