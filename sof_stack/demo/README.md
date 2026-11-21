# calc

An expression calculator in c: evaluates lines such as `2 * (3 + x)` and prints the results, with variables set by `x = 4`.

`calc` reads lines from `-e` options, then from files, or from standard input, and evaluates each one. A line is an expression, whose value is printed, an assignment, `name = expression`, which sets a variable and prints nothing, or a blank line or comment. Numbers are doubles. An error is reported with the file, line and column, as _file:line:column: message_, and the lines after it are still evaluated.

_main.c_ is the program. The evaluating is done by _calc.c_, declared in _calc.h_, which keeps a `calc_t` that the program provides and sets up with `calc_init()`. `calc_eval()` evaluates a line, returning its value and saying whether it was an assignment, and on an error, `calc_message()` and the `column` field say what went wrong and where. `calc_set()`, `calc_get()` and `calc_unset()` set, read and remove variables, and `calc_next()` loops over them. `calc_free()` releases the evaluator's memory, variables included.

A line is split into tokens as it is read, and each token is handled as it comes by the shunting-yard algorithm: values go onto one stack and operators onto another, and an operator is applied to the values on top of the value stack as soon as the operators after it show its operands are complete. Variables are kept in a hashmap from name to value. The stacks and the copy of the line are kept from one line to the next, so their memory is reused.

The two stacks come from the [sof_stack](../../sof_stack/) library in _../../sof_stack/_ (_sof_stack.c_ and _sof_stack.h_), and the variables are kept in the [sof_hashmap](../../sof_hashmap/) library in _../../sof_hashmap/_ (_sof_hashmap.c_ and _sof_hashmap.h_), whose keys are hashed by the [sof_hash](../../sof_hash/) library in _../../sof_hash/_ (_sof_hash.c_ and _sof_hash.h_, which use [rapidhash](https://github.com/Nicoshev/rapidhash) in _rapidhash.h_, under its own MIT license). The line is held by the [sof_string](../../sof_string/) library in _../../sof_string/_ (_sof_string.c_ and _sof_string.h_). Memory is allocated through the [sof_alloc](../../sof_alloc/) library in _../../sof_alloc/_ (_sof_alloc.c_ and _sof_alloc.h_). Built with `make debug=1`, the evaluator traces each line, token, push and operator applied to standard error through the [sof_debug](../../sof_debug/) library in _../../sof_debug/_ (_sof_debug.c_ and _sof_debug.h_); otherwise the tracing is compiled out. The program reads its options with the [sof_copt](../../sof_copt/) library in _../../sof_copt/_ (_sof_copt.c_ and _sof_copt.h_). Beyond those, it needs nothing but the c standard library and its math library, _-lm_.

    calc_t calc;
    calc_init(&calc, NULL);                         // no allocator: the standard functions
    double value;
    int assigned;
    calc_eval(&calc, "x = 4", &value, &assigned);  // value 4, assigned 1
    if (calc_eval(&calc, "2 * (3 + x)", &value, &assigned) != CALC_OK) {
        printf("%zu: %s\n", calc.column, calc_message(&calc));
    }                                               // value 14, assigned 0
    calc_free(&calc);

## Building

    make            # builds ./calc
    make debug=1    # builds with debug info, without optimization, tracing to stderr
    ./calc          # evaluates lines typed on standard input

`make` in the library's directory, _../_, builds it with the library, as do `make sof_stack` and `make` from the root of the project.

To build it in another project, copy _main.c_, _calc.c_ and _calc.h_, with _sof_stack.c_ and _sof_stack.h_ from _../../sof_stack/_, _sof_hashmap.c_ and _sof_hashmap.h_ from _../../sof_hashmap/_, _sof_hash.c_, _sof_hash.h_ and _rapidhash.h_ from _../../sof_hash/_, _sof_string.c_ and _sof_string.h_ from _../../sof_string/_, _sof_alloc.c_ and _sof_alloc.h_ from _../../sof_alloc/_, _sof_debug.c_ and _sof_debug.h_ from _../../sof_debug/_, and _sof_copt.c_ and _sof_copt.h_ from _../../sof_copt/_, into it, and link with _-lm_. To use only the evaluator, leave out _main.c_ and the _sof_copt_ files.

## Usage

    calc [-h] [-p digits] [-e expression]... [file]...

- `-h`: print the usage message, with the list `sof_copt_print()` makes, and exit.
- `-p` _digits_: print results with _digits_ significant digits, from 1 to 17 (default 15, so 0.1 + 0.2 prints as 0.3). A result of -0 is printed as 0.
- `-e` _expression_: evaluate _expression_ as a line, before any file. Can be given more than once; they are evaluated in order. In messages, the file is _-e_ and the line is which `-e` it is, counting from 1.
- _file_: lines to evaluate. Several are evaluated in turn, sharing their variables. With no _file_ and no `-e`, or a _file_ of `-`, it reads standard input.

It exits with **0**, **1** if a line had an error, a file couldn't be read or memory ran out, or **2** if the options are wrong.

Numbers are written as in c: _42_, _1.5_, _.5_, _2._, _6.02e23_. The operators, from lowest precedence to highest:

- `+` and `-`, from left to right;
- `*`, `/` and `%` (the remainder, with the sign of the left operand, as `fmod()` gives), from left to right;
- unary `-` and `+`;
- `^`, or `**`, the power, from right to left, so `2 ^ 3 ^ 2` is 512 and `-2 ^ 2` is -4.

Parentheses group. The functions `abs`, `ceil`, `cos`, `exp`, `floor`, `ln` (natural), `log` (base 10), `round`, `sin`, `sqrt` and `tan` take one argument in parentheses, in radians for the trigonometric ones. A name is a letter or underscore, then letters, digits and underscores, up to 63 bytes. The variables _pi_ and _e_ are set at the start, and _ans_ holds the value of the last expression that wasn't an assignment. A `#` starts a comment that runs to the end of the line.

For example, the monthly payment on a loan and the interest paid on it:

    $ cat loan.txt
    # monthly payment on a loan
    rate = 0.05 / 12
    n = 30 * 12
    principal = 250000
    principal * rate / (1 - (1 + rate)^-n)
    ans * n - principal   # interest paid
    sqrt(2) * cos(pi / 4)
    2 ^ 3 ^ 2
    total / 2
    (1 + 2
    10 % 0
    $ ./calc -p 10 < loan.txt
    1342.054058
    233139.4607
    1
    512
    standard input:9:1: unknown variable 'total'
    standard input:10:1: unmatched '('
    standard input:11:4: modulo by zero

and from the command line:

    $ ./calc -e 'x = 4' -e '2 * (3 + x)' -e 'ans / 7'
    14
    2

## Errors

`calc_eval()` returns an error code, and sets a message and a column, for each of these:

- **CALC_SYNTAX**: the line isn't an expression or an assignment, as in _expected a value, not the end of the line_, _expected an operator, not '3'_, _unmatched '('_, _unmatched ')'_, _unknown character '$'_, _'sqrt' is a function: write sqrt(x)_, _'sqrt' is a function and can't be assigned_, _'=' can only follow a name at the start of the line_, _name too long_ or _number too long_ (more than 255 bytes).
- **CALC_NO_MATCH**: _unknown variable 'y'_.
- **CALC_MATH**: _division by zero_, _modulo by zero_, _number '1e999' out of range_, or an operation whose result isn't a finite number: _result of 'sqrt' is undefined_ (for not a number, as from `sqrt(-1)`) or _result of '^' is out of range_ (for an infinity, as from `10 ^ 400` or `ln(0)`).
- **CALC_NO_MEM**: _out of memory_, with column **0**.

The column is the 1-based byte offset in the line of the token where the problem was found: the operator or function for a math error, the name for an unknown variable, the parenthesis left unmatched. A token quoted in a message is cut to 24 bytes, followed by _..._ if it was longer. A line with an error changes no variable.

## Functions

`calc_error_t calc_init(calc_t *calc, const sof_alloc_t *alloc)`

Initialize the evaluator _calc_ the caller provides, and set the variables _pi_ and _e_. _alloc_ is the allocator for all of the evaluator's memory, **NULL** meaning the standard functions (see [sof_alloc](../../sof_alloc/)); it is copied into the evaluator, so it can be on the stack. Returns **CALC_OK**, **CALC_NO_MEM** if memory couldn't be allocated, or **CALC_ERROR** if _calc_ is **NULL** or _alloc_ has a _malloc_ but no _free_ or _free_sized_; an error leaves _calc_ empty.

`calc_error_t calc_eval(calc_t *calc, const char *line, double *value, int *assigned)`

Evaluate the NUL-terminated _line_, which is an expression or an assignment, without its newline. An expression's value is also stored in the variable _ans_; an assignment sets its variable and leaves _ans_ alone. If _value_ isn't **NULL**, it is set to the value on **CALC_OK** and left alone otherwise. If _assigned_ isn't **NULL**, it is set to **1** if the line was an assignment that succeeded, or **0** otherwise. Returns **CALC_OK**; **CALC_EMPTY** if the line holds nothing but spaces or a comment; **CALC_SYNTAX**, **CALC_NO_MATCH**, **CALC_MATH** or **CALC_NO_MEM**, as described under Errors, with a message from `calc_message()` and a column in `column`, changing no variable; or **CALC_ERROR** if _line_ is **NULL**.

`const char *calc_message(const calc_t *calc)`

Return the message saying why the last `calc_eval()` failed, or "" if it succeeded or there was none. It is good until the next `calc_eval()` or `calc_free()`.

`calc_error_t calc_set(calc_t *calc, const char *name, double value)`

Set the variable _name_ to _value_, creating it if it isn't set; the evaluator keeps a copy of _name_. Returns **CALC_OK**, **CALC_NO_MEM**, leaving the variable as it was, or **CALC_ERROR** if _name_ is **NULL** or isn't a name a variable can have: a letter or underscore, then letters, digits and underscores, at most **CALC_NAME_MAX** (63) bytes, and not the name of a function.

`calc_error_t calc_get(const calc_t *calc, const char *name, double *value)`

Get the value of the variable _name_ into _*value_, if _value_ isn't **NULL**. Returns **CALC_OK**, **CALC_NO_MATCH** if the variable isn't set, leaving _*value_ alone, or **CALC_ERROR** if _name_ is **NULL**.

`calc_error_t calc_unset(calc_t *calc, const char *name)`

Remove the variable _name_. Returns **CALC_OK**, **CALC_NO_MATCH** if it isn't set, or **CALC_ERROR** if _name_ is **NULL**.

`const char *calc_next(const calc_t *calc, size_t *position, double *value)`

Return the name of the next variable, setting _*value_ to its value if _value_ isn't **NULL**, or return **NULL** when there are no more. Set _*position_ to **0** to start; each call updates it. The variables come in no particular order. The name is good until the variable is unset or the evaluator freed, and the loop must not call `calc_eval()`, `calc_set()` or `calc_unset()`.

    size_t position = 0;
    double value;
    const char *name;
    while ((name = calc_next(&calc, &position, &value)) != NULL) {
        ...
    }

`calc_error_t calc_free(calc_t *calc)`

Free everything the evaluator allocated, its variables included. The `calc_t` itself belongs to the caller and isn't freed; it is left empty. Returns **CALC_OK**.

Every function accepts a **NULL** _calc_: `calc_next()` returns **NULL**, `calc_message()` returns "", and the others return **CALC_ERROR**. `calc_next()` also returns **NULL** for a **NULL** _position_. An evaluator that was freed, or whose `calc_init()` failed, is empty and stays that way until it is initialized again: it has no variables, `calc_message()` returns "", `calc_next()` returns **NULL**, and the others return **CALC_ERROR**. `calc_eval()` sets _*assigned_ to **0** in every case but a successful assignment.

## Types

    typedef enum calc_error_t {
        CALC_OK = 0,
        CALC_NO_MEM,
        CALC_NO_MATCH,      // a variable isn't set
        CALC_ERROR,
        CALC_SYNTAX,        // the line isn't an expression or an assignment
        CALC_MATH,          // division by zero, or a result that isn't a finite number
        CALC_EMPTY,         // the line holds no expression
    } calc_error_t;

The error codes. See Errors for what `calc_eval()` returns each one for.

    typedef struct calc_t {
        sof_hashmap_t vars;             // name to value, a double the data points to
        sof_stack_t values;             // the value stack, of doubles
        sof_stack_t ops;                // the operator stack
        sof_string_t line;              // a copy of the line being evaluated
        size_t column;                  // where the last error was found, from 1, or 0
        char message[CALC_MESSAGE_SIZE]; // why, or ""
        sof_alloc_t alloc;              // copy of the allocator; zeroed for the standard functions
        int ready;                      // 1 once initialized, 0 when freed or failed
    } calc_t;

The fields can be read but should not be changed by the application. Nothing points into the `calc_t`, so it can be moved or copied between calls, as long as only one copy is used afterwards.

## Threading

_calc.c_ has no globals and does no locking, so separate evaluators can be used on separate threads freely. `calc_get()`, `calc_message()` and `calc_next()` only read the evaluator, so any number of threads can call them at once as long as nothing writes to it. `calc_eval()` writes even when it changes no variable, since it uses the evaluator's stacks, copy of the line and regular expression, so it needs the evaluator to itself, as do `calc_set()`, `calc_unset()` and `calc_free()`. With a readers-writer lock, take the write lock for those and the read lock for the rest.

The program evaluates its lines one after another on one thread.
