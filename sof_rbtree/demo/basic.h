/**
 * @file basic.h
 * @author Warren Mann (warren@nonvol.io)
 * @brief A BASIC interpreter in c, for the basic program.
 * @details A basic_t holds a BASIC program and runs it: ECMA-55 Minimal
 * BASIC (ANSI X3.60-1978), with multiple statements per line, IF ... THEN
 * statements, string variables and arrays, string functions, AND, OR, NOT
 * and MOD as extensions. Lines are given one at a time, as they are typed:
 * a line that starts with a number is stored in the program, or deletes the
 * line of that number if nothing follows it, and any other line is run at
 * once. The commands RUN, LIST, NEW, CONT, LOAD and SAVE are statements
 * that can be run that way. Output, INPUT and the reports of exceptions the
 * program continues after go through callbacks, so the interpreter never
 * touches standard input or output itself; LOAD and SAVE read and write
 * files.
 *
 * Each line is split into tokens when it is entered, so a line with a
 * misplaced quote or a character BASIC doesn't use is refused at once; the
 * rest of the syntax is checked as the line runs. Program lines are kept in
 * a sof_rbtree ordered by line number, variables, arrays and functions in
 * sof_hashmaps, GOSUB returns and FOR loops on sof_stacks, the program's
 * DATA in a sof_vector, and strings in sof_strings. RND comes from a
 * sof_random generator seeded by the seed given to basic_init(), so a
 * program gives the same numbers every run.
 * @version 0.0.1
 * @date 2026-11-04
 * @copyright Copyright (c) 2026
 */

#ifndef BASIC_H
#define BASIC_H

#include <stddef.h>
#include <stdint.h>

#include "sof_alloc.h"
#include "sof_hashmap.h"
#include "sof_random.h"
#include "sof_rbtree.h"
#include "sof_stack.h"
#include "sof_string.h"
#include "sof_vector.h"

/// @brief The highest line number a program can have.
#define BASIC_LINE_MAX 99999

/// @brief The longest name a variable, array or function can have, in bytes,
/// its '$' included.
#define BASIC_NAME_MAX 31

/// @brief The size of basic_t's message buffer, NUL included.
#define BASIC_MESSAGE_SIZE 256

/// @brief The width of an output line: PRINT starts a new line rather than
/// write past it.
#define BASIC_MARGIN 80

/// @brief The width of a print zone, which a comma in PRINT moves to the
/// start of the next one of.
#define BASIC_ZONE 15

/**
 * @brief Error codes for the basic functions.
 * @details BASIC_NO_MATCH is never returned; it is there so the codes match
 * those of the other libraries.
 */
typedef enum basic_error_t {
    BASIC_OK = 0,
    BASIC_NO_MEM,
    BASIC_NO_MATCH,
    BASIC_ERROR,
    /// @brief A line isn't BASIC: it was refused when entered, or the
    /// program stopped at it. basic_message() says why and where.
    BASIC_SYNTAX,
    /// @brief The program stopped at a fatal exception, such as a subscript
    /// out of range, or a command failed, as LOAD of a file that can't be
    /// read. basic_message() says why and where.
    BASIC_RUNTIME,
    /// @brief The program ran a STOP statement. CONT goes on after it, if
    /// the program isn't changed first. basic_message() says where.
    BASIC_STOPPED,
} basic_error_t;

/**
 * @brief Writes output: PRINT's, INPUT's prompts, and LIST's.
 * @details text is length bytes, not NUL-terminated, and may hold newlines.
 * ctx is the ctx given to basic_init().
 * @return 0, or non-zero if the output couldn't be written, which stops the
 * program with a fatal exception.
 */
typedef int (*basic_write_func_t)(const char *text, size_t length, void *ctx);

/**
 * @brief Reads a line for INPUT.
 * @details Called after INPUT's prompt has been written. The callback puts
 * the line into line, which is empty, without its newline. ctx is the ctx
 * given to basic_init().
 * @return 0 if a line was read, or non-zero at the end of the input, which
 * stops the program with a fatal exception.
 */
typedef int (*basic_read_func_t)(sof_string_t *line, void *ctx);

/**
 * @brief Reports an exception the program goes on after, as ECMA-55 has
 * for division by zero, overflow, a TAB argument below 1 and a bad INPUT
 * reply.
 * @details message is NUL-terminated, without a newline, such as "line 20:
 * division by zero". ctx is the ctx given to basic_init().
 */
typedef void (*basic_warn_func_t)(const char *message, void *ctx);

/**
 * @brief A BASIC interpreter and the program it holds.
 * @details lines maps each line number to its line, held in an internal
 * struct basic_line_t. scalars maps each simple variable's name to its
 * value, arrays each array's name to its elements, and functions each DEF
 * function's name to where it is defined. gosubs holds the places RETURN
 * goes back to, and loops the FOR loops running. data holds the program's
 * DATA items in order, and data_next the index of the next one READ reads.
 * random is the generator RND draws from, started from seed by every RUN.
 * The callbacks and ctx are those given to basic_init(). line, node and pc
 * say which token of which line is running, and immediate is the line
 * typed without a number being run, or NULL. cont_node and cont_pc say
 * where CONT goes on, if can_continue is set. base is the lower bound of
 * array subscripts, set by OPTION BASE. column is the column the next
 * output goes in, from 0. message says why the last call failed, and input
 * holds the last line read for INPUT. alloc is a copy of the allocator
 * (zeroed, meaning the standard functions, if it was given none), and ready
 * is 1 once basic_init() has succeeded and 0 when it was freed or failed.
 * The other fields are the interpreter's own. None of the fields should be
 * changed by the application.
 *
 * The sof_rbtree in lines must not move while it holds nodes, so a basic_t
 * must not be moved or copied once initialized.
 *
 * Every function accepts a NULL basic_t, and one that was freed or whose
 * basic_init() failed, as described for each.
 *
 * The interpreter does no locking. basic_message() only reads it; every
 * other function writes it, basic_list() included, since it keeps track of
 * the output's column.
 */
typedef struct basic_t {
    sof_rbtree_t lines;
    sof_hashmap_t scalars;
    sof_hashmap_t arrays;
    sof_hashmap_t functions;
    sof_stack_t gosubs;
    sof_stack_t loops;
    sof_vector_t data;
    size_t data_next;
    sof_random_t random;
    uint64_t seed;
    basic_write_func_t write_func;
    basic_read_func_t read_func;
    basic_warn_func_t warn_func;
    void *ctx;
    struct basic_line_t *line;
    sof_rbtree_node_t *node;
    size_t pc;
    struct basic_line_t *immediate;
    sof_rbtree_node_t *cont_node;
    size_t cont_pc;
    int can_continue;
    int base;
    /// @brief Set when the program has changed since its DEFs and DATA were
    /// last gathered, so they must be gathered again before they are used.
    int changed;
    /// @brief Set by a statement that moved to another place, so the end of
    /// the statement it was in isn't checked.
    int jumped;
    /// @brief How deeply expressions and DEF functions are nested.
    size_t depth;
    size_t column;
    char message[BASIC_MESSAGE_SIZE];
    sof_string_t input;
    sof_alloc_t alloc;
    int ready;
} basic_t;

/**
 * @brief Frees everything the interpreter allocated, its program and
 * variables included.
 * @details The basic_t itself isn't freed (it belongs to the caller); it is
 * left empty, and acts like one whose basic_init() failed until it is
 * initialized again.
 * @param b The interpreter.
 * @return BASIC_OK, or BASIC_ERROR if b is NULL or not initialized.
 */
extern basic_error_t basic_free(basic_t *b);

/**
 * @brief Initializes an interpreter, with no program, in a basic_t the
 * caller provides.
 * @param b The interpreter to initialize. It can be anywhere: on the stack,
 * in another structure or on the heap, but must not move afterwards.
 * @param write_func Writes the output. NULL throws it away.
 * @param read_func Reads lines for INPUT. NULL makes INPUT fail as at the end
 * of the input.
 * @param warn_func Reports exceptions the program goes on after. NULL writes
 * them through write_func instead, each on a line of its own.
 * @param ctx Passed to every callback.
 * @param seed The seed RND's numbers start from at every RUN.
 * @param alloc The memory functions the interpreter uses for everything, or
 * NULL for the standard functions. The interpreter keeps a copy, so it can
 * be on the stack.
 * @return BASIC_OK, BASIC_NO_MEM, or BASIC_ERROR if b is NULL or alloc has a
 * malloc but no free; on an error, b is left empty.
 */
extern basic_error_t basic_init(basic_t *b, basic_write_func_t write_func, basic_read_func_t read_func, basic_warn_func_t warn_func, void *ctx, uint64_t seed, const sof_alloc_t *alloc);

/**
 * @brief Enters a line, as if it were typed.
 * @details A line that starts with a line number, from 1 to BASIC_LINE_MAX,
 * is stored in the program, replacing any line of that number; if nothing
 * but spaces follows the number, the line of that number is deleted. Any
 * other line is run at once, as statements separated by colons, which can
 * include the commands RUN, LIST, NEW, CONT, LOAD and SAVE. A run that
 * leaves the output partway through a line ends it with a newline. Blank
 * lines do nothing. Changing the program stops CONT from going on.
 * @param b The interpreter.
 * @param text The line, NUL-terminated, without its newline.
 * @return BASIC_OK, BASIC_SYNTAX, BASIC_RUNTIME, BASIC_STOPPED or
 * BASIC_NO_MEM, with basic_message() saying why for any but BASIC_OK; or
 * BASIC_ERROR if b or text is NULL or b isn't initialized. A line that is
 * refused leaves the program as it was.
 */
extern basic_error_t basic_line(basic_t *b, const char *text);

/**
 * @brief Writes the program's lines through the write callback.
 * @details Each line is written as its number, a space and the line as it
 * was entered, then a newline.
 * @param b The interpreter.
 * @param first The first line number to list.
 * @param last The last line number to list.
 * @return BASIC_OK, BASIC_RUNTIME if the write callback failed, or
 * BASIC_ERROR if b is NULL or not initialized.
 */
extern basic_error_t basic_list(basic_t *b, unsigned long first, unsigned long last);

/**
 * @brief Replaces the program with one read from a file, as LOAD does.
 * @details The program and its variables are cleared first, as by
 * basic_new(). Each line of the file that isn't blank must start with a
 * line number, and is stored as basic_line() stores it; carriage returns
 * before newlines are ignored. Loading stops at the first line that is
 * refused, keeping the lines before it.
 * @param b The interpreter.
 * @param path The file's name.
 * @return BASIC_OK, BASIC_SYNTAX if a line was refused, BASIC_RUNTIME if the
 * file couldn't be read, or BASIC_NO_MEM, with basic_message() saying why;
 * or BASIC_ERROR if b or path is NULL or b isn't initialized.
 */
extern basic_error_t basic_load(basic_t *b, const char *path);

/**
 * @brief Returns the message saying why the last call failed.
 * @param b The interpreter.
 * @return The message, such as "line 20: subscript out of range", or "" if
 * the last call succeeded, or b is NULL or not initialized. It is good
 * until the interpreter is next called.
 */
extern const char *basic_message(const basic_t *b);

/**
 * @brief Deletes the program and its variables, as NEW does.
 * @param b The interpreter.
 * @return BASIC_OK, or BASIC_ERROR if b is NULL or not initialized.
 */
extern basic_error_t basic_new(basic_t *b);

/**
 * @brief Runs the program from its first line, as RUN does.
 * @details The variables, arrays, GOSUB returns, FOR loops and OPTION BASE
 * are cleared, READ starts again from the first DATA item, and RND starts
 * again from the seed.
 * @param b The interpreter.
 * @return As basic_line().
 */
extern basic_error_t basic_run(basic_t *b);

/**
 * @brief Writes the program to a file, as SAVE does, in the form LIST
 * writes it, so basic_load() reads it back.
 * @param b The interpreter.
 * @param path The file's name. A file of that name is replaced.
 * @return BASIC_OK, BASIC_RUNTIME if the file couldn't be written, with
 * basic_message() saying why, or BASIC_ERROR if b or path is NULL or b
 * isn't initialized.
 */
extern basic_error_t basic_save(basic_t *b, const char *path);

#endif // BASIC_H
