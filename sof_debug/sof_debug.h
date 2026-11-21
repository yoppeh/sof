/**
 * @file sof_debug.h
 * @author Warren Mann (warren@nonvol.io)
 * @brief Debug tracing, indented by call depth, in c.
 * @details The macros sof_debug(), sof_debug_enter(), sof_debug_leave(),
 * sof_debug_return(), sof_debug_indent_inc() and sof_debug_indent_dec()
 * trace a program's calls and messages to stderr, each line indented by how
 * deep the calling thread is in the functions that called
 * sof_debug_enter(), and starting with the source file and line. They do
 * this only where DEBUG is defined when this header is included. Otherwise
 * they do nothing, their arguments aren't evaluated, and sof_debug_return()
 * simply returns.
 *
 * Like <assert.h>, this header can be included again after defining or
 * undefining DEBUG, and the macros follow the new setting. Define
 * SOF_DEBUG_STREAM before including it to trace to another FILE * than
 * stderr.
 *
 * The functions behind the macros, sof_debug_print(), sof_debug_indent()
 * and sof_debug_depth(), are always compiled, so tracing can be turned on
 * in some source files and not others.
 * @version 0.0.1
 * @date 2026-11-03
 * @copyright Copyright (c) 2026
 */

#ifndef SOF_DEBUG_H
#define SOF_DEBUG_H

#include <stddef.h>
#include <stdio.h>

#if defined(__GNUC__) || defined(__clang__)
/// @brief Has the compiler check sof_debug_print()'s format against its
/// arguments.
#define SOF_DEBUG_PRINTF_FORMAT(f, a) __attribute__((format(printf, f, a)))
#else
#define SOF_DEBUG_PRINTF_FORMAT(f, a)
#endif

/// @brief The spaces each level of depth indents by.
#define SOF_DEBUG_INDENT_WIDTH 4

/// @brief The most spaces a line is indented by, however deep the thread is.
#define SOF_DEBUG_INDENT_MAX 80

/**
 * @brief Writes one line of trace: indentation, location and message.
 * @details Writes SOF_DEBUG_INDENT_WIDTH spaces for each level of the
 * calling thread's depth (at most SOF_DEBUG_INDENT_MAX), then "file:line: ",
 * then format and its arguments, as fprintf() would. No newline is added.
 * The line is formatted into a 512-byte buffer on the stack and written
 * with one fwrite(), so lines from different threads, and from processes
 * sharing an unbuffered stream such as stderr, don't mix. A longer line is
 * written in parts, with the stream locked where the platform has
 * flockfile() (POSIX).
 * @param stream Where to write. NULL writes nothing.
 * @param file The source file to name, normally __FILE__. NULL leaves out
 * the location.
 * @param line The line to name, normally __LINE__.
 * @param format A printf() format. NULL writes the indentation and location
 * alone.
 */
extern void sof_debug_print(FILE *stream, const char *file, int line, const char *format, ...) SOF_DEBUG_PRINTF_FORMAT(4, 5);

/**
 * @brief Returns the calling thread's depth.
 * @details Each thread has its own depth, starting at 0.
 * @return The depth.
 */
extern size_t sof_debug_depth(void);

/**
 * @brief Changes the calling thread's depth.
 * @details The depth never goes below 0: a change that would take it there
 * leaves it at 0.
 * @param levels The levels to add, or, if negative, to take away.
 */
extern void sof_debug_indent(int levels);

#endif // SOF_DEBUG_H

// The macros are outside the include guard, so that including this header
// again redefines them to follow DEBUG, as <assert.h> does with NDEBUG.

#undef sof_debug
#undef sof_debug_enter
#undef sof_debug_indent_dec
#undef sof_debug_indent_inc
#undef sof_debug_leave
#undef sof_debug_return

#ifndef SOF_DEBUG_STREAM
/// @brief The stream the macros write to. Define it before including this
/// header to write somewhere else.
#define SOF_DEBUG_STREAM stderr
#endif

#ifdef DEBUG

/**
 * @brief Writes a message, as printf() would, indented and with the file and
 * line. No newline is added.
 */
#define sof_debug(...)                                                    \
    sof_debug_print(SOF_DEBUG_STREAM, __FILE__, __LINE__, __VA_ARGS__)

/**
 * @brief Writes the name of the function it is in, as "name()", and goes one
 * level deeper. Put it at the start of a function, and leave the function
 * with sof_debug_return() or sof_debug_leave().
 */
#define sof_debug_enter()                                                 \
    do {                                                                  \
        sof_debug_print(SOF_DEBUG_STREAM, __FILE__, __LINE__, "%s()\n", \
                        __func__);                                       \
        sof_debug_indent(1);                                              \
    } while (0)

/// @brief Comes back one level, for the end of a void function that called
/// sof_debug_enter() and doesn't return through sof_debug_return().
#define sof_debug_leave() sof_debug_indent(-1)

/**
 * @brief Comes back one level and returns, from a function that called
 * sof_debug_enter(): sof_debug_return(value) returns value, and
 * sof_debug_return() returns from a void function. The value is evaluated after coming back,
 * so what it traces is at the caller's depth.
 */
#define sof_debug_return(...)                                             \
    do {                                                                  \
        sof_debug_indent(-1);                                             \
        return __VA_ARGS__;                                               \
    } while (0)

/// @brief Goes one level deeper, without writing anything.
#define sof_debug_indent_inc() sof_debug_indent(1)

/// @brief Comes back one level, without writing anything.
#define sof_debug_indent_dec() sof_debug_indent(-1)

#else

#define sof_debug(...) ((void)0)
#define sof_debug_enter() ((void)0)
#define sof_debug_leave() ((void)0)
#define sof_debug_return(...) return __VA_ARGS__
#define sof_debug_indent_inc() ((void)0)
#define sof_debug_indent_dec() ((void)0)

#endif // DEBUG
