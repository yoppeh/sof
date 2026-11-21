# logrun

A log writer in c: logs the lines of files or standard input at the levels they name, through a leveled log.

`logrun` writes each line of its input to a log, as a message at the level its first word names: `warn disk almost full` is logged at **WARN** as "disk almost full", and so is `WARN: disk almost full`. A line that names no level is logged at the default level, **INFO** unless `-d` says otherwise. Lines after the log's level, **INFO** unless `-l` says otherwise, are dropped. Each line in the log names the input and line the message came from, where a program's log would name its source file and line. It turns the output of a script, or a file of notes, into lines in the same form as a program's log, on standard error or appended to a file.

_main.c_ is the program. Its log is a `sof_log_t` from the [sof_log](../../sof_log/) library, set up with `sof_log_init()` with the stream, the level, from `-l`, and the name, from `-n`. `-l` and `-d` are read with `sof_log_level_parse()`, and each line's first word too. Each message is written with `sof_log_write()`, given the input's name and line number in place of `__FILE__` and `__LINE__`. Once the input is read, `sof_log_flush()` waits until every line is in the stream, and `sof_log_free()` stops the log. The program's own messages, its errors and, with `-v`, what it did, go to a second log, on standard error, through the macros `sof_log_error()` and `sof_log_info()`, which name the line of _main.c_ they are on; `sof_log_level_name()` names the levels in them.

How lines are written is chosen when _sof_log.c_ is compiled, and `make threaded=1` builds the program again, as _logrun-threaded_, with `SOF_LOG_THREADED`. _main.c_ is the same in both; only _sof_log.c_ is compiled differently, and the program can tell which it has from the log's `writer`, which is **NULL** without a writer thread. In _logrun_, `sof_log_write()` writes and flushes each line before it returns, so a slow stream slows the program down, and `sof_log_flush()` has nothing left to write. In _logrun-threaded_, each log has a thread that writes its lines, `sof_log_write()` only formats a line and queues it, and `sof_log_flush()` waits for the queue to empty. `-v` times the two, with the [sof_timer](../../sof_timer/) library, so the difference shows (see Usage).

_example.txt_ is an example input: the notes of a backup, with lines at every level. The log is written by the [sof_log](../../sof_log/) library in _../../sof_log/_ (_sof_log.c_ and _sof_log.h_), which allocates through the [sof_alloc](../../sof_alloc/) library in _../../sof_alloc/_ (_sof_alloc.c_ and _sof_alloc.h_). Lines of any length are read into a string from the [sof_string](../../sof_string/) library in _../../sof_string/_ (_sof_string.c_ and _sof_string.h_), the time logging takes is measured with the [sof_timer](../../sof_timer/) library in _../../sof_timer/_ (_sof_timer.c_ and _sof_timer.h_), and the options are read with the [sof_copt](../../sof_copt/) library in _../../sof_copt/_ (_sof_copt.c_ and _sof_copt.h_). Beyond those, it needs the c standard library, and what sof_log needs: POSIX's `flockfile()`, `getpid()` and `gmtime_r()` where there are, and for _logrun-threaded_, POSIX threads.

    sof_log_t log;
    sof_log_init(&log, stderr, SOF_LOG_LEVEL_INFO, "logrun", NULL);   // info and before, to stderr
    sof_log_level_t at = SOF_LOG_LEVEL_INFO;
    sof_log_level_parse("warn", &at);                                // SOF_LOG_LEVEL_WARN
    sof_log_write(&log, at, "example.txt", 5, "%s", message);        // names the input's line
    sof_log_flush(&log);                                             // everything is in stderr
    sof_log_free(&log);

## Building

    make                # builds ./logrun
    make threaded=1     # builds ./logrun-threaded, with sof_log.c compiled with SOF_LOG_THREADED
    make debug=1        # builds with debug info and without optimization
    ./logrun -l debug example.txt     # the example's lines, all but the trace one

`make` in the library's directory, _../_, builds it with the library, as do `make sof_log` and `make` from the root of the project; `threaded=1` there builds _logrun-threaded_.

To build it in another project, copy _main.c_, with _sof_log.c_ and _sof_log.h_ from _../../sof_log/_, _sof_alloc.c_ and _sof_alloc.h_ from _../../sof_alloc/_, _sof_string.c_ and _sof_string.h_ from _../../sof_string/_, _sof_timer.c_ and _sof_timer.h_ from _../../sof_timer/_, and _sof_copt.c_ and _sof_copt.h_ from _../../sof_copt/_, into it. For a writer thread, compile _sof_log.c_ with `-D SOF_LOG_THREADED` and link with `-pthread`.

## Usage

    logrun [-h] [-v] [-l level] [-d level] [-n name] [-o file] [file]...

- `-h`: print the usage message, with the list `sof_copt_print()` makes, and exit.
- `-v`: log, to standard error, the levels in use and which way lines are written, how many lines each input had, how many were written and dropped, and how long logging them and then flushing the log took.
- `-l` _level_: write messages at _level_ and before, and drop the rest: `error`, `warn`, `info` (the default), `debug`, `trace` or `all`, which is `trace`, in any case.
- `-d` _level_: log lines whose first word names no level at _level_ (by default, `info`).
- `-n` _name_: the name on every line of the log (by default, `logrun`). `-n ""` leaves it out.
- `-o` _file_: append the log to _file_, creating it if need be, instead of writing it to standard error.
- _file_: the input. With no _file_, or `-`, it reads standard input, named _stdin_ in the log. Several files are logged one after the other.

A line's first word, up to a space or tab, names a level if it is one of the names `-l` takes, in any case, perhaps followed by a `:`. The message is the rest of the line, after the spaces and tabs that follow the word, or the whole line if it names no level. Any line can be logged, an empty one too. Only a level's own name counts: `all`, which `-l` takes for a log that writes everything, names no message's level, so a line starting "All" is an ordinary line, logged whole at the default level.

It exits with **0**; **1** if a file couldn't be read or the log couldn't be opened or written, after logging the rest; or **2** if the options are wrong.

For example, with _example.txt_, at the default level, then at every level, under another name, appended to a file:

    $ ./logrun example.txt
    2026-11-20 00:58:47  logrun  26091  example.txt:1  INFO   starting backup of /home
    2026-11-20 00:58:47  logrun  26091  example.txt:4  INFO   copied 1832 files
    2026-11-20 00:58:47  logrun  26091  example.txt:5  WARN   /home/warren/tmp/core is 2 GB, skipping
    2026-11-20 00:58:47  logrun  26091  example.txt:6  ERROR  /home/shared is not readable
    2026-11-20 00:58:47  logrun  26091  example.txt:7  INFO   backup finished with 1 error

    $ ./logrun -l all -n backup -o backup.log example.txt
    $ cat backup.log
    2026-11-20 01:00:00  backup  27566  example.txt:1  INFO   starting backup of /home
    2026-11-20 01:00:00  backup  27566  example.txt:2  DEBUG  opened /home/warren/dev/capi
    2026-11-20 01:00:00  backup  27566  example.txt:3  TRACE  read 4096 bytes
    2026-11-20 01:00:00  backup  27566  example.txt:4  INFO   copied 1832 files
    2026-11-20 01:00:00  backup  27566  example.txt:5  WARN   /home/warren/tmp/core is 2 GB, skipping
    2026-11-20 01:00:00  backup  27566  example.txt:6  ERROR  /home/shared is not readable
    2026-11-20 01:00:00  backup  27566  example.txt:7  INFO   backup finished with 1 error

with `-v`, whose lines, from the program's own log, name the lines of _main.c_ that wrote them:

    $ ./logrun -v -l warn example.txt
    2026-11-20 13:48:02  logrun  61830  main.c:295  INFO   logging at WARN and before, lines with no level at INFO; lines are written as they are logged
    2026-11-20 13:48:02  logrun  61830  example.txt:5  WARN   /home/warren/tmp/core is 2 GB, skipping
    2026-11-20 13:48:02  logrun  61830  example.txt:6  ERROR  /home/shared is not readable
    2026-11-20 13:48:02  logrun  61830  main.c:159  INFO   example.txt: 7 lines
    2026-11-20 13:48:02  logrun  61830  main.c:334  INFO   7 lines: 2 written, 5 dropped for their level
    2026-11-20 13:48:02  logrun  61830  main.c:335  INFO   logging took 0.058 ms, flushing 0.000 ms

and the difference `SOF_LOG_THREADED` makes, logging the complete works of Shakespeare (see _../../test-data/README.md_) to a slow reader, which waits a second before it reads anything. _logrun_ is held up by every line the pipe can't take, while _logrun-threaded_ queues them and goes on, and waits for its writer when it flushes. (With _logrun-threaded_, the two logs each have a writer thread, so lines from the two on one stream can come out in a different order than they were logged in.)

    $ ./logrun -v -o /dev/stdout ../../test-data/shakespeare.txt | (sleep 1; cat > /dev/null)
    2026-11-20 13:48:00  logrun  61773  main.c:295  INFO   logging at INFO and before, lines with no level at INFO; lines are written as they are logged
    2026-11-20 13:48:01  logrun  61773  main.c:159  INFO   ../../test-data/shakespeare.txt: 196398 lines
    2026-11-20 13:48:01  logrun  61773  main.c:334  INFO   196398 lines: 196397 written, 1 dropped for their level
    2026-11-20 13:48:01  logrun  61773  main.c:335  INFO   logging took 1166.527 ms, flushing 0.000 ms

    $ ./logrun-threaded -v -o /dev/stdout ../../test-data/shakespeare.txt | (sleep 1; cat > /dev/null)
    2026-11-20 13:48:01  logrun  61821  main.c:295  INFO   logging at INFO and before, lines with no level at INFO; SOF_LOG_THREADED: lines are queued for a writer thread
    2026-11-20 13:48:01  logrun  61821  main.c:159  INFO   ../../test-data/shakespeare.txt: 196398 lines
    2026-11-20 13:48:02  logrun  61821  main.c:334  INFO   196398 lines: 196397 written, 1 dropped for their level
    2026-11-20 13:48:02  logrun  61821  main.c:335  INFO   logging took 30.719 ms, flushing 997.780 ms

The line dropped is the one that starts with "Trace", logged at **TRACE**; lines starting "All" are ordinary lines.
