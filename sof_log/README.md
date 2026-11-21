# sof_log

Leveled logging to a stream in c.

The library has a `sof_log_t` type that describes a log. The application provides the `sof_log_t` (on the stack, inside another structure or on the heap) and sets it up with `sof_log_init()`, which takes the stream to write to, the least important level to write, the application's name and an optional allocator. The macros `sof_log_error()`, `sof_log_warn()`, `sof_log_info()`, `sof_log_debug()` and `sof_log_trace()` write a message, formatted as `printf()` would, at their level, naming the source file and line they are in; a message after the log's level is dropped without its arguments being evaluated. `sof_log_write()` is the function behind them, for messages whose level or location is only known at run time. `sof_log_set_level()` changes the level, `sof_log_level_name()` and `sof_log_level_parse()` turn levels into names and back, `sof_log_flush()` waits until every line is written, and `sof_log_free()` stops the log once the application is finished with it.

Each message is one line: the time (UTC), the log's name, the process id, the source file and line, the level and the message:

    2026-11-03 21:54:01  server  4242  main.c:31  WARN   disk 91% full

How lines are written is chosen when _sof_log.c_ is compiled. Ordinarily `sof_log_write()` formats the line in a buffer on the stack, writes it to the stream with one `fwrite()` and flushes it before it returns, so the line is with the operating system, and survives the program crashing, by the time the call returns (see `sof_log_write()`). Compiled with `SOF_LOG_THREADED` defined (`-D SOF_LOG_THREADED`), _sof_log.c_ gives each log a thread of its own that writes the lines, so `sof_log_write()` only formats the line, in memory allocated for it, and queues it; the writer thread writes the lines in order and flushes the stream whenever it has written every queued line. That keeps a slow stream from holding up the threads that log, at the cost of an allocation per line and of lines not being in the stream yet when `sof_log_write()` returns. Code that uses the log, and the `sof_log_t` itself, are the same either way, so only _sof_log.c_ is compiled with `SOF_LOG_THREADED`.

The library's demo is the [logrun](demo/) program in _demo/_, which logs the lines of files or standard input at the levels their first words name, and, built with `make threaded=1` as _logrun-threaded_, shows what `SOF_LOG_THREADED` changes. The tests are in _test.c_, a program of their own that the Makefile builds and runs, using the harness the collection's tests share in [../test/](../test/). It is built twice: as _test_, with _sof_log.c_ compiled as usual, and as _test-threaded_, with _sof_log.c_ and _test.c_ compiled with `SOF_LOG_THREADED`; `make check` runs both. All declarations are in _sof_log.h_. Memory is allocated through the [sof_alloc](../sof_alloc/) library in _../sof_alloc/_ (_sof_alloc.c_ and _sof_alloc.h_). Beyond that, the library needs the c standard library, and, where there are, POSIX's `flockfile()`, to keep long lines from different threads whole, and `getpid()`; `SOF_LOG_THREADED` needs POSIX threads. On POSIX systems the time is worked out from the `time_t` by arithmetic, as `gmtime_r()` would give it; elsewhere `gmtime()` reads it.

    sof_log_t logger;
    sof_log_init(&logger, stderr, SOF_LOG_LEVEL_INFO, "server", NULL);    // info and above, to stderr;
                                                                         // no allocator
    sof_log_warn(&logger, "disk %d%% full", percent);                     // written
    sof_log_debug(&logger, "checked %s", path);                           // dropped
    ...
    sof_log_free(&logger);                                                // doesn't close stderr

## Building

    make               # compiles the library and builds ./test, ./test-threaded and the demo, demo/logrun
    make check         # builds and runs the tests, as usual and with SOF_LOG_THREADED
    make sanitize      # runs the tests with AddressSanitizer and UndefinedBehaviorSanitizer
    make memcheck      # runs the tests under valgrind (Linux)
    make debug=1       # builds with debug info and without optimization
    make threaded=1    # builds the demo as demo/logrun-threaded, with SOF_LOG_THREADED

The tests cover every function in both builds: the form of a line, checked byte for byte against what `printf()` gives for lines that end on either side of the 512-byte buffer and for names and files longer than it, the time against `gmtime_r()`, levels and the macros, lines reaching the file as they are written, short and long, without `SOF_LOG_THREADED`, and when the log is flushed or freed with it, lines from eight threads at once coming out whole and in order, a stream that can't be written, the allocator's counts, and each allocation failing in turn.

To use the library in another project, copy _sof_log.c_ and _sof_log.h_, and _sof_alloc.c_ and _sof_alloc.h_ from _../sof_alloc/_, into it. To write from a thread, compile _sof_log.c_ with `-D SOF_LOG_THREADED` and link with `-pthread`. To build its tests there too, copy _test.c_, and _test.h_ from _../test/_, and compile _test.c_, _sof_log.c_ and _sof_alloc.c_ with _-pthread_, adding `-D SOF_LOG_THREADED` for _test.c_ and _sof_log.c_ to test the threaded build.

## Levels

    typedef enum sof_log_level_t {
        SOF_LOG_LEVEL_ERROR = 0,    // something failed
        SOF_LOG_LEVEL_WARN,         // something may be wrong
        SOF_LOG_LEVEL_INFO,         // what the program is doing, in outline
        SOF_LOG_LEVEL_DEBUG,        // detail for finding problems
        SOF_LOG_LEVEL_TRACE,        // everything
        SOF_LOG_LEVEL_ALL = SOF_LOG_LEVEL_TRACE,
    } sof_log_level_t;

A log writes the messages at its own level and every level before it: a log at **SOF_LOG_LEVEL_INFO** writes errors, warnings and info messages, and drops debug and trace messages. In lines, the levels are named **ERROR**, **WARN**, **INFO**, **DEBUG** and **TRACE**. The names have the **SOF_LOG_LEVEL_** prefix because **SOF_LOG_ERROR** is the error code.

## Macros

`sof_log_error(log, format, ...)`, `sof_log_warn(log, format, ...)`, `sof_log_info(log, format, ...)`, `sof_log_debug(log, format, ...)`, `sof_log_trace(log, format, ...)`

Write a message at the macro's level, as `sof_log_write()` does, with `__FILE__` and `__LINE__` as the location, if the _log_ writes that level. Otherwise nothing is written and _format_ and its arguments aren't evaluated. _log_ is a `sof_log_t *`, evaluated once; a **NULL** _log_ writes nothing. Each macro is a single statement, safe in an `if` without braces. They discard `sof_log_write()`'s result.

`SOF_LOG_AT(log, level, format, ...)`

The macro behind them, for a level held in a variable.

## Functions

`sof_log_error_t sof_log_init(sof_log_t *log, FILE *stream, sof_log_level_t level, const char *name, const sof_alloc_t *alloc)`

Initialize the _log_ the caller provides, to write the messages at _level_ and before to _stream_, such as **stderr** or a file the caller opened. The log never closes _stream_. _name_ is written on every line; the log keeps a copy, and **NULL** or "" leaves it out. _alloc_ is the allocator for the copy of the name and, with `SOF_LOG_THREADED`, for the writer thread's state and every line; **NULL** means the standard functions (see [sof_alloc](../sof_alloc/)). It is copied into the log, so it can be on the stack. With `SOF_LOG_THREADED`, lines are allocated by the threads that log them and freed by the writer thread, so its functions must be safe to call from several threads at once, as the standard ones are. Returns **SOF_LOG_OK**, **SOF_LOG_NO_MEM** if memory couldn't be allocated, or **SOF_LOG_ERROR** if _stream_ is **NULL**, _level_ isn't a `sof_log_level_t`, _alloc_ has a _malloc_ but no _free_ or _free_sized_, or the writer thread couldn't be started; an error leaves the _log_ empty.

`sof_log_error_t sof_log_write(const sof_log_t *log, sof_log_level_t level, const char *file, int line, const char *format, ...)`

Write a message at _level_ to the _log_, if _level_ is at or before the log's level. The message is _format_ and its arguments, formatted as `printf()` would, of any length; a newline is added. _file_ and _line_ name where it comes from, normally `__FILE__` and `__LINE__`; a **NULL** _file_ leaves them out. Without `SOF_LOG_THREADED`, the line is formatted in a 512-byte buffer, written with one `fwrite()` and flushed before this returns, with the stream locked where there is `flockfile()`, so lines from threads logging at once don't mix; a longer line is written in parts under the same lock. The line is with the operating system when this returns, so a crash, `abort()` or `_exit()` afterwards doesn't lose it. The flush is a system call, and most of what a line costs: about 140 ns to a file on an Intel i9 with Linux, and 1.4 µs on an Apple M4 Max with macOS, whose system calls cost about ten times as much. With `SOF_LOG_THREADED`, the line is formatted once (twice if it is longer than 512 bytes, to fill memory allocated for its length) and queued for the writer thread, which is woken only when the queue was empty; lines still queued when the program exits or crashes are lost unless `sof_log_flush()` or `sof_log_free()` was called. The compiler checks _format_ against the arguments, with gcc and clang. Returns **SOF_LOG_OK**, also when the message is dropped for its level; **SOF_LOG_NO_MEM** if the line couldn't be allocated (with `SOF_LOG_THREADED`); or **SOF_LOG_ERROR** if _format_ is **NULL**, _level_ isn't a `sof_log_level_t`, or writing failed. With `SOF_LOG_THREADED`, failed writes are reported by `sof_log_flush()` instead.

`sof_log_error_t sof_log_set_level(sof_log_t *log, sof_log_level_t level)`

Change the least important level the _log_ writes to _level_. Returns **SOF_LOG_OK**, or **SOF_LOG_ERROR** if _level_ isn't a `sof_log_level_t`, leaving the level as it was.

`const char *sof_log_level_name(sof_log_level_t level)`

Return the name of _level_ as it appears in lines: "ERROR", "WARN", "INFO", "DEBUG" or "TRACE". Returns **NULL** if _level_ isn't a `sof_log_level_t`.

`sof_log_error_t sof_log_level_parse(const char *name, sof_log_level_t *level)`

Set _level_ to the level named _name_, for reading a level from an option or a configuration file. _name_ is a name as `sof_log_level_name()` returns it, in any case, or "all" for **SOF_LOG_LEVEL_ALL**. Returns **SOF_LOG_OK**, **SOF_LOG_NO_MATCH**, leaving _level_ as it was, if _name_ isn't the name of a level, or **SOF_LOG_ERROR** if _name_ or _level_ is **NULL**.

`sof_log_error_t sof_log_flush(const sof_log_t *log)`

Wait until every line logged so far is in the stream, and flush it. Without `SOF_LOG_THREADED`, each line has already been written and flushed, so this only calls `fflush()` again. With it, this waits for the writer thread to write every line queued before the call. Returns **SOF_LOG_OK**, or **SOF_LOG_ERROR** if writing or flushing a line has failed since `sof_log_init()` or the last `sof_log_flush()`.

`sof_log_error_t sof_log_free(sof_log_t *log)`

Stop the _log_ and free what it allocated. Every line logged before the call is written first: with `SOF_LOG_THREADED`, the writer thread writes every queued line and ends before this returns. The stream is flushed but not closed, since it belongs to the caller, who can close it afterwards. The `sof_log_t` itself isn't freed; it is left empty.

Every function accepts a **NULL** _log_: `sof_log_free()`, `sof_log_flush()`, `sof_log_set_level()` and `sof_log_write()` return **SOF_LOG_ERROR**. A log that was freed, or whose `sof_log_init()` failed, is empty and stays that way: `sof_log_write()`, `sof_log_flush()` and `sof_log_set_level()` return **SOF_LOG_ERROR**, the macros write nothing, and `sof_log_free()` returns **SOF_LOG_OK**, until it is initialized again.

## Types

    typedef enum sof_log_error_t {
        SOF_LOG_OK = 0,
        SOF_LOG_NO_MEM,
        SOF_LOG_NO_MATCH,
        SOF_LOG_ERROR,
    } sof_log_error_t;

    typedef struct sof_log_t {
        FILE *stream;                       // where lines go; NULL once freed
        sof_log_level_t level;              // the least important level written
        char *name;                         // the log's copy of the name, or NULL
        sof_alloc_t alloc;                  // copy of the allocator; zeroed for the standard functions
        struct sof_log_writer_t *writer;    // the writer thread, with SOF_LOG_THREADED; else NULL
    } sof_log_t;

The fields of `sof_log_t` can be read but should not be changed by the application; use `sof_log_set_level()` to change the level. `struct sof_log_writer_t` is private to _sof_log.c_. The layout is the same with and without `SOF_LOG_THREADED`.

## Threading

Any number of threads can log to the same `sof_log_t` at once, with the macros or `sof_log_write()`, which only read it and so take a `const sof_log_t *`, as `sof_log_flush()` does; the macros accept one too. With `SOF_LOG_THREADED`, the queue they add to belongs to the writer, not to the `sof_log_t`, and has a lock of its own. Without `SOF_LOG_THREADED`, each line is handed to the stream with one `fwrite()`, which holds the stream's lock for the call, so lines don't mix; lines over 512 bytes are written in parts, whole where there is `flockfile()` (on POSIX systems) and possibly mixed elsewhere. Lines from separate processes writing to one file go out as each process's stream buffer fills, so they can mix at those points unless the stream is line-buffered. With `SOF_LOG_THREADED`, lines are queued under the log's own lock and written by its thread, in the order they were queued, so each thread's lines come out in the order it logged them.

`sof_log_flush()` can be called while other threads log; with `SOF_LOG_THREADED` it waits for their lines too, until the queue is empty. `sof_log_init()`, `sof_log_set_level()` and `sof_log_free()` must not run at the same time as any other call on the same log. A program that changes the level while other threads log should hold a reader-writer lock: shared for logging, exclusive for `sof_log_set_level()`. Separate logs share nothing and can be used on separate threads, even when they write to the same stream, though lines from two logs on one stream can mix where there is no `flockfile()`.
