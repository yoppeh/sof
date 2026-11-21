/**
 * @file main.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief Converts comma-separated values to JSON, and JSON to comma-separated
 * values.
 * @details Reads each file, or standard input, a record at a time with a
 * sof_csv reader, and writes each record as it is read with a sof_json
 * writer: by default, as one JSON array of objects, keyed by the names in
 * each file's first record; with -l, as JSON Lines, an object to a line;
 * and with -r, as arrays of fields, the first record included. The writer
 * passes its output to standard output a record at a time, so a file of
 * any size is converted in the memory one record takes.
 *
 * With -j, it goes the other way: it reads JSON with a sof_json reader,
 * a top-level array of records or a series of records, as in JSON Lines,
 * reads each record whole into a sof_json document as it comes, and writes
 * it with a sof_csv writer. The first record that is an object names the
 * columns, and its keys are looked up in each object after it. Again a file
 * of any size is converted in the memory one record takes.
 *
 *     csvjson [-h] [-a] [-j] [-l] [-n] [-r] [-d delimiter] [-i spaces] [file]...
 *
 * With no file, or a file named "-", it reads standard input. Its options
 * are read with sof_copt. It exits with 0, 1 if a file couldn't be read,
 * memory ran out or the output couldn't be written, and 2 for a usage
 * error.
 * @version 0.0.1
 * @date 2026-11-19
 * @copyright Copyright (c) 2026
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sof_copt.h"
#include "sof_csv.h"
#include "sof_json.h"
#include "sof_string.h"

/// @brief The exit codes.
#define EXIT_DONE 0
#define EXIT_TROUBLE 1
#define EXIT_USAGE 2

/// @brief The code the option validators return for a bad argument, after
/// printing why. sof_copt keeps it in the option's stop_value.
#define BAD_ARGUMENT 1

/// @brief The spaces each level is indented by when -i isn't given.
#define DEFAULT_INDENT 2

/// @brief The most spaces -i takes.
#define INDENT_MAX 16

/// @brief What csv_file() and json_file() return: the file was converted,
/// it couldn't be and the next can be tried, or nothing more can be written.
#define CONVERT_DONE 0
#define CONVERT_TROUBLE 1
#define CONVERT_STOP 2

/// @brief The settings the options give, filled in by their validators.
typedef struct settings_t {
    size_t indent;
    char delimiter;
} settings_t;

/// @brief What csv_file() and write_record() work with.
typedef struct convert_t {
    /// @brief The writer.
    sof_json_writer_t *w;
    /// @brief The input's delimiter.
    char delimiter;
    /// @brief -n: fields that are JSON numbers are written as numbers, and
    /// empty fields as null.
    int numbers;
    /// @brief -r: records are written as arrays, and the first is a record
    /// like the others.
    int rows;
} convert_t;

/// @brief What json_file() and json_record() work with, for -j.
typedef struct to_csv_t {
    /// @brief The CSV writer.
    sof_csv_writer_t csv;
    /// @brief The documents records are read into. Once a record that is an
    /// object names the columns, it stays in its document, and the other is
    /// used for the rest.
    sof_json_doc_t docs[2];
    /// @brief The document holding the record that names the columns, or
    /// NULL until there is one.
    sof_json_doc_t *names;
    /// @brief Writes values that are objects or arrays as JSON, for a field.
    sof_json_writer_t nested;
} to_csv_t;

/// @brief Converts one file: csv_file() or json_file().
typedef int (*convert_func_t)(FILE *f, const char *name, void *ctx);

static int convert_all(char **paths, int count, convert_func_t convert, void *ctx);
static int csv_file(FILE *f, const char *name, void *ctx);
static int delimiter_validate(sof_copt_t *option, void *ctx);
static int indent_validate(sof_copt_t *option, void *ctx);
static int is_number(const char *s, size_t length);
static sof_csv_error_t json_field(to_csv_t *to, const sof_json_value_t *v);
static int json_file(FILE *f, const char *name, void *ctx);
static sof_csv_error_t json_record(to_csv_t *to, sof_json_doc_t **d);
static int to_csv(char **paths, int count, char delimiter);
static void usage(FILE *stream);
static sof_json_error_t write_field(convert_t *convert, const sof_string_t *field);
static sof_json_error_t write_record(const sof_csv_reader_t *r, const sof_string_t *fields, size_t count, convert_t *convert);
static int write_stdout(const char *data, size_t length, void *ctx);

static sof_copt_t opt_help = {
    .option = "-h",
    .description = "print this help and exit",
};

static sof_copt_t opt_ascii = {
    .option = "-a",
    .description = "write characters outside ASCII as \\u escapes",
};

static sof_copt_t opt_json = {
    .option = "-j",
    .description = "read JSON and write CSV, taking only -d",
};

static sof_copt_t opt_lines = {
    .option = "-l",
    .description = "write JSON Lines, a record to a line, rather than one array",
};

static sof_copt_t opt_numbers = {
    .option = "-n",
    .description = "write fields that are numbers as numbers, and empty ones as null",
};

static sof_copt_t opt_rows = {
    .option = "-r",
    .description = "write each record as an array, without taking names from the first",
};

static sof_copt_t opt_delimiter = {
    .option = "-d",
    .description = "the character between fields, or \"tab\" (default ,)",
    .type = SOF_COPT_ARG_REQUIRED,
    .validate = delimiter_validate,
};

static sof_copt_t opt_indent = {
    .option = "-i",
    .description = "indent each level by this many spaces, 0 for one line (default 2)",
    .type = SOF_COPT_ARG_REQUIRED,
    .validate = indent_validate,
};

SOF_COPT_OPTIONS static sof_copt_t *options[] = {
    &opt_help, &opt_ascii, &opt_json, &opt_lines, &opt_numbers, &opt_rows, &opt_delimiter, &opt_indent,
};

/// @brief Converts each file in turn with convert, reading standard input
/// for "-", until one can't be written.
/// @return EXIT_DONE, or EXIT_TROUBLE if a file couldn't be opened or
/// converted.
static int convert_all(char **paths, int count, convert_func_t convert, void *ctx) {
    int status = EXIT_DONE;
    for (int i = 0; i < count; i++) {
        int is_stdin = strcmp(paths[i], "-") == 0;
        const char *name = is_stdin ? "standard input" : paths[i];
        FILE *f = is_stdin ? stdin : fopen(paths[i], "rb");
        if (f == NULL) {
            fprintf(stderr, "csvjson: %s: %s\n", name, strerror(errno));
            status = EXIT_TROUBLE;
            continue;
        }
        int rc = convert(f, name, ctx);
        if (is_stdin) {
            clearerr(stdin);
        } else {
            fclose(f);
        }
        if (rc != CONVERT_DONE) {
            status = EXIT_TROUBLE;
            if (rc == CONVERT_STOP) {
                break;
            }
        }
    }
    return status;
}

/// @brief Reads f, CSV, to its end, writing each record as JSON as it is
/// read. ctx is the convert_t.
/// @return CONVERT_DONE, CONVERT_TROUBLE if f couldn't be read, or
/// CONVERT_STOP if memory ran out or the output couldn't be written, after
/// printing why.
static int csv_file(FILE *f, const char *name, void *ctx) {
    convert_t *convert = ctx;
    sof_csv_reader_t r;
    sof_string_t *fields;
    size_t count;
    sof_csv_error_t rc;
    sof_json_error_t written = SOF_JSON_OK;
    unsigned flags = convert->rows ? 0 : SOF_CSV_HEADER;
    if (sof_csv_reader_init(&r, convert->delimiter, flags, sof_csv_read_file, f, NULL) != SOF_CSV_OK) {
        fprintf(stderr, "csvjson: out of memory\n");
        return CONVERT_STOP;
    }
    while ((rc = sof_csv_reader_next(&r, &fields, &count)) == SOF_CSV_OK) {
        if ((written = write_record(&r, fields, count, convert)) != SOF_JSON_OK) {
            break;
        }
    }
    int result = CONVERT_DONE;
    if (written == SOF_JSON_NO_MEM || rc == SOF_CSV_NO_MEM) {
        fprintf(stderr, "csvjson: out of memory reading %s\n", name);
        result = CONVERT_STOP;
    } else if (written != SOF_JSON_OK) {
        fprintf(stderr, "csvjson: write error\n");
        result = CONVERT_STOP;
    } else if (rc == SOF_CSV_READ) {
        fprintf(stderr, "csvjson: %s: read error\n", name);
        result = CONVERT_TROUBLE;
    } else if (r.unterminated) {
        fprintf(stderr, "csvjson: %s: the last quoted field has no closing quote\n", name);
    }
    sof_csv_reader_free(&r);
    return result;
}

/// @brief Reads -d's argument, a single character or "tab", into the
/// settings in ctx.
static int delimiter_validate(sof_copt_t *option, void *ctx) {
    settings_t *settings = ctx;
    const char *s = option->value;
    if (strcmp(s, "tab") == 0 || strcmp(s, "\\t") == 0) {
        settings->delimiter = '\t';
    } else if (strlen(s) == 1 && s[0] != '"' && s[0] != '\n' && s[0] != '\r') {
        settings->delimiter = s[0];
    } else {
        fprintf(stderr, "csvjson: -d needs one character other than a quote, or \"tab\", not \"%s\"\n", s);
        return BAD_ARGUMENT;
    }
    return 0;
}

/// @brief Reads -i's argument, a number of spaces, into the settings in
/// ctx.
static int indent_validate(sof_copt_t *option, void *ctx) {
    settings_t *settings = ctx;
    const char *s = option->value;
    char *end;
    errno = 0;
    unsigned long value = strtoul(s, &end, 10);
    if (*s < '0' || *s > '9' || *end != '\0' || errno != 0 || value > INDENT_MAX) {
        fprintf(stderr, "csvjson: -i needs a number from 0 to %d, not \"%s\"\n", INDENT_MAX, s);
        return BAD_ARGUMENT;
    }
    settings->indent = (size_t)value;
    return 0;
}

/// @brief Says whether the length bytes at s are a number as JSON writes
/// one: an optional minus, an integer part without leading zeros, and an
/// optional fraction and exponent.
static int is_number(const char *s, size_t length) {
    size_t i = 0;
    if (i < length && s[i] == '-') {
        i++;
    }
    if (i < length && s[i] == '0') {
        i++;
    } else if (i < length && s[i] >= '1' && s[i] <= '9') {
        while (i < length && s[i] >= '0' && s[i] <= '9') {
            i++;
        }
    } else {
        return 0;
    }
    if (i < length && s[i] == '.') {
        size_t start = ++i;
        while (i < length && s[i] >= '0' && s[i] <= '9') {
            i++;
        }
        if (i == start) {
            return 0;
        }
    }
    if (i < length && (s[i] == 'e' || s[i] == 'E')) {
        i++;
        if (i < length && (s[i] == '+' || s[i] == '-')) {
            i++;
        }
        size_t start = i;
        while (i < length && s[i] >= '0' && s[i] <= '9') {
            i++;
        }
        if (i == start) {
            return 0;
        }
    }
    return i == length;
}

/// @brief Writes a JSON value as a CSV field: a string's text, a number as
/// it was written, true or false, nothing for null or a missing value, and
/// an object or array as compact JSON.
/// @return As sof_csv_writer_field_bytes().
static sof_csv_error_t json_field(to_csv_t *to, const sof_json_value_t *v) {
    switch (sof_json_value_type(v)) {
    case SOF_JSON_BOOL:
        return sof_csv_writer_field(&to->csv, v->as.boolean ? "true" : "false");
    case SOF_JSON_NUMBER:
    case SOF_JSON_STRING:
        return sof_csv_writer_field_bytes(&to->csv, v->as.text, v->length);
    case SOF_JSON_ARRAY:
    case SOF_JSON_OBJECT: {
        size_t length;
        sof_json_writer_clear(&to->nested);
        if (sof_json_writer_value(&to->nested, v) != SOF_JSON_OK) {
            return SOF_CSV_NO_MEM;
        }
        // Less the newline after a top-level value.
        const char *json = sof_json_writer_output(&to->nested, &length);
        return sof_csv_writer_field_bytes(&to->csv, json, length - 1);
    }
    default:
        return sof_csv_writer_field_bytes(&to->csv, NULL, 0);
    }
}

/// @brief Reads f, JSON, to its end, writing each record as CSV as it is
/// read: each element of a top-level array, and each other top-level value.
/// ctx is the to_csv_t.
/// @return CONVERT_DONE, CONVERT_TROUBLE if f couldn't be read or isn't
/// JSON, or CONVERT_STOP if memory ran out or the output couldn't be
/// written, after printing why.
static int json_file(FILE *f, const char *name, void *ctx) {
    to_csv_t *to = ctx;
    sof_json_reader_t r;
    sof_json_error_t rc;
    sof_csv_error_t written = SOF_CSV_OK;
    if (sof_json_reader_init(&r, SOF_JSON_LINES, sof_json_read_file, f, NULL) != SOF_JSON_OK) {
        fprintf(stderr, "csvjson: out of memory\n");
        return CONVERT_STOP;
    }
    sof_json_doc_t *d = to->names == &to->docs[0] ? &to->docs[1] : &to->docs[0];
    int in_array = 0;
    for (;;) {
        // At the top level, an array is gone into, to read its elements
        // one at a time; anything else is a record.
        if (!in_array) {
            sof_json_type_t type;
            rc = sof_json_reader_peek(&r, &type);
            if (rc == SOF_JSON_OK && type == SOF_JSON_ARRAY) {
                rc = sof_json_reader_next(&r, NULL);
                in_array = 1;
            }
            if (rc != SOF_JSON_OK) {
                break;
            }
        }
        rc = sof_json_doc_read(d, &r);
        if (rc == SOF_JSON_NO_MATCH && in_array) {
            in_array = 0;
            continue;
        }
        if (rc != SOF_JSON_OK || (written = json_record(to, &d)) != SOF_CSV_OK) {
            break;
        }
    }
    int result = CONVERT_DONE;
    if (written == SOF_CSV_NO_MEM || rc == SOF_JSON_NO_MEM) {
        fprintf(stderr, "csvjson: out of memory reading %s\n", name);
        result = CONVERT_STOP;
    } else if (written != SOF_CSV_OK) {
        fprintf(stderr, "csvjson: write error\n");
        result = CONVERT_STOP;
    } else if (rc == SOF_JSON_READ) {
        fprintf(stderr, "csvjson: %s: read error\n", name);
        result = CONVERT_TROUBLE;
    } else if (rc == SOF_JSON_SYNTAX || rc == SOF_JSON_TOO_DEEP) {
        fprintf(stderr, "csvjson: %s:%zu:%zu: %s\n", name, r.line, r.column, rc == SOF_JSON_SYNTAX ? "not JSON" : "objects and arrays nest too deep");
        result = CONVERT_TROUBLE;
    }
    sof_json_reader_free(&r);
    return result;
}

/// @brief Writes the record in *d as a CSV record: an object's values, in
/// the order of the names, an array's elements, or a single value.
/// @details The first object with a member names the columns: its keys are
/// written as a record first, and it is kept, with *d moved to the other
/// document for the records after it. Members of later objects whose keys
/// aren't among the names are left out, and names an object hasn't got are
/// empty fields.
/// @return As sof_csv_writer_record_end().
static sof_csv_error_t json_record(to_csv_t *to, sof_json_doc_t **d) {
    const sof_json_value_t *v = (*d)->root;
    sof_json_type_t type = sof_json_value_type(v);
    sof_csv_error_t rc = SOF_CSV_OK;
    size_t count = 0;
    if (type == SOF_JSON_OBJECT && to->names == NULL && sof_json_value_count(v) > 0) {
        to->names = *d;
        *d = *d == &to->docs[0] ? &to->docs[1] : &to->docs[0];
        for (size_t i = 0; rc == SOF_CSV_OK && i < sof_json_value_count(v); i++) {
            const sof_json_member_t *m = sof_json_value_member(v, i);
            rc = sof_csv_writer_field_bytes(&to->csv, m->key, m->key_length);
        }
        if (rc == SOF_CSV_OK) {
            rc = sof_csv_writer_record_end(&to->csv);
        }
    }
    if (type == SOF_JSON_OBJECT) {
        const sof_json_value_t *names = to->names != NULL ? to->names->root : NULL;
        count = sof_json_value_count(names);
        for (size_t i = 0; rc == SOF_CSV_OK && i < count; i++) {
            const sof_json_member_t *m = sof_json_value_member(names, i);
            rc = json_field(to, sof_json_value_get_bytes(v, m->key, m->key_length));
        }
    } else if (type == SOF_JSON_ARRAY) {
        count = sof_json_value_count(v);
        for (size_t i = 0; rc == SOF_CSV_OK && i < count; i++) {
            rc = json_field(to, sof_json_value_at(v, i));
        }
    } else {
        rc = json_field(to, v);
        count = 1;
    }
    // A record has at least one field.
    if (rc == SOF_CSV_OK && count == 0) {
        rc = sof_csv_writer_field_bytes(&to->csv, NULL, 0);
    }
    return rc == SOF_CSV_OK ? sof_csv_writer_record_end(&to->csv) : rc;
}

/// @brief Converts JSON to CSV, for -j.
/// @return The exit code.
static int to_csv(char **paths, int count, char delimiter) {
    static to_csv_t to;
    if (sof_csv_writer_init(&to.csv, delimiter, 0, sof_csv_write_file, stdout, NULL) != SOF_CSV_OK || sof_json_doc_init(&to.docs[0], NULL) != SOF_JSON_OK || sof_json_doc_init(&to.docs[1], NULL) != SOF_JSON_OK || sof_json_writer_init(&to.nested, 0, 0, NULL, NULL, NULL) != SOF_JSON_OK) {
        fprintf(stderr, "csvjson: out of memory\n");
        return EXIT_TROUBLE;
    }
    int status = convert_all(paths, count, json_file, &to);
    if (sof_csv_writer_flush(&to.csv) != SOF_CSV_OK) {
        fprintf(stderr, "csvjson: write error\n");
        status = EXIT_TROUBLE;
    }
    sof_csv_writer_free(&to.csv);
    sof_json_doc_free(&to.docs[0]);
    sof_json_doc_free(&to.docs[1]);
    sof_json_writer_free(&to.nested);
    return status;
}

/// @brief Prints the usage message, with the options sof_copt knows.
static void usage(FILE *stream) {
    fprintf(stream, "usage: csvjson [-h] [-a] [-j] [-l] [-n] [-r] [-d delimiter] [-i spaces] [file]...\n");
    sof_copt_print(stream);
    fprintf(stream,
        "Converts comma-separated values to JSON: an array of objects keyed by the\n"
        "names in each file's first record. With -j, converts JSON, an array of\n"
        "records or one record after another, to comma-separated values. Reads\n"
        "standard input if there are no files or a file is \"-\". Exits with 0, 1 if\n"
        "a file can't be read or the output written, 2 for bad options.\n");
}

/// @brief Writes a field as a string, or with -n as a number if it is one,
/// or null if it is empty.
static sof_json_error_t write_field(convert_t *convert, const sof_string_t *field) {
    if (convert->numbers && field->length == 0) {
        return sof_json_writer_null(convert->w);
    }
    if (convert->numbers && is_number(field->data, field->length)) {
        return sof_json_writer_raw(convert->w, field->data, field->length);
    }
    return sof_json_writer_string_bytes(convert->w, field->data, field->length);
}

/// @brief Writes a record as an object, keyed by the names in the header,
/// or with -r as an array.
/// @details A field past the last name is keyed by its column number,
/// counting from 1, and a name past the last field gets null.
static sof_json_error_t write_record(const sof_csv_reader_t *r, const sof_string_t *fields, size_t count, convert_t *convert) {
    sof_json_writer_t *w = convert->w;
    size_t names_count;
    const sof_string_t *names = sof_csv_reader_header(r, &names_count);
    sof_json_error_t rc = convert->rows ? sof_json_writer_array_begin(w) : sof_json_writer_object_begin(w);
    size_t columns = convert->rows || count > names_count ? count : names_count;
    for (size_t i = 0; rc == SOF_JSON_OK && i < columns; i++) {
        if (!convert->rows) {
            if (i < names_count) {
                rc = sof_json_writer_key_bytes(w, names[i].data, names[i].length);
            } else {
                char number[24];
                snprintf(number, sizeof(number), "%zu", i + 1);
                rc = sof_json_writer_key(w, number);
            }
        }
        if (rc == SOF_JSON_OK) {
            rc = i < count ? write_field(convert, &fields[i]) : sof_json_writer_null(w);
        }
    }
    if (rc == SOF_JSON_OK) {
        rc = convert->rows ? sof_json_writer_array_end(w) : sof_json_writer_object_end(w);
    }
    if (rc == SOF_JSON_OK) {
        // Inside the outer array, the writer passes output on only as it
        // fills; send each record on as it ends.
        rc = sof_json_writer_flush(w);
    }
    return rc;
}

/// @brief The writer's write function: writes to standard output.
static int write_stdout(const char *data, size_t length, void *ctx) {
    (void)ctx;
    return fwrite(data, 1, length, stdout) == length ? 0 : 1;
}

int main(int ac, char **av) {
    settings_t settings = {DEFAULT_INDENT, ','};
    int index;
    sof_copt_t *option;
    sof_copt_error_t err = sof_copt_parse(ac, av, &settings, &index, &option);
    switch (err) {
    case SOF_COPT_OK:
        break;
    case SOF_COPT_NO_MATCH:
        fprintf(stderr, "csvjson: unknown option %s\n", av[index]);
        usage(stderr);
        return EXIT_USAGE;
    case SOF_COPT_MISSING_ARGUMENT:
        fprintf(stderr, "csvjson: %s needs an argument\n", option->option);
        usage(stderr);
        return EXIT_USAGE;
    case SOF_COPT_UNEXPECTED_ARGUMENT:
        fprintf(stderr, "csvjson: %s takes no argument\n", option->option);
        usage(stderr);
        return EXIT_USAGE;
    case SOF_COPT_STOPPED:
        return EXIT_USAGE;
    default:
        fprintf(stderr, "csvjson: can't read the options (error %d)\n", (int)err);
        return EXIT_USAGE;
    }
    if (opt_help.present) {
        usage(stdout);
        return EXIT_DONE;
    }
    if (opt_lines.present && opt_indent.present) {
        fprintf(stderr, "csvjson: JSON Lines are never indented, so -l and -i don't go together\n");
        usage(stderr);
        return EXIT_USAGE;
    }
    if (opt_json.present && (opt_ascii.present || opt_lines.present || opt_numbers.present || opt_rows.present || opt_indent.present)) {
        fprintf(stderr, "csvjson: -j writes CSV, so of the other options it takes only -d\n");
        usage(stderr);
        return EXIT_USAGE;
    }

    static char *standard_input[] = {"-"};
    char **paths = av + index;
    int num_paths = ac - index;
    if (num_paths == 0) {
        paths = standard_input;
        num_paths = 1;
    }
    if (opt_json.present) {
        int status = to_csv(paths, num_paths, settings.delimiter);
        if (fflush(stdout) != 0) {
            fprintf(stderr, "csvjson: write error\n");
            status = EXIT_TROUBLE;
        }
        return status;
    }

    sof_json_writer_t w;
    convert_t convert = {&w, settings.delimiter, opt_numbers.present, opt_rows.present};
    size_t indent = opt_lines.present ? 0 : settings.indent;
    unsigned flags = opt_ascii.present ? SOF_JSON_ASCII : 0;
    if (sof_json_writer_init(&w, indent, flags, write_stdout, NULL, NULL) != SOF_JSON_OK) {
        fprintf(stderr, "csvjson: out of memory\n");
        return EXIT_TROUBLE;
    }
    if (!opt_lines.present) {
        sof_json_writer_array_begin(&w);
    }
    int status = convert_all(paths, num_paths, csv_file, &convert);
    // Close the outer array, even after an error, so what was written is
    // whole JSON.
    if (!opt_lines.present && sof_json_writer_array_end(&w) != SOF_JSON_OK && status == EXIT_DONE) {
        fprintf(stderr, "csvjson: write error\n");
        status = EXIT_TROUBLE;
    }
    sof_json_writer_free(&w);
    if (fflush(stdout) != 0) {
        fprintf(stderr, "csvjson: write error\n");
        status = EXIT_TROUBLE;
    }
    return status;
}
