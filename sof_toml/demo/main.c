/**
 * @file main.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief Converts TOML to JSON, and JSON to TOML.
 * @details Reads a TOML file, or standard input, whole into a sof_toml
 * document, and writes it as JSON with a sof_json writer: tables as
 * objects, arrays as arrays, strings, integers, floats and booleans as
 * themselves, and dates and times as strings in their TOML form. Floats
 * JSON has no form for, inf and nan, are written as strings too.
 *
 * With -j, it goes the other way: it reads one JSON object with a sof_json
 * reader into a sof_json document, and writes it as TOML with a sof_toml
 * writer, each object's members that aren't objects first, then those that
 * are as tables, and arrays of objects as arrays of tables. Numbers that are
 * integers become integers, the rest floats. TOML has no null, so a null is
 * an error.
 *
 * With -t, the JSON either way is the tagged JSON of toml-test, the TOML
 * project's test suite, in which each value is an object of its type and
 * its text, such as {"type": "integer", "value": "42"}. tomljson -t is then
 * a decoder toml-test can test, and tomljson -j -t an encoder.
 *
 *     tomljson [-h] [-a] [-j] [-t] [-i spaces] [file]
 *
 * With no file, or a file named "-", it reads standard input. Its options
 * are read with sof_copt. It exits with 0, 1 if the file couldn't be read
 * or converted, memory ran out or the output couldn't be written, and 2
 * for a usage error.
 * @version 0.0.1
 * @date 2026-11-19
 * @copyright Copyright (c) 2026
 */

#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sof_copt.h"
#include "sof_json.h"
#include "sof_toml.h"

/// @brief The exit codes.
#define EXIT_DONE 0
#define EXIT_TROUBLE 1
#define EXIT_USAGE 2

/// @brief The code the option validators return for a bad argument, after
/// printing why. sof_copt keeps it in the option's stop_value.
#define BAD_ARGUMENT 1

/// @brief The spaces each level of JSON is indented by when -i isn't given.
#define DEFAULT_INDENT 2

/// @brief The most spaces -i takes.
#define INDENT_MAX 16

/// @brief The settings the options give, filled in by their validators.
typedef struct settings_t {
    size_t indent;
} settings_t;

/// @brief What converting JSON to TOML works with.
typedef struct to_toml_t {
    sof_toml_writer_t *w;
    /// @brief -t: the JSON is toml-test's tagged JSON.
    int tagged;
    /// @brief Why the JSON couldn't be converted, if it couldn't.
    const char *why;
} to_toml_t;

static int indent_validate(sof_copt_t *option, void *ctx);
static int is_section(const to_toml_t *to, const sof_json_value_t *j);
static int is_tagged(const sof_json_value_t *j, const char **type, const char **value, size_t *length);
static sof_json_error_t json_value(sof_json_writer_t *w, const sof_toml_value_t *v, int tagged);
static int to_json(FILE *f, const char *name, size_t indent, unsigned flags, int tagged);
static int to_toml(FILE *f, const char *name, unsigned flags, int tagged);
static int toml_members(to_toml_t *to, const sof_json_value_t *object);
static int toml_value(to_toml_t *to, const sof_json_value_t *j);
static void usage(FILE *stream);
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
    .description = "read JSON and write TOML",
};

static sof_copt_t opt_tagged = {
    .option = "-t",
    .description = "the JSON is toml-test's tagged JSON, each value its type and text",
};

static sof_copt_t opt_indent = {
    .option = "-i",
    .description = "indent JSON by this many spaces a level, 0 for one line (default 2)",
    .type = SOF_COPT_ARG_REQUIRED,
    .validate = indent_validate,
};

SOF_COPT_OPTIONS static sof_copt_t *options[] = {
    &opt_help, &opt_ascii, &opt_json, &opt_tagged, &opt_indent,
};

/// @brief Reads -i's argument, a number of spaces, into the settings in
/// ctx.
static int indent_validate(sof_copt_t *option, void *ctx) {
    settings_t *settings = ctx;
    const char *s = option->value;
    char *end;
    errno = 0;
    unsigned long value = strtoul(s, &end, 10);
    if (*s < '0' || *s > '9' || *end != '\0' || errno != 0 || value > INDENT_MAX) {
        fprintf(stderr, "tomljson: -i needs a number from 0 to %d, not \"%s\"\n", INDENT_MAX, s);
        return BAD_ARGUMENT;
    }
    settings->indent = (size_t)value;
    return 0;
}

/// @brief Says whether a JSON value is written as a section of its own in
/// TOML, after the key/value pairs of its table: an object that is a table,
/// or a non-empty array of them.
static int is_section(const to_toml_t *to, const sof_json_value_t *j) {
    if (sof_json_value_type(j) == SOF_JSON_OBJECT) {
        return !to->tagged || !is_tagged(j, NULL, NULL, NULL);
    }
    size_t count = sof_json_value_count(j);
    if (sof_json_value_type(j) != SOF_JSON_ARRAY || count == 0) {
        return 0;
    }
    for (size_t i = 0; i < count; i++) {
        const sof_json_value_t *e = sof_json_value_at(j, i);
        if (sof_json_value_type(e) != SOF_JSON_OBJECT || (to->tagged && is_tagged(e, NULL, NULL, NULL))) {
            return 0;
        }
    }
    return 1;
}

/// @brief Says whether a JSON value is a tagged value of toml-test's, an
/// object of two strings, type and value, and if so, gives them.
static int is_tagged(const sof_json_value_t *j, const char **type, const char **value, size_t *length) {
    if (sof_json_value_count(j) != 2) {
        return 0;
    }
    const char *t = sof_json_value_string(sof_json_value_get(j, "type"), NULL);
    size_t n;
    const char *v = sof_json_value_string(sof_json_value_get(j, "value"), &n);
    if (t == NULL || v == NULL) {
        return 0;
    }
    if (type != NULL) {
        *type = t;
        *value = v;
        *length = n;
    }
    return 1;
}

/// @brief Writes a TOML value as JSON, plain or tagged.
static sof_json_error_t json_value(sof_json_writer_t *w, const sof_toml_value_t *v, int tagged) {
    static const char *names[] = {"", "string", "integer", "float", "bool", "datetime", "datetime-local", "date-local", "time-local"};
    char text[SOF_TOML_DATETIME_SIZE];
    sof_json_error_t rc = SOF_JSON_OK;
    size_t length = 0;
    switch (v->type) {
    case SOF_TOML_ARRAY:
        rc = sof_json_writer_array_begin(w);
        for (size_t i = 0; rc == SOF_JSON_OK && i < v->length; i++) {
            rc = json_value(w, sof_toml_value_at(v, i), tagged);
        }
        return rc == SOF_JSON_OK ? sof_json_writer_array_end(w) : rc;
    case SOF_TOML_TABLE:
        rc = sof_json_writer_object_begin(w);
        for (size_t i = 0; rc == SOF_JSON_OK && i < v->length; i++) {
            const sof_toml_member_t *m = sof_toml_value_member(v, i);
            rc = sof_json_writer_key_bytes(w, m->key, m->key_length);
            if (rc == SOF_JSON_OK) {
                rc = json_value(w, m->value, tagged);
            }
        }
        return rc == SOF_JSON_OK ? sof_json_writer_object_end(w) : rc;
    case SOF_TOML_STRING:
        if (!tagged) {
            return sof_json_writer_string_bytes(w, v->as.text, v->length);
        }
        break;
    case SOF_TOML_INTEGER:
        if (!tagged) {
            return sof_json_writer_int(w, v->as.integer);
        }
        length = (size_t)snprintf(text, sizeof(text), "%lld", (long long)v->as.integer);
        break;
    case SOF_TOML_FLOAT:
        if (!tagged && isfinite(v->as.number)) {
            return sof_json_writer_double(w, v->as.number);
        }
        // An infinity or NaN, or a tagged float: inf and nan are written as
        // TOML writes them, and the rest with all the digits a double
        // needs, as toml-test reads them.
        if (isnan(v->as.number)) {
            length = (size_t)snprintf(text, sizeof(text), "nan");
        } else if (isinf(v->as.number)) {
            length = (size_t)snprintf(text, sizeof(text), v->as.number < 0 ? "-inf" : "inf");
        } else {
            length = (size_t)snprintf(text, sizeof(text), "%.17g", v->as.number);
        }
        if (!tagged) {
            return sof_json_writer_string_bytes(w, text, length);
        }
        break;
    case SOF_TOML_BOOL:
        if (!tagged) {
            return sof_json_writer_bool(w, v->as.boolean);
        }
        length = (size_t)snprintf(text, sizeof(text), v->as.boolean ? "true" : "false");
        break;
    default:
        length = sof_toml_datetime_format(v->type, &v->as.datetime, text, sizeof(text));
        if (!tagged) {
            return sof_json_writer_string_bytes(w, text, length);
        }
        break;
    }
    // A tagged value: its type and its text.
    rc = sof_json_writer_object_begin(w);
    if (rc == SOF_JSON_OK) {
        rc = sof_json_writer_key(w, "type");
    }
    if (rc == SOF_JSON_OK) {
        rc = sof_json_writer_string(w, names[v->type]);
    }
    if (rc == SOF_JSON_OK) {
        rc = sof_json_writer_key(w, "value");
    }
    if (rc == SOF_JSON_OK) {
        rc = v->type == SOF_TOML_STRING ? sof_json_writer_string_bytes(w, v->as.text, v->length) : sof_json_writer_string_bytes(w, text, length);
    }
    return rc == SOF_JSON_OK ? sof_json_writer_object_end(w) : rc;
}

/// @brief Reads f, TOML, whole, and writes it as JSON.
/// @return EXIT_DONE or EXIT_TROUBLE, after printing why.
static int to_json(FILE *f, const char *name, size_t indent, unsigned flags, int tagged) {
    sof_toml_doc_t d;
    sof_json_writer_t w;
    int status = EXIT_DONE;
    if (sof_toml_doc_init(&d, NULL) != SOF_TOML_OK || sof_json_writer_init(&w, indent, flags, write_stdout, NULL, NULL) != SOF_JSON_OK) {
        fprintf(stderr, "tomljson: out of memory\n");
        sof_toml_doc_free(&d);
        return EXIT_TROUBLE;
    }
    sof_toml_error_t rc = sof_toml_doc_read(&d, sof_toml_read_file, f);
    if (rc == SOF_TOML_OK) {
        sof_json_error_t written = json_value(&w, d.root, tagged);
        if (written != SOF_JSON_OK) {
            fprintf(stderr, "tomljson: %s\n", written == SOF_JSON_NO_MEM ? "out of memory" : "write error");
            status = EXIT_TROUBLE;
        }
    } else if (rc == SOF_TOML_NO_MEM || rc == SOF_TOML_READ) {
        fprintf(stderr, "tomljson: %s: %s\n", name, rc == SOF_TOML_NO_MEM ? "out of memory" : "read error");
        status = EXIT_TROUBLE;
    } else {
        fprintf(stderr, "tomljson: %s:%zu:%zu: %s\n", name, d.line, d.column, d.message);
        status = EXIT_TROUBLE;
    }
    sof_json_writer_free(&w);
    sof_toml_doc_free(&d);
    return status;
}

/// @brief Reads f, one JSON object, and writes it as TOML.
/// @return EXIT_DONE or EXIT_TROUBLE, after printing why.
static int to_toml(FILE *f, const char *name, unsigned flags, int tagged) {
    sof_json_reader_t r;
    sof_json_doc_t d;
    sof_toml_writer_t w;
    if (sof_json_reader_init(&r, 0, sof_json_read_file, f, NULL) != SOF_JSON_OK || sof_json_doc_init(&d, NULL) != SOF_JSON_OK || sof_toml_writer_init(&w, flags, write_stdout, NULL, NULL) != SOF_TOML_OK) {
        fprintf(stderr, "tomljson: out of memory\n");
        sof_json_reader_free(&r);
        return EXIT_TROUBLE;
    }
    int status = EXIT_TROUBLE;
    sof_json_error_t rc = sof_json_doc_read(&d, &r);
    if (rc == SOF_JSON_OK) {
        // Nothing may follow the object.
        rc = sof_json_reader_next(&r, NULL);
        rc = rc == SOF_JSON_NO_MATCH ? SOF_JSON_OK : rc == SOF_JSON_OK ? SOF_JSON_SYNTAX : rc;
    }
    if (rc == SOF_JSON_NO_MEM || rc == SOF_JSON_READ) {
        fprintf(stderr, "tomljson: %s: %s\n", name, rc == SOF_JSON_NO_MEM ? "out of memory" : "read error");
    } else if (rc != SOF_JSON_OK) {
        fprintf(stderr, "tomljson: %s:%zu:%zu: not JSON\n", name, r.line, r.column);
    } else if (sof_json_value_type(d.root) != SOF_JSON_OBJECT || (tagged && is_tagged(d.root, NULL, NULL, NULL))) {
        fprintf(stderr, "tomljson: %s: a TOML document is a table, so the JSON must be an object\n", name);
    } else {
        to_toml_t to = {&w, tagged, NULL};
        if (!toml_members(&to, d.root)) {
            fprintf(stderr, "tomljson: %s: %s\n", name, to.why != NULL ? to.why : "out of memory");
        } else if (sof_toml_writer_flush(&w) != SOF_TOML_OK) {
            fprintf(stderr, "tomljson: write error\n");
        } else {
            status = EXIT_DONE;
        }
    }
    sof_toml_writer_free(&w);
    sof_json_doc_free(&d);
    sof_json_reader_free(&r);
    return status;
}

/// @brief Writes an object's members as a table's: those that aren't
/// sections first, then those that are.
/// @return 1, or 0 if a member couldn't be written.
static int toml_members(to_toml_t *to, const sof_json_value_t *object) {
    for (int sections = 0; sections < 2; sections++) {
        for (size_t i = 0; i < sof_json_value_count(object); i++) {
            const sof_json_member_t *m = sof_json_value_member(object, i);
            if (is_section(to, m->value) != sections) {
                continue;
            }
            if (sof_toml_writer_key_bytes(to->w, m->key, m->key_length) != SOF_TOML_OK || !toml_value(to, m->value)) {
                return 0;
            }
        }
    }
    return 1;
}

/// @brief Writes a JSON value as TOML, after its key or in an array.
/// @return 1, or 0 if it couldn't be written, with why in to->why unless
/// memory ran out.
static int toml_value(to_toml_t *to, const sof_json_value_t *j) {
    sof_toml_writer_t *w = to->w;
    const char *type, *value;
    size_t length;
    int64_t i;
    double x;
    switch (sof_json_value_type(j)) {
    case SOF_JSON_NULL:
        to->why = "TOML has no null";
        return 0;
    case SOF_JSON_BOOL:
        return sof_toml_writer_bool(w, j->as.boolean) == SOF_TOML_OK;
    case SOF_JSON_STRING:
        return sof_toml_writer_string_bytes(w, j->as.text, j->length) == SOF_TOML_OK;
    case SOF_JSON_NUMBER:
        if (sof_json_value_int(j, &i) == SOF_JSON_OK && strpbrk(j->as.text, ".eE") == NULL) {
            return sof_toml_writer_int(w, i) == SOF_TOML_OK;
        }
        if (sof_json_value_double(j, &x) != SOF_JSON_OK) {
            to->why = "a number is too large for a double";
            return 0;
        }
        return sof_toml_writer_double(w, x) == SOF_TOML_OK;
    case SOF_JSON_ARRAY: {
        sof_toml_error_t rc = is_section(to, j) ? sof_toml_writer_table_array_begin(w) : sof_toml_writer_array_begin(w);
        for (size_t k = 0; rc == SOF_TOML_OK && k < sof_json_value_count(j); k++) {
            if (!toml_value(to, sof_json_value_at(j, k))) {
                return 0;
            }
        }
        return rc == SOF_TOML_OK && sof_toml_writer_array_end(w) == SOF_TOML_OK;
    }
    default:
        break;
    }
    if (!to->tagged || !is_tagged(j, &type, &value, &length)) {
        return sof_toml_writer_table_begin(w) == SOF_TOML_OK && toml_members(to, j) && sof_toml_writer_table_end(w) == SOF_TOML_OK;
    }
    // A tagged value: its text, as toml-test writes it, becomes the type.
    to->why = "a tagged value isn't one TOML has";
    if (strcmp(type, "string") == 0) {
        return sof_toml_writer_string_bytes(w, value, length) == SOF_TOML_OK;
    }
    if (strcmp(type, "integer") == 0) {
        return sof_json_number_int(value, length, &i) == SOF_JSON_OK && sof_toml_writer_int(w, i) == SOF_TOML_OK;
    }
    if (strcmp(type, "bool") == 0) {
        return (strcmp(value, "true") == 0 || strcmp(value, "false") == 0) && sof_toml_writer_bool(w, value[0] == 't') == SOF_TOML_OK;
    }
    if (strcmp(type, "float") == 0) {
        const char *magnitude = value + (value[0] == '+' || value[0] == '-');
        if (strcmp(magnitude, "inf") == 0 || strcmp(magnitude, "nan") == 0) {
            x = magnitude[0] == 'i' ? HUGE_VAL : NAN;
            return sof_toml_writer_double(w, value[0] == '-' ? -x : x) == SOF_TOML_OK;
        }
        return sof_json_number_double(value, length, &x) == SOF_JSON_OK && sof_toml_writer_double(w, x) == SOF_TOML_OK;
    }
    sof_toml_type_t t;
    sof_toml_datetime_t dt;
    static const char *names[] = {"datetime", "datetime-local", "date-local", "time-local"};
    return sof_toml_datetime_parse(value, length, &t, &dt) == SOF_TOML_OK && strcmp(type, names[t - SOF_TOML_DATETIME]) == 0 && sof_toml_writer_datetime(w, t, &dt) == SOF_TOML_OK;
}

/// @brief Prints the usage message, with the options sof_copt knows.
static void usage(FILE *stream) {
    fprintf(stream, "usage: tomljson [-h] [-a] [-j] [-t] [-i spaces] [file]\n");
    sof_copt_print(stream);
    fprintf(stream,
        "Converts TOML to JSON, or with -j, a JSON object to TOML. With -t, the\n"
        "JSON is toml-test's tagged JSON, so tomljson -t is a toml-test decoder and\n"
        "tomljson -j -t an encoder. Reads standard input if there is no file or it\n"
        "is \"-\". Exits with 0, 1 if the file can't be read or converted or the\n"
        "output written, 2 for bad options.\n");
}

/// @brief The writers' write function: writes to standard output.
static int write_stdout(const char *data, size_t length, void *ctx) {
    (void)ctx;
    return fwrite(data, 1, length, stdout) == length ? 0 : 1;
}

int main(int ac, char **av) {
    settings_t settings = {DEFAULT_INDENT};
    int index;
    sof_copt_t *option;
    sof_copt_error_t err = sof_copt_parse(ac, av, &settings, &index, &option);
    switch (err) {
    case SOF_COPT_OK:
        break;
    case SOF_COPT_NO_MATCH:
        fprintf(stderr, "tomljson: unknown option %s\n", av[index]);
        usage(stderr);
        return EXIT_USAGE;
    case SOF_COPT_MISSING_ARGUMENT:
        fprintf(stderr, "tomljson: %s needs an argument\n", option->option);
        usage(stderr);
        return EXIT_USAGE;
    case SOF_COPT_UNEXPECTED_ARGUMENT:
        fprintf(stderr, "tomljson: %s takes no argument\n", option->option);
        usage(stderr);
        return EXIT_USAGE;
    case SOF_COPT_STOPPED:
        return EXIT_USAGE;
    default:
        fprintf(stderr, "tomljson: can't read the options (error %d)\n", (int)err);
        return EXIT_USAGE;
    }
    if (opt_help.present) {
        usage(stdout);
        return EXIT_DONE;
    }
    if (opt_json.present && opt_indent.present) {
        fprintf(stderr, "tomljson: -j writes TOML, which isn't indented, so -j and -i don't go together\n");
        usage(stderr);
        return EXIT_USAGE;
    }
    if (ac - index > 1) {
        fprintf(stderr, "tomljson: one file at a time\n");
        usage(stderr);
        return EXIT_USAGE;
    }
    const char *path = ac - index == 1 ? av[index] : "-";
    int is_stdin = strcmp(path, "-") == 0;
    const char *name = is_stdin ? "standard input" : path;
    FILE *f = is_stdin ? stdin : fopen(path, "rb");
    if (f == NULL) {
        fprintf(stderr, "tomljson: %s: %s\n", name, strerror(errno));
        return EXIT_TROUBLE;
    }
    int status;
    if (opt_json.present) {
        status = to_toml(f, name, opt_ascii.present ? SOF_TOML_ASCII : 0, opt_tagged.present);
    } else {
        status = to_json(f, name, settings.indent, opt_ascii.present ? SOF_JSON_ASCII : 0, opt_tagged.present);
    }
    if (!is_stdin) {
        fclose(f);
    }
    if (fflush(stdout) != 0) {
        fprintf(stderr, "tomljson: write error\n");
        status = EXIT_TROUBLE;
    }
    return status;
}
