# anagram

An anagram finder in c: the groups of words in a word list that are anagrams of each other, and the anagrams of any given word.

`anagram` reads the words of files or standard input, gathers them into groups of words made of the same characters in any order, such as _listen_ and _silent_, and prints the largest groups first. With `-w`, it prints the anagrams of each word asked for instead.

_main.c_ is the program. The grouping is done by _anagram.c_, declared in _anagram.h_, which keeps an `anagram_t` that the program provides and sets up with `anagram_init()`. `anagram_add()` adds a word to the group of its anagrams, making the group if it is the first. `anagram_find()` finds the group a word belongs in, whether or not the word is in it, and `anagram_key()` makes the key a group is found by. `anagram_sort()` puts the groups in order, largest first, and the words in each group in order, and `anagram_next()` and `anagram_traverse()` visit the groups in that order. `anagram_clear()` removes every word, and `anagram_free()` releases the collection's memory.

A group's key is its words' characters, sorted. It is made in a [sof_string](../../sof_string/): the word, with its ASCII letters folded to lowercase by `sof_string_tolower()` if the collection folds case, is cut into characters, each a lead byte and the UTF-8 continuation bytes after it, and each is packed into a `uint32_t` with its bytes from the top down, so that comparing two compares their bytes. The characters are sorted with `sof_sort_quick()` and put back together, so _listen_ gives _eilnst_ and _été_ gives _téé_, with the bytes of each _é_ kept together. A [sof_hashmap](../../sof_hashmap/) with string keys maps each key to its group. A key of up to 64 characters is sorted on the stack; a longer one in memory from the collection's allocator.

Groups are kept in a hashmap from the [sof_hashmap](../../sof_hashmap/) library in _../../sof_hashmap/_ (_sof_hashmap.c_ and _sof_hashmap.h_), whose keys are hashed by the [sof_hash](../../sof_hash/) library in _../../sof_hash/_ (_sof_hash.c_ and _sof_hash.h_, which use [rapidhash](https://github.com/Nicoshev/rapidhash) in _rapidhash.h_, under its own MIT license). Keys are made in strings from the [sof_string](../../sof_string/) library in _../../sof_string/_ (_sof_string.c_ and _sof_string.h_), and sorted, as are the words and groups, by the [sof_sort](../../sof_sort/) library in _../../sof_sort/_ (_sof_sort.c_ and _sof_sort.h_). Memory is allocated through the [sof_alloc](../../sof_alloc/) library in _../../sof_alloc/_ (_sof_alloc.c_ and _sof_alloc.h_). The program reads its options with the [sof_copt](../../sof_copt/) library in _../../sof_copt/_ (_sof_copt.c_ and _sof_copt.h_). Beyond those, it needs nothing but the c standard library.

    anagram_t an;
    anagram_init(&an, 1, NULL);                             // fold case; the standard allocator
    anagram_add(&an, "listen");                             // for each word in the list
    anagram_add(&an, "Silent");
    const anagram_group_t *g = anagram_find(&an, "tinsel"); // listen and Silent
    anagram_sort(&an);                                      // largest groups first
    anagram_free(&an);

## Building

    make            # builds ./anagram
    make debug=1    # builds with debug info and without optimization
    ./anagram ../../test-data/words.txt               # the largest groups of anagrams
    ./anagram -w listen ../../test-data/words.txt     # the anagrams of listen

`make` in the library's directory, _../_, builds it with the library, as do `make sof_sort` and `make` from the root of the project.

To build it in another project, copy _main.c_, _anagram.c_ and _anagram.h_, with _sof_hashmap.c_ and _sof_hashmap.h_ from _../../sof_hashmap/_, _sof_hash.c_, _sof_hash.h_ and _rapidhash.h_ from _../../sof_hash/_, _sof_string.c_ and _sof_string.h_ from _../../sof_string/_, _sof_sort.c_ and _sof_sort.h_ from _../../sof_sort/_, _sof_alloc.c_ and _sof_alloc.h_ from _../../sof_alloc/_, and _sof_copt.c_ and _sof_copt.h_ from _../../sof_copt/_, into it. To use only the collection, _anagram.c_ and _anagram.h_ need all of those but the _sof_copt_ files.

## Usage

    anagram [-h] [-i] [-m min] [-t top] [-w word]... [file]...

- `-h`: print the usage message, with the list `sof_copt_print()` makes, and exit.
- `-i`: ignore case: ASCII letters are folded to lowercase in keys, so _Listen_ and _silent_ are anagrams. Words are printed as they were read, and _Listen_ and _listen_ are two words of a group. Letters outside ASCII aren't folded.
- `-m` _min_: print only groups of at least _min_ words, 1 or more (default 2).
- `-t` _top_: print the _top_ largest groups (default 10; 0 prints all of them). Groups of the same size are printed in the order of their first words, and the words of a group in byte order, which for UTF-8 is code point order.
- `-w` _word_: print _word_ and its anagrams in the list, leaving out _word_ itself (in any case, with `-i`), or _none_. _word_ need not be in the list. Can be given more than once; the groups aren't printed then, and `-m` and `-t` have no effect.
- _file_: the word list. With no _file_, or `-`, it reads standard input. With several, their words make one list. A word is a run of bytes that aren't white space, so the list can have one word to a line or many; a word given twice is counted once. A NUL byte separates words, as white space does.

It exits with **0**, **1** if a file couldn't be read or memory ran out, or **2** if the options are wrong.

Words are read as UTF-8, and a character is a lead byte and the continuation bytes after it, so two words are anagrams if they are made of the same characters. Bytes that aren't valid UTF-8 are still grouped consistently: a stray continuation byte is a character of its own.

For example, the largest groups in the word list in _../../test-data/_ (see _../../test-data/README.md_):

    $ ./anagram -t 5 ../../test-data/words.txt
    words: 263533, groups: 234999, of 2 or more words: 20639
    13  alerts alters artels estral laster rastle ratels salter slater staler stelar talers tarsel
    13  apers apres asper pares parse pears prase presa rapes reaps spaer spare spear
    13  arets aster earst rates reast resat stare stear strae tares taser tears teras
    12  least leats salet setal slate stale steal stela taels tales teals tesla
    11  anestri antsier nastier ratines resiant retains retinas retsina stainer starnie stearin

how many groups have 9 words or more:

    $ ./anagram -m 9 -t 1 ../../test-data/words.txt
    words: 263533, groups: 234999, of 9 or more words: 29
    13  alerts alters artels estral laster rastle ratels salter slater staler stelar talers tarsel

and the anagrams of a few words, ignoring case:

    $ ./anagram -i -w listen -w Organ -w xyzzy ../../test-data/words.txt
    listen: elints enlist inlets silent tinsel
    Organ: argon groan nagor orang
    xyzzy: none

## Functions

`anagram_error_t anagram_init(anagram_t *an, int fold_case, const sof_alloc_t *alloc)`

Initialize the collection _an_ the caller provides. If _fold_case_ is non-zero, ASCII letters are folded to lowercase in keys, so _Listen_ and _silent_ are anagrams; words are kept as they were given. _alloc_ is the allocator for all of the collection's memory, **NULL** meaning the standard functions (see [sof_alloc](../../sof_alloc/)); it is copied into the collection, so it can be on the stack. Returns **ANAGRAM_OK**, **ANAGRAM_NO_MEM** if memory couldn't be allocated, or **ANAGRAM_ERROR** if _an_ is **NULL** or _alloc_ has a _malloc_ but no _free_ or _free_sized_; an error leaves _an_ empty.

`anagram_error_t anagram_add(anagram_t *an, const char *word)`

Add a copy of the NUL-terminated UTF-8 _word_ to the group of its anagrams, making a new group if no word added before has the same key. The groups, and the words in each, stay in the order they were added until `anagram_sort()` is called again. Returns **ANAGRAM_OK**; **ANAGRAM_NO_MATCH** if the word is already in its group, which is left as it was; **ANAGRAM_NO_MEM** if memory ran out, which leaves the collection as it was; or **ANAGRAM_ERROR** if _word_ is **NULL** or empty.

`const anagram_group_t *anagram_find(const anagram_t *an, const char *word)`

Return the group of the anagrams of the NUL-terminated _word_: the group it would be added to, which may hold _word_ itself. _word_ need not be in the collection. Returns **NULL** if no word added has the same key, _word_ is **NULL**, or memory for its key couldn't be allocated through the collection's allocator.

`anagram_error_t anagram_key(const anagram_t *an, const char *word, sof_string_t *key)`

Make the key of the NUL-terminated _word_ in _key_, a string the caller initialized (see [sof_string](../../sof_string/)), which is emptied first and grows with its own allocator. The key is the word's characters sorted by their bytes, which for UTF-8 is code point order, after folding its ASCII letters to lowercase if the collection folds case: _listen_ gives _eilnst_. This is how `anagram_add()` and `anagram_find()` make keys. Returns **ANAGRAM_OK**; **ANAGRAM_NO_MEM** if memory couldn't be allocated, through the collection's allocator for a word of more than 64 characters or the key's for the key, leaving _key_ with some other content; or **ANAGRAM_ERROR** if _word_ or _key_ is **NULL**, or _key_ is dead or a `sof_string_const()`.

`anagram_error_t anagram_sort(anagram_t *an)`

Put the groups in order, those with the most words first and those of the same size by their first word, and the words of each group in byte order, which for UTF-8 is code point order. Nothing is allocated. Returns **ANAGRAM_OK**.

`const anagram_group_t *anagram_next(const anagram_t *an, size_t *position)`

Return the next group, or **NULL** when there are no more. Set _*position_ to **0** to start; each call updates it. The groups come in the order `anagram_sort()` put them in, with any made since after them, and the loop must not call `anagram_add()`, `anagram_sort()` or `anagram_clear()`.

    size_t position = 0;
    const anagram_group_t *group;
    while ((group = anagram_next(&an, &position)) != NULL) {
        ...
    }

`int anagram_traverse(const anagram_t *an, anagram_traverse_func_t cb, void *ctx)`

Call _cb_ with each group and _ctx_, in the order `anagram_next()` gives them. If _cb_ returns a non-zero value, the traversal stops and returns that value. Otherwise returns **0** once every group has been visited (at once, if _cb_ is **NULL**).

`anagram_error_t anagram_clear(anagram_t *an)`

Remove every word and group, keeping the collection's options. Group pointers aren't good afterwards. Returns **ANAGRAM_OK**.

`anagram_error_t anagram_free(anagram_t *an)`

Free everything the collection allocated. The `anagram_t` itself belongs to the caller and isn't freed; it is left empty. Group pointers aren't good afterwards. Returns **ANAGRAM_OK**.

Every function accepts a **NULL** _an_: `anagram_find()` and `anagram_next()` return **NULL**, `anagram_traverse()` returns **-1**, and the others return **ANAGRAM_ERROR**. `anagram_next()` returns **NULL** for a **NULL** _position_. A collection that was freed, or whose `anagram_init()` failed, is empty and stays that way: it finds nothing, `anagram_traverse()` returns **-1**, and `anagram_add()`, `anagram_key()`, `anagram_sort()` and `anagram_clear()` return **ANAGRAM_ERROR**, until it is initialized again. `anagram_free()` returns **ANAGRAM_OK** for it.

## Types

    typedef enum anagram_error_t {
        ANAGRAM_OK = 0,
        ANAGRAM_NO_MEM,
        ANAGRAM_NO_MATCH,
        ANAGRAM_ERROR,
    } anagram_error_t;

    typedef struct anagram_group_t {
        const char *key;    // the words' characters, sorted; owned by the hashmap
        char **words;       // the words, each NUL-terminated
        size_t count;       // how many words there are
        size_t capacity;    // how many the array has room for
    } anagram_group_t;

A group of words that are anagrams of each other. No two of its words are the same, and it always has at least one. The fields can be read but should not be changed by the application. A group stays at the same address until `anagram_clear()` or `anagram_free()`, but its _words_ array moves when a word is added to it.

    typedef struct anagram_t {
        int fold_case;              // non-zero if ASCII letters are folded in keys
        sof_hashmap_t map;          // key to group, in each element's data
        anagram_group_t **groups;   // the groups, in order once sorted
        size_t count;               // how many groups there are
        size_t capacity;            // how many the array has room for
        size_t words;               // how many words there are in all
        sof_string_t key;           // where anagram_add() makes keys
        sof_alloc_t alloc;          // copy of the allocator; zeroed for the standard functions
    } anagram_t;

The fields can be read but should not be changed by the application. _map.elements_ is **NULL** for a collection that was freed or whose `anagram_init()` failed.

    typedef int (*anagram_traverse_func_t)(const anagram_group_t *group, void *ctx)

Called for each group visited by `anagram_traverse()`, with the _ctx_ given to it. Return **0** to continue, or a non-zero value to stop the traversal; that value is returned by `anagram_traverse()`. It must not call `anagram_add()`, `anagram_sort()`, `anagram_clear()` or `anagram_free()`.

## Threading

_anagram.c_ has no globals and does no locking, so separate collections can be used on separate threads freely. `anagram_find()`, `anagram_key()`, `anagram_next()` and `anagram_traverse()` only read the collection, so any number of threads can call them at once as long as nothing writes to it; `anagram_add()`, `anagram_sort()`, `anagram_clear()` and `anagram_free()` write, and need the collection to themselves. `anagram_find()` always allocates its key, and `anagram_key()` allocates for a word of more than 64 characters, through the collection's allocator, so an allocator shared that way must be safe to call from several threads. With a readers-writer lock, take the write lock for the functions that write and the read lock for the rest. `anagram_key()` writes the _key_ it is given, so each thread needs its own.

The program is single-threaded.
