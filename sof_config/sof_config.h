/**
 * @file sof_config.h
 * @author Warren Mann (warren@nonvol.io)
 * @brief Configuration file parser in C.
 * @details Parses .ini style files of sections and key = value assignments.
 * Every assignment is stored, to be looked up by section and key, and can
 * also be passed to a callback as it is parsed.
 * @version 0.0.1
 * @date 2026-11-03
 * @copyright Copyright (c) 2026
 */

#ifndef SOF_CONFIG_H
#define SOF_CONFIG_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "sof_alloc.h"

/**
 * @brief Error codes for the config functions.
 * @details
 * - SOF_CONFIG_OK: no error.
 * - SOF_CONFIG_NO_MEM: memory couldn't be allocated.
 * - SOF_CONFIG_NO_MATCH: no assignment has the section and key looked up.
 * - SOF_CONFIG_ERROR: a NULL argument, a config that isn't initialized, or
 *   a call made from inside the assignment callback.
 * - SOF_CONFIG_NO_OPEN: the file couldn't be opened.
 * - SOF_CONFIG_NO_READ: reading the file failed.
 * - SOF_CONFIG_UNEXPECTED_CHARACTER: a character appeared where it isn't
 *   allowed.
 * - SOF_CONFIG_UNEXPECTED_EOL: a line ended in the middle of a section
 *   declaration, before the = of an assignment, or inside a quoted value.
 * - SOF_CONFIG_PREMATURE_EOF: the input ended in the same places.
 * - SOF_CONFIG_BAD_ESCAPE: an escape sequence in a quoted value isn't one of
 *   those recognized.
 * - SOF_CONFIG_BAD_VALUE: a value isn't of the type asked for, or is out
 *   of its range.
 * - SOF_CONFIG_STOPPED: the assignment callback stopped the parse; its
 *   value is in the config's stop_value.
 */
typedef enum sof_config_error_t {
    SOF_CONFIG_OK = 0,
    SOF_CONFIG_NO_MEM,
    SOF_CONFIG_NO_MATCH,
    SOF_CONFIG_ERROR,
    SOF_CONFIG_NO_OPEN,
    SOF_CONFIG_NO_READ,
    SOF_CONFIG_UNEXPECTED_CHARACTER,
    SOF_CONFIG_UNEXPECTED_EOL,
    SOF_CONFIG_PREMATURE_EOF,
    SOF_CONFIG_BAD_ESCAPE,
    SOF_CONFIG_BAD_VALUE,
    SOF_CONFIG_STOPPED,
} sof_config_error_t;

/**
 * @brief A stored assignment.
 * @details section is NULL for an assignment before the first section
 * declaration. If a section and key are assigned more than once, the entry
 * holds the last value, and the line it is on, but keeps the place of the
 * first. The strings belong to the config, and are good until
 * sof_config_clear() or sof_config_free(). The fields can be read but should
 * not be changed by the application.
 */
typedef struct sof_config_entry_t {
    const char *section;
    const char *key;
    const char *value;
    size_t line;
} sof_config_entry_t;

/**
 * @brief Receives each key = value assignment as it is parsed.
 * @details Called after the assignment is stored. The strings are only good
 * until the callback returns; copy any that are needed later, or look them
 * up after the parse. It must not call into the config it was called for.
 * @param section The NUL-terminated current section, or NULL before the first
 * section declaration.
 * @param key The NUL-terminated key.
 * @param value The NUL-terminated value, trimmed, with any quotes removed and
 * escape sequences replaced.
 * @param ctx The ctx given to sof_config_init().
 * @return 0 to continue parsing, or a non-zero value to stop: the parse
 * function returns SOF_CONFIG_STOPPED, and the value is kept in the config's
 * stop_value.
 */
typedef int (*sof_config_assignment_func_t)(const char *section, const char *key, const char *value, void *ctx);

/**
 * @brief Called for each entry visited by sof_config_traverse().
 * @details Return 0 to continue, or a non-zero value to stop the traversal
 * early. It must not parse into, clear or free the config. ctx is the ctx
 * given to sof_config_traverse().
 */
typedef int (*sof_config_traverse_func_t)(const sof_config_entry_t *entry, void *ctx);

/// @brief The parser's internal state, defined in sof_config.c.
struct sof_config_state_t;

/**
 * @brief A configuration: a parser, and the assignments it has stored.
 * @details The application provides the sof_config_t and sets it up with
 * sof_config_init(). Each parse adds the assignments it finds to those already
 * stored, so a file of defaults can be parsed and then a file that overrides
 * some of them. sof_config_clear() removes them all, and sof_config_free()
 * releases the config's memory when the application is finished with it.
 *
 * count is the number of entries stored. line and column are the position,
 * starting at 1, where the last parse stopped: on an error, the character at
 * fault, or the start of the escape sequence for SOF_CONFIG_BAD_ESCAPE.
 * Columns count bytes. stop_value is the value the callback returned to stop
 * the last parse, or 0. handler and ctx are those given to sof_config_init().
 * The fields can be read, but should not be changed by the application.
 *
 * Every function accepts a NULL config, and a config that was freed or whose
 * sof_config_init() failed (state is NULL), as described for each.
 *
 * The config does no locking. sof_config_find(), sof_config_get(), the typed
 * getters, sof_config_next() and sof_config_traverse() only read it; every
 * other function writes it.
 */
typedef struct sof_config_t {
    sof_config_assignment_func_t handler;
    void *ctx;
    size_t count;
    size_t line;
    size_t column;
    int stop_value;
    struct sof_config_state_t *state;
} sof_config_t;

/**
 * @brief Removes every stored entry.
 * @param config The config to clear.
 * @return SOF_CONFIG_OK, or SOF_CONFIG_ERROR if config is NULL or this is
 * called from inside the assignment callback.
 */
extern sof_config_error_t sof_config_clear(sof_config_t *config);

/**
 * @brief Returns a description of an error code.
 * @param error The error code to describe.
 * @return A NUL-terminated description, or "Unknown error" if error isn't a
 * sof_config_error_t.
 */
extern const char *sof_config_error_string(sof_config_error_t error);

/**
 * @brief Finds the entry for a section and key.
 * @param config The config to search.
 * @param section The section, or NULL for assignments outside any section.
 * @param key The key.
 * @return The entry, or NULL if there isn't one, or config or key is NULL.
 */
extern const sof_config_entry_t *sof_config_find(const sof_config_t *config, const char *section, const char *key);

/**
 * @brief Releases the memory the config allocated, including every entry.
 * @details The sof_config_t itself belongs to the caller and isn't freed; it
 * is left empty, and can be initialized again.
 * @param config The config to free.
 * @return SOF_CONFIG_OK, or SOF_CONFIG_ERROR if config is NULL or this is
 * called from inside the assignment callback.
 */
extern sof_config_error_t sof_config_free(sof_config_t *config);

/**
 * @brief Returns the value for a section and key.
 * @param config The config to search.
 * @param section The section, or NULL for assignments outside any section.
 * @param key The key.
 * @return The value, good until sof_config_clear() or sof_config_free(), or
 * NULL if there isn't one, or config or key is NULL.
 */
extern const char *sof_config_get(const sof_config_t *config, const char *section, const char *key);

/**
 * @brief Gets a value as a boolean.
 * @details true, yes, on and 1 are true; false, no, off and 0 are false, in
 * any case.
 * @param config The config to search.
 * @param section The section, or NULL for assignments outside any section.
 * @param key The key.
 * @param value Set to 1 or 0. Left unchanged on any error, so it can be set
 * to a default first.
 * @return SOF_CONFIG_OK, SOF_CONFIG_NO_MATCH if there is no such entry,
 * SOF_CONFIG_BAD_VALUE if its value isn't one of those above, or
 * SOF_CONFIG_ERROR if config, key or value is NULL.
 */
extern sof_config_error_t sof_config_get_bool(const sof_config_t *config, const char *section, const char *key, int *value);

/**
 * @brief Gets a value as a floating-point number.
 * @details The value must be a whole decimal or hexadecimal floating-point
 * number, as strtod() reads them, and finite.
 * @param config The config to search.
 * @param section The section, or NULL for assignments outside any section.
 * @param key The key.
 * @param value Set to the number. Left unchanged on any error.
 * @return As sof_config_get_bool().
 */
extern sof_config_error_t sof_config_get_double(const sof_config_t *config, const char *section, const char *key, double *value);

/**
 * @brief Gets a value as an integer.
 * @details The value must be an optional sign, then decimal digits, or 0x
 * and hexadecimal digits, and fit in an int64_t. A leading 0 doesn't make it
 * octal.
 * @param config The config to search.
 * @param section The section, or NULL for assignments outside any section.
 * @param key The key.
 * @param value Set to the number. Left unchanged on any error.
 * @return As sof_config_get_bool().
 */
extern sof_config_error_t sof_config_get_int(const sof_config_t *config, const char *section, const char *key, int64_t *value);

/**
 * @brief Sets up a config the caller provides.
 * @param config The config to initialize.
 * @param handler Called with each assignment parsed, or NULL.
 * @param ctx Passed to handler.
 * @param alloc The memory functions for everything the config allocates,
 * including its entries and their strings, or NULL for the standard
 * functions. The config keeps a copy, so it can be on the stack.
 * @return SOF_CONFIG_OK, SOF_CONFIG_NO_MEM if memory couldn't be allocated,
 * or SOF_CONFIG_ERROR if config is NULL or alloc has a malloc but no free; on
 * an error, config is left empty.
 */
extern sof_config_error_t sof_config_init(sof_config_t *config, sof_config_assignment_func_t handler, void *ctx, const sof_alloc_t *alloc);

/**
 * @brief Returns the next entry, in the order their sections and keys first
 * appeared.
 * @details Set *position to 0 to start; each call updates it. Don't parse
 * into, clear or free the config until the loop ends.
 * @param config The config to loop over.
 * @param position The place in the loop.
 * @return The next entry, or NULL when there are no more, or if config or
 * position is NULL.
 */
extern const sof_config_entry_t *sof_config_next(const sof_config_t *config, size_t *position);

/**
 * @brief Opens and parses a file, adding its assignments to those stored.
 * @param config The config to parse with.
 * @param file_name The path of the file to parse.
 * @return SOF_CONFIG_OK if the whole file was parsed; SOF_CONFIG_STOPPED if
 * the handler stopped the parse; SOF_CONFIG_NO_OPEN if the file couldn't be
 * opened; SOF_CONFIG_NO_READ if reading it failed;
 * SOF_CONFIG_UNEXPECTED_CHARACTER, SOF_CONFIG_UNEXPECTED_EOL,
 * SOF_CONFIG_PREMATURE_EOF or SOF_CONFIG_BAD_ESCAPE if its syntax is wrong;
 * SOF_CONFIG_NO_MEM if memory couldn't be allocated; or SOF_CONFIG_ERROR if
 * config or file_name is NULL, config isn't initialized, or this is called from
 * inside the assignment callback. On an error, the assignments before it are
 * stored.
 */
extern sof_config_error_t sof_config_parse(sof_config_t *config, const char *file_name);

/**
 * @brief Parses an open file, such as stdin, from where it is to its end.
 * @details The file is read but not closed.
 * @param config The config to parse with.
 * @param file The file to parse.
 * @return As sof_config_parse(), except SOF_CONFIG_NO_OPEN, and with
 * SOF_CONFIG_ERROR if file is NULL.
 */
extern sof_config_error_t sof_config_parse_file(sof_config_t *config, FILE *file);

/**
 * @brief Parses text in memory.
 * @param config The config to parse with.
 * @param text The text to parse, which needn't be NUL-terminated.
 * @param length The length of text in bytes.
 * @return As sof_config_parse(), except SOF_CONFIG_NO_OPEN and
 * SOF_CONFIG_NO_READ, and with SOF_CONFIG_ERROR if text is NULL.
 */
extern sof_config_error_t sof_config_parse_string(sof_config_t *config, const char *text, size_t length);

/**
 * @brief Calls cb with each entry and ctx, in the order of sof_config_next().
 * @param config The config to traverse.
 * @param cb The function to call.
 * @param ctx Passed to cb.
 * @return 0 once every entry has been visited, the non-zero value cb returned
 * to stop, or -1 if config or cb is NULL.
 */
extern int sof_config_traverse(const sof_config_t *config, sof_config_traverse_func_t cb, void *ctx);

#endif // SOF_CONFIG_H
