/**
 * @file sof_toml.h
 * @author Warren Mann (warren@nonvol.io)
 * @brief TOML in C: a document tree and a streaming writer.
 * @details The library's types and functions share the sof_toml_ prefix
 * and one error enum, sof_toml_error_t. It reads and writes TOML 1.1.0, as
 * https://toml.io/en/v1.1.0 defines it, and has two parts.
 *
 * The document, sof_toml_doc_t, parses a TOML text into a tree of
 * sof_toml_value_t, kept in an arena: the root table, its key/value pairs,
 * and the tables and arrays of tables its headers define, with each table's
 * members in the order they were defined and, for tables with more than
 * SOF_TOML_INDEX_MIN members, an index that finds a member by its key in
 * constant time. It checks the text against the specification as it goes,
 * keys and tables defined twice included, and says where the first error is.
 * The sof_toml_value_ functions read the tree, and
 * sof_toml_writer_value() writes it out again.
 *
 * The writer, sof_toml_writer_t, writes TOML one piece at a time: the
 * application gives each key, and writes strings, integers, floats,
 * booleans and dates and times, begins and ends tables and arrays, and the
 * writer adds the table headers, equals signs, quotes, escapes, commas and
 * line breaks. A table that is the value of a key in a table written as a
 * section of its own becomes a [table] section, and an array of tables a
 * series of [[array]] sections; inside an array or an inline table, they
 * are written inline. It keeps track of what is open, so a piece in the
 * wrong place, such as a key/value pair after a table's sub-tables, which
 * TOML has no way to write, is refused, and what it writes is always valid
 * TOML so far.
 *
 * Output goes into a buffer, a sof_string_t, which the application can
 * read, and, if it gives a write function, is passed to that function
 * whenever the buffer passes SOF_TOML_FLUSH_SIZE bytes, and by
 * sof_toml_writer_flush().
 *
 * Each value, and each member's key, has a span: where it is in the text
 * the document was parsed from, as a byte offset and length, so an
 * application holding the text can say which line a value is on, with
 * sof_string_position(), or change the text by splicing it at those offsets,
 * keeping its comments and layout, where writing the document out again
 * would lose them.
 *
 * TOML has no streaming reader here, unlike sof_json: a table can be
 * defined in pieces anywhere in a document, and whether a key or table is
 * defined twice can't be known until the whole document has been read, so
 * a TOML text is read whole, into a document.
 * @version 0.0.1
 * @date 2026-11-19
 * @copyright Copyright (c) 2026
 */

#ifndef SOF_TOML_H
#define SOF_TOML_H

#include <stddef.h>
#include <stdint.h>

#include "sof_alloc.h"
#include "sof_hashmap.h"
#include "sof_mem.h"
#include "sof_stack.h"
#include "sof_string.h"

/// @brief The size the buffer can reach before a writer with a write
/// function passes it on.
#define SOF_TOML_FLUSH_SIZE 4096

/// @brief The least sof_toml_doc_read() asks the read function for: it
/// asks for all the room the text read so far has, once there is room for
/// this much more.
#define SOF_TOML_READ_SIZE 4096

/// @brief The deepest arrays and inline tables can nest in what a document
/// parses; deeper input is refused with SOF_TOML_TOO_DEEP. The parser
/// recurses into them, so the limit keeps it within the stack.
#define SOF_TOML_DEPTH_MAX 256

/// @brief A document's tables with more members than this are indexed, so
/// sof_toml_value_get() finds a key without looking at every member.
#define SOF_TOML_INDEX_MIN 8

/// @brief The bytes sof_toml_datetime_format() needs at most, its NUL
/// included.
#define SOF_TOML_DATETIME_SIZE 40

/**
 * @brief Flags for sof_toml_writer_init(), which can be combined with |.
 */
typedef enum sof_toml_flag_t {
    /// @brief Write every character outside ASCII in strings and keys as a
    /// \u or \U escape, so the output is ASCII.
    SOF_TOML_ASCII = 1,
} sof_toml_flag_t;

/**
 * @brief Error codes for the sof_toml functions.
 */
typedef enum sof_toml_error_t {
    SOF_TOML_OK = 0,
    SOF_TOML_NO_MEM,
    SOF_TOML_NO_MATCH,
    SOF_TOML_ERROR,
    /// @brief The write function failed. What was written is kept in the
    /// buffer.
    SOF_TOML_WRITE,
    /// @brief The read function failed.
    SOF_TOML_READ,
    /// @brief The input isn't TOML.
    SOF_TOML_SYNTAX,
    /// @brief The input defines a key or table twice, or adds to one in a
    /// way TOML doesn't allow, such as a key in an inline table after it
    /// ends.
    SOF_TOML_DUPLICATE,
    /// @brief Arrays and inline tables in the input nest deeper than
    /// SOF_TOML_DEPTH_MAX.
    SOF_TOML_TOO_DEEP,
    /// @brief An integer in the input doesn't fit an int64_t, or a float is
    /// too large for a double.
    SOF_TOML_RANGE,
} sof_toml_error_t;

/**
 * @brief The types of a document's values.
 */
typedef enum sof_toml_type_t {
    /// @brief No value.
    SOF_TOML_NONE = 0,
    SOF_TOML_STRING,
    /// @brief A signed 64-bit integer.
    SOF_TOML_INTEGER,
    /// @brief A double.
    SOF_TOML_FLOAT,
    /// @brief true or false.
    SOF_TOML_BOOL,
    /// @brief A date and time with an offset from UTC: 1979-05-27T07:32:00Z.
    SOF_TOML_DATETIME,
    /// @brief A date and time without an offset: 1979-05-27T07:32:00.
    SOF_TOML_DATETIME_LOCAL,
    /// @brief A date: 1979-05-27.
    SOF_TOML_DATE_LOCAL,
    /// @brief A time of day: 07:32:00.
    SOF_TOML_TIME_LOCAL,
    SOF_TOML_ARRAY,
    SOF_TOML_TABLE,
} sof_toml_type_t;

/**
 * @brief A date, a time, or both, with or without an offset.
 * @details Which fields mean something depends on the type the date and
 * time has: a SOF_TOML_DATE_LOCAL has only the date, a SOF_TOML_TIME_LOCAL
 * only the time, and only a SOF_TOML_DATETIME has an offset; the others
 * are 0.
 */
typedef struct sof_toml_datetime_t {
    /// @brief The year, 0 to 9999.
    int year;
    /// @brief The month, 1 to 12.
    int month;
    /// @brief The day of the month, 1 to 28, 29, 30 or 31.
    int day;
    /// @brief The hour, 0 to 23.
    int hour;
    /// @brief The minute, 0 to 59.
    int minute;
    /// @brief The second, 0 to 60 (60 for a leap second). 0 if the text left
    /// the seconds out, as TOML 1.1 allows.
    int second;
    /// @brief The fraction of the second, in nanoseconds, 0 to 999999999.
    /// Digits past the ninth are dropped, as TOML says, not rounded.
    uint32_t nanosecond;
    /// @brief The offset from UTC in minutes, east positive, -1439 to 1439:
    /// 0 for Z.
    int offset;
} sof_toml_datetime_t;

/**
 * @brief Gives the document input.
 * @details Called with a buffer of size bytes and the ctx given to
 * sof_toml_doc_read(). It puts up to size bytes in buffer and their number
 * in *length, which is 0 at the end of the input. It may return fewer bytes
 * than size, as a pipe or socket does. It must not call into the document.
 * @return 0, or non-zero if the input couldn't be read, which
 * sof_toml_doc_read() returns as SOF_TOML_READ.
 */
typedef int (*sof_toml_read_func_t)(char *buffer, size_t size, size_t *length, void *ctx);

/**
 * @brief Receives output from the writer.
 * @details Called with the length bytes of output at data, which aren't
 * NUL-terminated and belong to the writer, and the ctx given to
 * sof_toml_writer_init(). It must not call into the writer. Return 0 if
 * every byte was written, or non-zero if they couldn't be, in which case the
 * writer keeps them all in its buffer.
 */
typedef int (*sof_toml_write_func_t)(const char *data, size_t length, void *ctx);

/**
 * @brief Where something is in the text a document was parsed from.
 * @details The README's Spans section says what each kind of value's span
 * covers.
 */
typedef struct sof_toml_span_t {
    /// @brief Its first byte's offset from the start of the text, a byte
    /// order mark included.
    size_t offset;
    /// @brief Its size in bytes: 0 for a table with no text of its own.
    size_t length;
} sof_toml_span_t;

/**
 * @brief A value in a document.
 * @details Values belong to their document, and are good until it parses
 * again, is cleared or is freed. The application may read the fields, or use
 * the sof_toml_value_ functions, which also take NULL, but must not change
 * them.
 */
typedef struct sof_toml_value_t {
    /// @brief The value's type, SOF_TOML_STRING to SOF_TOML_TABLE.
    sof_toml_type_t type;
    /// @brief The bytes of a string, not counting the NUL, the elements of
    /// an array, or the members of a table; 0 otherwise.
    size_t length;
    /// @brief For a table or array, 1 if the text wrote it inline: a table as
    /// {...}, rather than under a [table] header or with dotted keys, and an
    /// array as [...], rather than as [[array]] sections. 0 otherwise.
    /// sof_toml_writer_value() writes it the same way.
    int is_inline;
    /// @brief Where the value is in the text: a scalar's text as written,
    /// quotes included; an inline array or table from its [ or { to its ]
    /// or }; a table with a header, from the header's [ to the end of the
    /// last value of a key/value pair under it, or the header alone if it
    /// has none; the root table, the same from the start of the text; an
    /// array of tables, from its first element's header to the end of its
    /// last element. A table made by a dotted key, or named on the way to
    /// another in a header, and not defined by a header of its own, has
    /// length 0, at the key that first named it.
    sof_toml_span_t span;
    union {
        /// @brief For SOF_TOML_BOOL, 1 for true or 0 for false.
        int boolean;
        /// @brief For SOF_TOML_INTEGER, the integer.
        int64_t integer;
        /// @brief For SOF_TOML_FLOAT, the number.
        double number;
        /// @brief For a string, its bytes decoded, UTF-8 that can hold NULs,
        /// NUL-terminated.
        const char *text;
        /// @brief For the four date and time types, the date and time.
        sof_toml_datetime_t datetime;
        /// @brief For an array, its length elements, or NULL if it has none.
        const struct sof_toml_value_t *const *elements;
        /// @brief For a table, its members and index.
        struct {
            /// @brief The length members, in the order they were defined, or
            /// NULL if there are none.
            const struct sof_toml_member_t *members;
            /// @brief The document's index of members by key, if this table
            /// has more than SOF_TOML_INDEX_MIN of them, or NULL.
            const sof_hashmap_t *index;
        } table;
    } as;
} sof_toml_value_t;

/**
 * @brief A member of a table in a document: a key and its value.
 * @details As a value, it belongs to the document, and can be read but not
 * changed.
 */
typedef struct sof_toml_member_t {
    /// @brief The key, decoded: UTF-8, which can hold NULs, NUL-terminated.
    /// A dotted key or table header gives each of its parts to a table of
    /// its own, so a key here is one part.
    const char *key;
    /// @brief The bytes in key, not counting the NUL.
    size_t key_length;
    /// @brief Where the key is in the text, as written, quotes included.
    /// For a dotted key or table header, the part that is this key.
    sof_toml_span_t key_span;
    /// @brief The value.
    const sof_toml_value_t *value;
    /// @brief The table the member is in.
    const sof_toml_value_t *table;
} sof_toml_member_t;

/**
 * @brief A TOML document: a text parsed whole, as a tree.
 * @details The application provides the sof_toml_doc_t (on the stack, in
 * another structure or on the heap) and sets it up with sof_toml_doc_init().
 * The fields can be read but must not be changed.
 *
 * Every function accepts a NULL document, and one that was freed or whose
 * sof_toml_doc_init() failed (ready is 0), which holds nothing.
 */
typedef struct sof_toml_doc_t {
    /// @brief The root table, or NULL if nothing has been parsed.
    const sof_toml_value_t *root;
    /// @brief The arena the values, members, keys and strings are in.
    sof_mem_arena_t arena;
    /// @brief The index of the members of tables with more than
    /// SOF_TOML_INDEX_MIN of them, in the arena, or NULL if there are
    /// none.
    sof_hashmap_t *index;
    /// @brief While a text is parsed, the decoded string or key being read.
    sof_string_t text;
    /// @brief The memory functions the arena and the rest are allocated
    /// with.
    sof_alloc_t alloc;
    /// @brief After SOF_TOML_SYNTAX, SOF_TOML_DUPLICATE, SOF_TOML_TOO_DEEP
    /// or SOF_TOML_RANGE, what is wrong, as a sentence without a full stop,
    /// such as "a key is defined twice"; otherwise NULL. It is a string
    /// constant.
    const char *message;
    /// @brief After such an error, the line it is on, counting from 1;
    /// otherwise 0.
    size_t line;
    /// @brief After such an error, its column, in bytes and counting from 1;
    /// otherwise 0.
    size_t column;
    /// @brief After such an error, its offset in bytes from the start of the
    /// text; otherwise 0.
    size_t offset;
    /// @brief 1 once sof_toml_doc_init() has succeeded, 0 when freed or
    /// failed.
    int ready;
} sof_toml_doc_t;

/**
 * @brief A TOML writer.
 * @details The application provides the sof_toml_writer_t (on the stack, in
 * another structure or on the heap) and sets it up with
 * sof_toml_writer_init(). The fields can be read but must not be changed.
 * out.data moves as the buffer grows, so a pointer into it is good only
 * until the next call that writes.
 *
 * Every function accepts a NULL writer, and one that was freed or whose
 * sof_toml_writer_init() failed (ready is 0), which writes nothing.
 */
typedef struct sof_toml_writer_t {
    /// @brief The output not yet passed to the write function: all of it,
    /// if there is none.
    sof_string_t out;
    /// @brief What is open, outermost first: the document's root table, then
    /// each table, array and inline table begun and not yet ended.
    sof_stack_t open;
    /// @brief The keys of the tables written as sections that are open, as
    /// a TOML dotted key, for their headers.
    sof_string_t path;
    /// @brief The key waiting for its value, as TOML writes it: bare, or
    /// quoted and escaped.
    sof_string_t key;
    /// @brief 1 while a key is waiting for its value.
    int has_key;
    /// @brief While sof_toml_writer_value() writes, 1, so output isn't
    /// passed on until the whole value is written.
    int holding;
    /// @brief The sof_toml_flag_t flags.
    unsigned flags;
    /// @brief The function output is passed to, or NULL.
    sof_toml_write_func_t write;
    /// @brief Application context passed to write.
    void *ctx;
    /// @brief The bytes passed to write so far.
    size_t written;
    /// @brief 1 once sof_toml_writer_init() has succeeded, 0 when freed or
    /// failed.
    int ready;
} sof_toml_writer_t;

/**
 * @brief Formats a date and time as TOML writes it, which is RFC 3339's
 * form.
 * @details The date is written as 1979-05-27, the time as 07:32:00, with a
 * fraction of the second, without trailing zeros, if it has one, as in
 * 07:32:00.5, and the offset as Z or as +05:30; a date and time have a T
 * between them.
 * @param type SOF_TOML_DATETIME, SOF_TOML_DATETIME_LOCAL,
 * SOF_TOML_DATE_LOCAL or SOF_TOML_TIME_LOCAL: which fields of dt are
 * written.
 * @param dt The date and time.
 * @param buffer Where the text goes, followed by a NUL.
 * @param size The bytes buffer has room for: SOF_TOML_DATETIME_SIZE is
 * always enough.
 * @return The length of the text, not counting the NUL, or 0, writing
 * nothing, if type isn't one of the four, a field of dt is out of its range
 * (a day past the end of its month included), dt or buffer is NULL, or the
 * text doesn't fit.
 */
extern size_t sof_toml_datetime_format(sof_toml_type_t type, const sof_toml_datetime_t *dt, char *buffer, size_t size);

/**
 * @brief Parses a date and time as TOML writes them.
 * @details The text is an offset date-time, a local date-time, a local date
 * or a local time, as TOML 1.1 defines them, and nothing else: the T between
 * the date and time can be a t or a space, Z can be z, the seconds can be
 * left out, and a fraction of the second can have any number of digits, of
 * which the first nine are kept.
 * @param text The text. It needn't be NUL-terminated.
 * @param length Its size in bytes.
 * @param type Where the type goes: SOF_TOML_DATETIME,
 * SOF_TOML_DATETIME_LOCAL, SOF_TOML_DATE_LOCAL or SOF_TOML_TIME_LOCAL.
 * @param dt Where the date and time go, with the fields the type doesn't use
 * 0.
 * @return SOF_TOML_OK; SOF_TOML_SYNTAX if the text isn't a date or time,
 * or a field is out of its range, such as February 30; or SOF_TOML_ERROR if
 * text, type or dt is NULL. On failure type and dt are left alone.
 */
extern sof_toml_error_t sof_toml_datetime_parse(const char *text, size_t length, sof_toml_type_t *type, sof_toml_datetime_t *dt);

/**
 * @brief Empties a document.
 * @details The values it held are gone; root is NULL. The memory is kept
 * for the next text.
 * @param d The document.
 * @return SOF_TOML_OK, or SOF_TOML_ERROR if d is NULL or not initialized.
 */
extern sof_toml_error_t sof_toml_doc_clear(sof_toml_doc_t *d);

/**
 * @brief Releases a document's memory.
 * @details The sof_toml_doc_t itself isn't freed (it belongs to the
 * caller); it is left empty, and acts like a document whose
 * sof_toml_doc_init() failed until it is initialized again.
 * @param d The document.
 * @return SOF_TOML_OK, also for a document not initialized, or
 * SOF_TOML_ERROR if d is NULL.
 */
extern sof_toml_error_t sof_toml_doc_free(sof_toml_doc_t *d);

/**
 * @brief Initializes an empty document in a sof_toml_doc_t the caller
 * provides.
 * @details Nothing is allocated until a text is parsed.
 * @param d The document to initialize.
 * @param alloc The memory functions for the document, or NULL for the
 * standard functions. The document keeps a copy, so it can be on the stack.
 * @return SOF_TOML_OK, or SOF_TOML_ERROR if d is NULL or alloc has a
 * malloc but no free. On failure the document is left empty and not
 * initialized.
 */
extern sof_toml_error_t sof_toml_doc_init(sof_toml_doc_t *d, const sof_alloc_t *alloc);

/**
 * @brief Parses a TOML text into a document.
 * @details The text must be a TOML 1.1.0 document, in UTF-8; a byte order
 * mark at its start is skipped. An empty text is an empty table. What the
 * document held before is replaced.
 * @param d The document.
 * @param text The text. It can be NULL if length is 0.
 * @param length Its size in bytes.
 * @return SOF_TOML_OK, with the root table in root; SOF_TOML_NO_MEM;
 * SOF_TOML_SYNTAX if the text isn't TOML, SOF_TOML_DUPLICATE if it defines
 * a key or table twice, SOF_TOML_TOO_DEEP if it nests too deep, or
 * SOF_TOML_RANGE if a number is too large, each with what and where in
 * message, line, column and offset; or SOF_TOML_ERROR if d is NULL or not
 * initialized, or text is NULL and length isn't 0. On failure the document
 * is empty.
 */
extern sof_toml_error_t sof_toml_doc_parse(sof_toml_doc_t *d, const void *text, size_t length);

/**
 * @brief Reads a TOML text through a read function and parses it into a
 * document.
 * @details Reads to the end of the input, at least SOF_TOML_READ_SIZE
 * bytes at a time, straight into the string that holds it, then parses it
 * as sof_toml_doc_parse() does. The text is kept only
 * while it is parsed, so an application that will use the values' spans
 * reads the text itself and calls sof_toml_doc_parse().
 * @param d The document.
 * @param read The function input comes from, such as sof_toml_read_file().
 * @param ctx The context pointer passed to read, such as a FILE *.
 * @return As sof_toml_doc_parse(), and SOF_TOML_READ if the read function
 * failed, or SOF_TOML_ERROR if read is NULL.
 */
extern sof_toml_error_t sof_toml_doc_read(sof_toml_doc_t *d, sof_toml_read_func_t read, void *ctx);

/**
 * @brief A read function for a FILE *.
 * @details Pass it to sof_toml_doc_read() with the FILE * as ctx. It reads
 * with fread(), and never closes the file.
 * @return 0, or 1 if the file couldn't be read or ctx is NULL.
 */
extern int sof_toml_read_file(char *buffer, size_t size, size_t *length, void *ctx);

/**
 * @brief Returns an element of an array.
 * @param array The array.
 * @param index The element's index, counting from 0.
 * @return The element, or NULL if array is NULL or not an array, or index
 * is past its end.
 */
extern const sof_toml_value_t *sof_toml_value_at(const sof_toml_value_t *array, size_t index);

/**
 * @brief Gives a boolean's value.
 * @param v The value.
 * @param value Where 1 for true or 0 for false goes.
 * @return SOF_TOML_OK, SOF_TOML_NO_MATCH if v is NULL or not a boolean,
 * or SOF_TOML_ERROR if value is NULL.
 */
extern sof_toml_error_t sof_toml_value_bool(const sof_toml_value_t *v, int *value);

/**
 * @brief Gives the number of elements in an array or members in a table.
 * @param v The array or table.
 * @return The number, or 0 if v is NULL or neither.
 */
extern size_t sof_toml_value_count(const sof_toml_value_t *v);

/**
 * @brief Gives a date and time's value.
 * @param v The value, of any of the four date and time types.
 * @param value Where the date and time go; sof_toml_value_type() says which
 * of its fields mean something.
 * @return SOF_TOML_OK, SOF_TOML_NO_MATCH if v is NULL or not a date or
 * time, or SOF_TOML_ERROR if value is NULL.
 */
extern sof_toml_error_t sof_toml_value_datetime(const sof_toml_value_t *v, sof_toml_datetime_t *value);

/**
 * @brief Gives a number's value as a double.
 * @details A float is given as it is, and an integer converted, to the
 * nearest double if it has more than 53 bits.
 * @param v The value.
 * @param value Where the double goes.
 * @return SOF_TOML_OK, SOF_TOML_NO_MATCH if v is NULL or neither a float
 * nor an integer, or SOF_TOML_ERROR if value is NULL.
 */
extern sof_toml_error_t sof_toml_value_double(const sof_toml_value_t *v, double *value);

/**
 * @brief Finds the value of a table's member by its key.
 * @details The key is one key, not a dotted key: "a.b" is the member whose
 * key has a full stop in it. sof_toml_value_path() follows a dotted key. A
 * table with more than SOF_TOML_INDEX_MIN members is looked up in the
 * document's index; a smaller one is searched.
 * @param table The table.
 * @param key The NUL-terminated UTF-8 key.
 * @return The value, or NULL if no member has the key, or table is NULL or
 * not a table, or key is NULL.
 */
extern const sof_toml_value_t *sof_toml_value_get(const sof_toml_value_t *table, const char *key);

/**
 * @brief Finds the value of a table's member by its key, from bytes.
 * @details As sof_toml_value_get(), with the length bytes at key, which can
 * hold NULs.
 * @param table The table.
 * @param key The key. It can be NULL if length is 0.
 * @param length Its size in bytes.
 * @return As sof_toml_value_get().
 */
extern const sof_toml_value_t *sof_toml_value_get_bytes(const sof_toml_value_t *table, const void *key, size_t length);

/**
 * @brief Gives an integer's value.
 * @param v The value.
 * @param value Where the integer goes. It is left alone on failure.
 * @return SOF_TOML_OK, SOF_TOML_NO_MATCH if v is NULL or not an integer,
 * or SOF_TOML_ERROR if value is NULL.
 */
extern sof_toml_error_t sof_toml_value_int(const sof_toml_value_t *v, int64_t *value);

/**
 * @brief Returns a member of a table, in the order the members were
 * defined.
 * @param table The table.
 * @param index The member's index, counting from 0.
 * @return The member, or NULL if table is NULL or not a table, or index is
 * past its end.
 */
extern const sof_toml_member_t *sof_toml_value_member(const sof_toml_value_t *table, size_t index);

/**
 * @brief Finds a value by a path of keys and array indexes.
 * @details The path is a TOML key, with array indexes in brackets after any
 * part: "" is v itself, "server.port" the member port of the table server,
 * "products[1].name" the member name of the second element of the array
 * products, and "site.\"google.com\"" the member google.com of the table
 * site. Parts are bare keys, or quoted as TOML quotes keys, with whitespace
 * allowed around the full stops as in TOML; an index is decimal digits, and
 * "[0][2]" goes into an array in an array.
 * @param v The value to start from.
 * @param path The NUL-terminated path.
 * @return The value, or NULL if there is none, the path isn't one, or v or
 * path is NULL.
 */
extern const sof_toml_value_t *sof_toml_value_path(const sof_toml_value_t *v, const char *path);

/**
 * @brief Returns a string's bytes.
 * @param v The value.
 * @param length Where the number of bytes goes, not counting the NUL, or
 * NULL. It is set to 0 if v isn't a string.
 * @return The bytes, decoded UTF-8 that can hold NULs, NUL-terminated, which
 * belong to the document; or NULL if v is NULL or not a string.
 */
extern const char *sof_toml_value_string(const sof_toml_value_t *v, size_t *length);

/**
 * @brief Gives a value's type.
 * @param v The value.
 * @return Its type, or SOF_TOML_NONE if v is NULL.
 */
extern sof_toml_type_t sof_toml_value_type(const sof_toml_value_t *v);

/**
 * @brief Begins an inline array.
 * @details The array is a value, so it goes where a value can: after a key,
 * in a table or an inline table, or as an element of an inline array. Values
 * written until sof_toml_writer_array_end() are its elements, written on one
 * line, and tables begun in it are written inline.
 * @param w The writer.
 * @return SOF_TOML_OK, SOF_TOML_NO_MEM, or SOF_TOML_ERROR if w is NULL or
 * not initialized, or a value can't go here. On failure nothing is written.
 */
extern sof_toml_error_t sof_toml_writer_array_begin(sof_toml_writer_t *w);

/**
 * @brief Ends the array, or array of tables, begun last.
 * @details An array of tables with no tables is written as key = [], in the
 * table it is in, if that table has no sections after it yet.
 * @param w The writer.
 * @return SOF_TOML_OK, SOF_TOML_NO_MEM, or SOF_TOML_ERROR if w is NULL or
 * not initialized, what is open last isn't an array, or it is an empty array
 * of tables whose table already has sections. On failure nothing is written
 * and the array stays open.
 */
extern sof_toml_error_t sof_toml_writer_array_end(sof_toml_writer_t *w);

/**
 * @brief Writes true or false.
 * @param w The writer.
 * @param value Non-zero for true, 0 for false.
 * @return As sof_toml_writer_string().
 */
extern sof_toml_error_t sof_toml_writer_bool(sof_toml_writer_t *w, int value);

/**
 * @brief Starts again, empty.
 * @details Discards the buffer, without passing it to the write function,
 * and everything open, so the next key is the root table's. written goes
 * back to 0. The memory is kept, and so are the flags and write function.
 * @param w The writer.
 * @return SOF_TOML_OK, or SOF_TOML_ERROR if w is NULL or not initialized.
 */
extern sof_toml_error_t sof_toml_writer_clear(sof_toml_writer_t *w);

/**
 * @brief Says whether the output is whole.
 * @param w The writer.
 * @return 1 if nothing is open but the root table and no key is waiting, so
 * the output is a complete document, or 0, also for a NULL or uninitialized
 * writer.
 */
extern int sof_toml_writer_complete(const sof_toml_writer_t *w);

/**
 * @brief Writes a date and time.
 * @details Written as sof_toml_datetime_format() formats it.
 * @param w The writer.
 * @param type SOF_TOML_DATETIME, SOF_TOML_DATETIME_LOCAL,
 * SOF_TOML_DATE_LOCAL or SOF_TOML_TIME_LOCAL: which fields of dt are
 * written.
 * @param dt The date and time.
 * @return As sof_toml_writer_string(), and SOF_TOML_ERROR if dt is NULL,
 * type isn't one of the four, or a field of dt is out of its range.
 */
extern sof_toml_error_t sof_toml_writer_datetime(sof_toml_writer_t *w, sof_toml_type_t type, const sof_toml_datetime_t *dt);

/**
 * @brief Gives the number of tables and arrays that are open, not counting
 * the root table.
 * @param w The writer.
 * @return The number, 0 at the root, or 0 if w is NULL or not initialized.
 */
extern size_t sof_toml_writer_depth(const sof_toml_writer_t *w);

/**
 * @brief Writes a float from a double.
 * @details The number is written by sof_real_format(), with the fewest
 * significant digits, up to 17, that read back as the same double, the
 * closest to it of those, with a '.' as its decimal point whatever the
 * locale, and always as a float: 1.0, 0.1, 1e+100, -0.0. Numbers from 1e-4
 * up to 1e17 are written without an exponent, as 100.0 rather than 1e+02.
 * Infinities are inf and -inf, and NaNs nan.
 * @param w The writer.
 * @param value The number.
 * @return As sof_toml_writer_string().
 */
extern sof_toml_error_t sof_toml_writer_double(sof_toml_writer_t *w, double value);

/**
 * @brief Passes the buffer to the write function.
 * @details Does nothing if there is no write function or the buffer is
 * empty. Output can be flushed at any point; call it once the document is
 * written.
 * @param w The writer.
 * @return SOF_TOML_OK, emptying the buffer; SOF_TOML_WRITE if the write
 * function failed, which leaves the buffer as it was; or SOF_TOML_ERROR if
 * w is NULL or not initialized.
 */
extern sof_toml_error_t sof_toml_writer_flush(sof_toml_writer_t *w);

/**
 * @brief Releases the writer's memory.
 * @details The buffer is discarded, not flushed: call
 * sof_toml_writer_flush() first to keep it. The sof_toml_writer_t itself
 * isn't freed (it belongs to the caller); it is left empty, and acts like a
 * writer whose sof_toml_writer_init() failed until it is initialized
 * again.
 * @param w The writer.
 * @return SOF_TOML_OK, also for a writer not initialized, or
 * SOF_TOML_ERROR if w is NULL.
 */
extern sof_toml_error_t sof_toml_writer_free(sof_toml_writer_t *w);

/**
 * @brief Initializes a writer in a sof_toml_writer_t the caller provides.
 * @details The root table of the document is open from the start, so the
 * first call is usually sof_toml_writer_key().
 * @param w The writer to initialize.
 * @param flags The sof_toml_flag_t flags: SOF_TOML_ASCII, or 0.
 * @param write The function to pass output to, or NULL to keep it all in
 * the buffer.
 * @param ctx The context pointer passed to write.
 * @param alloc The memory functions for the buffer and the record of what is
 * open, or NULL for the standard functions. The writer keeps a copy, so it
 * can be on the stack.
 * @return SOF_TOML_OK, SOF_TOML_NO_MEM, or SOF_TOML_ERROR if w is NULL
 * or alloc has a malloc but no free. On failure the writer is left empty and
 * not initialized.
 */
extern sof_toml_error_t sof_toml_writer_init(sof_toml_writer_t *w, unsigned flags, sof_toml_write_func_t write, void *ctx, const sof_alloc_t *alloc);

/**
 * @brief Begins an inline table.
 * @details The table goes where a value can, as sof_toml_writer_array_begin()
 * says, and is written on one line, as {a = 1, b = 2}, whatever it holds.
 * Its members are written as a key and then a value until
 * sof_toml_writer_table_end().
 * @param w The writer.
 * @return As sof_toml_writer_array_begin().
 */
extern sof_toml_error_t sof_toml_writer_inline_begin(sof_toml_writer_t *w);

/**
 * @brief Writes an integer.
 * @param w The writer.
 * @param value The integer.
 * @return As sof_toml_writer_string().
 */
extern sof_toml_error_t sof_toml_writer_int(sof_toml_writer_t *w, int64_t value);

/**
 * @brief Gives the key of the next member of the table open last.
 * @details Nothing is written until the value is: the key is kept, and
 * written with it, bare if it is letters, digits, hyphens and underscores,
 * and otherwise quoted, with escapes as sof_toml_writer_string() writes
 * them. The writer doesn't check that keys are unique.
 * @param w The writer.
 * @param key The NUL-terminated UTF-8 key.
 * @return SOF_TOML_OK, SOF_TOML_NO_MEM, or SOF_TOML_ERROR if w or key is
 * NULL, w is not initialized, what is open last isn't a table or inline
 * table, or a key is already waiting. On failure no key is kept.
 */
extern sof_toml_error_t sof_toml_writer_key(sof_toml_writer_t *w, const char *key);

/**
 * @brief Gives the key of the next member, from bytes.
 * @details As sof_toml_writer_key(), with the length bytes at key, which
 * can hold NULs, written as \u0000.
 * @param w The writer.
 * @param key The UTF-8 key. It can be NULL if length is 0, for the empty
 * key "".
 * @param length Its size in bytes.
 * @return As sof_toml_writer_key(), and SOF_TOML_ERROR if key is NULL and
 * length isn't 0.
 */
extern sof_toml_error_t sof_toml_writer_key_bytes(sof_toml_writer_t *w, const void *key, size_t length);

/**
 * @brief Returns the output in the buffer.
 * @details The output belongs to the writer; it is good until the next call
 * that writes, flushes, clears or frees.
 * @param w The writer.
 * @param length Where the length of the output in bytes goes, or NULL. It
 * is set to 0 if w is NULL or not initialized.
 * @return The output, followed by a NUL, or NULL if w is NULL or not
 * initialized.
 */
extern const char *sof_toml_writer_output(const sof_toml_writer_t *w, size_t *length);

/**
 * @brief Writes a string.
 * @details The string is put in double quotes, with ", \ and the control
 * characters escaped, as \b, \t, \n, \f, \r or a \u escape. UTF-8 is written
 * as it is, or with SOF_TOML_ASCII as \u and \U escapes. Bytes that aren't
 * valid UTF-8 are written as U+FFFD, one for each error, as
 * sof_utf8_decode() finds them.
 *
 * A value goes after a key, in a table or inline table, or as an element of
 * an inline array. In a table written as a section, it goes on a line of its
 * own, as key = value, and the table's header, if it has one, is written
 * before its first key/value pair.
 * @param w The writer.
 * @param s The NUL-terminated UTF-8 string.
 * @return SOF_TOML_OK, SOF_TOML_NO_MEM, or SOF_TOML_ERROR if w or s is
 * NULL, w is not initialized, or a value can't go here: in a table with no
 * key waiting, in a table that has had a section of its own written after
 * it, or in an array of tables. On failure nothing is written.
 */
extern sof_toml_error_t sof_toml_writer_string(sof_toml_writer_t *w, const char *s);

/**
 * @brief Writes a string, from bytes.
 * @details As sof_toml_writer_string(), with the length bytes at s, which
 * can hold NULs, written as \u0000.
 * @param w The writer.
 * @param s The UTF-8 string. It can be NULL if length is 0.
 * @param length Its size in bytes.
 * @return As sof_toml_writer_string(), and SOF_TOML_ERROR if s is NULL and
 * length isn't 0.
 */
extern sof_toml_error_t sof_toml_writer_string_bytes(sof_toml_writer_t *w, const void *s, size_t length);

/**
 * @brief Begins an array of tables.
 * @details After a key, in a table written as a section, begins an array
 * whose elements, each begun with sof_toml_writer_table_begin(), are
 * written as [[key]] sections, until sof_toml_writer_array_end(). Anywhere
 * else a value can go, it begins an inline array, as
 * sof_toml_writer_array_begin() does.
 * @param w The writer.
 * @return As sof_toml_writer_array_begin().
 */
extern sof_toml_error_t sof_toml_writer_table_array_begin(sof_toml_writer_t *w);

/**
 * @brief Begins a table.
 * @details After a key, in a table written as a section, begins a table
 * written as a section of its own, under a [key] header with the keys of the
 * tables it is in; the header is written before its first key/value pair, or
 * at its end if it has none and no sections in it. Members written until
 * sof_toml_writer_table_end() are its own. Once a table has begun a
 * section, the table it is in can have no more key/value pairs, since they
 * would fall in the section.
 *
 * In an array of tables, begins its next element, under a [[key]] header.
 * Anywhere else a value can go, in an inline table or inline array, it
 * begins an inline table, as sof_toml_writer_inline_begin() does.
 * @param w The writer.
 * @return As sof_toml_writer_array_begin().
 */
extern sof_toml_error_t sof_toml_writer_table_begin(sof_toml_writer_t *w);

/**
 * @brief Ends the table, or inline table, begun last.
 * @param w The writer.
 * @return SOF_TOML_OK, SOF_TOML_NO_MEM, or SOF_TOML_ERROR if w is NULL
 * or not initialized, what is open last isn't a table begun by the
 * application, or it has a key waiting for its value. On failure nothing is
 * written and the table stays open.
 */
extern sof_toml_error_t sof_toml_writer_table_end(sof_toml_writer_t *w);

/**
 * @brief Writes a document's value, with all it holds.
 * @details The value goes where a value can, and is written as the calls
 * for each of its parts would write it. A table, after a key in a table
 * written as a section, is written as a section, with its key/value pairs
 * first and then its tables and arrays of tables, each a section; an array
 * of tables, there, as [[key]] sections, and anything else inline. Tables
 * and arrays the text wrote inline (is_inline is 1) are written inline
 * again, so a document is written much as it was read. A table
 * given with no key waiting, where the table open last is written as a
 * section, such as the root, has its members written in that table, so
 * sof_toml_writer_value(w, d.root) writes a whole document. The whole value
 * is built in the buffer before any of it is passed to the write function,
 * so if a call fails, nothing is written.
 * @param w The writer.
 * @param v The value, from a sof_toml_doc_t.
 * @return As sof_toml_writer_string(), and SOF_TOML_ERROR if v is NULL.
 */
extern sof_toml_error_t sof_toml_writer_value(sof_toml_writer_t *w, const sof_toml_value_t *v);

#endif // SOF_TOML_H
