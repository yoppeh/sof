/**
 * @file sof_log.h
 * @author Warren Mann (warren@nonvol.io)
 * @brief Leveled logging to a stream in c.
 * @details A sof_log_t writes messages at or below its level to a FILE *, one
 * line each, starting with the time (UTC), the application's name, the
 * process id, the source file and line, and the message's level. The macros
 * sof_log_error(), sof_log_warn(), sof_log_info(), sof_log_debug() and sof_log_trace() fill in
 * the file and line, and evaluate their arguments only when the message
 * will be written.
 *
 * How lines are written is chosen when sof_log.c is compiled. Ordinarily
 * sof_log_write() writes the line to the stream and flushes it before
 * returning, so the line is with the operating system, and survives the
 * program crashing, by the time the call returns. Compiled with
 * SOF_LOG_THREADED defined, sof_log.c gives each sof_log_t a thread of its own that
 * writes the lines, so sof_log_write() only formats the line and queues it;
 * this needs POSIX threads. Code that uses the log is the same either way,
 * and sof_log_t has the same layout, so only sof_log.c needs SOF_LOG_THREADED.
 * @version 0.0.1
 * @date 2026-11-03
 * @copyright Copyright (c) 2026
 */

#ifndef SOF_LOG_H
#define SOF_LOG_H

#include <stddef.h>
#include <stdio.h>

#include "sof_alloc.h"

#if defined(__GNUC__) || defined(__clang__)
/// @brief Has the compiler check sof_log_write()'s format against its arguments.
#define SOF_LOG_PRINTF_FORMAT(f, a) __attribute__((format(printf, f, a)))
#else
#define SOF_LOG_PRINTF_FORMAT(f, a)
#endif

/**
 * @brief Error codes for the log functions.
 */
typedef enum sof_log_error_t {
    SOF_LOG_OK = 0,
    SOF_LOG_NO_MEM,
    SOF_LOG_NO_MATCH,
    SOF_LOG_ERROR,
} sof_log_error_t;

/**
 * @brief The levels of messages, from the most to the least important.
 * @details A log writes the messages at its own level and every level
 * before it: a log at SOF_LOG_LEVEL_INFO writes errors, warnings and info
 * messages, and drops debug and trace messages.
 */
typedef enum sof_log_level_t {
    /// @brief Something failed.
    SOF_LOG_LEVEL_ERROR = 0,
    /// @brief Something may be wrong.
    SOF_LOG_LEVEL_WARN,
    /// @brief What the program is doing, in outline.
    SOF_LOG_LEVEL_INFO,
    /// @brief Detail for finding problems.
    SOF_LOG_LEVEL_DEBUG,
    /// @brief Everything.
    SOF_LOG_LEVEL_TRACE,
    /// @brief The same as SOF_LOG_LEVEL_TRACE: a log that writes every message.
    SOF_LOG_LEVEL_ALL = SOF_LOG_LEVEL_TRACE,
} sof_log_level_t;

/**
 * @brief A log.
 * @details stream and level are those given to sof_log_init() (level can be
 * changed with sof_log_set_level()), name a copy of its name, made with the
 * log's allocator (NULL if it was given none), and alloc a copy of its
 * allocator (zeroed, meaning the standard functions, if it was given none).
 * writer is the writer thread's state where sof_log.c was compiled with
 * SOF_LOG_THREADED, and NULL otherwise. These fields can be read, but should not
 * be changed by the application.
 *
 * sof_log_init() must be called before using a log, and sof_log_free() when it is
 * no longer needed. Every function accepts a NULL log, and a log that was
 * freed or whose sof_log_init() failed (stream is NULL), which writes nothing.
 *
 * sof_log_write(), the macros and sof_log_flush() can be called from any number of
 * threads at once; see sof_log_write(). sof_log_init(), sof_log_set_level() and
 * sof_log_free() must not run at the same time as any other call on the same
 * log.
 */
typedef struct sof_log_t {
    FILE *stream;
    sof_log_level_t level;
    char *name;
    sof_alloc_t alloc;
    struct sof_log_writer_t *writer;
} sof_log_t;

/**
 * @brief Waits until every line written so far is in the stream, and
 * flushes it.
 * @details Without SOF_LOG_THREADED, sof_log_write() has already written
 * and flushed each line, so this only calls fflush() again. With it, this waits for the
 * writer thread to write every line queued before the call, then flushes. The log's
 * fields aren't changed, so it can be const; with SOF_LOG_THREADED, the
 * writer's failure flag is cleared under the writer's own lock.
 * @param log The log.
 * @return SOF_LOG_OK, or SOF_LOG_ERROR if log is NULL or empty, or if writing or
 * flushing a line failed since sof_log_init() or the last sof_log_flush().
 */
extern sof_log_error_t sof_log_flush(const sof_log_t *log);

/**
 * @brief Stops a log, after writing every line queued, and frees what it
 * allocated.
 * @details With SOF_LOG_THREADED, the writer thread writes every queued line and
 * ends before this returns. The stream is flushed but not closed: it belongs
 * to the caller, who closes it, if it needs closing, after this. The sof_log_t
 * itself isn't freed; it is left empty, and acts like a log whose sof_log_init()
 * failed until it is initialized again.
 * @param log The log to free.
 * @return SOF_LOG_OK, or SOF_LOG_ERROR if log is NULL.
 */
extern sof_log_error_t sof_log_free(sof_log_t *log);

/**
 * @brief Initializes a log in a sof_log_t the caller provides.
 * @param log The log to initialize. It can be anywhere: on the stack, in
 * another structure or on the heap.
 * @param stream Where to write, such as stderr or a file the caller opened.
 * The log doesn't close it.
 * @param level The least important level written; see sof_log_level_t.
 * @param name The application's name, written on every line. The log keeps
 * a copy. NULL or "" leaves it out of the lines.
 * @param alloc The memory functions the log uses, or NULL for the standard
 * functions. The log keeps a copy, so it can be on the stack. With
 * SOF_LOG_THREADED, lines are allocated by the threads that write them and freed
 * by the writer thread, so the functions must be safe to call from several
 * threads at once, as the standard ones are.
 * @return SOF_LOG_OK, SOF_LOG_NO_MEM, or SOF_LOG_ERROR if log or stream is NULL, level
 * isn't a sof_log_level_t, alloc has a malloc but no free, or the writer thread
 * couldn't be started; on an error, the log is left empty.
 */
extern sof_log_error_t sof_log_init(sof_log_t *log, FILE *stream, sof_log_level_t level, const char *name, const sof_alloc_t *alloc);

/**
 * @brief Returns the name of a level, as it appears in log lines.
 * @param level The level.
 * @return "ERROR", "WARN", "INFO", "DEBUG" or "TRACE", or NULL if level isn't
 * a sof_log_level_t.
 */
extern const char *sof_log_level_name(sof_log_level_t level);

/**
 * @brief Finds the level with a name, for reading a level from an option or
 * a configuration file.
 * @param name A name as sof_log_level_name() returns it, in any case ("warn",
 * "Warn" and "WARN" are the same), or "all", for SOF_LOG_LEVEL_ALL.
 * @param level Set to the level found; left as it was if there is none.
 * @return SOF_LOG_OK, SOF_LOG_NO_MATCH if name isn't the name of a level, or
 * SOF_LOG_ERROR if name or level is NULL.
 */
extern sof_log_error_t sof_log_level_parse(const char *name, sof_log_level_t *level);

/**
 * @brief Changes the least important level a log writes.
 * @details It must not be called while other threads use the log.
 * @param log The log.
 * @param level The new level; see sof_log_level_t.
 * @return SOF_LOG_OK, or SOF_LOG_ERROR if log is NULL or empty or level isn't a
 * sof_log_level_t, leaving the level as it was.
 */
extern sof_log_error_t sof_log_set_level(sof_log_t *log, sof_log_level_t level);

/**
 * @brief Writes a message to the log, if its level is at or before the
 * log's.
 * @details The line is the time (UTC, as "2026-11-03 21:54:01"), the log's
 * name, the process id (where there is one: on POSIX systems), "file:line",
 * the level's name and the message, separated by two spaces and followed by
 * a newline, as in:
 *
 *     2026-11-03 21:54:01  server  4242  main.c:31  WARN   disk 91% full
 *
 * Without SOF_LOG_THREADED, the line is formatted in a 512-byte buffer on
 * the stack, written with one fwrite() and flushed, with the stream locked
 * where there is flockfile() (on POSIX systems), so lines from threads
 * writing at once don't mix; a longer line is written in parts under the
 * same lock. The line is with the operating system when this returns, so
 * a crash, abort() or _exit() afterwards doesn't lose it. The flush is a
 * system call, most of what a line costs: about 140 ns to a file on Linux
 * and 1.4 us on macOS. With SOF_LOG_THREADED, the line is formatted, in memory allocated
 * for it, and queued for the writer thread, which writes the lines in the
 * order they were queued and flushes the stream whenever it has written
 * every queued line; lines still queued when the program exits or crashes
 * are lost, so call sof_log_flush() or sof_log_free() first. The log's fields aren't changed, so it can be const;
 * with SOF_LOG_THREADED, the writer's queue is changed under the writer's
 * own lock.
 * @param log The log.
 * @param level The message's level.
 * @param file The source file to name, normally __FILE__. NULL leaves out
 * "file:line".
 * @param line The line to name, normally __LINE__.
 * @param format A printf() format, then its arguments. No newline is needed.
 * @return SOF_LOG_OK, also when the message's level is after the log's and
 * nothing is written; SOF_LOG_NO_MEM if the line couldn't be allocated (with
 * SOF_LOG_THREADED); or SOF_LOG_ERROR if log or format is NULL, the log is empty,
 * level isn't a sof_log_level_t, or writing failed (without SOF_LOG_THREADED; with
 * it, sof_log_flush() reports failed writes).
 */
extern sof_log_error_t sof_log_write(const sof_log_t *log, sof_log_level_t level, const char *file, int line, const char *format, ...) SOF_LOG_PRINTF_FORMAT(5, 6);

/**
 * @brief Writes a message at a level, from this file and line, if the log
 * writes that level.
 * @details log can be a const sof_log_t *. The arguments after log, a
 * printf() format and its arguments, are evaluated only if the message will
 * be written. log is evaluated once.
 * The macro is a single statement, safe in an if without braces.
 */
#define SOF_LOG_AT(log, at, ...)                                                     \
    do {                                                                             \
        const sof_log_t *sof_log_at_log_ = (log);                                    \
        if (sof_log_at_log_ != NULL && (at) <= sof_log_at_log_->level) {             \
            sof_log_write(sof_log_at_log_, (at), __FILE__, __LINE__, __VA_ARGS__); \
        }                                                                            \
    } while (0)

/// @brief Writes an error message: sof_log_error(&log, "format", ...).
#define sof_log_error(log, ...) SOF_LOG_AT((log), SOF_LOG_LEVEL_ERROR, __VA_ARGS__)
/// @brief Writes a warning: sof_log_warn(&log, "format", ...).
#define sof_log_warn(log, ...) SOF_LOG_AT((log), SOF_LOG_LEVEL_WARN, __VA_ARGS__)
/// @brief Writes an info message: sof_log_info(&log, "format", ...).
#define sof_log_info(log, ...) SOF_LOG_AT((log), SOF_LOG_LEVEL_INFO, __VA_ARGS__)
/// @brief Writes a debug message: sof_log_debug(&log, "format", ...).
#define sof_log_debug(log, ...) SOF_LOG_AT((log), SOF_LOG_LEVEL_DEBUG, __VA_ARGS__)
/// @brief Writes a trace message: sof_log_trace(&log, "format", ...).
#define sof_log_trace(log, ...) SOF_LOG_AT((log), SOF_LOG_LEVEL_TRACE, __VA_ARGS__)

#endif // SOF_LOG_H
