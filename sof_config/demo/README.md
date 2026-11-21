# ini

A query tool for .ini files in c: every setting, one section, or one key's value, read as a string, integer, number or boolean.

`ini` parses _.ini_ files, each overriding the settings of those before it, as a file of defaults and a file of local changes would be read, and prints every setting once, with its last value, as `section.key = "value"`. With `-s` it prints only one section, and with `-k` only the value of one key, plain, for use in a script; `-t` reads that value as an integer, a floating-point number or a boolean, and fails if it isn't one. With `-a` it prints every assignment as it is parsed, those that are overridden too, and with `-c` it only checks the syntax. A syntax error is reported with the file, line and column.

_main.c_ is the program. It parses with the [sof_config](../../sof_config/) library: one `sof_config_t`, set up with `sof_config_init()`, parses each file in turn with `sof_config_parse()`, or standard input with `sof_config_parse_file()`, and stores every assignment, a later one replacing the value of an earlier one with the same section and key. On an error, the `sof_config_t`'s `line` and `column` say where, and `sof_config_error_string()` what went wrong. The settings are printed by `sof_config_traverse()`, in the order they first appeared, or, with `-a`, by the callback given to `sof_config_init()`, as each assignment is parsed. `-k` finds the value with `sof_config_get()`, or with `-t`, `sof_config_get_int()`, `sof_config_get_double()` or `sof_config_get_bool()`, and on a bad value `sof_config_find()` gives the line it was assigned on. Values are printed in quotes with the escapes sof_config reads, so each takes one line and the output could be parsed again; `-k` prints them as they are.

_example.ini_ is an example input: a small web service's settings, one of them assigned twice. The settings are stored by the [sof_config](../../sof_config/) library in _../../sof_config/_ (_sof_config.c_ and _sof_config.h_), which finds them through the [sof_hashmap](../../sof_hashmap/) library in _../../sof_hashmap/_ (_sof_hashmap.c_ and _sof_hashmap.h_), whose keys are hashed by the [sof_hash](../../sof_hash/) library in _../../sof_hash/_ (_sof_hash.c_, _sof_hash.h_ and _rapidhash.h_, which is [rapidhash](https://github.com/Nicoshev/rapidhash), under its own MIT license), checks `\u` and `\U` escapes with _sof_utf.h_ from the [sof_utf](../../sof_utf/) library in _../../sof_utf/_, and allocates through the [sof_alloc](../../sof_alloc/) library in _../../sof_alloc/_ (_sof_alloc.c_ and _sof_alloc.h_). The program reads its options with the [sof_copt](../../sof_copt/) library in _../../sof_copt/_ (_sof_copt.c_ and _sof_copt.h_). Beyond those, it needs nothing but the c standard library.

    sof_config_t config;
    sof_config_init(&config, NULL, NULL, NULL);    // no callback or allocator
    sof_config_parse(&config, "example.ini");      // stores every assignment
    sof_config_parse(&config, "local.ini");        // overrides some of them
    int64_t port;
    if (sof_config_get_int(&config, "server", "port", &port) == SOF_CONFIG_OK) {
        ...                                        // 8443
    }
    sof_config_free(&config);

## Building

    make            # builds ./ini
    make debug=1    # builds with debug info and without optimization
    ./ini example.ini                             # every setting
    ./ini -s server -k port -t int example.ini    # one, as an integer

`make` in the library's directory, _../_, builds it with the library, as do `make sof_config` and `make` from the root of the project.

To build it in another project, copy _main.c_, with _sof_config.c_ and _sof_config.h_ from _../../sof_config/_, _sof_hashmap.c_ and _sof_hashmap.h_ from _../../sof_hashmap/_, _sof_hash.c_, _sof_hash.h_ and _rapidhash.h_ from _../../sof_hash/_, _sof_utf.h_ from _../../sof_utf/_, _sof_alloc.c_ and _sof_alloc.h_ from _../../sof_alloc/_, and _sof_copt.c_ and _sof_copt.h_ from _../../sof_copt/_, into it.

## Usage

    ini [-h] [-a] [-c] [-s section] [-k key [-t type]] [file]...

- `-h`: print the usage message, with the list `sof_copt_print()` makes, and exit.
- `-a`: print every assignment as it is parsed, in the order of the input, including those a later one overrides, instead of each setting once with its last value. With more than one input, each line starts with the name of the file it came from.
- `-c`: only check the syntax: parse the input and print nothing but errors, whatever else is given.
- `-s` _section_: print only the settings in _section_, and with `-k`, look the key up there. An empty _section_, `-s ""`, means the assignments before the first section declaration. It is an error, exit **1**, if there are none.
- `-k` _key_: print only the value of _key_, in the section given with `-s` or outside any section, as it is, without quotes or escapes. It can't be used with `-a`.
- `-t` _type_: read the value of `-k` as _type_: `string` (the default), `int`, an optional sign and decimal digits, or `0x` and hexadecimal digits, printed in decimal; `double`, a finite floating-point number, printed with up to 15 significant digits; or `bool`, one of `true`, `yes`, `on` or `1`, printed as `true`, or `false`, `no`, `off` or `0`, printed as `false`, in any case.
- _file_: the input. With no _file_, or `-`, it reads standard input. With several, they are parsed in order, each overriding the settings of those before it.

The syntax is sof_config's (see [its README](../../sof_config/)): comments start with `;` or `#`, `[name]` starts a section, and each assignment is a key, `=` and a value, raw to the end of the line or quoted, with escapes. A setting assigned more than once keeps its last value, and the place it first appeared in.

It exits with **0**; **1** if the key or section asked for isn't there, or the value isn't of the type asked for; or **2** if the options are wrong, a file can't be read, its syntax is wrong (with the file, line and column), or memory runs out.

For example, with _example.ini_, in which _server.port_ is assigned twice:

    $ ./ini example.ini
    name = "example service"
    debug = "off"
    server.host = "0.0.0.0"
    server.port = "8443"
    server.timeout = "2.5"
    server.banner = "Welcome!\n\tMind the gap."
    database.url = "postgres://db.example.com:5432/app"
    database.pool = "0x10"
    database.readonly = "yes"

    $ ./ini -s database example.ini
    database.url = "postgres://db.example.com:5432/app"
    database.pool = "0x10"
    database.readonly = "yes"

one value at a time, as a script would read them:

    $ ./ini -s database -k pool -t int example.ini
    16
    $ ./ini -k debug -t bool example.ini
    false
    $ ./ini -s server -k host -t int example.ini
    server.host = "0.0.0.0", on line 6, isn't a valid int

overriding with a second file, here standard input, and seeing every assignment as it is parsed:

    $ printf '[server]\nport = 9000\n' | ./ini -a -s server example.ini -
    example.ini: server.host = "0.0.0.0"
    example.ini: server.port = "8080"
    example.ini: server.timeout = "2.5"
    example.ini: server.banner = "Welcome!\n\tMind the gap."
    example.ini: server.port = "8443"
    standard input: server.port = "9000"

and errors:

    $ printf '[server\nport = 1\n' | ./ini
    standard input:1:8: Unexpected end of line
    $ printf 'motd = "a \\q"\n' | ./ini
    standard input:1:11: Invalid escape sequence

_../../test-data/config-data.ini_, the file sof_config's tests read, has every kind of value in it.
