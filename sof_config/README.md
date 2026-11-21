# sof_config

A parser for .ini style configuration files in c.

The library has a `sof_config_t` type that holds a configuration: the assignments parsed so far, and the parser. The application provides the `sof_config_t` (on the stack, inside another structure or on the heap) and sets it up with `sof_config_init()`, optionally giving it a callback that receives each `key = value` assignment as it is parsed, a _ctx_ for it, and an allocator for the config's memory. `sof_config_parse()` opens and parses a file, `sof_config_parse_file()` parses a file that is already open, such as standard input, and `sof_config_parse_string()` parses text in memory. Each returns **SOF_CONFIG_OK** or an error code, and on an error the `sof_config_t`'s `line` and `column` say where. `sof_config_error_string()` describes an error code. `sof_config_get()` returns the value for a section and key, and `sof_config_get_int()`, `sof_config_get_double()` and `sof_config_get_bool()` convert it; `sof_config_find()` returns the whole entry, with the line it came from. `sof_config_next()` and `sof_config_traverse()` visit every entry in the order they first appeared. `sof_config_clear()` removes the entries, and `sof_config_free()` releases the config's memory once the application is finished with it.

Every assignment is stored, under its section and key. Parsing more input adds to what is stored, and an assignment to a section and key that is already there replaces its value, so a file of defaults can be parsed and then a file that overrides some of them. The callback sees every assignment, including the ones that replace others, so it can reject unknown keys or duplicates with the line they are on. The input is read a buffer at a time, and there are no limits on the lengths of lines, sections, keys or values.

The library's demo is the [ini](demo/) program in _demo/_, which prints the settings of .ini files, one section, or one key's value, read as a string, integer, number or boolean, and reports syntax errors with the file, line and column. The tests are in _test.c_, a program of their own that the Makefile builds and runs, using the harness the collection's tests share in [../test/](../test/); they parse _../test-data/config-data.ini_ among other things. All config declarations are in _sof_config.h_. Entries are found through the [sof_hashmap](../sof_hashmap/) library in _../sof_hashmap/_ (_sof_hashmap.c_ and _sof_hashmap.h_), which uses the [sof_hash](../sof_hash/) library in _../sof_hash/_ (_sof_hash.c_, _sof_hash.h_ and _rapidhash.h_, which is [rapidhash](https://github.com/Nicoshev/rapidhash), under its own MIT license). Memory is allocated through the [sof_alloc](../sof_alloc/) library in _../sof_alloc/_ (_sof_alloc.c_ and _sof_alloc.h_), and `\u` and `\U` escapes are checked and encoded through the [sof_utf](../sof_utf/) library in _../sof_utf/_ (_sof_utf.h_). Beyond those, the library needs nothing but the c standard library.

    sof_config_t config;
    sof_config_init(&config, NULL, NULL, NULL);                     // no callback or allocator: just store
    if (sof_config_parse(&config, "app.ini") != SOF_CONFIG_OK) {         // stores each assignment
        printf("app.ini:%zu:%zu: error\n", config.line, config.column);
    }
    int64_t port = 8080;                                       // the default, kept if there is no port
    sof_config_get_int(&config, "server", "port", &port);
    const char *host = sof_config_get(&config, "server", "host");  // NULL if there is none
    ...
    sof_config_free(&config);                                       // frees the stored strings too

## Building

    make             # compiles the library and builds ./test and the demo, demo/ini
    make check       # builds and runs the tests
    make sanitize    # runs the tests with AddressSanitizer and UndefinedBehaviorSanitizer
    make memcheck    # runs the tests under valgrind (Linux)
    make debug=1     # builds with debug info and without optimization

Besides testing each function, the tests read a config on threads as the Threading section below describes: readers using every reading function at once on a config parsed beforehand, with no lock, and readers under a reader-writer lock while a writer parses each new version over the old one, or clears it and parses again, with every reader checking that what it sees is all from one version. The tests are linked with _-pthread_; the library itself doesn't use threads.

To use the library in another project, copy _sof_config.c_ and _sof_config.h_, _sof_hashmap.c_ and _sof_hashmap.h_ from _../sof_hashmap/_, _sof_alloc.c_ and _sof_alloc.h_ from _../sof_alloc/_, _sof_hash.c_, _sof_hash.h_ and _rapidhash.h_ from _../sof_hash/_, and _sof_utf.h_ from _../sof_utf/_, into it. To build its tests there too, copy _test.c_, and _test.h_ from _../test/_, and compile _test.c_, _sof_config.c_, _sof_hashmap.c_, _sof_alloc.c_ and _sof_hash.c_ with _-pthread_; the tests open _../test-data/config-data.ini_, so copy that file as well, to the same place relative to where they run.

## Syntax

    ; a comment runs to the end of the line
    # and so does this one
    key = value                 ; outside any section
    [server.http]
    host = example.com          ; raw values run to the end of the line
    motd = "Welcome; it's  \"quoted\"\n"

The input is lines, each blank, a comment, a section declaration or an assignment. Whitespace is spaces, tabs, carriage returns (so CRLF line ends work), form feeds and vertical tabs. A UTF-8 byte order mark at the start of the input is skipped.

- **Comments** start with `;` or `#` and run to the end of the line. They can have a line to themselves, or follow a section declaration or a value.
- **Section declarations**, `[name]`, make _name_ the current section until the next declaration. Names can have ASCII letters, digits, `_`, `-`, `.`, spaces and tabs; they are trimmed, and can't be empty. Assignments before the first declaration have no section. A section can be declared more than once.
- **Assignments** are a key, `=` and a value, with the key and the `=` on the same line. Keys can have ASCII letters, digits, `_`, `-` and `.`, and are case sensitive, as are section names.
- **Raw values** run to the end of the line, or to a `;` or `#` that follows whitespace, which starts a comment, and are trimmed. They can be empty, and can hold any other character, including `=`, `"`, `\` and UTF-8, with no escapes: `semi;colon` and `url#fragment` are values, but a value that starts with `;` or `#` must be quoted.
- **Quoted values** are enclosed in `"` and must end on the line they start on. They keep their spaces, and anything after the closing `"` must be whitespace or a comment. These escapes are replaced:
    - `\a`, `\b`, `\f`, `\n`, `\r`, `\t` and `\v`: the control characters;
    - `\\`, `\"` and `\'`: the characters themselves;
    - `\x` and two hexadecimal digits: that byte, which can't be 0;
    - `\u` and four, or `\U` and eight, hexadecimal digits: that Unicode code point in UTF-8; it can't be 0, a surrogate, or above 10FFFF.

  Any other escape is an error, **SOF_CONFIG_BAD_ESCAPE**.
- NUL bytes aren't allowed outside comments.

The characters `[`, `]`, `;`, `#`, `=`, `"` and `\` are constants at the top of _sof_config.c_.

## Functions

`sof_config_error_t sof_config_init(sof_config_t *config, sof_config_assignment_func_t handler, void *ctx, const sof_alloc_t *alloc)`

Initialize the _config_ the caller provides. _handler_, if not **NULL**, is called with each assignment parsed and the _ctx_. _alloc_ is the allocator for everything the config allocates, including its entries and their strings; **NULL** means the standard functions (see [sof_alloc](../sof_alloc/)). It is copied into the config, so it can be on the stack. Returns **SOF_CONFIG_OK**, **SOF_CONFIG_NO_MEM** if memory couldn't be allocated, or **SOF_CONFIG_ERROR** if _alloc_ has a _malloc_ but no _free_ or _free_sized_; an error leaves the _config_ empty.

`sof_config_error_t sof_config_parse(sof_config_t *config, const char *file_name)`

Open the file _file_name_, parse it, storing its assignments, and close it. Returns **SOF_CONFIG_OK** if the whole file was parsed; **SOF_CONFIG_STOPPED** if _handler_ returned non-zero, which is kept in `stop_value`; **SOF_CONFIG_NO_OPEN** if the file couldn't be opened; **SOF_CONFIG_NO_READ** if reading it failed; **SOF_CONFIG_UNEXPECTED_CHARACTER**, **SOF_CONFIG_UNEXPECTED_EOL**, **SOF_CONFIG_PREMATURE_EOF** or **SOF_CONFIG_BAD_ESCAPE** if its syntax is wrong; or **SOF_CONFIG_NO_MEM** if memory couldn't be allocated. The assignments before an error are stored, and `line` and `column` give the character at fault (for **SOF_CONFIG_BAD_ESCAPE**, the backslash). Columns count bytes, from 1.

`sof_config_error_t sof_config_parse_file(sof_config_t *config, FILE *file)`

Parse an open _file_, such as `stdin`, from where it is to its end. The _file_ isn't closed. Returns the same as `sof_config_parse()`, but for **SOF_CONFIG_NO_OPEN**.

`sof_config_error_t sof_config_parse_string(sof_config_t *config, const char *text, size_t length)`

Parse the _length_ bytes at _text_, which needn't be NUL-terminated. Returns the same as `sof_config_parse()`, but for **SOF_CONFIG_NO_OPEN** and **SOF_CONFIG_NO_READ**.

`const char *sof_config_error_string(sof_config_error_t error)`

Return a description of _error_, or `"Unknown error"` if it isn't a `sof_config_error_t`.

`const char *sof_config_get(const sof_config_t *config, const char *section, const char *key)`

Return the value of _key_ in _section_, or outside any section if _section_ is **NULL**, or **NULL** if there isn't one. The value is good until `sof_config_clear()` or `sof_config_free()`.

`sof_config_error_t sof_config_get_int(const sof_config_t *config, const char *section, const char *key, int64_t *value)`

Set _value_ to the value of _key_ in _section_ as an integer: an optional sign, then decimal digits, or `0x` and hexadecimal digits. A leading **0** doesn't make it octal. Returns **SOF_CONFIG_OK**, **SOF_CONFIG_NO_MATCH** if there's no such value, or **SOF_CONFIG_BAD_VALUE** if it isn't an integer or doesn't fit in an `int64_t`. On any error _value_ is left as it was, so it can be set to a default first.

`sof_config_error_t sof_config_get_double(const sof_config_t *config, const char *section, const char *key, double *value)`

The same as `sof_config_get_int()`, for a finite decimal or hexadecimal floating-point number, as `strtod()` reads them (so in the program's locale).

`sof_config_error_t sof_config_get_bool(const sof_config_t *config, const char *section, const char *key, int *value)`

The same as `sof_config_get_int()`, setting _value_ to **1** for `true`, `yes`, `on` or `1`, and **0** for `false`, `no`, `off` or `0`, in any case.

`const sof_config_entry_t *sof_config_find(const sof_config_t *config, const char *section, const char *key)`

Return the entry for _key_ in _section_, with its value and the line it was assigned on, or **NULL** if there isn't one.

`const sof_config_entry_t *sof_config_next(const sof_config_t *config, size_t *position)`

Return the next entry, in the order their sections and keys first appeared, or **NULL** when there are no more. Set _position_ to 0 to start; each call updates it. Don't parse, clear or free until the loop ends.

    size_t position = 0;
    const sof_config_entry_t *e;
    while ((e = sof_config_next(&config, &position)) != NULL) {
        ...
    }

`int sof_config_traverse(const sof_config_t *config, sof_config_traverse_func_t cb, void *ctx)`

Call _cb_ with each entry and _ctx_, in the same order as `sof_config_next()`. If _cb_ returns a non-zero value, the traversal stops and returns that value. Otherwise returns **0** once every entry has been visited, or **-1** if _config_ or _cb_ is **NULL**.

`sof_config_error_t sof_config_clear(sof_config_t *config)`

Remove every entry. The _config_ can still be used.

`sof_config_error_t sof_config_free(sof_config_t *config)`

Remove every entry and free the memory `sof_config_init()` allocated. The `sof_config_t` itself belongs to the caller and isn't freed; it is left empty, and can be initialized again.

Every function accepts a **NULL** _config_ or _key_, and the parse functions a **NULL** _file_name_, _file_ or _text_: `sof_config_find()`, `sof_config_get()` and `sof_config_next()` return **NULL**, `sof_config_traverse()` returns **-1**, and the others return **SOF_CONFIG_ERROR**, as do the typed getters for a **NULL** _value_. A config that was freed, or whose `sof_config_init()` failed, is empty and stays that way: lookups find nothing (the typed getters return **SOF_CONFIG_NO_MATCH**), the parse functions return **SOF_CONFIG_ERROR** until it is initialized again, and `sof_config_clear()` and `sof_config_free()` return **SOF_CONFIG_OK**. The parse functions, `sof_config_clear()` and `sof_config_free()` also return **SOF_CONFIG_ERROR** if _handler_ calls them for the config it was called for.

## Types

    typedef enum sof_config_error_t {
        SOF_CONFIG_OK = 0,
        SOF_CONFIG_NO_MEM,
        SOF_CONFIG_NO_MATCH,                 // no such section and key
        SOF_CONFIG_ERROR,                    // a NULL argument, a config not initialized, or a call from handler
        SOF_CONFIG_NO_OPEN,                  // the file couldn't be opened
        SOF_CONFIG_NO_READ,                  // reading the file failed
        SOF_CONFIG_UNEXPECTED_CHARACTER,     // a character where it isn't allowed
        SOF_CONFIG_UNEXPECTED_EOL,           // a line ended inside a declaration, before an =, or inside quotes
        SOF_CONFIG_PREMATURE_EOF,            // the input ended in the same places
        SOF_CONFIG_BAD_ESCAPE,               // an escape sequence that isn't recognized
        SOF_CONFIG_BAD_VALUE,                // a value that isn't of the type asked for
        SOF_CONFIG_STOPPED,                  // handler stopped the parse
    } sof_config_error_t;

    typedef struct sof_config_entry_t {
        const char *section;            // NULL outside any section
        const char *key;
        const char *value;              // the last value assigned
        size_t line;                    // the line of the last assignment
    } sof_config_entry_t;

An entry belongs to the config, and its strings are good until `sof_config_clear()` or `sof_config_free()`. It shouldn't be changed by the application.

    typedef struct sof_config_t {
        sof_config_assignment_func_t handler;
        void *ctx;                      // passed to handler
        size_t count;                   // entries stored
        size_t line;                    // where the last parse stopped
        size_t column;
        int stop_value;                 // what handler returned to stop, or 0
        struct sof_config_state_t *state;    // the parser and entries; NULL if not initialized
    } sof_config_t;

The fields of `sof_config_t` can be read but should not be changed by the application. `struct sof_config_state_t` is private to _sof_config.c_.

    typedef int (*sof_config_assignment_func_t)(const char *section, const char *key, const char *value, void *ctx)

Receives each assignment as it is parsed, after it is stored: the current _section_, or **NULL** before the first declaration; the _key_; the _value_, trimmed, without its quotes, and with escape sequences replaced; and the _ctx_ given to `sof_config_init()`. The strings are only good until it returns; the stored entry can be looked up after the parse. Return **0** to continue, or a non-zero value to stop parsing: the parse function returns **SOF_CONFIG_STOPPED**, and the value is kept in `stop_value`. It must not call into the config it was called for.

    typedef int (*sof_config_traverse_func_t)(const sof_config_entry_t *entry, void *ctx)

Called for each entry visited by `sof_config_traverse()`, with the _ctx_ given to it. Return **0** to continue, or a non-zero value to stop the traversal; that value is returned by `sof_config_traverse()`. It must not parse into, clear or free the config.

## Threading

The library has no global state and does no locking, so separate configs can be used on separate threads at once. `sof_config_find()`, `sof_config_get()`, `sof_config_get_int()`, `sof_config_get_double()`, `sof_config_get_bool()`, `sof_config_next()` and `sof_config_traverse()` only read a config, so any number of threads can call them at once as long as nothing writes to it; the parse functions, `sof_config_clear()`, `sof_config_init()` and `sof_config_free()` write. A program that parses its configuration once at startup and then only reads it needs no lock. One that parses again while other threads read should guard the config with a reader-writer lock, keeping in mind that:

- a value or entry pointer is only safe to use while the lock is held, since a parse may replace the value and `sof_config_clear()` frees it;
- the callback runs on the parsing thread while it holds the write lock, so it must not take the lock again.

`sof_config_error_string()` only reads constant strings and can be called from any thread.
