# basic

A BASIC interpreter in c: ECMA-55 Minimal BASIC with string functions, run from a file or interactively.

`basic` runs a BASIC program from a file, or reads lines from standard input as if they were typed at an old home computer: a line that starts with a number is stored in the program, and any other line is run at once, so `RUN`, `LIST`, `NEW`, `CONT`, `LOAD` and `SAVE` work as commands. The language is ECMA-55 Minimal BASIC (ANSI X3.60-1978), with the extensions most BASICs have: several statements to a line, `IF ... THEN` statements, string arrays, string functions, `AND`, `OR`, `NOT` and `MOD`. Errors are reported with the line they happened in, as _line 20: subscript out of range_. It started as a port of the author's earlier interpreter, abasic, which was modelled on Atari 8-bit BASIC; the Atari parts are gone, and the language follows the standard instead.

_main.c_ is the program. The interpreting is done by _basic.c_ and _basic_scan.c_, declared in _basic.h_, which keep a `basic_t` that the program provides and sets up with `basic_init()`, giving it callbacks for the output, for the lines `INPUT` reads and for the exceptions a program goes on after, so the interpreter never touches standard input or output itself. `basic_line()` enters a line as if it were typed: it stores or deletes a program line, or runs a line without a number. `basic_load()` replaces the program with one read from a file, `basic_run()` runs it, `basic_list()` writes its lines through the output callback and `basic_save()` writes them to a file. `basic_new()` deletes the program and its variables, and `basic_message()` says why the last call failed. `basic_free()` releases everything.

A line is split into tokens when it is entered, so a line with a quote missing or a character BASIC doesn't use is refused at once and the program is left as it was; the rest of the syntax is checked when the line runs, and an error there stops the program at that line. Running a line walks its tokens, parsing and carrying out each statement as it comes, with expressions evaluated by recursive descent. Program lines are kept in a red-black tree ordered by line number, so a place in the program is a line and a token in it, and `GOTO`, `GOSUB`, `RETURN`, `NEXT` and `CONT` only move the place. Simple variables, arrays and `DEF` functions are kept in three hashmaps, the places `RETURN` goes back to and the `FOR` loops running on two stacks, and the program's `DATA` items, gathered before the program first needs them, in a vector.

_basic_scan.c_ holds the scanner, which splits lines into tokens and reads and writes numbers as BASIC does, declared in _basic_scan.h_, which only the interpreter includes. _guess.bas_, _hangman.bas_, _primes.bas_ and _kepler.bas_ are example programs. Program lines are kept in the [sof_rbtree](../../sof_rbtree/) library in _../../sof_rbtree/_ (_sof_rbtree.c_ and _sof_rbtree.h_), and variables, arrays and functions in the [sof_hashmap](../../sof_hashmap/) library in _../../sof_hashmap/_ (_sof_hashmap.c_ and _sof_hashmap.h_), whose keys are hashed by the [sof_hash](../../sof_hash/) library in _../../sof_hash/_ (_sof_hash.c_ and _sof_hash.h_, which use [rapidhash](https://github.com/Nicoshev/rapidhash) in _rapidhash.h_, under its own MIT license). `GOSUB` returns and `FOR` loops are on stacks from the [sof_stack](../../sof_stack/) library in _../../sof_stack/_ (_sof_stack.c_ and _sof_stack.h_), and tokens and `DATA` items in vectors from the [sof_vector](../../sof_vector/) library in _../../sof_vector/_ (_sof_vector.c_ and _sof_vector.h_), which uses the [sof_sort](../../sof_sort/) library in _../../sof_sort/_ (_sof_sort.c_ and _sof_sort.h_). Strings are held by the [sof_string](../../sof_string/) library in _../../sof_string/_ (_sof_string.c_ and _sof_string.h_). `RND` draws from the [sof_random](../../sof_random/) library in _../../sof_random/_ (_sof_random.c_ and _sof_random.h_), and memory is allocated through the [sof_alloc](../../sof_alloc/) library in _../../sof_alloc/_ (_sof_alloc.c_ and _sof_alloc.h_). The program reads its options with the [sof_copt](../../sof_copt/) library in _../../sof_copt/_ (_sof_copt.c_ and _sof_copt.h_). Beyond those, it needs nothing but the c standard library and its math library, _-lm_; the program also uses POSIX's `isatty()`.

    basic_t b;
    basic_init(&b, write_out, read_in, NULL, &io, 42, NULL); // reports in the output; RND from seed 42
    basic_line(&b, "10 FOR I = 1 TO 3: PRINT I;: NEXT I");  // stores line 10
    if (basic_run(&b) != BASIC_OK) {                         // writes " 1  2  3 \n"
        fprintf(stderr, "%s\n", basic_message(&b));
    }
    basic_line(&b, "PRINT I * 2");                           // writes " 8 \n"
    basic_free(&b);

## Building

    make            # builds ./basic
    make debug=1    # builds with debug info and without optimization
    ./basic         # type BASIC at it; Ctrl-D (end of input) leaves
    ./basic primes.bas  # runs a program

`make` in the library's directory, _../_, builds it with the library, as do `make sof_rbtree` and `make` from the root of the project.

To build it in another project, copy _main.c_, _basic.c_, _basic.h_, _basic_scan.c_ and _basic_scan.h_, with _sof_rbtree.c_ and _sof_rbtree.h_ from _../../sof_rbtree/_, _sof_hashmap.c_ and _sof_hashmap.h_ from _../../sof_hashmap/_, _sof_hash.c_, _sof_hash.h_ and _rapidhash.h_ from _../../sof_hash/_, _sof_stack.c_ and _sof_stack.h_ from _../../sof_stack/_, _sof_vector.c_ and _sof_vector.h_ from _../../sof_vector/_, _sof_sort.c_ and _sof_sort.h_ from _../../sof_sort/_, _sof_string.c_ and _sof_string.h_ from _../../sof_string/_, _sof_random.c_ and _sof_random.h_ from _../../sof_random/_, _sof_alloc.c_ and _sof_alloc.h_ from _../../sof_alloc/_, and _sof_copt.c_ and _sof_copt.h_ from _../../sof_copt/_, into it, and link with _-lm_. To use only the interpreter, leave out _main.c_ and the _sof_copt_ files.

## Usage

    basic [-h] [-s seed] [file]

- `-h`: print the usage message, with the list `sof_copt_print()` makes, and exit.
- `-s` _seed_: start `RND` from _seed_, a number from 0 to 18446744073709551615, so a program draws the same numbers every time it is run. Without it, the seed comes from the time. Every `RUN` starts again from the seed.
- _file_: the program to run. It is loaded as `LOAD` loads it, then run, and `INPUT` reads standard input. Without a _file_, each line of standard input is entered as if it were typed, and a prompt, `> `, is shown when standard input is a terminal.

Errors and the exceptions a program goes on after are written to standard error; the program's output goes to standard output. When standard input isn't a terminal, each reply `INPUT` reads is echoed after its prompt, so the output reads as it would on a terminal. There is no way to break into a running program: an endless loop runs until the interpreter is killed.

It exits with **0**, **1** if the file couldn't be loaded, the program stopped with an error, or, reading standard input that isn't a terminal, any line had an error, or **2** if the options are wrong. A program ended by `STOP` exits with **0**.

The primes up to 300, from _primes.bas_; the numbers that don't fit on a line go on the next:

    $ ./basic primes.bas
     2  3  5  7  11  13  17  19  23  29  31  37  41  43  47  53  59  61  67  71  73 
     79  83  89  97  101  103  107  109  113  127  131  137  139  149  151  157 
     163  167  173  179  181  191  193  197  199  211  223  227  229  233  239  241 
     251  257  263  269  271  277  281  283  293 
     62 primes up to 300 

the planets' years by Kepler's third law, from _kepler.bas_, which uses `DATA`, `READ`, `RESTORE`, `DEF` and `TAB`:

    $ ./basic kepler.bas
    Planet     Distance (AU)   Year (Earth years)  Year (days)
    Mercury     .387            .241                88 
    Venus       .723            .615                225 
    Earth       1               1                   365 
    Mars        1.524           1.881               687 
    Jupiter     5.203           11.868              4335 
    Saturn      9.537           29.452              10757 
    Uranus      19.19           84.064              30705 
    Neptune     30.07           164.892             60227 

    Closest: Mercury, whose year is 87.9 days.

a game of _guess.bas_, with the replies piped in, one of them not a number, which `INPUT` reports and asks for again:

    $ printf '50\n75\nabc\n62\n12.5\n68\n72\n70\n71\nn\n' | ./basic -s 7 guess.bas 2>&1
    I'm thinking of a number from 1 to 100.
    Your guess? 50
    Too low.
    Your guess? 75
    Too high.
    Your guess? abc
    line 50: "abc" isn't a number, type the line again
    Your guess? 62
    Too low.
    Your guess? 12.5
    A whole number from 1 to 100, please.
    Your guess? 68
    Too low.
    Your guess? 72
    Too high.
    Your guess? 70
    Too low.
    Your guess? 71
    Right! It was 71 and you took 8 guesses.
    Play again (y/n)? n
    Goodbye.

a game of _hangman.bas_, which picks its word from `DATA` with `RND`:

    $ printf 'e\ns\nT\nst\na\nr\ns\ni\nn\ng\nn\n' | ./basic -s 3 hangman.bas
    ------   misses: 0 of 6
    Letter? e
    No e.
    ------   misses: 1 of 6
    Letter? s
    s-----   misses: 1 of 6
    Letter? T
    st----   misses: 1 of 6
    Letter? st
    One letter, please.
    Letter? a
    No a.
    st----   misses: 2 of 6
    Letter? r
    str---   misses: 2 of 6
    Letter? s
    You tried s already.
    Letter? i
    stri--   misses: 2 of 6
    Letter? n
    strin-   misses: 2 of 6
    Letter? g
    string   misses: 2 of 6
    You got it: string
    Another (y/n)? n

and lines entered as if typed, with `STOP` and `CONT`, an error, an exception the program goes on after, and `SAVE` and `LOAD`:

    $ ./basic 2>&1 <<'EOF'
    10 FOR I = 1 TO 3
    20 PRINT I; I * I
    30 IF I = 2 THEN STOP
    40 NEXT I
    LIST
    RUN
    PRINT "I is"; I
    CONT
    20 PRINT I; SQR(I - 3)
    RUN
    PRINT 1 / 0
    GOTO 5
    SAVE "/tmp/squares.bas"
    NEW
    LOAD "/tmp/squares.bas"
    LIST 20
    EOF
    10 FOR I = 1 TO 3
    20 PRINT I; I * I
    30 IF I = 2 THEN STOP
    40 NEXT I
     1  1 
     2  4 
    stopped at line 30
    I is 2 
     3  9 
     1 
    line 20: square root of a negative number
    division by zero
     1.79769313E+308 
    there is no line 5
    20 PRINT I; SQR(I - 3)

## The language

This is the language as ECMA-55 Minimal BASIC defines it, which every BASIC since has built on, with the extensions marked as such. Keywords and names can be typed in either case; `LIST` shows lines as they were typed. Keywords must be set apart from names and numbers by spaces or punctuation, so `FORI=1TO9` is not `FOR I = 1 TO 9`.

Statements of ECMA-55:

- `LET` _variable_ `=` _expression_: assigns a number to a numeric variable or a string to a string variable.
- `PRINT` _items_: prints numbers and strings, separated by `;`, which adds nothing, or `,`, which moves to the start of the next print zone of 15 columns, and `TAB(`_n_`)`, which moves to column _n_ (counting from 1), on a new line if the output is past it already. A `PRINT` that ends with `;` or `,` doesn't end its line. An item that won't fit before the margin, at 80 columns, starts a new line, and a string longer than a line is continued on the next.
- `INPUT` _variables_: prints `? ` and reads a line of values separated by commas, numbers for numeric variables and strings, quoted or not, for string variables. A reply with the wrong number of values, or something other than a number for a numeric variable, is reported and asked for again; nothing is assigned until the reply fits.
- `IF` _condition_ `THEN` _line_: goes to the line if the condition is true.
- `GOTO` _line_ (or `GO TO`), `GOSUB` _line_ (or `GO SUB`) and `RETURN`.
- `ON` _expression_ `GOTO` _line_, _line_, ...: goes to the first line if the expression rounds to 1, the second if 2, and so on.
- `FOR` _variable_ `=` _first_ `TO` _limit_ [`STEP` _step_] and `NEXT` _variable_: the limit and step (1 if not given) are taken once, when the loop starts. A loop that should run no times goes straight past its `NEXT`, and the variable is left one step past the limit when the loop ends.
- `DIM` _array_`(`_bounds_`)`, ...: makes arrays, of one or two dimensions in ECMA-55. An array used without a `DIM` has a bound of 10 in each dimension. `OPTION BASE 0` or `OPTION BASE 1` sets the lowest subscript, 0 unless it is given; it must come before any array is made. Subscripts are rounded to the nearest whole number.
- `DATA` _items_, `READ` _variables_ and `RESTORE`: `READ` assigns the program's `DATA` items in order, from all its `DATA` statements in line order, and `RESTORE` starts again from the first. Items are numbers or strings, quoted or not; spaces around an unquoted item are dropped.
- `DEF FN`_x_`(`_parameter_`) =` _expression_: defines a function, used as `FN`_x_`(`_argument_`)`, whose parameter is local to it. A `DEF` can be anywhere in the program, even after the lines that use it.
- `REM` _remark_, `END`, `STOP` and `RANDOMIZE`.
- The functions `ABS`, `ATN`, `COS`, `EXP`, `INT` (the largest whole number not above its argument), `LOG` (natural), `RND` (a number from 0 up to 1), `SGN`, `SIN`, `SQR` and `TAN`, in radians where they take an angle.
- Numbers such as `42`, `-1.5`, `.5` and `6.02E23`, the operators `+`, `-`, `*`, `/` and `^`, and the comparisons `=`, `<>`, `<`, `<=`, `>` and `>=`. `^` groups from left to right, so `2 ^ 3 ^ 2` is 64, and comes before a sign, so `-2 ^ 2` is -4.
- String constants in double quotes, and simple string variables, whose names end in `$`. Strings can be compared with `=` and `<>`.

Extensions:

- Several statements on a line, separated by `:`.
- `IF` _condition_ `THEN` _statements_, which runs the rest of the line if the condition is true, and `IF` _condition_ `GOTO` _line_. A false condition skips the rest of the line, whatever follows `THEN`. A condition can be any numeric expression, true if it isn't 0.
- `LET` can be left out: `X = 1`.
- Names of up to 31 letters and digits, starting with a letter, rather than a letter and an optional digit; a name that starts with `FN` is a function's.
- String arrays: `DIM A$(10)` is an array of 11 strings. Arrays of up to four dimensions, and `DIM` bounds that are expressions rather than numbers.
- Strings joined with `+`, and compared with `<`, `<=`, `>` and `>=`, byte by byte.
- `AND`, `OR` and `NOT`, which treat 0 as false and anything else as true, and give -1 for true and 0 for false, as the comparisons do; and `MOD`, the remainder after dividing and rounding down, so `-7 MOD 3` is 2.
- The string functions `LEN(`_s_`)`, `ASC(`_s_`)` (the code of its first byte), `CHR$(`_n_`)` (the byte of code _n_, from 0 to 255), `STR$(`_n_`)` (a number as `PRINT` writes it, without the spaces), `VAL(`_s_`)` (the number _s_ starts with, after any spaces, or 0 if it doesn't start with one), `LEFT$(`_s_`,` _n_`)`, `RIGHT$(`_s_`,` _n_`)`, `MID$(`_s_`,` _start_ [`,` _n_]`)` and `INSTR(`[_start_`,`] _s_`,` _sought_`)` (where _sought_ is found in _s_, from 1, or 0).
- `DEF` functions of strings (`DEF FNA$(S$) = ...`), and of up to eight parameters, or none.
- `ON` _expression_ `GOSUB` _lines_, `RESTORE` _line_ (from the first `DATA` item at or after the line), `RANDOMIZE` _seed_, and `RND(`_x_`)`, whose argument is ignored. `NEXT` without a variable ends the innermost loop. `INPUT "`_prompt_`";` _variables_ prints the prompt and `? `, and with a `,` after the prompt, the prompt alone.
- The commands, typed without a line number: `RUN` [_line_] clears the variables and arrays and runs the program from its first line, or the line given; `LIST` [_first_][`-`[_last_]] writes the program, or some of its lines; `NEW` deletes the program and its variables; `CONT` goes on after a `STOP`, if the program hasn't been changed since; `LOAD "`_file_`"` replaces the program with one from a file, clearing the variables, and `SAVE "`_file_`"` writes the program to one, as `LIST` writes it. Other statements can be typed without a line number too, to be run at once, and can use the program's variables and functions and `GOTO` or `GOSUB` into it.

Numbers are doubles. `PRINT` writes them with nine significant digits, in the forms ECMA-55 gives: a whole number with no point (`42`), a point and the digits (`3.25`, `.001`, with no 0 before the point), or with an exponent when they don't fit in nine digits without one (`1.5E+10`, `2E-12`), with a space before them, or `-`, and a space after them. A variable that was never assigned is 0, or the empty string.

ECMA-55 reports some exceptions and goes on after them: division by zero, zero raised to a negative power and arithmetic that overflows give the largest number there is, with the right sign, `TAB` of less than 1 is taken as 1, and a bad reply to `INPUT` is asked for again. These are reported through the warning callback, as _line 30: division by zero_. Every other exception stops the program with an error: a subscript out of range, the square root of a negative number, the logarithm of a number that isn't positive, a negative number raised to a power that isn't whole, `RETURN` without `GOSUB`, `NEXT` without `FOR`, `READ` past the last `DATA` item or of a string into a numeric variable, `ON` with a value out of range, a line that doesn't exist, and the end of the input in `INPUT`.

Where this departs from ECMA-55, besides the extensions: the syntax of a line is checked when it runs rather than before the program starts, so a program can run until it reaches a line with an error; a string mixed with a number is a syntax error found the same way; `END` needn't be the last line; and line numbers go up to 99999 rather than 9999. Expressions and functions can nest 200 deep, a program can have 10000 `GOSUB`s without a `RETURN` and 10000 loops running, an array can have up to 10,000,000 elements, and a string up to 1 MiB.

## Functions

`basic_error_t basic_init(basic_t *b, basic_write_func_t write_func, basic_read_func_t read_func, basic_warn_func_t warn_func, void *ctx, uint64_t seed, const sof_alloc_t *alloc)`

Initialize the interpreter _b_ the caller provides, with no program. _write_func_ writes the output, which is thrown away if it is **NULL**; _read_func_ reads the lines `INPUT` reads, and **NULL** makes `INPUT` fail as at the end of the input; _warn_func_ reports the exceptions a program goes on after, and **NULL** writes them through _write_func_ instead, each on a line of its own. _ctx_ is passed to every callback. _seed_ is the seed `RND` starts from at every `RUN`. _alloc_ is the allocator for all of the interpreter's memory, **NULL** meaning the standard functions (see [sof_alloc](../../sof_alloc/)); it is copied into the interpreter, so it can be on the stack. The `basic_t` must not move once initialized. Returns **BASIC_OK**, **BASIC_NO_MEM** if memory couldn't be allocated, or **BASIC_ERROR** if _b_ is **NULL** or _alloc_ has a _malloc_ but no _free_ or _free_sized_; an error leaves _b_ empty.

`basic_error_t basic_line(basic_t *b, const char *text)`

Enter the NUL-terminated line _text_, without its newline, as if it were typed. A line that starts with a line number, from 1 to **BASIC_LINE_MAX** (99999), is stored in the program, replacing any line of that number, or deletes the line of that number if nothing but spaces follows the number. Any other line is run at once, as statements separated by colons. A blank line does nothing. A run that leaves the output partway through a line ends the line. Returns **BASIC_OK**; **BASIC_SYNTAX** if the line was refused or the program stopped at a syntax error; **BASIC_RUNTIME** if it stopped at any other error, or a command failed; **BASIC_STOPPED** if it ran `STOP`; or **BASIC_NO_MEM**. For each but **BASIC_OK**, `basic_message()` says why. A line that is refused leaves the program as it was.

`basic_error_t basic_run(basic_t *b)`

Run the program from its first line, as `RUN` does: the variables, arrays, `GOSUB` returns, `FOR` loops and `OPTION BASE` are cleared, `READ` starts again from the first `DATA` item, and `RND` starts again from the seed. Returns as `basic_line()` does.

`basic_error_t basic_load(basic_t *b, const char *path)`

Replace the program with one read from the file _path_, as `LOAD` does. The program and its variables are cleared first. Each line of the file that isn't blank must start with a line number, and is stored as `basic_line()` stores it; carriage returns before newlines are ignored. Loading stops at the first line that is refused, keeping the lines before it. Returns **BASIC_OK**; **BASIC_SYNTAX** if a line was refused, with a message such as _prog.bas:3: line 30: syntax error: unterminated string_; **BASIC_RUNTIME** if the file couldn't be read; or **BASIC_NO_MEM**.

`basic_error_t basic_save(basic_t *b, const char *path)`

Write the program to the file _path_, replacing any file of that name, in the form `LIST` writes it, which `basic_load()` reads back. Returns **BASIC_OK**, or **BASIC_RUNTIME** if the file couldn't be written.

`basic_error_t basic_list(basic_t *b, unsigned long first, unsigned long last)`

Write the program's lines numbered from _first_ to _last_ through the write callback, each as its number, a space, the line as it was entered and a newline. Returns **BASIC_OK**, or **BASIC_RUNTIME** if the write callback failed.

`basic_error_t basic_new(basic_t *b)`

Delete the program and its variables, as `NEW` does. Returns **BASIC_OK**.

`const char *basic_message(const basic_t *b)`

Return the message saying why the last call failed, such as _line 20: subscript out of range_ or _stopped at line 40_, or "" if it succeeded. Messages about a program line start with its number. It is good until the interpreter is next called.

`basic_error_t basic_free(basic_t *b)`

Free everything the interpreter allocated, its program and variables included. The `basic_t` itself belongs to the caller and isn't freed; it is left empty. Returns **BASIC_OK**.

Every function accepts a **NULL** _b_: `basic_message()` returns "", and the others return **BASIC_ERROR**, as they do for a **NULL** _text_ or _path_. An interpreter that was freed, or whose `basic_init()` failed, is empty and stays that way until it is initialized again: `basic_message()` returns "", and the others return **BASIC_ERROR**.

## Types

    typedef enum basic_error_t {
        BASIC_OK = 0,
        BASIC_NO_MEM,
        BASIC_NO_MATCH,     // never returned
        BASIC_ERROR,
        BASIC_SYNTAX,       // a line isn't BASIC
        BASIC_RUNTIME,      // a fatal exception, or a command that failed
        BASIC_STOPPED,      // the program ran STOP
    } basic_error_t;

The error codes. **BASIC_NO_MATCH** is there so the codes match those of the other libraries.

    typedef int (*basic_write_func_t)(const char *text, size_t length, void *ctx);

Writes _length_ bytes of output, which aren't NUL-terminated and may hold newlines, with the _ctx_ given to `basic_init()`. Returns **0**, or non-zero if the output couldn't be written, which stops the program with _can't write the output_.

    typedef int (*basic_read_func_t)(sof_string_t *line, void *ctx);

Reads a line for `INPUT`, after its prompt has been written: the callback appends the line, without its newline, to _line_, which is empty. Returns **0** if a line was read, or non-zero at the end of the input, which stops the program with _end of input_.

    typedef void (*basic_warn_func_t)(const char *message, void *ctx);

Reports an exception the program goes on after, such as _line 20: division by zero_, NUL-terminated and without a newline.

    typedef struct basic_t {
        sof_rbtree_t lines;         // line number to line
        sof_hashmap_t scalars;      // name to simple variable
        sof_hashmap_t arrays;       // name to array
        sof_hashmap_t functions;    // name to DEF function
        sof_stack_t gosubs;         // where RETURN goes back to
        sof_stack_t loops;          // the FOR loops running
        sof_vector_t data;          // the DATA items, in order
        size_t data_next;           // the item READ reads next
        sof_random_t random;        // RND's generator
        uint64_t seed;              // and its seed
        ...                         // the callbacks, ctx, and where the program is running
        int base;                   // OPTION BASE
        size_t column;              // the column the output is in, from 0
        char message[BASIC_MESSAGE_SIZE]; // why the last call failed, or ""
        sof_string_t input;         // the last line INPUT read
        sof_alloc_t alloc;          // copy of the allocator; zeroed for the standard functions
        int ready;                  // 1 once initialized, 0 when freed or failed
    } basic_t;

The fields can be read but should not be changed by the application. The tree of lines points into the `basic_t`, so it must not be moved or copied once initialized.

## Threading

_basic.c_ and _basic_scan.c_ have no globals and do no locking, so separate interpreters can be used on separate threads freely. Only `basic_message()` only reads the interpreter; every other function writes it, even `basic_list()`, which keeps track of the output's column, so an interpreter needs a thread to itself while any other function runs. With a readers-writer lock, take the read lock for `basic_message()` and the write lock for the rest. The callbacks are called on the thread that called the interpreter.

The program runs one interpreter on one thread.
