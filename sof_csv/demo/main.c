/**
 * @file main.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief Cuts columns out of CSV files, by name or number.
 * @details Reads each file, or standard input, a record at a time with a
 * sof_csv_reader_t, and writes the columns -f names, in its order, with a
 * sof_csv_writer_t, so a file of any size is cut in the memory one record
 * takes. Without -f it writes every column, which converts between
 * delimiters, line endings and quoting. Unless -H says the input has none,
 * each file's first line is its header, which -f's names are looked up in,
 * and the output starts with the header of the columns cut.
 *
 *     csvcut [-h] [-H] [-c] [-q] [-s] [-d delimiter] [-o delimiter] [-f columns] [file]...
 *
 * With no file, or a file named "-", it reads standard input. Its options
 * are read with sof_copt. It exits with 0, 1 if a file couldn't be read or
 * isn't valid CSV, a column isn't there, memory ran out or the output
 * couldn't be written, and 2 for a usage error.
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
#include "sof_string.h"
#include "sof_vector.h"

/// @brief The exit codes.
#define EXIT_DONE 0
#define EXIT_TROUBLE 1
#define EXIT_USAGE 2

/// @brief The code the option validators return for a bad argument, after
/// printing why. sof_copt keeps it in the option's stop_value.
#define BAD_ARGUMENT 1

/// @brief A column -f names: by name, or if name is dead, by number.
typedef struct column_t {
    /// @brief The name, or a dead string for a column given by number.
    sof_string_t name;
    /// @brief The column's index, counting from 0: given, or found in the
    /// header.
    size_t index;
} column_t;

/// @brief The settings the options give, filled in by their validators.
typedef struct settings_t {
    char delimiter;
    char output_delimiter;
} settings_t;

/// @brief What cut_file() works with.
typedef struct cut_t {
    /// @brief The columns -f names, column_t, or none for every column.
    sof_vector_t columns;
    /// @brief The fields of the record written, sof_string_t views of the
    /// fields read.
    sof_vector_t row;
    /// @brief The writer, to standard output.
    sof_csv_writer_t *w;
    /// @brief The reader's flags.
    unsigned flags;
    /// @brief The input's delimiter.
    char delimiter;
    /// @brief 1 once a header has been written.
    int wrote_header;
} cut_t;

static int add_column(sof_vector_t *columns, const char *s, size_t length);
static int cut_file(FILE *f, const char *name, cut_t *cut);
static int delimiter_read(const char *s, char *delimiter);
static int delimiter_validate(sof_copt_t *option, void *ctx);
static void free_column(void *element, void *ctx);
static int parse_columns(sof_vector_t *columns, const char *list, int numbers_only);
static int read_number(const char *s, size_t length, size_t *number);
static int resolve(const sof_csv_reader_t *r, cut_t *cut, const char *name);
static void usage(FILE *stream);
static sof_csv_error_t write_row(cut_t *cut, const sof_string_t *fields, size_t count);

static sof_copt_t opt_help = {
    .option = "-h",
    .description = "print this help and exit",
};

static sof_copt_t opt_no_header = {
    .option = "-H",
    .description = "the input has no header: name columns by number only",
};

static sof_copt_t opt_crlf = {
    .option = "-c",
    .description = "end output records with CRLF rather than LF",
};

static sof_copt_t opt_quote = {
    .option = "-q",
    .description = "quote every output field",
};

static sof_copt_t opt_strict = {
    .option = "-s",
    .description = "read RFC 4180 strictly: stop at invalid CSV, and keep blank lines as records",
};

static sof_copt_t opt_delimiter = {
    .option = "-d",
    .description = "the input's delimiter, one character or \"tab\" (default ,)",
    .type = SOF_COPT_ARG_REQUIRED,
    .validate = delimiter_validate,
};

static sof_copt_t opt_output = {
    .option = "-o",
    .description = "the output's delimiter (default the input's)",
    .type = SOF_COPT_ARG_REQUIRED,
    .validate = delimiter_validate,
};

static sof_copt_t opt_fields = {
    .option = "-f",
    .description = "the columns to write, in order: names and numbers from 1, and ranges such as 2-4, separated by commas",
    .type = SOF_COPT_ARG_REQUIRED,
};

SOF_COPT_OPTIONS static sof_copt_t *options[] = {
    &opt_help, &opt_no_header, &opt_crlf, &opt_quote, &opt_strict, &opt_delimiter, &opt_output, &opt_fields,
};

/// @brief Adds a column to the list: a number or range of numbers, or a
/// name.
/// @return 0, -1 if memory ran out, or -2 for a bad number or range.
static int add_column(sof_vector_t *columns, const char *s, size_t length) {
    column_t column = {{0}, 0};
    const char *dash = memchr(s, '-', length);
    size_t first, last;
    if (read_number(s, length, &first) == 0) {
        last = first;
    } else if (dash != NULL && read_number(s, (size_t)(dash - s), &first) == 0) {
        if (read_number(dash + 1, length - (size_t)(dash - s) - 1, &last) != 0 || last < first) {
            return -2;
        }
    } else {
        if (sof_string_from_bytes(&column.name, s, length, NULL) != SOF_STRING_OK) {
            return -1;
        }
        if (sof_vector_push(columns, &column) != SOF_VECTOR_OK) {
            sof_string_free(&column.name);
            return -1;
        }
        return 0;
    }
    if (first == 0) {
        return -2;
    }
    for (size_t n = first; n <= last; n++) {
        column.index = n - 1;
        if (sof_vector_push(columns, &column) != SOF_VECTOR_OK) {
            return -1;
        }
    }
    return 0;
}

/// @brief Cuts the columns out of one file.
/// @return EXIT_DONE, or EXIT_TROUBLE after printing why.
static int cut_file(FILE *f, const char *name, cut_t *cut) {
    sof_csv_reader_t r;
    sof_string_t *fields;
    size_t count;
    sof_csv_error_t rc;
    int resolved = 0;
    if (sof_csv_reader_init(&r, cut->delimiter, cut->flags, sof_csv_read_file, f, NULL) != SOF_CSV_OK) {
        fprintf(stderr, "csvcut: out of memory\n");
        return EXIT_TROUBLE;
    }
    int status = EXIT_DONE;
    while ((rc = sof_csv_reader_next(&r, &fields, &count)) == SOF_CSV_OK) {
        if (!resolved) {
            // The header was read with the first record.
            if (resolve(&r, cut, name) != 0) {
                status = EXIT_TROUBLE;
                break;
            }
            resolved = 1;
        }
        if ((rc = write_row(cut, fields, count)) != SOF_CSV_OK) {
            break;
        }
    }
    if (!resolved && status == EXIT_DONE && sof_csv_reader_header(&r, NULL) != NULL && resolve(&r, cut, name) != 0) {
        // A header with no record after it, or one that couldn't be read,
        // is still written.
        status = EXIT_TROUBLE;
    }
    switch (rc) {
    case SOF_CSV_OK:
    case SOF_CSV_NO_MATCH:
        break;
    case SOF_CSV_SYNTAX:
        fprintf(stderr, "csvcut: %s: line %zu isn't valid CSV\n", name, r.line);
        status = EXIT_TROUBLE;
        break;
    case SOF_CSV_READ:
        fprintf(stderr, "csvcut: %s: read error\n", name);
        status = EXIT_TROUBLE;
        break;
    case SOF_CSV_NO_MEM:
        fprintf(stderr, "csvcut: out of memory reading %s\n", name);
        status = EXIT_TROUBLE;
        break;
    default:
        fprintf(stderr, "csvcut: write error\n");
        status = EXIT_TROUBLE;
        break;
    }
    if (r.unterminated) {
        fprintf(stderr, "csvcut: %s: the last quoted field has no closing quote\n", name);
    }
    sof_csv_reader_free(&r);
    return status;
}

/// @brief Reads a delimiter: one character other than a quote or line
/// break, or "tab".
/// @return 0, or -1 if s isn't one.
static int delimiter_read(const char *s, char *delimiter) {
    if (strcmp(s, "tab") == 0 || strcmp(s, "\\t") == 0) {
        *delimiter = '\t';
        return 0;
    }
    if (strlen(s) == 1 && s[0] != '"' && s[0] != '\n' && s[0] != '\r') {
        *delimiter = s[0];
        return 0;
    }
    return -1;
}

/// @brief Reads -d's or -o's argument into the settings in ctx.
static int delimiter_validate(sof_copt_t *option, void *ctx) {
    settings_t *settings = ctx;
    char *delimiter = option == &opt_delimiter ? &settings->delimiter : &settings->output_delimiter;
    if (delimiter_read(option->value, delimiter) != 0) {
        fprintf(stderr, "csvcut: %s needs one character other than a quote, or \"tab\", not \"%s\"\n", option->option, option->value);
        return BAD_ARGUMENT;
    }
    return 0;
}

/// @brief The columns vector's delete function: frees a column's name.
static void free_column(void *element, void *ctx) {
    (void)ctx;
    sof_string_free(&((column_t *)element)->name);
}

/// @brief Reads -f's list into columns.
/// @return 0, or -1 after printing why.
static int parse_columns(sof_vector_t *columns, const char *list, int numbers_only) {
    const char *p = list;
    for (;;) {
        const char *comma = strchr(p, ',');
        size_t length = comma != NULL ? (size_t)(comma - p) : strlen(p);
        size_t before = columns->count;
        int rc = length == 0 ? -2 : add_column(columns, p, length);
        if (rc == -1) {
            fprintf(stderr, "csvcut: out of memory\n");
            return -1;
        }
        if (rc == -2 || (numbers_only && columns->count > before && ((column_t *)sof_vector_at(columns, before))->name.data != NULL)) {
            fprintf(stderr, "csvcut: -f: \"%.*s\" isn't %s\n", (int)length, p, numbers_only ? "a column number or range, as -H needs" : "a column name, number or range");
            return -1;
        }
        if (comma == NULL) {
            return 0;
        }
        p = comma + 1;
    }
}

/// @brief Reads a decimal number from the length bytes at s.
/// @return 0, or -1 if they aren't one.
static int read_number(const char *s, size_t length, size_t *number) {
    size_t value = 0;
    if (length == 0 || length > 9) {
        return -1;
    }
    for (size_t i = 0; i < length; i++) {
        if (s[i] < '0' || s[i] > '9') {
            return -1;
        }
        value = value * 10 + (size_t)(s[i] - '0');
    }
    *number = value;
    return 0;
}

/// @brief Finds the columns -f names in the file's header, and writes the
/// header of the columns cut if it is the first.
/// @return 0, or -1 after printing why.
static int resolve(const sof_csv_reader_t *r, cut_t *cut, const char *name) {
    for (size_t i = 0; i < cut->columns.count; i++) {
        column_t *column = sof_vector_at(&cut->columns, i);
        if (column->name.data != NULL && sof_csv_reader_column(r, column->name.data, &column->index) != SOF_CSV_OK) {
            fprintf(stderr, "csvcut: %s: there is no column \"%s\"\n", name, column->name.data);
            return -1;
        }
    }
    size_t count;
    const sof_string_t *header = sof_csv_reader_header(r, &count);
    if (header != NULL && !cut->wrote_header) {
        cut->wrote_header = 1;
        if (write_row(cut, header, count) != SOF_CSV_OK) {
            fprintf(stderr, "csvcut: write error\n");
            return -1;
        }
    }
    return 0;
}

/// @brief Prints the usage message, with the options sof_copt knows.
static void usage(FILE *stream) {
    fprintf(stream, "usage: csvcut [-h] [-H] [-c] [-q] [-s] [-d delimiter] [-o delimiter] [-f columns] [file]...\n");
    sof_copt_print(stream);
    fprintf(stream,
        "Writes the columns -f names, in its order, of CSV files, or of standard input if\n"
        "there are none or a file is \"-\"; without -f, every column. Unless -H is given,\n"
        "each file's first line is its header, and the output starts with a header.\n"
        "Exits with 0, 1 if a file can't be read or cut, 2 for bad options.\n");
}

/// @brief Writes the columns of a record, or of a header: those -f named,
/// with an empty field for one past the end of the record, or every one.
static sof_csv_error_t write_row(cut_t *cut, const sof_string_t *fields, size_t count) {
    if (cut->columns.count == 0) {
        return sof_csv_writer_record(cut->w, fields, count);
    }
    sof_vector_clear(&cut->row);
    for (size_t i = 0; i < cut->columns.count; i++) {
        const column_t *column = sof_vector_at(&cut->columns, i);
        sof_string_t field = column->index < count ? sof_string_view(fields[column->index].data, fields[column->index].length) : sof_string_view(NULL, 0);
        if (sof_vector_push(&cut->row, &field) != SOF_VECTOR_OK) {
            return SOF_CSV_NO_MEM;
        }
    }
    return sof_csv_writer_record(cut->w, (const sof_string_t *)cut->row.elements, cut->row.count);
}

int main(int ac, char **av) {
    settings_t settings = {',', 0};
    int index;
    sof_copt_t *option;
    sof_copt_error_t err = sof_copt_parse(ac, av, &settings, &index, &option);
    switch (err) {
    case SOF_COPT_OK:
        break;
    case SOF_COPT_NO_MATCH:
        fprintf(stderr, "csvcut: unknown option %s\n", av[index]);
        usage(stderr);
        return EXIT_USAGE;
    case SOF_COPT_MISSING_ARGUMENT:
        fprintf(stderr, "csvcut: %s needs an argument\n", option->option);
        usage(stderr);
        return EXIT_USAGE;
    case SOF_COPT_UNEXPECTED_ARGUMENT:
        fprintf(stderr, "csvcut: %s takes no argument\n", option->option);
        usage(stderr);
        return EXIT_USAGE;
    case SOF_COPT_STOPPED:
        return EXIT_USAGE;
    default:
        fprintf(stderr, "csvcut: can't read the options (error %d)\n", (int)err);
        return EXIT_USAGE;
    }
    if (opt_help.present) {
        usage(stdout);
        return EXIT_DONE;
    }

    sof_csv_writer_t w;
    cut_t cut = {{0}, {0}, &w, 0, settings.delimiter, 0};
    char output_delimiter = opt_output.present ? settings.output_delimiter : settings.delimiter;
    unsigned flags = (opt_crlf.present ? SOF_CSV_CRLF : 0) | (opt_quote.present ? SOF_CSV_QUOTE_ALL : 0);
    cut.flags = (opt_no_header.present ? 0 : SOF_CSV_HEADER) | (opt_strict.present ? SOF_CSV_STRICT : 0);
    if (sof_vector_init(&cut.columns, sizeof(column_t), 0, free_column, NULL, NULL) != SOF_VECTOR_OK || sof_vector_init(&cut.row, sizeof(sof_string_t), 0, NULL, NULL, NULL) != SOF_VECTOR_OK || sof_csv_writer_init(&w, output_delimiter, flags, sof_csv_write_file, stdout, NULL) != SOF_CSV_OK) {
        fprintf(stderr, "csvcut: out of memory\n");
        return EXIT_TROUBLE;
    }
    int status = EXIT_DONE;
    if (opt_fields.present && parse_columns(&cut.columns, opt_fields.value, opt_no_header.present) != 0) {
        status = EXIT_USAGE;
    }

    static char *standard_input[] = {"-"};
    char **paths = av + index;
    int num_paths = ac - index;
    if (num_paths == 0) {
        paths = standard_input;
        num_paths = 1;
    }
    for (int i = 0; i < num_paths && status != EXIT_USAGE; i++) {
        int is_stdin = strcmp(paths[i], "-") == 0;
        const char *name = is_stdin ? "standard input" : paths[i];
        FILE *f = is_stdin ? stdin : fopen(paths[i], "rb");
        if (f == NULL) {
            fprintf(stderr, "csvcut: %s: %s\n", name, strerror(errno));
            status = EXIT_TROUBLE;
            continue;
        }
        if (cut_file(f, name, &cut) != EXIT_DONE) {
            status = EXIT_TROUBLE;
        }
        if (is_stdin) {
            clearerr(stdin);
        } else {
            fclose(f);
        }
    }
    if (sof_csv_writer_flush(&w) != SOF_CSV_OK || fflush(stdout) != 0) {
        fprintf(stderr, "csvcut: write error\n");
        status = EXIT_TROUBLE;
    }
    sof_csv_writer_free(&w);
    sof_vector_free(&cut.columns);
    sof_vector_free(&cut.row);
    return status;
}
