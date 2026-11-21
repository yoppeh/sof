/**
 * @file sof_json.h
 * @author Warren Mann (warren@nonvol.io)
 * @brief JSON in C: a streaming writer, a streaming reader and a document
 * tree.
 * @details The library's types and functions share the sof_json_ prefix
 * and one error enum, sof_json_error_t. It has three parts.
 *
 * The writer, sof_json_writer_t, writes JSON one piece at a time: the
 * application begins and ends objects and arrays, gives each member's key,
 * and writes strings, numbers, booleans and nulls, and the writer adds the
 * commas, colons, quotes, escapes and, if asked, the line breaks and
 * indentation. It keeps track of the objects and arrays that are open, so a
 * piece in the wrong place, such as a value in an object without its key, is
 * refused, and what it writes is always valid JSON so far.
 *
 * Output goes into a buffer, a sof_string_t, which the application can
 * read, and, if it gives a write function, is passed to that function
 * whenever a top-level value is finished or the buffer passes
 * SOF_JSON_FLUSH_SIZE bytes, and by sof_json_writer_flush(). If the write
 * function fails in the middle of a value, the output stays in the buffer
 * and is passed again at the next flush; only the flush at the end of a
 * top-level value, and sof_json_writer_flush(), report a failure. Each
 * top-level value is followed by a newline, so a writer can write one
 * document, or a series of them in the JSON Lines format.
 *
 * Strings are taken as UTF-8. Bytes that aren't valid UTF-8 are written as
 * U+FFFD, the replacement character, so the output is always valid UTF-8;
 * with SOF_JSON_ASCII, every character outside ASCII is written as a \u
 * escape, so the output is ASCII.
 *
 * The reader, sof_json_reader_t, takes JSON apart one token at a time, from
 * bytes in memory or from input it pulls through a read function, so a
 * document of any size is read in the memory its longest string or number
 * takes. It checks the text against RFC 8259's grammar as it goes, and
 * returns each token with its strings decoded, and its span: where it is in
 * the input, as a byte offset and length.
 *
 * The document, sof_json_doc_t, holds a whole value read by the reader as
 * a tree of sof_json_value_t, kept in an arena, with each object's members
 * in their order and, for objects with more than SOF_JSON_INDEX_MIN
 * members, an index that finds a member by its key in constant time. The
 * sof_json_value_ functions read the tree, and sof_json_writer_value()
 * writes it out again. Each value, and each member's key, has a span, so an
 * application holding the text can say which line a value is on, with
 * sof_string_position(), or change the text by splicing it at those offsets,
 * keeping its layout.
 * @version 0.0.1
 * @date 2026-11-19
 * @copyright Copyright (c) 2026
 */

#ifndef SOF_JSON_H
#define SOF_JSON_H

#include <stddef.h>
#include <stdint.h>

#include "sof_alloc.h"
#include "sof_hashmap.h"
#include "sof_mem.h"
#include "sof_stack.h"
#include "sof_string.h"

/// @brief The size the buffer can reach before a writer with a write
/// function passes it on, without waiting for the top-level value to end.
#define SOF_JSON_FLUSH_SIZE 4096

/// @brief The size of the buffer a reader with a read function starts with,
/// and asks the read function to fill. It doubles when a token doesn't fit.
#define SOF_JSON_READ_SIZE 4096

/// @brief The deepest objects and arrays can nest in what the reader reads.
/// RFC 8259 lets a parser set such a limit; deeper input is refused with
/// SOF_JSON_TOO_DEEP.
#define SOF_JSON_DEPTH_MAX 1024

/// @brief A document's objects with more members than this are indexed, so
/// sof_json_value_get() finds a key without looking at every member.
#define SOF_JSON_INDEX_MIN 8

/**
 * @brief Flags for sof_json_writer_init() and sof_json_reader_init(),
 * which can be combined with |.
 */
typedef enum sof_json_flag_t {
    /// @brief For a writer, write every character outside ASCII as a \u
    /// escape, with a surrogate pair for those past U+FFFF, so the output is
    /// ASCII.
    SOF_JSON_ASCII = 1,
    /// @brief For a reader, read a series of top-level values, as in JSON
    /// Lines, rather than one: whitespace, or nothing, can separate them, and
    /// the input can be empty.
    SOF_JSON_LINES = 2,
} sof_json_flag_t;

/**
 * @brief Error codes for the sof_json functions.
 */
typedef enum sof_json_error_t {
    SOF_JSON_OK = 0,
    SOF_JSON_NO_MEM,
    SOF_JSON_NO_MATCH,
    SOF_JSON_ERROR,
    /// @brief The write function failed. What was written is kept in the
    /// buffer.
    SOF_JSON_WRITE,
    /// @brief The read function failed.
    SOF_JSON_READ,
    /// @brief The input isn't JSON.
    SOF_JSON_SYNTAX,
    /// @brief Objects and arrays in the input nest deeper than
    /// SOF_JSON_DEPTH_MAX.
    SOF_JSON_TOO_DEEP,
    /// @brief A number doesn't fit the type asked for, or isn't an integer
    /// where one was asked for.
    SOF_JSON_RANGE,
} sof_json_error_t;

/**
 * @brief The types of the reader's tokens and the document's values.
 * @details A document's values are SOF_JSON_NULL to SOF_JSON_OBJECT. The
 * reader returns those, with SOF_JSON_ARRAY and SOF_JSON_OBJECT meaning an
 * array or object begins, and the last three as well.
 */
typedef enum sof_json_type_t {
    /// @brief No value or token.
    SOF_JSON_NONE = 0,
    SOF_JSON_NULL,
    /// @brief true or false.
    SOF_JSON_BOOL,
    SOF_JSON_NUMBER,
    SOF_JSON_STRING,
    SOF_JSON_ARRAY,
    SOF_JSON_OBJECT,
    /// @brief The reader's token for the key of an object's member.
    SOF_JSON_KEY,
    /// @brief The reader's token for the end of an array.
    SOF_JSON_ARRAY_END,
    /// @brief The reader's token for the end of an object.
    SOF_JSON_OBJECT_END,
} sof_json_type_t;

/**
 * @brief Gives the reader input.
 * @details Called with a buffer of size bytes, the reader's own, and the
 * ctx given to sof_json_reader_init(). It puts up to size bytes in buffer
 * and their number in *length, which is 0 at the end of the input. It may
 * return fewer bytes than size, as a pipe or socket does. It must not call
 * into the reader.
 * @return 0, or non-zero if the input couldn't be read, which
 * sof_json_reader_next() returns as SOF_JSON_READ.
 */
typedef int (*sof_json_read_func_t)(char *buffer, size_t size, size_t *length, void *ctx);

/**
 * @brief Receives output from the writer.
 * @details Called with the length bytes of output at data, which aren't
 * NUL-terminated and belong to the writer, and the ctx given to
 * sof_json_writer_init(). It must not call into the writer. Return 0 if every
 * byte was written, or non-zero if they couldn't be, in which case the
 * writer keeps them all in its buffer and the call that flushed returns
 * SOF_JSON_WRITE.
 */
typedef int (*sof_json_write_func_t)(const char *data, size_t length, void *ctx);

/**
 * @brief Where something is in the input.
 */
typedef struct sof_json_span_t {
    /// @brief Its first byte's offset from the start of the input.
    size_t offset;
    /// @brief Its size in bytes.
    size_t length;
} sof_json_span_t;

/**
 * @brief A token the reader returns.
 * @details The application provides it to sof_json_reader_next(), which
 * fills it in. text belongs to the reader, and is good until its next call.
 */
typedef struct sof_json_token_t {
    /// @brief What the token is: SOF_JSON_NULL, SOF_JSON_BOOL,
    /// SOF_JSON_NUMBER or SOF_JSON_STRING for a value; SOF_JSON_ARRAY or
    /// SOF_JSON_OBJECT where one begins; SOF_JSON_ARRAY_END or
    /// SOF_JSON_OBJECT_END where it ends; SOF_JSON_KEY for a member's key;
    /// or SOF_JSON_NONE if no token was read.
    sof_json_type_t type;
    /// @brief For a string or key, its bytes decoded: UTF-8, which can hold
    /// NULs written as \u0000. For a number, its text as written, which
    /// sof_json_number_double(), sof_json_number_int() and
    /// sof_json_number_uint() convert. NUL-terminated either way, and "" for
    /// the other tokens.
    const char *text;
    /// @brief The bytes in text, not counting the NUL.
    size_t length;
    /// @brief For SOF_JSON_BOOL, 1 for true or 0 for false; otherwise 0.
    int boolean;
    /// @brief The objects and arrays open after the token: 1 for an array
    /// that begins at the top level, and 0 for its end.
    size_t depth;
    /// @brief Where the token is in the input, as written: a string or key
    /// with its quotes and escapes, but not a key's colon, a number or
    /// literal, or the one [, {, ] or } of an array or object's beginning or
    /// end. 0 and 0 if no token was read.
    sof_json_span_t span;
} sof_json_token_t;

/**
 * @brief A JSON reader.
 * @details The application provides the sof_json_reader_t (on the stack, in
 * another structure or on the heap) and sets it up with
 * sof_json_reader_init() or sof_json_reader_init_bytes(). The fields can be
 * read but must not be changed.
 *
 * Every function accepts a NULL reader, and one that was freed or whose
 * init failed (ready is 0), which reads nothing.
 */
typedef struct sof_json_reader_t {
    /// @brief The reader's buffer, size bytes, with a read function, or NULL
    /// with bytes.
    char *buffer;
    /// @brief The size of buffer.
    size_t size;
    /// @brief The input: buffer, or the application's bytes.
    const char *input;
    /// @brief The bytes in input.
    size_t length;
    /// @brief Where in input the next byte to take apart is.
    size_t position;
    /// @brief Where in input the token being read begins: the bytes from
    /// here on are kept when buffer is refilled.
    size_t mark;
    /// @brief The bytes of input dropped from the front of buffer to make
    /// room.
    size_t consumed;
    /// @brief The decoded text of the token last read.
    sof_string_t text;
    /// @brief A byte for each object (1) or array (0) that is open,
    /// outermost first.
    sof_stack_t open;
    /// @brief The sof_json_flag_t flags.
    unsigned flags;
    /// @brief The function input comes from, or NULL with bytes.
    sof_json_read_func_t read;
    /// @brief Application context passed to read.
    void *ctx;
    /// @brief The memory functions the buffer, text and open are allocated
    /// with.
    sof_alloc_t alloc;
    /// @brief What the reader expects next.
    int state;
    /// @brief The type of the token last returned.
    sof_json_type_t last;
    /// @brief While sof_json_reader_skip() is going on, 1 more than the
    /// depth it skips to, or 0.
    size_t skip;
    /// @brief 1 once read has reported the end of the input.
    int ended;
    /// @brief SOF_JSON_SYNTAX or SOF_JSON_TOO_DEEP once the reader has
    /// found one, which it then goes on returning, or SOF_JSON_OK.
    sof_json_error_t error;
    /// @brief The line, counting from 1, of the token last returned, or of
    /// the error.
    size_t line;
    /// @brief The column, in bytes and counting from 1, of the token last
    /// returned, or of the error.
    size_t column;
    /// @brief The offset in bytes, from the start of the input, of the token
    /// last returned, or of the error.
    size_t offset;
    /// @brief The line the next byte is on.
    size_t next_line;
    /// @brief The offset from the start of the input of the line next_line.
    size_t line_start;
    /// @brief The number of top-level values read whole.
    size_t values;
    /// @brief 1 once init has succeeded, 0 when freed or failed.
    int ready;
} sof_json_reader_t;

/**
 * @brief A value in a document.
 * @details Values belong to their document, and are good until it reads
 * another value, is cleared or is freed. The application may read the
 * fields, or use the sof_json_value_ functions, which also take NULL, but
 * must not change them.
 */
typedef struct sof_json_value_t {
    /// @brief SOF_JSON_NULL, SOF_JSON_BOOL, SOF_JSON_NUMBER,
    /// SOF_JSON_STRING, SOF_JSON_ARRAY or SOF_JSON_OBJECT.
    sof_json_type_t type;
    /// @brief The bytes of a string or number's text, not counting the NUL,
    /// the elements of an array, or the members of an object; 0 otherwise.
    size_t length;
    /// @brief Where the value is in the input: a string, number or literal
    /// as written, a string with its quotes; an array or object from its [
    /// or { to its ] or }.
    sof_json_span_t span;
    union {
        /// @brief For SOF_JSON_BOOL, 1 for true or 0 for false.
        int boolean;
        /// @brief For a string, its bytes decoded, UTF-8 that can hold NULs;
        /// for a number, its text as written. NUL-terminated.
        const char *text;
        /// @brief For an array, its length elements, or NULL if it has none.
        const struct sof_json_value_t *const *elements;
        /// @brief For an object, its members and index.
        struct {
            /// @brief The length members, in their order, or NULL if there
            /// are none.
            const struct sof_json_member_t *members;
            /// @brief The document's index of members by key, if this
            /// object has more than SOF_JSON_INDEX_MIN of them, or NULL.
            const sof_hashmap_t *index;
        } object;
    } as;
} sof_json_value_t;

/**
 * @brief A member of an object in a document.
 * @details As a value, it belongs to the document, and can be read but not
 * changed.
 */
typedef struct sof_json_member_t {
    /// @brief The key, decoded: UTF-8, which can hold NULs, NUL-terminated.
    const char *key;
    /// @brief The bytes in key, not counting the NUL.
    size_t key_length;
    /// @brief Where the key is in the input, as written, quotes included.
    sof_json_span_t key_span;
    /// @brief The value.
    const sof_json_value_t *value;
    /// @brief The object the member is in.
    const sof_json_value_t *object;
} sof_json_member_t;

/**
 * @brief A JSON document: a value read whole, as a tree.
 * @details The application provides the sof_json_doc_t (on the stack, in
 * another structure or on the heap) and sets it up with
 * sof_json_doc_init(). The fields can be read but must not be changed.
 *
 * Every function accepts a NULL document, and one that was freed or whose
 * sof_json_doc_init() failed (ready is 0), which holds nothing.
 */
typedef struct sof_json_doc_t {
    /// @brief The value read, or NULL if there is none.
    const sof_json_value_t *root;
    /// @brief The arena the values, members, keys and strings are in.
    sof_mem_arena_t arena;
    /// @brief The index of the members of objects with more than
    /// SOF_JSON_INDEX_MIN of them, in the arena, or NULL if there are
    /// none.
    sof_hashmap_t *index;
    /// @brief While a value is read, the objects and arrays open in it.
    sof_stack_t frames;
    /// @brief While a value is read, the elements of the arrays open in it.
    sof_stack_t elements;
    /// @brief While a value is read, the members of the objects open in it.
    sof_stack_t members;
    /// @brief The memory functions the arena and the rest are allocated
    /// with.
    sof_alloc_t alloc;
    /// @brief After SOF_JSON_SYNTAX or SOF_JSON_TOO_DEEP, the line of the
    /// error, counting from 1; otherwise 0.
    size_t line;
    /// @brief After SOF_JSON_SYNTAX or SOF_JSON_TOO_DEEP, the column of
    /// the error, in bytes and counting from 1; otherwise 0.
    size_t column;
    /// @brief After SOF_JSON_SYNTAX or SOF_JSON_TOO_DEEP, the offset of
    /// the error in bytes; otherwise 0.
    size_t offset;
    /// @brief 1 once sof_json_doc_init() has succeeded, 0 when freed or
    /// failed.
    int ready;
} sof_json_doc_t;

/**
 * @brief A JSON writer.
 * @details The application provides the sof_json_writer_t (on the stack, in
 * another structure or on the heap) and sets it up with sof_json_writer_init().
 * The fields can be read but must not be changed. out.data moves as the
 * buffer grows, so a pointer into it is good only until the next call that
 * writes.
 *
 * Every function accepts a NULL writer, and one that was freed or whose
 * sof_json_writer_init() failed (ready is 0), which writes nothing.
 */
typedef struct sof_json_writer_t {
    /// @brief The output not yet passed to the write function: all of it,
    /// if there is none.
    sof_string_t out;
    /// @brief One byte for each object or array that is open, outermost
    /// first.
    sof_stack_t open;
    /// @brief The spaces each level is indented by, or 0 to write
    /// everything on one line.
    size_t indent;
    /// @brief The sof_json_flag_t flags.
    unsigned flags;
    /// @brief The function output is passed to, or NULL.
    sof_json_write_func_t write;
    /// @brief Application context passed to write.
    void *ctx;
    /// @brief The number of top-level values finished.
    size_t values;
    /// @brief The bytes passed to write so far.
    size_t written;
    /// @brief 1 once sof_json_writer_init() has succeeded, 0 when freed or
    /// failed.
    int ready;
} sof_json_writer_t;

/**
 * @brief Empties a document.
 * @details The values it held are gone; root is NULL. The memory is kept
 * for the next value.
 * @param d The document.
 * @return SOF_JSON_OK, or SOF_JSON_ERROR if d is NULL or not initialized.
 */
extern sof_json_error_t sof_json_doc_clear(sof_json_doc_t *d);

/**
 * @brief Releases a document's memory.
 * @details The sof_json_doc_t itself isn't freed (it belongs to the
 * caller); it is left empty, and acts like a document whose
 * sof_json_doc_init() failed until it is initialized again.
 * @param d The document.
 * @return SOF_JSON_OK, also for a document not initialized, or
 * SOF_JSON_ERROR if d is NULL.
 */
extern sof_json_error_t sof_json_doc_free(sof_json_doc_t *d);

/**
 * @brief Initializes an empty document in a sof_json_doc_t the caller
 * provides.
 * @details Nothing is allocated until a value is read.
 * @param d The document to initialize.
 * @param alloc The memory functions for the document, or NULL for the
 * standard functions. The document keeps a copy, so it can be on the stack.
 * @return SOF_JSON_OK, or SOF_JSON_ERROR if d is NULL or alloc has a
 * malloc but no free. On failure the document is left empty and not
 * initialized.
 */
extern sof_json_error_t sof_json_doc_init(sof_json_doc_t *d, const sof_alloc_t *alloc);

/**
 * @brief Parses a JSON text into a document.
 * @details The text must be exactly one value, with whitespace around it if
 * any, as RFC 8259 defines a JSON text. What the document held before is
 * replaced.
 * @param d The document.
 * @param text The text, in UTF-8. It can be NULL if length is 0, which
 * isn't JSON.
 * @param length Its size in bytes.
 * @return SOF_JSON_OK, with the value in root; SOF_JSON_NO_MEM;
 * SOF_JSON_SYNTAX if the text isn't JSON, or SOF_JSON_TOO_DEEP if it
 * nests too deep, with where in line, column and offset; or
 * SOF_JSON_ERROR if d is NULL or not initialized, or text is NULL and
 * length isn't 0. On failure the document is empty.
 */
extern sof_json_error_t sof_json_doc_parse(sof_json_doc_t *d, const void *text, size_t length);

/**
 * @brief Reads the reader's next value into a document.
 * @details Reads one whole value, with what it holds: a top-level value, or
 * the next element of an array the reader is in, or the value of a member
 * whose key the application has read. What the document held before is
 * replaced. To read the elements of a large array one at a time, read its
 * SOF_JSON_ARRAY token with sof_json_reader_next(), then call this until
 * it returns SOF_JSON_NO_MATCH.
 * @param d The document.
 * @param r The reader.
 * @return SOF_JSON_OK, with the value in root; SOF_JSON_NO_MATCH, with the
 * document empty, at the end of the input or, having read it, at the end of
 * the array or object the reader is in; SOF_JSON_ERROR if the next token is
 * a key, which the reader has then read, or d or r is NULL or not
 * initialized; or as sof_json_reader_next() returns, with an error's place
 * in line, column and offset. On failure the document is empty, and the
 * reader has read part of the value.
 */
extern sof_json_error_t sof_json_doc_read(sof_json_doc_t *d, sof_json_reader_t *r);

/**
 * @brief Converts a number's text to a double.
 * @details The text is converted to the nearest double, correctly rounded
 * however many digits it has, by sof_real, which doesn't depend on the
 * locale.
 * @param text The number, as JSON writes it, such as a number token's text.
 * It needn't be NUL-terminated.
 * @param length Its size in bytes.
 * @param value Where the double goes.
 * @return SOF_JSON_OK; SOF_JSON_RANGE if the number is too large for a
 * double, with an infinity of its sign in value; SOF_JSON_SYNTAX if the
 * text isn't a JSON number; or SOF_JSON_ERROR if text or value is NULL.
 * Numbers too small for a double are 0 or subnormal, and SOF_JSON_OK.
 */
extern sof_json_error_t sof_json_number_double(const char *text, size_t length, double *value);

/**
 * @brief Converts a number's text to a signed integer.
 * @details The number needn't be written as an integer, as long as it is
 * one: 100, 1e2, 1.0e2 and 100.00 are all 100.
 * @param text The number, as JSON writes it. It needn't be NUL-terminated.
 * @param length Its size in bytes.
 * @param value Where the integer goes. It is left alone on failure.
 * @return SOF_JSON_OK; SOF_JSON_RANGE if the number isn't an integer or
 * is outside int64_t's range; SOF_JSON_SYNTAX if the text isn't a JSON
 * number; or SOF_JSON_ERROR if text or value is NULL.
 */
extern sof_json_error_t sof_json_number_int(const char *text, size_t length, int64_t *value);

/**
 * @brief Converts a number's text to an unsigned integer.
 * @details As sof_json_number_int(), for uint64_t. -0 is 0.
 * @param text The number, as JSON writes it. It needn't be NUL-terminated.
 * @param length Its size in bytes.
 * @param value Where the integer goes. It is left alone on failure.
 * @return As sof_json_number_int(), with SOF_JSON_RANGE if the number is
 * outside uint64_t's range.
 */
extern sof_json_error_t sof_json_number_uint(const char *text, size_t length, uint64_t *value);

/**
 * @brief A read function for a FILE *.
 * @details Pass it to sof_json_reader_init() with the FILE * as ctx. It
 * reads with fread(), and never closes the file.
 * @return 0, or 1 if the file couldn't be read or ctx is NULL.
 */
extern int sof_json_read_file(char *buffer, size_t size, size_t *length, void *ctx);

/**
 * @brief Starts again.
 * @details Drops the open objects and arrays, the token and any error, and
 * starts on line 1 as if nothing had been read, with values back to 0. A
 * reader with a read function drops the input read but not yet taken apart,
 * and goes on with whatever the read function gives next; one reading bytes
 * starts again at their first byte. The memory is kept.
 * @param r The reader.
 * @return SOF_JSON_OK, or SOF_JSON_ERROR if r is NULL or not initialized.
 */
extern sof_json_error_t sof_json_reader_clear(sof_json_reader_t *r);

/**
 * @brief Gives the number of objects and arrays that are open.
 * @param r The reader.
 * @return The number, 0 at the top level, or 0 if r is NULL or not
 * initialized.
 */
extern size_t sof_json_reader_depth(const sof_json_reader_t *r);

/**
 * @brief Releases the reader's memory.
 * @details The sof_json_reader_t itself isn't freed (it belongs to the
 * caller); it is left empty, and acts like a reader whose init failed until
 * it is initialized again. The input isn't closed.
 * @param r The reader.
 * @return SOF_JSON_OK, also for a reader not initialized, or
 * SOF_JSON_ERROR if r is NULL.
 */
extern sof_json_error_t sof_json_reader_free(sof_json_reader_t *r);

/**
 * @brief Initializes a reader of input from a read function, in a
 * sof_json_reader_t the caller provides.
 * @param r The reader to initialize.
 * @param flags The sof_json_flag_t flags: SOF_JSON_LINES, or 0.
 * @param read The function input comes from, such as sof_json_read_file().
 * @param ctx The context pointer passed to read, such as a FILE *.
 * @param alloc The memory functions for the buffer and the rest, or NULL for
 * the standard functions. The reader keeps a copy, so it can be on the
 * stack.
 * @return SOF_JSON_OK, SOF_JSON_NO_MEM, or SOF_JSON_ERROR if r or read is
 * NULL, or alloc has a malloc but no free. On failure the reader is left
 * empty and not initialized.
 */
extern sof_json_error_t sof_json_reader_init(sof_json_reader_t *r, unsigned flags, sof_json_read_func_t read, void *ctx, const sof_alloc_t *alloc);

/**
 * @brief Initializes a reader of bytes in memory, in a sof_json_reader_t the
 * caller provides.
 * @details The bytes aren't copied, so they must stay as they are while the
 * reader is used.
 * @param r The reader to initialize.
 * @param flags The sof_json_flag_t flags: SOF_JSON_LINES, or 0.
 * @param data The input, in UTF-8. It can be NULL if length is 0.
 * @param length Its size in bytes.
 * @param alloc The memory functions for the decoded text and the rest, or
 * NULL for the standard functions. The reader keeps a copy.
 * @return SOF_JSON_OK, SOF_JSON_NO_MEM, or SOF_JSON_ERROR if r is NULL,
 * data is NULL and length isn't 0, or alloc has a malloc but no free. On
 * failure the reader is left empty and not initialized.
 */
extern sof_json_error_t sof_json_reader_init_bytes(sof_json_reader_t *r, unsigned flags, const void *data, size_t length, const sof_alloc_t *alloc);

/**
 * @brief Reads the next token.
 * @details Takes bytes from the input, calling the read function when it
 * needs more, until a token is whole, checks that it can go where it is,
 * and returns it. An object is returned as SOF_JSON_OBJECT, then for each
 * member a SOF_JSON_KEY and its value, then SOF_JSON_OBJECT_END, and an
 * array as SOF_JSON_ARRAY, its elements and SOF_JSON_ARRAY_END. The
 * token's place is in line, column and offset.
 *
 * Input must be UTF-8, without a byte order mark, and strings are returned
 * decoded, as UTF-8; a \u escape of half a surrogate pair without its other
 * half is returned as U+FFFD, so the text is always valid UTF-8.
 *
 * A call that fails with SOF_JSON_NO_MEM or SOF_JSON_READ changes
 * nothing, and can be made again. One that fails with SOF_JSON_SYNTAX or
 * SOF_JSON_TOO_DEEP fails the same way again, with the error's place in
 * line, column and offset; sof_json_reader_clear() starts again.
 * @param r The reader.
 * @param token Where the token goes, or NULL. Its type is SOF_JSON_NONE
 * unless a token is returned.
 * @return SOF_JSON_OK; SOF_JSON_NO_MATCH at the end of the input, after
 * the value, or with SOF_JSON_LINES, after the last value;
 * SOF_JSON_NO_MEM; SOF_JSON_READ if the read function failed;
 * SOF_JSON_SYNTAX if the input isn't JSON, which includes input that ends
 * before the value does, and without SOF_JSON_LINES, input with anything
 * but whitespace after the value or none at all; SOF_JSON_TOO_DEEP if
 * objects and arrays nest more than SOF_JSON_DEPTH_MAX deep; or
 * SOF_JSON_ERROR if r is NULL or not initialized. After the end, it goes
 * on returning SOF_JSON_NO_MATCH, without calling the read function, until
 * the reader is cleared.
 */
extern sof_json_error_t sof_json_reader_next(sof_json_reader_t *r, sof_json_token_t *token);

/**
 * @brief Says what the next token is, without reading it.
 * @details The whitespace before the token, and a comma or colon, are read,
 * but the token itself is left for sof_json_reader_next(), which checks it:
 * a token that begins as a value does but goes on wrong, such as tru, is
 * found then. Use it to choose between reading a value with
 * sof_json_doc_read() and reading into it with sof_json_reader_next(),
 * such as the elements of a top-level array one at a time.
 * @param r The reader.
 * @param type Where the type goes, as the token's would be, or NULL. It is
 * SOF_JSON_NONE unless SOF_JSON_OK is returned.
 * @return SOF_JSON_OK; SOF_JSON_NO_MATCH at the end of the input;
 * SOF_JSON_SYNTAX if no token can begin with the next byte, or none can go
 * there; or otherwise as sof_json_reader_next() returns.
 */
extern sof_json_error_t sof_json_reader_peek(sof_json_reader_t *r, sof_json_type_t *type);

/**
 * @brief Skips a value.
 * @details After a SOF_JSON_ARRAY or SOF_JSON_OBJECT token, reads up to
 * and including its end. After a SOF_JSON_KEY, reads the member's value,
 * whole. After any other token, does nothing. If it fails with
 * SOF_JSON_NO_MEM or SOF_JSON_READ, calling it again goes on skipping.
 * @param r The reader.
 * @return SOF_JSON_OK, or as sof_json_reader_next() returns.
 */
extern sof_json_error_t sof_json_reader_skip(sof_json_reader_t *r);

/**
 * @brief Returns an element of an array.
 * @param array The array.
 * @param index The element's index, counting from 0.
 * @return The element, or NULL if array is NULL or not an array, or index
 * is past its end.
 */
extern const sof_json_value_t *sof_json_value_at(const sof_json_value_t *array, size_t index);

/**
 * @brief Gives a boolean's value.
 * @param v The value.
 * @param value Where 1 for true or 0 for false goes.
 * @return SOF_JSON_OK, SOF_JSON_NO_MATCH if v is NULL or not a boolean,
 * or SOF_JSON_ERROR if value is NULL.
 */
extern sof_json_error_t sof_json_value_bool(const sof_json_value_t *v, int *value);

/**
 * @brief Gives the number of elements in an array or members in an object.
 * @param v The array or object.
 * @return The number, or 0 if v is NULL or neither.
 */
extern size_t sof_json_value_count(const sof_json_value_t *v);

/**
 * @brief Gives a number's value as a double.
 * @param v The value.
 * @param value Where the double goes.
 * @return As sof_json_number_double(), and SOF_JSON_NO_MATCH if v is NULL
 * or not a number.
 */
extern sof_json_error_t sof_json_value_double(const sof_json_value_t *v, double *value);

/**
 * @brief Finds the value of an object's member by its key.
 * @details If the key is there more than once, the last member with it is
 * found, as most readers do. An object with more than SOF_JSON_INDEX_MIN
 * members is looked up in the document's index; a smaller one is searched.
 * @param object The object.
 * @param key The NUL-terminated UTF-8 key.
 * @return The value, or NULL if no member has the key, or object is NULL or
 * not an object, or key is NULL.
 */
extern const sof_json_value_t *sof_json_value_get(const sof_json_value_t *object, const char *key);

/**
 * @brief Finds the value of an object's member by its key, from bytes.
 * @details As sof_json_value_get(), with the length bytes at key, which can
 * hold NULs.
 * @param object The object.
 * @param key The key. It can be NULL if length is 0.
 * @param length Its size in bytes.
 * @return As sof_json_value_get().
 */
extern const sof_json_value_t *sof_json_value_get_bytes(const sof_json_value_t *object, const void *key, size_t length);

/**
 * @brief Gives a number's value as a signed integer.
 * @param v The value.
 * @param value Where the integer goes. It is left alone on failure.
 * @return As sof_json_number_int(), and SOF_JSON_NO_MATCH if v is NULL or
 * not a number.
 */
extern sof_json_error_t sof_json_value_int(const sof_json_value_t *v, int64_t *value);

/**
 * @brief Returns a member of an object, in the order of the object's
 * members.
 * @param object The object.
 * @param index The member's index, counting from 0.
 * @return The member, or NULL if object is NULL or not an object, or index
 * is past its end.
 */
extern const sof_json_member_t *sof_json_value_member(const sof_json_value_t *object, size_t index);

/**
 * @brief Finds a value by a JSON Pointer.
 * @details The pointer is as RFC 6901 defines it: "" is v itself, and each
 * "/" and the token after it go a level down, to the member of an object
 * with that key, or the element of an array with that index, written in
 * decimal without leading zeros. In a token, "~1" stands for "/" and "~0"
 * for "~". Keys are found as sof_json_value_get() finds them.
 * @param v The value to start from.
 * @param pointer The NUL-terminated pointer, such as "/items/0/name".
 * @return The value, or NULL if there is none, the pointer isn't one, or v
 * or pointer is NULL.
 */
extern const sof_json_value_t *sof_json_value_pointer(const sof_json_value_t *v, const char *pointer);

/**
 * @brief Returns a string's bytes.
 * @param v The value.
 * @param length Where the number of bytes goes, not counting the NUL, or
 * NULL. It is set to 0 if v isn't a string.
 * @return The bytes, decoded UTF-8 that can hold NULs, NUL-terminated, which
 * belong to the document; or NULL if v is NULL or not a string.
 */
extern const char *sof_json_value_string(const sof_json_value_t *v, size_t *length);

/**
 * @brief Gives a value's type.
 * @param v The value.
 * @return Its type, or SOF_JSON_NONE if v is NULL.
 */
extern sof_json_type_t sof_json_value_type(const sof_json_value_t *v);

/**
 * @brief Gives a number's value as an unsigned integer.
 * @param v The value.
 * @param value Where the integer goes. It is left alone on failure.
 * @return As sof_json_number_uint(), and SOF_JSON_NO_MATCH if v is NULL
 * or not a number.
 */
extern sof_json_error_t sof_json_value_uint(const sof_json_value_t *v, uint64_t *value);

/**
 * @brief Begins an array.
 * @details The array is a value, so it goes where a value can: at the top
 * level, in an array, or in an object after its key. Values written until
 * sof_json_writer_array_end() are its elements.
 * @param w The writer.
 * @return SOF_JSON_OK, SOF_JSON_NO_MEM, or SOF_JSON_ERROR if w is NULL
 * or not initialized, or a value can't go here. On failure nothing is
 * written.
 */
extern sof_json_error_t sof_json_writer_array_begin(sof_json_writer_t *w);

/**
 * @brief Ends the array begun last.
 * @param w The writer.
 * @return SOF_JSON_OK, SOF_JSON_NO_MEM, SOF_JSON_WRITE if this ends a
 * top-level value and the write function failed, or SOF_JSON_ERROR if w is
 * NULL or not initialized, or what is open last isn't an array. On failure
 * other than SOF_JSON_WRITE, nothing is written and the array stays open.
 */
extern sof_json_error_t sof_json_writer_array_end(sof_json_writer_t *w);

/**
 * @brief Writes true or false.
 * @param w The writer.
 * @param value Non-zero for true, 0 for false.
 * @return As sof_json_writer_null().
 */
extern sof_json_error_t sof_json_writer_bool(sof_json_writer_t *w, int value);

/**
 * @brief Starts again, empty.
 * @details Discards the buffer, without passing it to the write function,
 * and every object and array that is open, so the next value is a new
 * top-level value. values and written go back to 0. The memory is kept, and
 * so are the indent, flags and write function.
 * @param w The writer.
 * @return SOF_JSON_OK, or SOF_JSON_ERROR if w is NULL or not
 * initialized.
 */
extern sof_json_error_t sof_json_writer_clear(sof_json_writer_t *w);

/**
 * @brief Says whether the output is whole.
 * @param w The writer.
 * @return 1 if at least one top-level value has been written and nothing
 * is open, so the output is complete JSON (or JSON Lines), or 0, also for a
 * NULL or uninitialized writer.
 */
extern int sof_json_writer_complete(const sof_json_writer_t *w);

/**
 * @brief Gives the number of objects and arrays that are open.
 * @param w The writer.
 * @return The number, 0 at the top level, or 0 if w is NULL or not
 * initialized.
 */
extern size_t sof_json_writer_depth(const sof_json_writer_t *w);

/**
 * @brief Writes a number from a double.
 * @details The number is written by sof_real_format(), with the fewest
 * significant digits, up to 17, that read back as the same double, the
 * closest to it of those, as in 0.1, 1e+100 or -0, with a '.' as its
 * decimal point whatever the locale. Numbers from 1e-4 up to 1e17 are
 * written without an exponent, as 100 rather than 1e+02 and 0.001 rather
 * than 1e-03.
 * @param w The writer.
 * @param value The number. JSON has no infinities or NaNs.
 * @return As sof_json_writer_null(), and SOF_JSON_ERROR if value is infinite
 * or a NaN.
 */
extern sof_json_error_t sof_json_writer_double(sof_json_writer_t *w, double value);

/**
 * @brief Passes the buffer to the write function.
 * @details Does nothing if there is no write function or the buffer is
 * empty. Output can be flushed at any point, in the middle of a value too.
 * @param w The writer.
 * @return SOF_JSON_OK, emptying the buffer; SOF_JSON_WRITE if the write
 * function failed, which leaves the buffer as it was; or SOF_JSON_ERROR if
 * w is NULL or not initialized.
 */
extern sof_json_error_t sof_json_writer_flush(sof_json_writer_t *w);

/**
 * @brief Releases the writer's memory.
 * @details The buffer is discarded, not flushed: call sof_json_writer_flush()
 * first to keep it. The sof_json_writer_t itself isn't freed (it belongs to the
 * caller); it is left empty, and acts like a writer whose sof_json_writer_init()
 * failed until it is initialized again.
 * @param w The writer.
 * @return SOF_JSON_OK, also for a writer not initialized, or
 * SOF_JSON_ERROR if w is NULL.
 */
extern sof_json_error_t sof_json_writer_free(sof_json_writer_t *w);

/**
 * @brief Initializes a writer in a sof_json_writer_t the caller provides.
 * @param w The writer to initialize.
 * @param indent The spaces to indent each level of objects and arrays by,
 * with each member and element on a line of its own, or 0 to write each
 * top-level value on one line, with no spaces.
 * @param flags The sof_json_flag_t flags: SOF_JSON_ASCII, or 0.
 * @param write The function to pass output to, or NULL to keep it all in
 * the buffer.
 * @param ctx The context pointer passed to write.
 * @param alloc The memory functions for the buffer and the record of what is
 * open, or NULL for the standard functions. The writer keeps a copy, so it
 * can be on the stack.
 * @return SOF_JSON_OK, SOF_JSON_NO_MEM, or SOF_JSON_ERROR if w is NULL
 * or alloc has a malloc but no free. On failure the writer is left empty and
 * not initialized.
 */
extern sof_json_error_t sof_json_writer_init(sof_json_writer_t *w, size_t indent, unsigned flags, sof_json_write_func_t write, void *ctx, const sof_alloc_t *alloc);

/**
 * @brief Writes a number from a signed integer.
 * @param w The writer.
 * @param value The number.
 * @return As sof_json_writer_null().
 */
extern sof_json_error_t sof_json_writer_int(sof_json_writer_t *w, int64_t value);

/**
 * @brief Writes the key of an object's next member.
 * @details The key is escaped as sof_json_writer_string() escapes a string. The
 * value written next is the member's value. The writer doesn't check that
 * keys are unique.
 * @param w The writer.
 * @param key The NUL-terminated UTF-8 key.
 * @return SOF_JSON_OK, SOF_JSON_NO_MEM, or SOF_JSON_ERROR if w or key
 * is NULL, w is not initialized, or what is open last isn't an object
 * waiting for a key. On failure nothing is written.
 */
extern sof_json_error_t sof_json_writer_key(sof_json_writer_t *w, const char *key);

/**
 * @brief Writes the key of an object's next member, from bytes.
 * @details As sof_json_writer_key(), with the length bytes at key, which can
 * hold NULs, written as \u0000.
 * @param w The writer.
 * @param key The UTF-8 key. It can be NULL if length is 0.
 * @param length Its size in bytes.
 * @return As sof_json_writer_key(), and SOF_JSON_ERROR if key is NULL and
 * length isn't 0.
 */
extern sof_json_error_t sof_json_writer_key_bytes(sof_json_writer_t *w, const void *key, size_t length);

/**
 * @brief Writes null.
 * @details A value goes at the top level, in an array, or in an object after
 * its key. A top-level value is followed by a newline, and if there is a
 * write function, the buffer is passed to it.
 * @param w The writer.
 * @return SOF_JSON_OK, SOF_JSON_NO_MEM, SOF_JSON_WRITE if the value was
 * written but the write function failed, or SOF_JSON_ERROR if w is NULL or
 * not initialized, or a value can't go here. On failure other than
 * SOF_JSON_WRITE, nothing is written.
 */
extern sof_json_error_t sof_json_writer_null(sof_json_writer_t *w);

/**
 * @brief Begins an object.
 * @details The object goes where a value can, as sof_json_writer_array_begin()
 * says. Until sof_json_writer_object_end(), its members are written as a
 * sof_json_writer_key() and then a value.
 * @param w The writer.
 * @return As sof_json_writer_array_begin().
 */
extern sof_json_error_t sof_json_writer_object_begin(sof_json_writer_t *w);

/**
 * @brief Ends the object begun last.
 * @param w The writer.
 * @return As sof_json_writer_array_end(), and SOF_JSON_ERROR if what is open
 * last isn't an object, or it has a key waiting for its value.
 */
extern sof_json_error_t sof_json_writer_object_end(sof_json_writer_t *w);

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
extern const char *sof_json_writer_output(const sof_json_writer_t *w, size_t *length);

/**
 * @brief Writes a value that is already JSON.
 * @details The bytes are written as they are, as one value, where a value
 * can go, so they must be one whole JSON value: the writer doesn't check
 * them. They aren't indented further.
 * @param w The writer.
 * @param json The JSON.
 * @param length Its size in bytes.
 * @return As sof_json_writer_null(), and SOF_JSON_ERROR if json is NULL or
 * length is 0.
 */
extern sof_json_error_t sof_json_writer_raw(sof_json_writer_t *w, const void *json, size_t length);

/**
 * @brief Writes a string.
 * @details The string is put in quotes, with ", \ and the control
 * characters below U+0020 escaped, as \b, \f, \n, \r, \t or \u00XX. UTF-8
 * is written as it is, or with SOF_JSON_ASCII as \u escapes. Bytes that
 * aren't valid UTF-8 are written as U+FFFD, one for each error, as
 * sof_utf8_decode() finds them.
 * @param w The writer.
 * @param s The NUL-terminated UTF-8 string.
 * @return As sof_json_writer_null(), and SOF_JSON_ERROR if s is NULL.
 */
extern sof_json_error_t sof_json_writer_string(sof_json_writer_t *w, const char *s);

/**
 * @brief Writes a string, from bytes.
 * @details As sof_json_writer_string(), with the length bytes at s, which can
 * hold NULs, written as \u0000.
 * @param w The writer.
 * @param s The UTF-8 string. It can be NULL if length is 0.
 * @param length Its size in bytes.
 * @return As sof_json_writer_null(), and SOF_JSON_ERROR if s is NULL and length
 * isn't 0.
 */
extern sof_json_error_t sof_json_writer_string_bytes(sof_json_writer_t *w, const void *s, size_t length);

/**
 * @brief Writes a number from an unsigned integer.
 * @param w The writer.
 * @param value The number.
 * @return As sof_json_writer_null().
 */
extern sof_json_error_t sof_json_writer_uint(sof_json_writer_t *w, uint64_t value);

/**
 * @brief Writes a document's value, with all it holds.
 * @details The value goes where a value can, and is written as the calls
 * for each of its parts would write it, numbers with their text as it was
 * read. The whole value is built in the buffer before any of it is passed to
 * the write function, so if a call fails, nothing is written.
 * @param w The writer.
 * @param v The value, from a sof_json_doc_t.
 * @return As sof_json_writer_null(), and SOF_JSON_ERROR if v is NULL.
 */
extern sof_json_error_t sof_json_writer_value(sof_json_writer_t *w, const sof_json_value_t *v);

#endif // SOF_JSON_H
