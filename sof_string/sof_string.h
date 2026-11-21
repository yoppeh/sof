/**
 * @file sof_string.h
 * @author Warren Mann (warren@nonvol.io)
 * @brief Fat strings in C.
 * @details A sof_string_t keeps its length with its data, so it can hold any
 * bytes, NULs included, and the data is always followed by a NUL so it can
 * be passed as a c string. Regular expression search and split of these
 * strings are in sof_regex.
 * @version 0.0.1
 * @date 2026-11-04
 * @copyright Copyright (c) 2026
 */

#ifndef SOF_STRING_H
#define SOF_STRING_H

#include <stddef.h>

#include "sof_alloc.h"

/**
 * @brief A sof_string_t initializer for a string literal, without copying it.
 * @details The string's data is the literal itself, so it is read-only:
 * functions that change a string return SOF_STRING_ERROR for it, and
 * sof_string_free() empties it without freeing anything. The literal can
 * hold NULs, which are part of the string: sof_string_const("a\0b") has
 * length 3.
 */
#define sof_string_const(s) {(s), sizeof(s) - 1, 0, {NULL, NULL, NULL, NULL, NULL}}

/**
 * @brief Error codes for the sof_string functions.
 */
typedef enum sof_string_error_t {
    SOF_STRING_OK = 0,
    SOF_STRING_NO_MEM,
    SOF_STRING_NO_MATCH,
    SOF_STRING_ERROR,
} sof_string_error_t;

/**
 * @brief A string of bytes.
 * @details The application provides the sof_string_t and sets it up with
 * sof_string_init(), sof_string_from_bytes(), sof_string_from_cstr(),
 * sof_string_dup(), sof_string_substr(), or, read-only, sof_string_const()
 * or sof_string_view(). The fields can be read but not changed: data holds
 * length bytes and then a NUL (except in a sof_string_view(), whose bytes
 * needn't be followed by one), and is NULL for a string
 * that was freed or whose initialization failed, which acts as an empty
 * string that can't be changed. data moves when the string grows, so
 * pointers into it are good only until the next sof_string_append(),
 * sof_string_append_bytes(), sof_string_append_cstr(),
 * sof_string_append_char(), sof_string_reserve(), sof_string_insert(),
 * sof_string_insert_bytes(), sof_string_insert_cstr() or sof_string_free().
 */
typedef struct sof_string_t {
    /// @brief The bytes, followed by a NUL, or NULL.
    char *data;
    /// @brief The number of bytes in use, not counting the NUL.
    size_t length;
    /// @brief The size of the block data points to, NUL included, or 0 if
    /// the data isn't the string's own (a sof_string_const(), a
    /// sof_string_view() or a dead string).
    size_t size;
    /// @brief The memory functions data is allocated and freed with.
    sof_alloc_t alloc;
} sof_string_t;

/**
 * @brief Appends one string to another.
 * @details target grows as needed, to at least twice its size, so appending
 * a byte at a time takes linear time. suffix can be target itself, or share
 * its data, which doubles target.
 * @param target The string to append to.
 * @param suffix The string to append. A dead suffix is empty.
 * @return SOF_STRING_OK, SOF_STRING_NO_MEM if target couldn't grow, in
 * which case it is unchanged, or SOF_STRING_ERROR if either is NULL, or
 * target is dead or a sof_string_const().
 */
extern sof_string_error_t sof_string_append(sof_string_t *target, const sof_string_t *suffix);

/**
 * @brief Appends bytes to a string.
 * @details As sof_string_append(), with the length bytes at bytes, which can
 * be any bytes, NULs included. bytes can point into target's data.
 * @param target The string to append to.
 * @param bytes The bytes to append. It can be NULL if length is 0.
 * @param length How many bytes to append.
 * @return SOF_STRING_OK, SOF_STRING_NO_MEM if target couldn't grow, in
 * which case it is unchanged, or SOF_STRING_ERROR if target is NULL, dead or
 * a sof_string_const(), or bytes is NULL and length isn't 0.
 */
extern sof_string_error_t sof_string_append_bytes(sof_string_t *target, const void *bytes, size_t length);

/**
 * @brief Appends a c string to a string.
 * @details As sof_string_append(), with the bytes of suffix up to its NUL.
 * suffix can point into target's data.
 * @param target The string to append to.
 * @param suffix The NUL-terminated c string to append.
 * @return SOF_STRING_OK, SOF_STRING_NO_MEM if target couldn't grow, in
 * which case it is unchanged, or SOF_STRING_ERROR if either is NULL, or
 * target is dead or a sof_string_const().
 */
extern sof_string_error_t sof_string_append_cstr(sof_string_t *target, const char *suffix);

/**
 * @brief Compares two strings byte by byte, ignoring the case of ASCII
 * letters.
 * @details Each byte is folded as sof_string_tolower() folds it, then
 * compared as an unsigned byte, so the result is what sof_string_cmp()
 * would give for lower case copies of the two strings, without making them.
 * Other bytes, UTF-8 sequences included, compare as they are, whatever the
 * locale. A string that is a prefix of another is less than it. NULL and
 * dead strings are empty, so they are equal to each other and to "".
 * @param a The first string.
 * @param b The second string.
 * @return -1, 0 or 1 as a is less than, equal to or greater than b.
 */
extern int sof_string_casecmp(const sof_string_t *a, const sof_string_t *b);

/**
 * @brief Empties a string, keeping its memory for reuse.
 * @param string The string to empty.
 * @return SOF_STRING_OK, or SOF_STRING_ERROR if string is NULL, dead or a
 * sof_string_const().
 */
extern sof_string_error_t sof_string_clear(sof_string_t *string);

/**
 * @brief Compares two strings byte by byte, as unsigned bytes.
 * @details A string that is a prefix of another is less than it. NULL and
 * dead strings are empty, so they are equal to each other and to "".
 * @param a The first string.
 * @param b The second string.
 * @return -1, 0 or 1 as a is less than, equal to or greater than b.
 */
extern int sof_string_cmp(const sof_string_t *a, const sof_string_t *b);

/**
 * @brief Returns a string's data as a c string.
 * @details The data belongs to the string: don't change or free it. It ends
 * at the first NUL for functions that take a c string, which is before
 * length if the string holds a NUL.
 * @param string The string.
 * @return The data, followed by a NUL, or NULL if string is NULL or dead.
 */
extern const char *sof_string_cstr(const sof_string_t *string);

/**
 * @brief Removes bytes from a string.
 * @details The bytes after them move down, and the string's memory is kept.
 * @param string The string to remove from.
 * @param offset Where the bytes to remove start.
 * @param length How many to remove.
 * @return SOF_STRING_OK, or SOF_STRING_ERROR if string is NULL, dead or a
 * sof_string_const(), or the bytes don't lie within it.
 */
extern sof_string_error_t sof_string_delete(sof_string_t *string, size_t offset, size_t length);

/**
 * @brief Initializes a string with a copy of another.
 * @details target is overwritten without being freed, so it must not hold a
 * string of its own. The copy's memory is just large enough for its data.
 * @param target The string to initialize.
 * @param source The string to copy, which can be a sof_string_const().
 * @param alloc The memory functions for the copy, or NULL for the standard
 * functions. target keeps a copy, so it can be on the stack.
 * @return SOF_STRING_OK, SOF_STRING_NO_MEM, or SOF_STRING_ERROR if either
 * string is NULL, source is dead, target is source, or alloc has a malloc but
 * no free. On failure target is left dead, unless it is NULL or source.
 */
extern sof_string_error_t sof_string_dup(sof_string_t *target, const sof_string_t *source, const sof_alloc_t *alloc);

/**
 * @brief Frees a string's memory.
 * @details The sof_string_t itself isn't freed; it is left dead (data NULL)
 * until it is initialized again. A sof_string_const() is emptied the same
 * way, without freeing anything.
 * @param string The string to free.
 * @return SOF_STRING_OK, or SOF_STRING_ERROR if string is NULL.
 */
extern sof_string_error_t sof_string_free(sof_string_t *string);

/**
 * @brief Initializes a string with a copy of some bytes.
 * @details string is overwritten without being freed. Its memory is just
 * large enough for the bytes.
 * @param string The string to initialize.
 * @param bytes The bytes to copy, which can be any bytes, NULs included. It
 * can be NULL if length is 0, which makes an empty string.
 * @param length How many bytes to copy.
 * @param alloc The memory functions for the string, or NULL for the
 * standard functions. The string keeps a copy, so it can be on the stack.
 * @return SOF_STRING_OK, SOF_STRING_NO_MEM, or SOF_STRING_ERROR if string
 * is NULL, bytes is NULL and length isn't 0, or alloc has a malloc but no
 * free. On failure string is left dead.
 */
extern sof_string_error_t sof_string_from_bytes(sof_string_t *string, const void *bytes, size_t length, const sof_alloc_t *alloc);

/**
 * @brief Initializes a string with a copy of a c string.
 * @details string is overwritten without being freed.
 * @param string The string to initialize.
 * @param cstring The NUL-terminated c string to copy.
 * @param alloc The memory functions for the string, or NULL for the
 * standard functions. The string keeps a copy, so it can be on the stack.
 * @return SOF_STRING_OK, SOF_STRING_NO_MEM, or SOF_STRING_ERROR if string
 * or cstring is NULL, or alloc has a malloc but no free. On failure string is
 * left dead.
 */
extern sof_string_error_t sof_string_from_cstr(sof_string_t *string, const char *cstring, const sof_alloc_t *alloc);

/**
 * @brief Initializes an empty string.
 * @details string is overwritten without being freed.
 * @param string The string to initialize.
 * @param capacity The bytes it can hold before it grows, not counting the
 * NUL, which is always allocated too.
 * @param alloc The memory functions for the string, or NULL for the
 * standard functions. The string keeps a copy, so it can be on the stack.
 * @return SOF_STRING_OK, SOF_STRING_NO_MEM, or SOF_STRING_ERROR if string
 * is NULL or alloc has a malloc but no free. On failure string is left dead.
 */
extern sof_string_error_t sof_string_init(sof_string_t *string, size_t capacity, const sof_alloc_t *alloc);

/**
 * @brief Inserts one string into another.
 * @details The bytes from offset on move up to make room, and target grows
 * as sof_string_append() grows it. With sof_string_delete(), it changes a
 * text in place, such as at a span from sof_toml, sof_json or sof_csv.
 * @param target The string to insert into.
 * @param offset Where s goes, from 0 to target's length, which appends it.
 * @param s The string to insert, which can be target itself, or a
 * sof_string_view() of any part of it. A dead s is empty.
 * @return SOF_STRING_OK, SOF_STRING_NO_MEM if target couldn't grow, in
 * which case it is unchanged, or SOF_STRING_ERROR if either is NULL, target
 * is dead or a sof_string_const(), or offset is past target's length.
 */
extern sof_string_error_t sof_string_insert(sof_string_t *target, size_t offset, const sof_string_t *s);

/**
 * @brief Inserts bytes into a string.
 * @details As sof_string_insert().
 * @param target The string to insert into.
 * @param offset Where the bytes go, from 0 to target's length.
 * @param bytes The bytes, which can be any bytes, NULs included, and can
 * lie in target's own data. It can be NULL if length is 0, which inserts
 * nothing.
 * @param length How many bytes to insert.
 * @return As sof_string_insert(), and SOF_STRING_ERROR if bytes is NULL and
 * length isn't 0.
 */
extern sof_string_error_t sof_string_insert_bytes(sof_string_t *target, size_t offset, const void *bytes, size_t length);

/**
 * @brief Inserts a c string into a string.
 * @details As sof_string_insert().
 * @param target The string to insert into.
 * @param offset Where the bytes go, from 0 to target's length.
 * @param s The NUL-terminated c string, which can point into target's own
 * data.
 * @return As sof_string_insert().
 */
extern sof_string_error_t sof_string_insert_cstr(sof_string_t *target, size_t offset, const char *s);

/**
 * @brief Gives the line and column of an offset in a string.
 * @details For a place a parser reports as an offset, such as a span in
 * sof_toml, sof_json or sof_csv. Lines end at each line feed, so a CRLF
 * line's carriage return is the last column of its line.
 * @param string The string, which can be a sof_string_view() of a text.
 * A dead string is empty.
 * @param offset The offset, from 0 to the string's length, which is its
 * end.
 * @param line Where the line goes, counting from 1, or NULL.
 * @param column Where the column goes, in bytes and counting from 1, or
 * NULL.
 * @return SOF_STRING_OK, or SOF_STRING_ERROR, leaving line and column
 * alone, if string is NULL or offset is past its length.
 */
extern sof_string_error_t sof_string_position(const sof_string_t *string, size_t offset, size_t *line, size_t *column);

/**
 * @brief Makes room at the end of a string, for the caller to write into.
 * @details For a writer that builds its output a few bytes at a time, or a
 * reader that reads straight into the string: write up to length bytes at
 * the pointer returned, then add them with sof_string_commit(). The string
 * itself is unchanged, its length and contents included, and grows as
 * sof_string_append() grows it.
 * @param target The string to make room in.
 * @param length How many bytes the caller may write.
 * @return Where to write them, which is target's data plus its length and is
 * good only until the next function that changes target, or NULL if target
 * is NULL, dead or a sof_string_const(), or couldn't grow, in which case it
 * is unchanged.
 */
extern char *sof_string_reserve(sof_string_t *target, size_t length);

/**
 * @brief Initializes a string with a copy of part of another.
 * @details target is overwritten without being freed, so it must not hold a
 * string of its own.
 * @param target The string to initialize.
 * @param source The string to copy from, which can be a sof_string_const().
 * @param offset Where the part starts.
 * @param length How many bytes it has.
 * @param alloc The memory functions for the copy, or NULL for the standard
 * functions. target keeps a copy, so it can be on the stack.
 * @return SOF_STRING_OK, SOF_STRING_NO_MEM, or SOF_STRING_ERROR if either
 * string is NULL, source is dead, target is source, the part doesn't lie
 * within source, or alloc has a malloc but no free. On failure target is left
 * dead, unless it is NULL or source.
 */
extern sof_string_error_t sof_string_substr(sof_string_t *target, const sof_string_t *source, size_t offset, size_t length, const sof_alloc_t *alloc);

/**
 * @brief Converts the ASCII letters of a string to lower case.
 * @details Other bytes, UTF-8 sequences included, are left alone, whatever
 * the locale.
 * @param string The string to convert.
 * @return SOF_STRING_OK, or SOF_STRING_ERROR if string is NULL, dead or a
 * sof_string_const().
 */
extern sof_string_error_t sof_string_tolower(sof_string_t *string);

/**
 * @brief Converts the ASCII letters of a string to upper case.
 * @details Other bytes, UTF-8 sequences included, are left alone, whatever
 * the locale.
 * @param string The string to convert.
 * @return SOF_STRING_OK, or SOF_STRING_ERROR if string is NULL, dead or a
 * sof_string_const().
 */
extern sof_string_error_t sof_string_toupper(sof_string_t *string);

/**
 * @brief Returns a read-only string of some bytes, without copying them.
 * @details As sof_string_const(), for bytes that aren't a literal: the
 * string's data is the bytes themselves, so they must stay as they are for as
 * long as the string is used. Functions that change a string return
 * SOF_STRING_ERROR for it, and sof_string_free() empties it without freeing
 * anything. Unlike other strings, its data needn't be followed by a NUL, so
 * sof_string_cstr() of it is a c string only if the bytes are followed by
 * one; every other function uses only its length bytes.
 * @param bytes The bytes, which can be any bytes, NULs included. NULL is
 * empty, whatever length is.
 * @param length How many bytes there are.
 * @return The string, which needs no freeing.
 */
extern sof_string_t sof_string_view(const void *bytes, size_t length);

// The functions below are static inline, so the common case, a string with
// room, costs no call; writers spend much of their time appending a byte or
// two. They read and write the string's fields here, in its own header.

/**
 * @brief Appends one byte to a string.
 * @details As sof_string_append_bytes() with one byte, which can be any byte,
 * NUL included. When the string has room, this is a store and no call.
 * @param target The string to append to.
 * @param c The byte.
 * @return As sof_string_append_bytes().
 */
static inline sof_string_error_t sof_string_append_char(sof_string_t *target, char c) {
    if (target != NULL && target->length + 1 < target->size) {
        target->data[target->length++] = c;
        target->data[target->length] = '\0';
        return SOF_STRING_OK;
    }
    return sof_string_append_bytes(target, &c, 1);
}

/**
 * @brief Adds to a string the bytes written after sof_string_reserve().
 * @details The bytes are those at target's data plus its length; the NUL is
 * written after them.
 * @param target The string the room was made in.
 * @param length How many bytes were written, at most what was reserved.
 * @return SOF_STRING_OK, or SOF_STRING_ERROR, changing nothing, if target is
 * NULL, dead or a sof_string_const(), or there isn't room for length bytes
 * and the NUL.
 */
static inline sof_string_error_t sof_string_commit(sof_string_t *target, size_t length) {
    if (target == NULL || target->size == 0 || length >= target->size - target->length) {
        return SOF_STRING_ERROR;
    }
    target->length += length;
    target->data[target->length] = '\0';
    return SOF_STRING_OK;
}

#endif // SOF_STRING_H
