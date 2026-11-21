# sof_copt

Command line options defined in any module and merged at link time, in c.

Each option is a `sof_copt_t` that the application defines, in whichever source file the option belongs to, and lists in an array marked `SOF_COPT_OPTIONS`. A program made of several modules, such as one with a backend per file, lets each module declare its own options without a central list. The linker gathers every `SOF_COPT_OPTIONS` array in the program into one table. `sof_copt_parse()` matches the command line against that table: it records in each option whether it was given and its argument, calls a validate callback for each option found and a missing callback for each option that wasn't, either of which can stop it with a code of the program's own, and moves the operands (file names and the like) to the end of _argv_, saying where they start. Options can come anywhere, as with GNU `getopt`, and one-letter options can be combined, as in `-in5`. `sof_copt_parse_ordered()` does the same but stops at the first operand, for a program that passes the rest of its command line on. `sof_copt_print()` prints a usage message listing every option, `sof_copt_find()` looks an option up by name, and `sof_copt_count()` and `sof_copt_get()` loop over the table.

The arrays go in a section named _sof_copt_, and the linker defines symbols at its start and end: `__start_sof_copt` and `__stop_sof_copt` with the GNU and LLVM linkers on ELF platforms such as Linux, and `section$start$__DATA$sof_copt` and `section$end$__DATA$sof_copt` with Apple's linker. No linker script is needed, and the arrays survive `-flto`, `--gc-sections` and `-dead_strip`. Arrays from different files come out in the order the linker places them, which differs between platforms; the options in one array keep their order.

Because the table belongs to the whole program, this library is the exception to the collection's rule of a caller-owned structure with `init` and `free`: there is nothing to set up, and `sof_copt_parse()` writes into the options themselves. A program calls it once, from `main()`, before starting any threads.

The library's demo is the [lines](demo/) program in _demo/_, which prints the lines of files, numbered, with runs of blank lines squeezed, or cut to a range, with each feature's options defined in a source file of its own. The tests are in _test.c_, a program of their own that the Makefile builds and runs, using the harness the collection's tests share in [../test/](../test/), with more options in _test_module.c_ so the table is built from two object files. All sof_copt declarations are in _sof_copt.h_. The library needs nothing beyond the c standard library, but it does rely on the gcc and clang `section` and `used` attributes and on a linker that defines the section's start and end, as those named above do.

    static sof_copt_t opt_verbose = {                    // in any source file
        .option = "-v",
        .description = "say more",
    };
    static sof_copt_t opt_output = {
        .option = "-o",
        .description = "write to a file",
        .type = SOF_COPT_ARG_REQUIRED,                   // -o=file, -o file or -ofile
    };
    SOF_COPT_OPTIONS static sof_copt_t *options[] = {&opt_verbose, &opt_output};

    int main(int argc, char **argv) {
        int index;
        if (sof_copt_parse(argc, argv, NULL, &index, NULL) != SOF_COPT_OK) {
            sof_copt_print(stderr);                      // lists every module's options
            return 2;
        }
        if (opt_output.present) {
            ... opt_output.value ...                     // the file name, from argv
        }
        ... argv[index] to argv[argc - 1] are the operands, in order ...
    }

## Building

    make             # compiles the library and builds ./test and the demo, demo/lines
    make check       # builds and runs the tests
    make sanitize    # runs the tests with AddressSanitizer and UndefinedBehaviorSanitizer
    make memcheck    # runs the tests under valgrind (Linux)
    make debug=1     # builds with debug info and without optimization

Besides testing each function, the tests check that any non-zero value a validate or missing callback returns, negative ones and the extremes of `int` included, stops parsing with **SOF_COPT_STOPPED** and is kept unchanged in the option's `stop_value`, which the next parse clears. They also read the table on threads, as the Threading section below describes: after one `sof_copt_parse()` on the main thread, eight threads call `sof_copt_count()`, `sof_copt_get()`, `sof_copt_find()` and `sof_copt_print()` and read the options' `value` and `present` at once, and must all see what the main thread saw. The tests are linked with _-pthread_; the library itself doesn't use threads.

To use the library in another project, copy _sof_copt.c_ and _sof_copt.h_ into it. To build its tests there too, copy _test.c_ and _test_module.c_, and _test.h_ from _../test/_, and compile _test.c_, _test_module.c_ and _sof_copt.c_ with _-pthread_.

## Options

An argument matches an option when it equals the option's name, or when its part before the first '=' does. Names are matched whole, never as prefixes, and don't need to start with '-': "add" or "status" make commands. A `SOF_COPT_ARG_REQUIRED` option takes its argument after an '=' (`-o=file`) or as the next argument, whatever that is (`-o file`, `-o -x`). A `SOF_COPT_ARG_OPTIONAL` option only takes one after an '=' (`--color`, `--color=always`), and a `SOF_COPT_ARG_NONE` option given one (`-v=1`) is an error. An option given more than once keeps the last argument, and its validate callback is called each time.

An argument that starts with a single '-' and isn't an option's name is read as a group of one-letter options, as POSIX `getopt` does: `-in` is `-i -n`. Each character _c_ in it must be the whole name `-c` of an option, so only options named by a '-' and one character can be combined. An option in the group that takes an argument takes the rest of the group, after an '=' if it starts with one: `-n5`, `-in5` and `-in=5` are all `-i -n 5`, and `-on` gives `-o` the argument "n". If nothing is left, a `SOF_COPT_ARG_REQUIRED` option takes the next argument (`-io file`) and a `SOF_COPT_ARG_OPTIONAL` one has none. The options of a group are set, and validated, from left to right. A name in the table always wins over a group, which keeps it unambiguous: if a program defines `-in` as well as `-i` and `-n`, then `-in` is the option `-in`, and `-i -n` gives the other two. Names starting with "--" are never split.

An argument that is neither an option nor a group is an operand if it doesn't start with '-', or is a lone "-" (standard input, by custom). Any other argument starting with '-' is an error, so a negative number has to come after "--".

`sof_copt_parse()` reads options anywhere on the command line, as GNU `getopt` does by default: `prog file -v` is the same as `prog -v file`. It moves the pointers in _argv_ so that the options and their arguments come first and the operands, in the order they were given, come last, from _argv[index]_ on. "--" ends the options: it is kept in front of the operands, and every argument after it is an operand, whatever it looks like. Names without a '-', such as "add", are matched anywhere too, so an operand that happens to be one has to come after "--".

`sof_copt_parse_ordered()` stops at "--", which is skipped, or at the first operand, as POSIX `getopt` does: everything from there on is left to the program, and _argv_ isn't changed. A program that runs a command given on its command line, such as `prog -v ls -l`, uses it so that `-l` goes to the command.

An entry whose `option` is **NULL** isn't an option: `sof_copt_print()` prints its `description` as a heading. If two entries have the same `option`, the first in the table is the one that matches.

## Functions

`sof_copt_error_t sof_copt_parse(int argc, char *argv[], void *ctx, int *index, sof_copt_t **option)`

Parse the command line against the table, starting at _argv[1]_, reading options wherever they are, by the rules under Options above. It first clears every option's `value`, `present` and `stop_value`, so it can be called again. Each option found has `present` set to true and `value` to its argument (pointing into one of _argv_'s strings) or **NULL**, and then its `validate` callback is called. The pointers in _argv_ are reordered as it goes, options and their arguments first and then the operands in their order; the strings themselves aren't changed. Once the arguments are parsed, the `missing` callback of each option that wasn't given is called, in table order. Every callback gets _ctx_. If _index_ isn't **NULL** it is set to the index in _argv_ of the first operand, or _argc_ if there are none, so that _argv[index]_ to _argv[argc - 1]_ are the operands; if an argument caused an error, it is set to that argument's index in _argv_ as it is left (partly reordered) instead. If _option_ isn't **NULL** it is set to the option the error was found with, or **NULL** if there was no error or the argument matched no option. Returns **SOF_COPT_OK**; **SOF_COPT_NO_MATCH** for an argument starting with '-' that matches no option, or a group with a character that isn't one; **SOF_COPT_MISSING_ARGUMENT** for a `SOF_COPT_ARG_REQUIRED` option, alone or at the end of a group, on the last argument; **SOF_COPT_UNEXPECTED_ARGUMENT** for a `SOF_COPT_ARG_NONE` option given one (`-v=1`, `-iv=1`); **SOF_COPT_STOPPED** if a callback returned non-zero, which stops parsing, with _option_ set to the option it was called for, whose `stop_value` holds what it returned; or **SOF_COPT_ERROR** if _argv_ is **NULL** (with _argc_ above **0**), an argument before _argc_ is **NULL**, or an option's `type` isn't a `sof_copt_arg_t` value.

`sof_copt_error_t sof_copt_parse_ordered(int argc, char *const argv[], void *ctx, int *index, sof_copt_t **option)`

The same as `sof_copt_parse()`, groups included, except that it stops at "--", which is skipped, or at the first operand, and doesn't change _argv_. _index_ is set to the index of that first operand, or _argc_ if there is none, or on an error to the index of the argument that caused it. Every argument from _argv[index]_ on is the program's, whatever it looks like. Returns the same values as `sof_copt_parse()`.

`sof_copt_error_t sof_copt_print(FILE *stream)`

Print a usage message to _stream_. Each option gets a line, indented by two spaces, with its name, `=<arg>` or `[=arg]` if it takes an argument, then a line of dots and its `description`, so the descriptions line up. An option without a `description` gets just its name. An entry whose `option` is **NULL** prints its `description` on a line of its own, not indented. Returns **SOF_COPT_OK**, or **SOF_COPT_ERROR** if _stream_ is **NULL** or writing to it failed.

`sof_copt_t *sof_copt_find(const char *name)`

Return the first option in the table whose `option` is _name_, or **NULL** if there is none. A **NULL** _name_ finds nothing.

`size_t sof_copt_count(void)`

Return the number of entries in the table, headings included.

`sof_copt_t *sof_copt_get(size_t index)`

Return the entry at _index_ in the table, in the order `sof_copt_parse()` and `sof_copt_print()` use, or **NULL** if _index_ is `sof_copt_count()` or more.

    for (size_t i = 0; i < sof_copt_count(); i++) {
        sof_copt_t *opt = sof_copt_get(i);
        ...
    }

There is no structure to pass, so the **NULL** cases are those of the arguments: `sof_copt_parse()` and `sof_copt_parse_ordered()` return **SOF_COPT_ERROR** for a **NULL** _argv_ (with _argc_ above **0**) and accept **NULL** _ctx_, _index_ and _option_, `sof_copt_print()` returns **SOF_COPT_ERROR** for a **NULL** _stream_, and `sof_copt_find()` returns **NULL** for a **NULL** _name_. A program that defines no options links and runs: the table is empty.

## Types

    #define SOF_COPT_OPTIONS ...

Marks an array of `sof_copt_t` pointers to be merged into the table, as in `SOF_COPT_OPTIONS static sof_copt_t *options[] = {&a, &b};`. It can be used in any number of files. **NULL** entries are skipped, so an array may end with one. It sets the section, keeps the array from being discarded as unused, and keeps clang's AddressSanitizer from padding it.

    typedef enum sof_copt_error_t {
        SOF_COPT_OK = 0,
        SOF_COPT_NO_MEM,                 // not returned; sof_copt never allocates
        SOF_COPT_NO_MATCH,               // an argument starting with '-' matches no option or group
        SOF_COPT_ERROR,                  // a NULL argument, or an option with an invalid type
        SOF_COPT_MISSING_ARGUMENT,       // a SOF_COPT_ARG_REQUIRED option has no argument
        SOF_COPT_UNEXPECTED_ARGUMENT,    // a SOF_COPT_ARG_NONE option was given one
        SOF_COPT_STOPPED,                // a callback returned non-zero; see the option's stop_value
    } sof_copt_error_t;

What `sof_copt_parse()`, `sof_copt_parse_ordered()` and `sof_copt_print()` return. Callbacks don't return these: they return an `int` of the program's own, and a non-zero one makes `sof_copt_parse()` return **SOF_COPT_STOPPED**. So a program's codes never mix with the library's, and a `switch` on the result needs only the library's values:

    switch (sof_copt_parse(argc, argv, &settings, &index, &option)) {
    case SOF_COPT_OK:
        break;
    case SOF_COPT_STOPPED:                       // a callback's code, in option->stop_value
        return option->stop_value == OUT_OF_MEMORY ? 1 : 2;
    ...
    }

    typedef enum sof_copt_arg_t {
        SOF_COPT_ARG_NONE = 0,           // -v
        SOF_COPT_ARG_REQUIRED,           // -o=file, -o file, or -ofile in a group
        SOF_COPT_ARG_OPTIONAL,           // --color, --color=always, or -calways in a group
    } sof_copt_arg_t;

    typedef struct sof_copt_t {
        const char *option;                  // the name as typed; NULL for a heading
        const char *description;             // one line for sof_copt_print(); may be NULL
        sof_copt_arg_t type;
        sof_copt_validate_func_t validate;   // may be NULL
        sof_copt_missing_func_t missing;     // may be NULL
        const char *value;                   // set by sof_copt_parse(): the argument, or NULL
        bool present;                        // set by sof_copt_parse(): true if given
        int stop_value;                      // set by sof_copt_parse(): what a callback stopped it with, or 0
    } sof_copt_t;

The application sets `option`, `description`, `type`, `validate` and `missing`, usually in a static initializer, and reads `value`, `present` and `stop_value` after `sof_copt_parse()`. They don't need to be initialized; `sof_copt_parse()` clears them first. A missing callback may set `value` to give a default. A `sof_copt_t` can be listed in only one `SOF_COPT_OPTIONS` array, and must stay in place for the life of the program, as a static or global does.

    typedef int (*sof_copt_validate_func_t)(struct sof_copt_t *option, void *ctx)

Called by `sof_copt_parse()` each time _option_ is found, once its `value` and `present` are set, with the _ctx_ given to `sof_copt_parse()`. It can check and convert the argument, storing the result through _ctx_. Return **0** to go on, or a non-zero value of the program's choosing, such as a code for what was wrong, to stop parsing: `sof_copt_parse()` keeps it in _option_'s `stop_value` and returns **SOF_COPT_STOPPED**, with its _option_ argument set to _option_.

    typedef int (*sof_copt_missing_func_t)(struct sof_copt_t *option, void *ctx)

Called by `sof_copt_parse()` once all the arguments are parsed, if _option_ wasn't given, with the _ctx_ given to `sof_copt_parse()`. It can set a default, or return non-zero for an option the program can't do without. Return **0** to go on, or a non-zero value of the program's choosing to stop, as for the validate callback; _option_'s `present`, false, tells a stop here from one by its validate callback.

## Threading

The table is one for the whole program, and `sof_copt_parse()` and `sof_copt_parse_ordered()` write into its options (and `sof_copt_parse()` into _argv_), so call one of them from one thread, normally at the start of `main()`. Once it has returned, `sof_copt_count()`, `sof_copt_find()`, `sof_copt_get()` and `sof_copt_print()` only read, and any number of threads can call them, and read the options' `value`, `present` and `stop_value`, at once, as long as nothing parses again.
