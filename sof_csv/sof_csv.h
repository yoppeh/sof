/**
 * @file sof_csv.h
 * @author Warren Mann (warren@nonvol.io)
 * @brief A streaming reader and writer of comma-separated values in C.
 * @details A sof_csv_reader_t returns one record at a time, as an array of
 * sof_string_t fields, from input it pulls through a read function, and a
 * sof_csv_writer_t writes records a field or a record at a time, quoting
 * the fields that need it, into a buffer it passes to a write function as it
 * fills. A program converts a file of any size in the memory one record
 * takes:
 *
 *     while (sof_csv_reader_next(&r, &fields, &count) == SOF_CSV_OK) {
 *         ... sof_csv_writer_record(&w, out, n);
 *     }
 *
 * sof_csv_read_file() and sof_csv_write_file() are read and write
 * functions for a FILE *.
 *
 * Both follow RFC 4180. Fields are separated by a delimiter, ',' unless
 * another is given, and records by a line break. A field in double quotes
 * can hold the delimiter, line breaks, and "" for a quote. The reader takes
 * LF, CRLF and a lone CR as line breaks; the writer ends records with LF, or
 * with SOF_CSV_CRLF, CRLF. Fields are bytes: the library doesn't check or
 * change their encoding.
 *
 * By default the reader is lenient, as most are: it skips blank lines, a
 * quote in the middle of a field without quotes is part of the field, text
 * after a closing quote is added to the field, and a quoted field still open
 * at the end of the input ends there. With SOF_CSV_STRICT it reads exactly
 * RFC 4180's grammar, but for taking LF and CR as line breaks and fields of
 * any bytes: a blank line is a record of one empty field, as the grammar
 * makes it, and the rest are errors.
 *
 * sof_csv_reader_spans() says where the fields and the record last read are
 * in the input, as byte offsets and lengths, so an application holding the
 * text can say which line a field is on, with sof_string_position(), or
 * change the text by splicing it at those offsets.
 * @version 0.0.1
 * @date 2026-11-19
 * @copyright Copyright (c) 2026
 */

#ifndef SOF_CSV_H
#define SOF_CSV_H

#include <stddef.h>
#include <stdio.h>

#include "sof_alloc.h"
#include "sof_string.h"
#include "sof_vector.h"

/// @brief The bytes the reader asks its read function for at a time.
#define SOF_CSV_READ_SIZE 4096

/// @brief The size the writer's buffer can reach before the end of a record
/// passes it to the write function.
#define SOF_CSV_FLUSH_SIZE 4096

/**
 * @brief Flags for sof_csv_reader_init() and sof_csv_writer_init(), which
 * can be combined with |.
 */
typedef enum sof_csv_flag_t {
    /// @brief Reader: the first record of the input names the columns. The
    /// first sof_csv_reader_next() reads it and keeps it, for
    /// sof_csv_reader_header() and sof_csv_reader_column(), and returns
    /// the record after it.
    SOF_CSV_HEADER = 1,
    /// @brief Reader: read RFC 4180's grammar exactly, but for LF and CR
    /// line breaks and fields of any bytes. A blank line is a record of one
    /// empty field, and a quote in a field without quotes, text after a
    /// closing quote, and a quoted field open at the end of the input are
    /// errors, SOF_CSV_SYNTAX.
    SOF_CSV_STRICT = 2,
    /// @brief Writer: end records with CRLF, as RFC 4180 has it, rather
    /// than LF.
    SOF_CSV_CRLF = 4,
    /// @brief Writer: quote every field, not just those that need it.
    SOF_CSV_QUOTE_ALL = 8,
} sof_csv_flag_t;

/**
 * @brief Error codes for the sof_csv functions.
 */
typedef enum sof_csv_error_t {
    SOF_CSV_OK = 0,
    SOF_CSV_NO_MEM,
    /// @brief sof_csv_reader_next(): there are no more records.
    /// sof_csv_reader_column(): no column has the name.
    SOF_CSV_NO_MATCH,
    SOF_CSV_ERROR,
    /// @brief The input isn't valid CSV, which only a SOF_CSV_STRICT reader
    /// reports. The reader's line says where.
    SOF_CSV_SYNTAX,
    /// @brief The reader's read function failed.
    SOF_CSV_READ,
    /// @brief The writer's write function failed. What was written is kept
    /// in the buffer.
    SOF_CSV_WRITE,
} sof_csv_error_t;

/**
 * @brief Gives the reader input.
 * @details Called with a buffer of size bytes, the reader's own, and the
 * ctx given to sof_csv_reader_init(). It puts up to size bytes in buffer
 * and their number in *length, which is 0 at the end of the input. It may
 * return fewer bytes than size, as a pipe or socket does. It must not call
 * into the reader.
 * @return 0, or non-zero if the input couldn't be read, which
 * sof_csv_reader_next() returns as SOF_CSV_READ.
 */
typedef int (*sof_csv_read_func_t)(char *buffer, size_t size, size_t *length, void *ctx);

/**
 * @brief Receives output from the writer.
 * @details Called with the length bytes of output at data, which belong to
 * the writer, and the ctx given to sof_csv_writer_init(). It must not call
 * into the writer.
 * @return 0 if every byte was written, or non-zero if they couldn't be, in
 * which case the writer keeps them all in its buffer and the call that
 * flushed returns SOF_CSV_WRITE.
 */
typedef int (*sof_csv_write_func_t)(const char *data, size_t length, void *ctx);

/**
 * @brief Where a field or record is in the input.
 */
typedef struct sof_csv_span_t {
    /// @brief Its first byte's offset from the start of the input, or from
    /// where the reader was last cleared.
    size_t offset;
    /// @brief Its size in bytes.
    size_t length;
} sof_csv_span_t;

/**
 * @brief A CSV reader.
 * @details The application provides the sof_csv_reader_t (on the stack, in
 * another structure or on the heap) and sets it up with
 * sof_csv_reader_init(). The fields can be read but must not be changed.
 *
 * Every function accepts a NULL reader, and one that was freed or whose
 * sof_csv_reader_init() failed (ready is 0), which reads nothing.
 */
typedef struct sof_csv_reader_t {
    /// @brief The fields of the record being read, sof_string_t, kept from
    /// record to record so their memory is reused; there can be more than
    /// count, from an earlier, longer record.
    sof_vector_t fields;
    /// @brief The fields in the record being read, or last returned.
    size_t count;
    /// @brief The spans of the fields of the record being read, or last
    /// returned, sof_csv_span_t, kept from record to record; there can be
    /// more than count.
    sof_vector_t spans;
    /// @brief The span of the record being read, or last returned.
    sof_csv_span_t record;
    /// @brief The bytes of input before those in buffer: with position, the
    /// offset of the next byte.
    size_t consumed;
    /// @brief 1 if the last sof_csv_reader_next() returned a record.
    int returned;
    /// @brief With SOF_CSV_HEADER, the first record's fields,
    /// sof_string_t, once it has been read.
    sof_vector_t header;
    /// @brief The input read but not yet taken apart, SOF_CSV_READ_SIZE
    /// bytes.
    char *buffer;
    /// @brief The bytes in buffer.
    size_t length;
    /// @brief Where in buffer the next byte to take apart is.
    size_t position;
    /// @brief The byte between fields.
    char delimiter;
    /// @brief The sof_csv_flag_t flags.
    unsigned flags;
    /// @brief The function input comes from.
    sof_csv_read_func_t read;
    /// @brief Application context passed to read.
    void *ctx;
    /// @brief The memory functions the buffer is allocated and freed with.
    sof_alloc_t alloc;
    /// @brief Where the reader is in a field.
    int state;
    /// @brief 1 once the record being read has a byte.
    int in_record;
    /// @brief 1 if the last byte was a CR, which an LF may belong to.
    int after_cr;
    /// @brief 1 once read has reported the end of the input.
    int ended;
    /// @brief 1 if the input ended inside a quoted field, which a reader
    /// that isn't strict ends there.
    int unterminated;
    /// @brief The line being read, counting from 1: after
    /// SOF_CSV_SYNTAX, the line the error is on.
    size_t line;
    /// @brief The line the record last returned starts on.
    size_t record_line;
    /// @brief The records returned, the header not counted.
    size_t records;
    /// @brief 1 once sof_csv_reader_init() has succeeded, 0 when freed or
    /// failed.
    int ready;
} sof_csv_reader_t;

/**
 * @brief A CSV writer.
 * @details The application provides the sof_csv_writer_t and sets it up
 * with sof_csv_writer_init(). The fields can be read but must not be
 * changed. out.data moves as the buffer grows, so a pointer into it is good
 * only until the next call that writes.
 *
 * Every function accepts a NULL writer, and one that was freed or whose
 * sof_csv_writer_init() failed (ready is 0), which writes nothing.
 */
typedef struct sof_csv_writer_t {
    /// @brief The output not yet passed to the write function: all of it,
    /// if there is none.
    sof_string_t out;
    /// @brief The byte between fields.
    char delimiter;
    /// @brief The sof_csv_flag_t flags.
    unsigned flags;
    /// @brief The function output is passed to, or NULL.
    sof_csv_write_func_t write;
    /// @brief Application context passed to write.
    void *ctx;
    /// @brief The fields in the record being written.
    size_t fields;
    /// @brief The bytes written for the record being written.
    size_t record_length;
    /// @brief The records finished.
    size_t records;
    /// @brief The bytes passed to write so far.
    size_t written;
    /// @brief 1 once sof_csv_writer_init() has succeeded, 0 when freed or
    /// failed.
    int ready;
} sof_csv_writer_t;

/**
 * @brief A read function for a FILE *.
 * @details Pass it to sof_csv_reader_init() with the FILE * as ctx. It
 * reads with fread(), and never closes the file.
 * @return 0, or 1 if the file couldn't be read or ctx is NULL.
 */
extern int sof_csv_read_file(char *buffer, size_t size, size_t *length, void *ctx);

/**
 * @brief Starts again, on whatever the read function gives next.
 * @details Drops the input read but not yet taken apart, the record being
 * read and the header, and starts on line 1 as if nothing had been read,
 * with records and unterminated back to 0. The memory is kept. Use it to
 * go on after an error, or when the read function has moved on to new
 * input.
 * @param r The reader.
 * @return SOF_CSV_OK, or SOF_CSV_ERROR if r is NULL or not initialized.
 */
extern sof_csv_error_t sof_csv_reader_clear(sof_csv_reader_t *r);

/**
 * @brief Finds a column by its name in the header.
 * @param r The reader, made with SOF_CSV_HEADER, once the first
 * sof_csv_reader_next() has read the header.
 * @param name The name, NUL-terminated.
 * @param index Where the column's index, counting from 0, goes, or NULL. If
 * the name is there more than once, the first is found. Left alone if none
 * is.
 * @return SOF_CSV_OK, SOF_CSV_NO_MATCH if no column has the name or the
 * header hasn't been read, or SOF_CSV_ERROR if r or name is NULL, or r is
 * not initialized.
 */
extern sof_csv_error_t sof_csv_reader_column(const sof_csv_reader_t *r, const char *name, size_t *index);

/**
 * @brief Releases the reader's memory.
 * @details The sof_csv_reader_t itself isn't freed (it belongs to the
 * caller); it is left empty, and acts like a reader whose
 * sof_csv_reader_init() failed until it is initialized again. The input
 * isn't closed.
 * @param r The reader.
 * @return SOF_CSV_OK, also for a reader not initialized, or SOF_CSV_ERROR
 * if r is NULL.
 */
extern sof_csv_error_t sof_csv_reader_free(sof_csv_reader_t *r);

/**
 * @brief Returns the header, the input's first record, read with
 * SOF_CSV_HEADER.
 * @param r The reader.
 * @param count Where the number of names goes, or NULL. It is set to 0 if
 * there is no header.
 * @return The names, an array of count strings that belong to the reader,
 * good until it is cleared or freed; or NULL if the header hasn't been
 * read, the reader wasn't made with SOF_CSV_HEADER, or r is NULL or not
 * initialized.
 */
extern const sof_string_t *sof_csv_reader_header(const sof_csv_reader_t *r, size_t *count);

/**
 * @brief Initializes a reader in a sof_csv_reader_t the caller provides.
 * @param r The reader to initialize.
 * @param delimiter The byte between fields, such as ',', ';' or '\t'. It
 * can't be a quote, CR or LF.
 * @param flags The sof_csv_flag_t flags: SOF_CSV_HEADER, SOF_CSV_STRICT,
 * or 0.
 * @param read The function input comes from, such as sof_csv_read_file().
 * @param ctx The context pointer passed to read, such as a FILE *.
 * @param alloc The memory functions for the buffer, fields and header, or
 * NULL for the standard functions. The reader keeps a copy, so it can be on
 * the stack.
 * @return SOF_CSV_OK, SOF_CSV_NO_MEM, or SOF_CSV_ERROR if r or read is
 * NULL, the delimiter can't be used, or alloc has a malloc but no free. On
 * failure the reader is left empty and not initialized.
 */
extern sof_csv_error_t sof_csv_reader_init(sof_csv_reader_t *r, char delimiter, unsigned flags, sof_csv_read_func_t read, void *ctx, const sof_alloc_t *alloc);

/**
 * @brief Reads the next record.
 * @details Takes bytes from the buffer, calling the read function when it
 * is empty, until a record is whole, and returns its fields. They belong to
 * the reader and are reused by the next call: copy what is to be kept. The
 * application may change their bytes, but not free or grow them. A record
 * has at least one field. Blank lines are skipped, or with SOF_CSV_STRICT
 * are records of one empty field. With SOF_CSV_HEADER,
 * the first call reads the header first. The record's line is in
 * record_line.
 *
 * A call that fails with SOF_CSV_NO_MEM or SOF_CSV_READ can be made again,
 * and goes on where it stopped. One that fails with SOF_CSV_SYNTAX fails
 * the same way again; sof_csv_reader_clear() goes on from the next input.
 * @param r The reader.
 * @param fields Where a pointer to the array of fields goes, or NULL. It is
 * set to NULL unless a record is returned.
 * @param count Where the number of fields goes, or NULL. It is set to 0
 * unless a record is returned.
 * @return SOF_CSV_OK, SOF_CSV_NO_MATCH at the end of the input,
 * SOF_CSV_NO_MEM, SOF_CSV_READ if the read function failed,
 * SOF_CSV_SYNTAX if a strict reader found an error on line, or
 * SOF_CSV_ERROR if r is NULL or not initialized. After the end, it goes on
 * returning SOF_CSV_NO_MATCH, without calling the read function, until the
 * reader is cleared.
 */
extern sof_csv_error_t sof_csv_reader_next(sof_csv_reader_t *r, sof_string_t **fields, size_t *count);

/**
 * @brief Says where the fields, and the record, that sof_csv_reader_next()
 * returned last are in the input.
 * @details A field's span is its text as written, quotes and the "" in
 * them included, but not the delimiter after it; an empty field's has
 * length 0, where it would be. A record's runs from its first byte to the
 * line break that ends it, which isn't included, or to the end of the
 * input. Offsets count from the start of the input, or from where the
 * reader was last cleared.
 * @param r The reader.
 * @param count Where the number of spans, the record's number of fields,
 * goes, or NULL. It is set to 0 if there is no record.
 * @param record Where the record's span goes, or NULL. It is set to 0 and
 * 0 if there is no record.
 * @return The spans, one for each field, which belong to the reader and are
 * good until its next call; or NULL if the last sof_csv_reader_next()
 * didn't return a record, or r is NULL or not initialized.
 */
extern const sof_csv_span_t *sof_csv_reader_spans(const sof_csv_reader_t *r, size_t *count, sof_csv_span_t *record);

/**
 * @brief A write function for a FILE *.
 * @details Pass it to sof_csv_writer_init() with the FILE * as ctx. It
 * writes with fwrite(), and never flushes or closes the file.
 * @return 0, or 1 if the bytes couldn't all be written or ctx is NULL.
 */
extern int sof_csv_write_file(const char *data, size_t length, void *ctx);

/**
 * @brief Forgets the output, to start again.
 * @details Discards the buffer, without passing it to the write function,
 * and the record being written. records and written go back to 0. The
 * memory is kept, and so are the delimiter, flags and write function.
 * @param w The writer.
 * @return SOF_CSV_OK, or SOF_CSV_ERROR if w is NULL or not initialized.
 */
extern sof_csv_error_t sof_csv_writer_clear(sof_csv_writer_t *w);

/**
 * @brief Writes a field of the record being written, from a c string.
 * @param w The writer.
 * @param s The field, NUL-terminated.
 * @return As sof_csv_writer_field_bytes(), and SOF_CSV_ERROR if s is
 * NULL.
 */
extern sof_csv_error_t sof_csv_writer_field(sof_csv_writer_t *w, const char *s);

/**
 * @brief Writes a field of the record being written.
 * @details The field is put in quotes, with each quote doubled, if it holds
 * the delimiter, a quote, a CR or an LF, or with SOF_CSV_QUOTE_ALL, always.
 * Its bytes are written as they are, NULs included.
 * @param w The writer.
 * @param s The field's bytes. It can be NULL if length is 0.
 * @param length Its size in bytes.
 * @return SOF_CSV_OK, SOF_CSV_NO_MEM, or SOF_CSV_ERROR if w is NULL or
 * not initialized, or s is NULL and length isn't 0. On failure nothing is
 * written.
 */
extern sof_csv_error_t sof_csv_writer_field_bytes(sof_csv_writer_t *w, const void *s, size_t length);

/**
 * @brief Passes the buffer to the write function.
 * @details Does nothing if there is no write function or the buffer is
 * empty. It can be called in the middle of a record too.
 * @param w The writer.
 * @return SOF_CSV_OK, emptying the buffer; SOF_CSV_WRITE if the write
 * function failed, which leaves the buffer as it was; or SOF_CSV_ERROR if w
 * is NULL or not initialized.
 */
extern sof_csv_error_t sof_csv_writer_flush(sof_csv_writer_t *w);

/**
 * @brief Releases the writer's memory.
 * @details The buffer is discarded, not flushed: call sof_csv_writer_flush()
 * first to keep it. The sof_csv_writer_t itself isn't freed; it is left
 * empty, and acts like a writer whose sof_csv_writer_init() failed until it
 * is initialized again.
 * @param w The writer.
 * @return SOF_CSV_OK, also for a writer not initialized, or SOF_CSV_ERROR
 * if w is NULL.
 */
extern sof_csv_error_t sof_csv_writer_free(sof_csv_writer_t *w);

/**
 * @brief Initializes a writer in a sof_csv_writer_t the caller provides.
 * @param w The writer to initialize.
 * @param delimiter The byte to write between fields. It can't be a quote,
 * CR or LF.
 * @param flags The sof_csv_flag_t flags: SOF_CSV_CRLF,
 * SOF_CSV_QUOTE_ALL, or 0.
 * @param write The function to pass output to, such as
 * sof_csv_write_file(), or NULL to keep it all in the buffer.
 * @param ctx The context pointer passed to write, such as a FILE *.
 * @param alloc The memory functions for the buffer, or NULL for the
 * standard functions. The writer keeps a copy, so it can be on the stack.
 * @return SOF_CSV_OK, SOF_CSV_NO_MEM, or SOF_CSV_ERROR if w is NULL, the
 * delimiter can't be used, or alloc has a malloc but no free. On failure
 * the writer is left empty and not initialized.
 */
extern sof_csv_error_t sof_csv_writer_init(sof_csv_writer_t *w, char delimiter, unsigned flags, sof_csv_write_func_t write, void *ctx, const sof_alloc_t *alloc);

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
extern const char *sof_csv_writer_output(const sof_csv_writer_t *w, size_t *length);

/**
 * @brief Writes a whole record.
 * @details Writes each field, as sof_csv_writer_field_bytes() does, after
 * any written already, and ends the record, as sof_csv_writer_record_end()
 * does, so a record sof_csv_reader_next() returned can be written as it
 * is.
 * @param w The writer.
 * @param fields An array of count strings. Dead strings are empty fields.
 * @param count The number of fields.
 * @return As sof_csv_writer_record_end(), and SOF_CSV_ERROR if fields is
 * NULL and count isn't 0. On failure other than SOF_CSV_WRITE, nothing is
 * written.
 */
extern sof_csv_error_t sof_csv_writer_record(sof_csv_writer_t *w, const sof_string_t *fields, size_t count);

/**
 * @brief Ends the record being written.
 * @details Writes the line break. A record of one empty field is written
 * as "", so that a reader doesn't take it for a blank line. If there is a
 * write function and the buffer has reached SOF_CSV_FLUSH_SIZE bytes, it is
 * passed on.
 * @param w The writer.
 * @return SOF_CSV_OK, SOF_CSV_NO_MEM, SOF_CSV_WRITE if the record was
 * ended but the write function failed, or SOF_CSV_ERROR if w is NULL or not
 * initialized, or the record has no fields, which CSV can't write. On
 * failure other than SOF_CSV_WRITE, nothing is written and the record stays
 * open.
 */
extern sof_csv_error_t sof_csv_writer_record_end(sof_csv_writer_t *w);

#endif // SOF_CSV_H
