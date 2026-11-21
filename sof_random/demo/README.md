# markov

A Markov chain text generator in c: new text made from the n-grams of words or characters in other text.

`markov` learns every n-gram of characters, or with `-w` of words, in files or standard input, then prints new text in which each character or word follows the n - 1 before it, chosen at random as often as it followed them in the input. Which characters make up words and grams is a set given with `-c`: any other character breaks a run of characters, or separates words. The random numbers come from a seed given with `-s`, and the text depends on nothing else, so the same seed and input give the same text, on any machine.

_main.c_ is the program. The chain is kept by _markov.c_, declared in _markov.h_, in a `markov_t` that the program provides and sets up with `markov_init()`. `markov_add()` learns a piece of text, so the input is read in pieces and a gram can span two of them; `markov_flush()` ends the text, so no gram spans two files. `markov_build()` makes the chain from what was learned, and `markov_generate()` appends new text to a string, making the chain first if need be; `markov_find()` gives the state for a context. `markov_clear()` forgets what was learned and `markov_free()` releases the generator's memory.

The counting is done by the counter of the [ngram](../../sof_hashmap/demo/) program, sof_hashmap's demo, in _../../sof_hashmap/demo/_ (_ngram.c_ and _ngram.h_), which counts the n-grams and the (n - 1)-grams. Each (n - 1)-gram is a state of the chain, and each n-gram a transition, from the state it starts with to the word or character it ends with, the state's successor. A state's successors are sorted by bytes, and so are the starts, the states with a successor, and every choice is made by an integer weight from a sorted list, so nothing depends on the order of a hashmap. A chain starts at a start chosen as often as its successors were counted, and goes on until it reaches a context that ended the text, or a run of characters, where it starts again on a new line.

The states are found through a hashmap from the [sof_hashmap](../../sof_hashmap/) library in _../../sof_hashmap/_ (_sof_hashmap.c_ and _sof_hashmap.h_), whose keys are hashed by the [sof_hash](../../sof_hash/) library in _../../sof_hash/_ (_sof_hash.c_ and _sof_hash.h_, which use [rapidhash](https://github.com/Nicoshev/rapidhash) in _rapidhash.h_, under its own MIT license). Successors and starts are sorted by the [sof_sort](../../sof_sort/) library in _../../sof_sort/_ (_sof_sort.c_ and _sof_sort.h_), random numbers come from the [sof_random](../../sof_random/) library in _../../sof_random/_ (_sof_random.c_ and _sof_random.h_), the text is built in a string from the [sof_string](../../sof_string/) library in _../../sof_string/_ (_sof_string.c_ and _sof_string.h_), and characters are read with the [sof_utf](../../sof_utf/) library in _../../sof_utf/_ (_sof_utf.h_, which is all of it). Memory is allocated through the [sof_alloc](../../sof_alloc/) library in _../../sof_alloc/_ (_sof_alloc.c_ and _sof_alloc.h_). The program reads its options with the [sof_copt](../../sof_copt/) library in _../../sof_copt/_ (_sof_copt.c_ and _sof_copt.h_). Beyond those, it needs nothing but the c standard library.

    markov_options_t options = {NGRAM_WORDS, 3, NULL, 0};  // each word chosen by the 2 before it
    markov_t m;
    markov_init(&m, &options, NULL);                        // no allocator: the standard functions
    markov_add(&m, buffer, length);                         // as many times as there is text
    markov_flush(&m);                                       // at the end of each file
    sof_random_t r;
    sof_random_init(&r, 42);                                // the seed
    sof_string_t text;
    sof_string_init(&text, 0, NULL);
    markov_generate(&m, &r, 100, &text);                    // 100 words, appended to text
    sof_string_free(&text);
    markov_free(&m);

## Building

    make            # builds ./markov
    make debug=1    # builds with debug info and without optimization
    ./markov -w -s 2026 ../../test-data/shakespeare.txt    # 100 words of Shakespeare, of a sort

`make` in the library's directory, _../_, builds it with the library, as do `make sof_random` and `make` from the root of the project.

To build it in another project, copy _main.c_, _markov.c_ and _markov.h_, with _ngram.c_ and _ngram.h_ from _../../sof_hashmap/demo/_, _sof_hashmap.c_ and _sof_hashmap.h_ from _../../sof_hashmap/_, _sof_hash.c_, _sof_hash.h_ and _rapidhash.h_ from _../../sof_hash/_, _sof_alloc.c_ and _sof_alloc.h_ from _../../sof_alloc/_, _sof_utf.h_ from _../../sof_utf/_, _sof_sort.c_ and _sof_sort.h_ from _../../sof_sort/_, _sof_random.c_ and _sof_random.h_ from _../../sof_random/_, _sof_string.c_ and _sof_string.h_ from _../../sof_string/_, and _sof_copt.c_ and _sof_copt.h_ from _../../sof_copt/_, into it.

## Usage

    markov [-h] [-w] [-i] [-n order] [-c chars] [-l length] [-s seed] [file]...

- `-h`: print the usage message, with the list `sof_copt_print()` makes, and exit.
- `-w`: learn and generate words instead of characters. A word is a run of characters in the set, and the words generated are joined by single spaces and wrapped at 72 columns.
- `-i`: ignore case: ASCII letters are folded to lowercase, so _The_ and _the_ are the same word.
- `-n` _order_: the length of the grams learned, from 2 to 100: each word or character is chosen by the _order_ - 1 before it (default 3 for words, 6 for characters). The longer it is, the more the text generated copies the input.
- `-c` _chars_: the set of characters words or grams are made of, in UTF-8, as `ngram -c` takes it: `a-z0-9'` is the lowercase letters, the digits and the apostrophe. By default it is every printable ASCII character, the Latin-1 letters and signs (`¡-ÿ`), the em dash and the curly quotes, so punctuation stays with its word, and for characters, the space too. For words it must not hold the space.
- `-l` _length_: the number of words or characters to generate (default 100 words, or 500 characters), not counting the spaces between words or the newlines between chains.
- `-s` _seed_: the seed of the random numbers, from 0 to 2^64 - 1 (default 1). The same seed and input give the same text.
- _file_: the text to learn. With no _file_, or `-`, it reads standard input. With several, each is learned in turn, and no gram spans two of them.

It exits with **0**, **1** if a file couldn't be read, memory ran out, or the input has no _order_ words or characters in a row, or **2** if the options are wrong.

Each chain of text starts at a context chosen at random from the input and goes on until it reaches the end of a file, or of a run of characters, which with the default set is the end of a line; the next chain starts on a new line. The input is read as UTF-8, as `ngram` reads it.

For example, from a small text on standard input:

    $ printf 'the cat sat on the mat\nthe dog sat on the log\n' | ./markov -w -n 2 -s 4 -l 12
    mat the mat the dog sat on the log
    sat on the

Each word follows the one before it as it did in the input, and _log_, which ended it, ends a chain. From the complete works of Shakespeare, from Project Gutenberg (see _../../test-data/README.md_), 60 words, each chosen by the two before it:

    $ ./markov -w -s 2026 -l 60 ../../test-data/shakespeare.txt
    lives! If it be the trumpet to the stake, which to read his right wits
    and his quails ever Beat mine, inhooped, at odds. I will pursue her Even
    in the famous ancient city Tours, In presence of a nightingale.
    Hoppedance cries in Tom’s belly for two nights watched with you, Lord
    Woodville and Lord Hungerford; Most of the body;

and 240 characters, each chosen by the seven before it, in chains that end where the lines of the input end:

    $ ./markov -n 8 -s 1 -l 240 ../../test-data/shakespeare.txt
    o, came to be thy worthier place. If we live to bar my moneys”?
    thed. And therefore, sir, had asked him. There is the point you had power. God be thy pain?
    g can weeps!
     servant to the world with thee ne’er be wrench—would not. Nay, an the Lor

## Functions

`markov_error_t markov_init(markov_t *m, const markov_options_t *options, const sof_alloc_t *alloc)`

Initialize the generator _m_ the caller provides, to learn what _options_ say (see Types), or with **NULL** _options_, pairs of characters from `MARKOV_CHAR_CHARS`. _alloc_ is the allocator for all of the generator's memory, **NULL** meaning the standard functions (see [sof_alloc](../../sof_alloc/)); it is copied into the generator, so it can be on the stack. Returns **MARKOV_OK**, **MARKOV_NO_MEM** if memory couldn't be allocated, or **MARKOV_ERROR** if _m_ is **NULL**, _alloc_ has a _malloc_ but no _free_ or _free_sized_, the order is 1, the unit isn't an `ngram_unit_t`, the set of characters is empty or isn't valid UTF-8, or the unit is words and the set holds the space; an error leaves _m_ empty.

`markov_error_t markov_add(markov_t *m, const char *text, size_t length)`

Learn the _length_ bytes of _text_, which is UTF-8 and needn't be NUL-terminated; **NULL** _text_ learns nothing. They are counted by `ngram_add()`, so grams go on from one call to the next, until `markov_flush()`. The chain, if `markov_build()` made one, is dropped. Returns **MARKOV_OK**, or **MARKOV_NO_MEM** if memory ran out, in which case the grams before the one that couldn't be stored are counted.

`markov_error_t markov_flush(markov_t *m)`

End the text, so that no gram spans the text given before the call and after it. Call it at the end of each file. The chain, if there is one, is dropped. Returns **MARKOV_OK** or **MARKOV_NO_MEM**.

`markov_error_t markov_build(markov_t *m)`

Make the chain from the counts: a state for each (n - 1)-gram, with its successors sorted by bytes, the index from contexts to states, and the starts sorted by context. If the chain is already made and nothing was learned since, it does nothing. `markov_generate()` calls it, so it need only be called to make the chain ahead of time, as before generating on several threads, or to read it with `markov_find()`. Returns **MARKOV_OK**, or **MARKOV_NO_MEM** if memory ran out, which leaves no chain.

`markov_error_t markov_generate(markov_t *m, sof_random_t *r, size_t length, sof_string_t *out)`

Generate _length_ words or characters and append them to _out_, a string the caller initialized, with any allocator. The chain is made first if need be. Each chain of text starts with the context of a start, chosen with a weight of its total (the number of times something followed it), and goes on with a successor of the last n - 1 words or characters, chosen with a weight of its count, until it reaches a context with no successor; then it starts again after a newline. Words are joined by single spaces, and characters appended as they are. If _length_ is less than n - 1, or less than n - 1 are left when a chain starts, only the first of the start's words or characters are used. _r_ is a random number generator the caller initialized, which is advanced; with the same seed and the same text learned, the text is the same on every platform. Returns **MARKOV_OK**; **MARKOV_NO_MATCH** if nothing learned can start a chain (no n words or characters were found in a row), appending nothing; **MARKOV_NO_MEM** if memory ran out, leaving part of the text appended; or **MARKOV_ERROR** if _r_ is **NULL** or not initialized, or _out_ is **NULL**, freed or a `sof_string_const()`. A _length_ of **0** appends nothing and returns **MARKOV_OK**.

`const markov_state_t *markov_find(const markov_t *m, const char *context)`

Return the state for the NUL-terminated _context_: n - 1 words or characters as the counter stores them, words joined by single spaces and letters folded if `fold_case` is set. Returns **NULL** if the context wasn't found, or there is no chain, before `markov_build()` or after something was learned since. The state is good until the counts next change.

`markov_error_t markov_clear(markov_t *m)`

Forget everything learned, and the chain, keeping the options. Returns **MARKOV_OK**.

`markov_error_t markov_free(markov_t *m)`

Free everything the generator allocated. The `markov_t` itself belongs to the caller and isn't freed; it is left empty. Returns **MARKOV_OK**.

Every function accepts a **NULL** _m_: `markov_find()` returns **NULL**, and the others return **MARKOV_ERROR**. `markov_find()` returns **NULL** for a **NULL** _context_. A generator that was freed, or whose `markov_init()` failed, is empty and stays that way: `markov_find()` finds nothing, `markov_free()` returns **MARKOV_OK**, and the others return **MARKOV_ERROR**, until it is initialized again.

## Types

    typedef enum markov_error_t {
        MARKOV_OK = 0,
        MARKOV_NO_MEM,
        MARKOV_NO_MATCH,
        MARKOV_ERROR,
    } markov_error_t;

    typedef struct markov_options_t {
        ngram_unit_t unit;  // NGRAM_CHARS or NGRAM_WORDS
        size_t order;       // n, the gram length: 0 means 2; 1 is an error
        const char *chars;  // the set, as for -c; NULL means MARKOV_WORD_CHARS or MARKOV_CHAR_CHARS
        int fold_case;      // non-zero folds ASCII letters to lowercase
    } markov_options_t;

What `markov_init()` is to learn. A zeroed structure learns pairs of characters from `MARKOV_CHAR_CHARS`. The generator keeps what it needs, so the options, and the string _chars_, can go once `markov_init()` returns. `MARKOV_WORD_CHARS` is `"!-~¡-ÿ—‘-”"`, every printable ASCII character but the space, the Latin-1 letters and signs, the em dash and the curly quotes, and `MARKOV_CHAR_CHARS` is the same with the space.

    typedef struct markov_t {
        ngram_t counts;                 // the n-grams and (n - 1)-grams
        size_t order;                   // n
        sof_hashmap_t index;            // context to state; elements is NULL with no chain
        markov_state_t *states;         // a state per (n - 1)-gram, state_count of them
        size_t state_count;
        markov_successor_t *successors; // every state's successors, successor_count of them
        size_t successor_count;
        markov_state_t **starts;        // the states with a successor, sorted by context
        size_t start_count;
        uint64_t start_total;           // the sum of their totals
        sof_alloc_t alloc;              // copy of the allocator; zeroed for the standard functions
    } markov_t;

The fields can be read but should not be changed by the application. The chain is there from `markov_build()` until something is learned, or the generator is cleared or freed. Its strings point into the counter.

    typedef struct markov_state_t {
        const char *context;        // the n - 1 words or characters
        size_t count;               // how often the context was found
        uint64_t total;             // how often something followed it: the sum of the counts below
        markov_successor_t *next;   // what followed it, sorted by bytes, next_count of them
        size_t next_count;
    } markov_state_t;

    typedef struct markov_successor_t {
        const char *unit;           // the word or character
        size_t count;               // how often it followed the context
    } markov_successor_t;

A state of the chain, and a word or character that followed it. A state's _total_ is less than its _count_ by the times the context ended the text or a run of characters; a state with no successor ends a chain. They belong to the generator, and are good until the counts next change.

## Threading

_markov.c_ has no globals and does no locking, so separate generators can be used on separate threads freely. `markov_find()` only reads the generator, and so does `markov_generate()` once the chain is made, so after `markov_build()` any number of threads can generate from one generator at once, each with a `sof_random_t` and a string of its own, as long as nothing writes to it. `markov_generate()` allocates a string through the generator's allocator while it runs, so an allocator shared that way must be safe to call from several threads; the standard one is. With a readers-writer lock, take the write lock for `markov_add()`, `markov_flush()`, `markov_build()`, `markov_clear()` and `markov_free()`, and for `markov_generate()` unless the chain is made, and the read lock for the rest.
