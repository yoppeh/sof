# lines

A line printer in c: the lines of files, numbered, with runs of blank lines squeezed, or cut to a range, with each feature's options defined in its own source file.

`lines` prints the lines of files, or of standard input, as one input, as `cat` does. `-n` numbers them, or `--number=`_width_ in a field of a chosen width, `-s` prints one blank line where there are several in a row, and `-f` and `-c` print only a range of lines: from line _first_, at most _count_ lines. Lines keep their line endings, newline or carriage return and newline, so the output is the input's bytes but for what the options leave out or add.

The program is built to show what [sof_copt](../../sof_copt/) is for: options defined where they are used, with no central list. Each feature is a module of its own, which declares its options as `sof_copt_t` structures, lists them in a `SOF_COPT_OPTIONS` array, reads their arguments in its own validate and missing callbacks, and keeps what they ask for to itself. _main.c_ defines only `-h`; it calls `sof_copt_parse()` once, which finds every module's options through the table the linker builds from the arrays, reports the errors it returns, prints the usage message with `sof_copt_print()`, and then reads the input and asks the modules, through the functions _lines.h_ declares, what to do with each line. A new feature is a new file with its own options; neither _main.c_'s parsing nor its usage message changes.

_main.c_ has `-h`, reads the files and prints the lines; it also has `lines_number()`, which the modules' validate callbacks use to read a number. _number.c_ has `-n`, which takes no argument, and `--number`, a long option whose argument, the width of the numbers, is optional, and `number_print()`. An optional argument has to be attached to its option, so it goes on the long option, after an '=', where it reads naturally; on `-n` it would make `-n 3` mean `-n` and a file named _3_. _range.c_ has `-f` and `-c`, which take a required argument, with missing callbacks that set their defaults, and `range_select()`. _squeeze.c_ has `-s`, which takes no argument, and `squeeze_keep()`. Each module's array starts with an entry whose `option` is **NULL**, which `sof_copt_print()` prints as a heading. _lines.h_ declares what the modules offer _main.c_, and **LINES_BAD_ARGUMENT**, the application's code, **1**, that a validate callback returns once it has printed why an argument is wrong; sof_copt keeps it in the option's `stop_value`, and `sof_copt_parse()` returns **SOF_COPT_STOPPED**. The program reads its options with the [sof_copt](../../sof_copt/) library in _../../sof_copt/_ (_sof_copt.c_ and _sof_copt.h_), reads lines of any length into a string from the [sof_string](../../sof_string/) library in _../../sof_string/_ (_sof_string.c_ and _sof_string.h_), which allocates through the [sof_alloc](../../sof_alloc/) library in _../../sof_alloc/_ (_sof_alloc.c_ and _sof_alloc.h_). Beyond those, it needs nothing but the c standard library, and, as sof_copt does, the gcc and clang `section` and `used` attributes and a linker that defines the start and end of a section.

    // squeeze.c: a module's options, wherever the module is
    static sof_copt_t opt_squeeze = {
        .option = "-s",
        .description = "print one blank line in place of several in a row",
    };
    SOF_COPT_OPTIONS static sof_copt_t *options[] = {&opt_heading, &opt_squeeze};

    // main.c: one parse finds them all
    int index;
    sof_copt_t *option;
    if (sof_copt_parse(argc, argv, NULL, &index, &option) != SOF_COPT_OK) {
        sof_copt_print(stderr);                      // every module's options
        return 2;
    }

## Building

    make            # builds ./lines
    make debug=1    # builds with debug info and without optimization
    ./lines -h                       # the options, gathered from four files
    ./lines -sn -f 101 -c 8 ../../test-data/shakespeare.txt

`make` in the library's directory, _../_, builds it with the library, as do `make sof_copt` and `make` from the root of the project.

To build it in another project, copy _main.c_, _lines.h_, _number.c_, _range.c_ and _squeeze.c_, with _sof_copt.c_ and _sof_copt.h_ from _../../sof_copt/_, _sof_string.c_ and _sof_string.h_ from _../../sof_string/_, and _sof_alloc.c_ and _sof_alloc.h_ from _../../sof_alloc/_, into it.

## Usage

    lines [-h] [-n] [--number[=width]] [-s] [-f first] [-c count] [file]...

- `-h`: print the usage message, with the list `sof_copt_print()` makes, and exit.
- `-n`: put each line's number in front of it, right-aligned in a field 6 wide, and two spaces. The numbers are those of the input, counted across all the files, so they don't change with `-f` or `-s`.
- `--number`, `--number=`_width_: the same, in a field _width_ wide, from 1 to 20 (6 if no _width_ is given). The width is an optional argument, so it must follow the '='; it wins over `-n`, whichever comes first.
- `-s`: print one blank line in place of several in a row. A blank line is empty, but for its line ending.
- `-f` _first_: start at line _first_ of the input, from 1 (the default).
- `-c` _count_: read at most _count_ lines from _first_ on (by default, all of them). Lines squeezed out by `-s` count, and reading stops once they have been read, without reading the rest of the input.
- _file_: the input. With no _file_, or `-`, it reads standard input. Several files are read as one input, one after the other.

Options can come anywhere, before or after the files, and one-letter options can be combined, as in `-sn`; `--` ends them. It exits with **0**; **1** if a file couldn't be read, after printing the rest; or **2** if the options are wrong, with the usage message for options sof_copt rejects, or a message from the validate callback for an argument that is wrong.

For example, `-h`, with each module's options under its own heading (in the order the linker put the arrays in, which can differ between platforms):

    $ ./lines -h
    usage: lines [-h] [-n] [--number[=width]] [-s] [-f first] [-c count] [file]...
      -h .............. print this help and exit
    Numbering:
      -n .............. number the lines, in a field 6 wide
      --number[=arg] .. number the lines; =width sets the field's width (default 6)
    Range:
      -f=<arg> ........ start at this line (default 1)
      -c=<arg> ........ read at most this many lines from there (default all)
    Blank lines:
      -s .............. print one blank line in place of several in a row
    Prints the lines of the files, or of standard input if there are none or a
    file is "-", as one input. Exits with 0, 1 if a file couldn't be read, or
    2 if the options are wrong.

squeezed and numbered, with the width from the long option:

    $ printf 'one\n\n\n\ntwo\n\nthree\n' | ./lines -s --number=3
      1  one
      2  
      5  two
      6  
      7  three

a range of the complete works of Shakespeare (see _../../test-data/README.md_), whose two blank lines at 105 and 106 become one:

    $ ./lines -sn -f 101 -c 8 ../../test-data/shakespeare.txt
       101  Within thine own bud buriest thy content,
       102  And, tender churl, mak’st waste in niggarding:
       103    Pity the world, or else this glutton be,
       104    To eat the world’s due, by the grave and thee.
       105  
       107                      2
       108  

and errors, from sof_copt and from the modules' validate callbacks:

    $ ./lines -c
    -c needs an argument
    usage: lines [-h] [-n] [--number[=width]] [-s] [-f first] [-c count] [file]...
    ...
    $ ./lines -n=3
    -n takes no argument
    ...
    $ ./lines -s=1
    -s takes no argument
    ...
    $ ./lines -f 0
    -f wants a number from 1 up, not "0"
    $ ./lines --number=50
    --number wants a number from 1 to 20, not "50"
