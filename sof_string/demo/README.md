# spell

A spelling checker in c: finds the words in text that aren't in a dictionary, and suggests the nearest known words by edit distance.

`spell` loads a word list (_../../test-data/words.txt_ by default), reads files or standard input, and prints each word it doesn't know, once, where it first occurs, with the known words nearest to it. A word is a run of ASCII letters, and is known if it is in the dictionary as it is or in lowercase. The nearest words are those the fewest edits away, where an edit deletes a letter, inserts one, replaces one, or swaps two that are next to each other (the Damerau-Levenshtein distance): those one edit away, or if there are none, two. Among words at the same distance, those that occur most often in a text given with `-f` come first.

_main.c_ is the program. The dictionary is kept by _spell.c_, declared in _spell.h_, in a `spell_t` that the program provides and sets up with `spell_init()`. `spell_load()` adds the words of a word list, and `spell_add()` adds one word, or counts it again if it is there. `spell_find()` looks a word up, and `spell_suggest()` finds the words nearest to one, sorted by distance and count. `spell_clear()` empties the dictionary and `spell_free()` releases its memory.

The words are the keys of a hashmap, which the dictionary owns copies of, with the number of times each was added. `spell_suggest()` doesn't compare the word with every word in the dictionary: it makes every string one edit from it, in a buffer, looks each up, and for distance 2 makes every string one edit from each of those. Inserts and replaces use only the bytes found in the dictionary's words. For a word of n letters from a-z, that is about 54n lookups at distance 1 and the square of that at distance 2, which takes some milliseconds whatever the size of the dictionary. The words found go into a second hashmap, which keeps each once with the fewest edits it was found at, and are then sorted.

Words are kept in a hashmap from the [sof_hashmap](../../sof_hashmap/) library in _../../sof_hashmap/_ (_sof_hashmap.c_ and _sof_hashmap.h_), whose keys are hashed by the [sof_hash](../../sof_hash/) library in _../../sof_hash/_ (_sof_hash.c_ and _sof_hash.h_, which use [rapidhash](https://github.com/Nicoshev/rapidhash) in _rapidhash.h_, under its own MIT license). Suggestions are sorted by the [sof_sort](../../sof_sort/) library in _../../sof_sort/_ (_sof_sort.c_ and _sof_sort.h_), and memory is allocated through the [sof_alloc](../../sof_alloc/) library in _../../sof_alloc/_ (_sof_alloc.c_ and _sof_alloc.h_). The program reads its options with the [sof_copt](../../sof_copt/) library in _../../sof_copt/_ (_sof_copt.c_ and _sof_copt.h_), holds text with the [sof_string](../../sof_string/) library in _../../sof_string/_ (_sof_string.c_ and _sof_string.h_), measures time with the [sof_timer](../../sof_timer/) library in _../../sof_timer/_ (_sof_timer.c_ and _sof_timer.h_), and logs with the [sof_log](../../sof_log/) library in _../../sof_log/_ (_sof_log.c_ and _sof_log.h_). Beyond those, it needs nothing but the c standard library.

    spell_t sp;
    spell_init(&sp, NULL);                          // no allocator: the standard functions
    spell_load(&sp, list, length);                  // words separated by white space
    if (spell_find(&sp, "recieve", NULL) == NULL) { // not a word
        spell_suggestion_t s[5];
        size_t found;
        spell_suggest(&sp, "recieve", 2, s, 5, &found); // s[0].word is "receive"
    }
    spell_free(&sp);

## Building

    make            # builds ./spell
    make debug=1    # builds with debug info and without optimization
    ./spell file.txt                                        # checks file.txt against words.txt
    ./spell -f ../../test-data/shakespeare.txt file.txt     # the same, ranking suggestions by use

`make` in the library's directory, _../_, builds it with the library, as do `make sof_string` and `make` from the root of the project.

To build it in another project, copy _main.c_, _spell.c_ and _spell.h_, with _sof_hashmap.c_ and _sof_hashmap.h_ from _../../sof_hashmap/_, _sof_hash.c_, _sof_hash.h_ and _rapidhash.h_ from _../../sof_hash/_, _sof_alloc.c_ and _sof_alloc.h_ from _../../sof_alloc/_, _sof_sort.c_ and _sof_sort.h_ from _../../sof_sort/_, _sof_copt.c_ and _sof_copt.h_ from _../../sof_copt/_, _sof_string.c_ and _sof_string.h_ from _../../sof_string/_, _sof_timer.c_ and _sof_timer.h_ from _../../sof_timer/_, and _sof_log.c_ and _sof_log.h_ from _../../sof_log/_, into it. To use only the dictionary, _spell.c_ and _spell.h_ need just the _sof_hashmap_, _sof_hash_, _sof_sort_ and _sof_alloc_ files.

## Usage

    spell [-h] [-v] [-d dictionary] [-f corpus] [-n suggestions] [-e distance] [-m length] [file]...

- `-h`: print the usage message, with the list `sof_copt_print()` makes, and exit.
- `-v`: log, on standard error, how many words were loaded, checked and unknown, and how long loading, checking and finding suggestions took, measured with _sof_timer_.
- `-d` _dictionary_: the word list, its words separated by white space (default _../../test-data/words.txt_, which holds 263,533 words in lowercase, of two letters or more).
- `-f` _corpus_: a text whose words, those in the dictionary, are counted, so that among suggestions at the same distance, the words used most often in it come first. Without it, they come in byte order. Words are counted in lowercase.
- `-n` _suggestions_: print at most this many suggestions for a word, from 0 to 1000 (default 5).
- `-e` _distance_: suggest words at most this many edits away, from 0 to 2 (default 2). Words two edits away are looked for only when there are none one edit away, since that takes thousands of times as long.
- `-m` _length_: don't check words shorter than this (default 2, since the default dictionary has no words of one letter, such as _a_ and _I_).
- _file_: the text to check. With no _file_, or `-`, it reads standard input.

Each unknown word is printed once, on a line of its own, as the file name, the line it first occurs on, the word as it is in the text, and its suggestions, nearest first. A word that differs only in case from one already printed, such as _Teh_ and _teh_, isn't printed again. It exits with **0**, **1** if a file couldn't be read or memory ran out, or **2** if the options are wrong.

For example, with suggestions ranked by how often Shakespeare uses them:

    $ printf 'Teh quick brown fox jumpd over the lazzy dog.\nI recieve speling misteaks every day.\n' \
        | ./spell -n 3 -f ../../test-data/shakespeare.txt
    standard input:1: Teh: the, ten, te
    standard input:1: jumpd: jump, jumps, jumped
    standard input:1: lazzy: lazy, jazzy, lazzi
    standard input:2: recieve: receive, relieve
    standard input:2: speling: seeling, spelling, speeling
    standard input:2: misteaks: mistake, mistakes, misdeals

and without, with timings:

    $ printf 'Teh quick brown fox jumpd over the lazzy dog.\n' | ./spell -v -n 3
    2026-11-05 05:08:11  spell  79945  main.c:463  INFO   loaded 263533 words from ../../test-data/words.txt in 27.3 ms
    2026-11-05 05:08:11  spell  79945  main.c:497  INFO   checked 9 words in 0.1 ms: 3 unknown, 3 of them different
    2026-11-05 05:08:11  spell  79945  main.c:499  INFO   found suggestions in 0.0 ms, 0.013 ms a word
    standard input:1: Teh: eh, eth, feh
    standard input:1: jumpd: jump, jumped, jumps
    standard input:1: lazzy: jazzy, lazy, lazzi

_misteaks_ is two edits from _mistake_, so finding its suggestions takes about 7 ms on an Apple M-series Mac, where a word with suggestions one edit away takes about 0.01 ms.

## Functions

`spell_error_t spell_init(spell_t *sp, const sof_alloc_t *alloc)`

Initialize the empty dictionary _sp_ the caller provides. _alloc_ is the allocator for all of the dictionary's memory, **NULL** meaning the standard functions (see [sof_alloc](../../sof_alloc/)); it is copied into the dictionary, so it can be on the stack. Returns **SPELL_OK**, **SPELL_NO_MEM** if memory couldn't be allocated, or **SPELL_ERROR** if _sp_ is **NULL** or _alloc_ has a _malloc_ but no _free_ or _free_sized_; an error leaves _sp_ empty.

`spell_error_t spell_load(spell_t *sp, const char *text, size_t length)`

Add every word in the _length_ bytes of _text_, which needn't be NUL-terminated, as `spell_add()` adds it. Words are separated by ASCII white space (space, tab, newline, carriage return, vertical tab and form feed), so a list of a word on each line loads with either line ending. **NULL** _text_ adds nothing. Returns **SPELL_OK**, or **SPELL_NO_MEM** if memory ran out, in which case the words before the one that couldn't be added are in the dictionary.

`spell_error_t spell_add(spell_t *sp, const char *word)`

Add the NUL-terminated _word_ to the dictionary, which keeps a copy, or if it is there, count it again. Returns **SPELL_OK**, **SPELL_NO_MEM** if memory couldn't be allocated, which leaves the dictionary as it was, or **SPELL_ERROR** if _word_ is **NULL** or empty.

`const char *spell_find(const spell_t *sp, const char *word, size_t *count)`

Look up the NUL-terminated _word_, byte for byte, so case matters. Returns the dictionary's copy of the word, good until the dictionary is next written, or **NULL** if it isn't there. If _count_ isn't **NULL**, sets _*count_ to the number of times the word was added, or **0** if it isn't there.

`spell_error_t spell_suggest(const spell_t *sp, const char *word, size_t max_distance, spell_suggestion_t *suggestions, size_t max, size_t *found)`

Find the dictionary's words at most _max_distance_ edits from the NUL-terminated _word_, from **0** to **SPELL_MAX_DISTANCE** (2), by looking up every string that many edits away or fewer: deleting a byte, inserting a byte found in the dictionary's words, replacing a byte with one, or swapping two neighbouring bytes. _word_ itself, if it is in the dictionary, is found at distance **0**. The words found are sorted by distance, then by the number of times they were added, most first, then in byte order, and the first _max_ of them are put in _suggestions_, which has room for _max_; each holds the word, pointing into the dictionary and good until it is next written, its distance and its count. If _found_ isn't **NULL**, _*found_ is set to the number of words found, which can be more than _max_, or **0** on an error. _max_ can be **0**, with **NULL** _suggestions_, to only count them. Returns **SPELL_OK**, **SPELL_NO_MEM** if memory ran out, or **SPELL_ERROR** if _word_ is **NULL**, _max_distance_ is more than **SPELL_MAX_DISTANCE**, or _suggestions_ is **NULL** and _max_ isn't **0**.

`spell_error_t spell_clear(spell_t *sp)`

Remove every word, and forget the bytes and longest length seen, keeping the dictionary ready for more. Returns **SPELL_OK**.

`spell_error_t spell_free(spell_t *sp)`

Free everything the dictionary allocated. The `spell_t` itself belongs to the caller and isn't freed; it is left empty. Returns **SPELL_OK**.

Every function accepts a **NULL** _sp_: `spell_find()` returns **NULL** and the others return **SPELL_ERROR**. `spell_find()` returns **NULL** for a **NULL** _word_. A dictionary that was freed, or whose `spell_init()` failed, is empty and stays that way: `spell_find()` finds nothing, and `spell_add()`, `spell_load()`, `spell_suggest()` and `spell_clear()` return **SPELL_ERROR**, until it is initialized again. `spell_free()` returns **SPELL_OK** for it.

## Types

    typedef enum spell_error_t {
        SPELL_OK = 0,
        SPELL_NO_MEM,
        SPELL_NO_MATCH,
        SPELL_ERROR,
    } spell_error_t;

The error codes. **SPELL_NO_MATCH** isn't returned; a word that isn't found is a **NULL** from `spell_find()` or no suggestions from `spell_suggest()`.

    typedef struct spell_suggestion_t {
        const char *word;   // the word, as the dictionary holds it
        size_t distance;    // the fewest edits from the word looked up
        size_t count;       // the times the word was added
    } spell_suggestion_t;

A word `spell_suggest()` found. _word_ points into the dictionary, and is good until the dictionary is next written.

    typedef struct spell_t {
        sof_hashmap_t words;            // the words, with their counts
        unsigned char letters[256];     // non-zero for each byte in some word
        unsigned char alphabet[256];    // those bytes, in the order first seen
        size_t alphabet_length;
        size_t longest;                 // the length of the longest word
        sof_alloc_t alloc;              // copy of the allocator; zeroed for the standard functions
    } spell_t;

The fields can be read but should not be changed by the application. The keys of _words_ are the words, copies the dictionary owns, and the data of each element is its count, held in the pointer itself. The bytes in _alphabet_ are those `spell_suggest()` inserts and replaces, and it doesn't look up strings longer than _longest_.

## Threading

_spell.c_ has no globals and does no locking, so separate dictionaries can be used on separate threads freely. `spell_find()` and `spell_suggest()` only read the dictionary, so any number of threads can call them at once as long as nothing writes to it; `spell_suggest()` allocates its working memory through the dictionary's allocator, which must then be safe to call from several threads, as the standard one is. `spell_add()`, `spell_load()`, `spell_clear()`, `spell_init()` and `spell_free()` write, and need the dictionary to themselves. With a readers-writer lock, take the write lock for those and the read lock for the others, and don't keep a word `spell_find()` or `spell_suggest()` returned once the write lock has been taken.

The program checks its text on one thread.
