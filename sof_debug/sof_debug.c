/**
 * @file sof_debug.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief Debug tracing, indented by call depth, in c.
 * @version 0.0.1
 * @date 2026-11-03
 * @copyright Copyright (c) 2026
 */

// flockfile() is POSIX; ask for it in case the compiler is in strict c mode.
#if (defined(__unix__) || defined(__APPLE__)) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "sof_debug.h"

#if defined(__unix__) || defined(__APPLE__)
#define SOF_DEBUG_LOCK(stream) flockfile(stream)
#define SOF_DEBUG_UNLOCK(stream) funlockfile(stream)
#else
#define SOF_DEBUG_LOCK(stream) ((void)0)
#define SOF_DEBUG_UNLOCK(stream) ((void)0)
#endif

/// @brief The size of the buffer a line is formatted into on the stack; a
/// longer line is written in parts, with the stream locked.
#define LINE_BUFFER 512

static size_t put_location(char *to, size_t room, const char *file, int line);

/// @brief The calling thread's depth.
static _Thread_local size_t depth = 0;

size_t sof_debug_depth(void) {
    return depth;
}

void sof_debug_indent(int levels) {
    if (levels >= 0) {
        depth += (size_t)levels;
    } else if ((size_t)-(long)levels >= depth) {
        depth = 0;
    } else {
        depth -= (size_t)-(long)levels;
    }
}

void sof_debug_print(FILE *stream, const char *file, int line, const char *format, ...) {
    if (stream == NULL) {
        return;
    }
    int indent = SOF_DEBUG_INDENT_MAX;
    if (depth < SOF_DEBUG_INDENT_MAX / SOF_DEBUG_INDENT_WIDTH) {
        indent = (int)depth * SOF_DEBUG_INDENT_WIDTH;
    }
    // The line is formatted on the stack and written with one fwrite(),
    // which takes the stream's lock for the call, so it stays whole even on
    // an unbuffered stream such as stderr.
    char buffer[LINE_BUFFER];
    size_t length = (size_t)indent;
    memset(buffer, ' ', length);
    int fits = 1;
    if (file != NULL) {
        size_t n = put_location(buffer + length, sizeof(buffer) - length, file, line);
        fits = n != 0;
        length += n;
    }
    va_list args;
    if (fits && format != NULL) {
        va_start(args, format);
        int n = vsnprintf(buffer + length, sizeof(buffer) - length, format, args);
        va_end(args);
        if (n < 0) {
            return;
        }
        // vsnprintf() needs room for its NUL as well.
        fits = (size_t)n < sizeof(buffer) - length;
        length += (size_t)n;
    }
    if (fits) {
        fwrite(buffer, 1, length, stream);
        return;
    }
    SOF_DEBUG_LOCK(stream);
    fprintf(stream, "%*s", indent, "");
    if (file != NULL) {
        fprintf(stream, "%s:%d: ", file, line);
    }
    if (format != NULL) {
        va_start(args, format);
        vfprintf(stream, format, args);
        va_end(args);
    }
    SOF_DEBUG_UNLOCK(stream);
}

/// @brief Writes "file:line: " to to, which has room bytes, as fprintf()
/// would.
/// @return The bytes written, or 0 if they don't fit.
static size_t put_location(char *to, size_t room, const char *file, int line) {
    size_t length = strlen(file);
    // The file, ':', at most 11 characters of line, ':' and ' '.
    if (length + 14 > room) {
        return 0;
    }
    memcpy(to, file, length);
    to[length++] = ':';
    char digits[12];
    size_t n = 0;
    // Negated digit by digit, so that INT_MIN works too.
    int value = line;
    do {
        int digit = value % 10;
        digits[n++] = (char)('0' + (digit < 0 ? -digit : digit));
        value /= 10;
    } while (value != 0);
    if (line < 0) {
        to[length++] = '-';
    }
    while (n > 0) {
        to[length++] = digits[--n];
    }
    to[length++] = ':';
    to[length++] = ' ';
    return length;
}
