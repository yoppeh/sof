# sof_debug

Debug tracing, indented by call depth, in c.

The library is a set of macros that write trace lines to stderr: `sof_debug()` writes a message as `printf()` would, `sof_debug_enter()` at the start of a function writes its name and goes one level deeper, and `sof_debug_return()` or `sof_debug_leave()` comes back a level as the function returns. Every line starts with the source file and line, and is indented by how deep the calling thread is, so nested calls show as nested lines. `sof_debug_indent_inc()` and `sof_debug_indent_dec()` change the depth without writing anything.

The macros do this only where `DEBUG` is defined when _sof_debug.h_ is included, as `make debug=1` does for every library in this collection. Otherwise they compile to nothing, their arguments aren't evaluated, and `sof_debug_return()` simply returns, so tracing costs nothing in an ordinary build. Like _assert.h_, _sof_debug.h_ can be included again after defining or undefining `DEBUG`, and the macros follow the new setting. The functions behind the macros, `sof_debug_print()`, `sof_debug_indent()` and `sof_debug_depth()`, are always compiled, so tracing can be turned on in some source files and not others.

The library's demo is the [hanoi](demo/) program in _demo/_, which solves the Towers of Hanoi and, built with `make debug=1`, traces its recursion, each call indented by its depth. The tests are in _test.c_, a program of their own that the Makefile builds and runs, using the harness the collection's tests share in [../test/](../test/). The [calc](../sof_stack/demo/) program, sof_stack's demo in _../sof_stack/demo/_, uses it to trace its evaluator when built with `make debug=1`. All declarations are in _sof_debug.h_. The library needs nothing beyond the c standard library, and uses POSIX's `flockfile()` where there is one, to keep long lines from different threads whole.

    int parse(const char *s) {
        sof_debug_enter();                  // writes "parse.c:12: parse()"
        sof_debug("parsing \"%s\"\n", s);   // indented one level
        if (*s == '\0')
            sof_debug_return(-1);           // safe without braces
        int n = parse_number(s);            // its trace is indented further
        sof_debug_return(n);
    }

With `DEBUG` defined, that writes lines such as:

    parse.c:12: parse()
        parse.c:13: parsing "42"
        parse.c:31: parse_number()
            parse.c:32: digit 4
            parse.c:32: digit 2

## Building

    make             # compiles the library and builds ./test and the demo, demo/hanoi
    make check       # builds and runs the tests
    make sanitize    # runs the tests with AddressSanitizer and UndefinedBehaviorSanitizer
    make memcheck    # runs the tests under valgrind (Linux)
    make debug=1     # builds with debug info and without optimization

Besides testing each macro and function, the tests run threads tracing into one file at once, as the Threading section below describes, and check that every line comes back whole and indented for the depth of the thread that wrote it, while the main thread keeps a depth of its own. Lines whose ends fall on either side of the 512-byte buffer, a file name longer than it and the extreme line numbers are checked against what `fprintf()` writes. The tests are linked with _-pthread_; the library itself doesn't use threads.

To use the library in another project, copy _sof_debug.c_ and _sof_debug.h_ into it, compile _sof_debug.c_ with the project's own sources, and define `DEBUG` (as with `-D DEBUG`) to turn tracing on. To build its tests there too, copy _test.c_, and _test.h_ from _../test/_, and compile _test.c_ and _sof_debug.c_ with _-pthread_.

## Macros

`sof_debug(format, ...)`

Write _format_ and its arguments as `printf()` would, after the indentation and "_file_:_line_: ". No newline is added.

`sof_debug_enter()`

Write the name of the function it is in, as "_name_()", and go one level deeper. Every function that uses it must leave through `sof_debug_return()` or `sof_debug_leave()`.

`sof_debug_return(value)`, `sof_debug_return()`

Come back one level and return _value_, or return from a void function. It is a single statement, so it is safe in an `if` without braces. _value_ is evaluated after coming back, so anything it traces is at the caller's depth.

`sof_debug_leave()`

Come back one level without returning, for the end of a void function that falls off its closing brace.

`sof_debug_indent_inc()`, `sof_debug_indent_dec()`

Go one level deeper, or come back one, without writing anything: for indenting the lines written in a loop, for instance.

Without `DEBUG`, every macro but `sof_debug_return()` is `((void)0)` and `sof_debug_return()` is `return`.

`SOF_DEBUG_STREAM`

The `FILE *` the macros write to, **stderr** unless it is defined before _sof_debug.h_ is included. The tests define it to a temporary file.

## Functions

`void sof_debug_print(FILE *stream, const char *file, int line, const char *format, ...)`

Write one trace line to _stream_: `SOF_DEBUG_INDENT_WIDTH` (4) spaces for each level of the calling thread's depth, but at most `SOF_DEBUG_INDENT_MAX` (80), then "_file_:_line_: ", then _format_ and its arguments as `fprintf()` would. No newline is added. The line is formatted into a 512-byte buffer on the stack and written with one `fwrite()`, so it reaches _stream_ whole, even an unbuffered one such as **stderr**; a longer line is written in parts, with _stream_ locked for the whole line where there is `flockfile()`. The compiler checks _format_ against the arguments, with gcc and clang.

`size_t sof_debug_depth(void)`

Return the calling thread's depth, which starts at **0**.

`void sof_debug_indent(int levels)`

Add _levels_ to the calling thread's depth, or take them away if _levels_ is negative. The depth never goes below **0**: a change that would take it there leaves it at **0**, so a `sof_debug_return()` without a `sof_debug_enter()` does no harm.

A **NULL** _stream_ writes nothing. A **NULL** _file_ leaves out the location, and a **NULL** _format_ writes the indentation and location alone.

## Threading

Each thread has its own depth, so threads tracing at once are indented separately, and the library has no other state. Each line up to 512 bytes is written with one `fwrite()`, which holds the stream's lock for the call, so it comes out whole, without lines from other threads mixed into it. Longer lines are written in parts, whole where there is `flockfile()` (on POSIX systems); elsewhere, long lines written at the same time can mix. Any number of threads can use the macros and functions at once.
