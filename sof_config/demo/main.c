/**
 * @file main.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief Prints and queries .ini files.
 * @details Parses .ini files with sof_config, each later file overriding
 * the earlier ones, and prints every setting as section.key = "value", only
 * those of one section, or the value of one key, read as a string, an
 * integer, a floating-point number or a boolean.
 *
 *     ini [-h] [-a] [-c] [-s section] [-k key [-t type]] [file]...
 *
 * With no file, or a file named "-", the input is read from standard input.
 * Its options are read with sof_copt. It exits with 0, 1 if the key or
 * section asked for isn't there or the value isn't of the type asked for, and
 * 2 on any other error, such as a syntax error, which it reports with the
 * file, line and column.
 * @version 0.0.1
 * @date 2026-11-19
 * @copyright Copyright (c) 2026
 */

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "sof_config.h"
#include "sof_copt.h"

/// @brief The exit codes.
#define EXIT_FOUND 0
#define EXIT_NOT_FOUND 1
#define EXIT_TROUBLE 2

/// @brief An application code the validate callback of -t returns when it
/// names no type, once it has printed why; sof_copt keeps it in the
/// option's stop_value.
#define BAD_TYPE 1

/// @brief The types a value can be read as, with -t.
typedef enum value_type_t {
    TYPE_STRING = 0,
    TYPE_INT,
    TYPE_DOUBLE,
    TYPE_BOOL,
} value_type_t;

/// @brief The names of the types, as -t takes them, in the order of
/// value_type_t.
static const char *type_names[] = {"string", "int", "double", "bool"};

/// @brief What the options ask for, filled in by their callbacks.
typedef struct settings_t {
    value_type_t type;
} settings_t;

/// @brief What to print, and how many assignments have been: the ctx of the
/// assignment and traversal callbacks.
typedef struct printer_t {
    /// @brief 1 to print only the assignments in section.
    int filter;
    /// @brief The section to print, or NULL for the assignments outside any.
    const char *section;
    /// @brief The input being parsed, named before each assignment, or NULL.
    const char *file;
    /// @brief The assignments printed so far.
    size_t printed;
} printer_t;

static int matches(const printer_t *printer, const char *section);
static int parse_input(sof_config_t *config, printer_t *printer, int name_files, char **paths, int num_paths);
static int print_assignment(const char *section, const char *key, const char *value, void *ctx);
static int print_entry(const sof_config_entry_t *entry, void *ctx);
static void print_line(printer_t *printer, const char *section, const char *key, const char *value);
static void print_name(FILE *stream, const char *section, const char *key);
static void print_quoted(FILE *stream, const char *value);
static int print_value(const sof_config_t *config, const char *section, const char *key, value_type_t type);
static int type_validate(sof_copt_t *option, void *ctx);
static void usage(FILE *stream);

static sof_copt_t opt_help = {
    .option = "-h",
    .description = "print this help and exit",
};

static sof_copt_t opt_all = {
    .option = "-a",
    .description = "print every assignment as it is parsed, overridden ones too",
};

static sof_copt_t opt_check = {
    .option = "-c",
    .description = "only check the syntax: print nothing",
};

static sof_copt_t opt_section = {
    .option = "-s",
    .description = "print only this section; \"\" for the assignments outside any",
    .type = SOF_COPT_ARG_REQUIRED,
};

static sof_copt_t opt_key = {
    .option = "-k",
    .description = "print only the value of this key, in the section given with -s",
    .type = SOF_COPT_ARG_REQUIRED,
};

static sof_copt_t opt_type = {
    .option = "-t",
    .description = "read the value of -k as a string (default), int, double or bool",
    .type = SOF_COPT_ARG_REQUIRED,
    .validate = type_validate,
};

SOF_COPT_OPTIONS static sof_copt_t *options[] = {
    &opt_help,
    &opt_all,
    &opt_check,
    &opt_section,
    &opt_key,
    &opt_type,
};

/// @brief Tells whether an assignment in section is one to print.
/// @return 1 if it is, or 0.
static int matches(const printer_t *printer, const char *section) {
    if (!printer->filter) {
        return 1;
    }
    if (printer->section == NULL || section == NULL) {
        return printer->section == section;
    }
    return strcmp(printer->section, section) == 0;
}

/// @brief Parses the files, or standard input if there are none, into
/// config, in order, so that each overrides those before it.
/// @param name_files 1 to set printer's file to the name of each input as it
/// is parsed.
/// @return 0, or EXIT_TROUBLE if a file couldn't be read or parsed, or memory
/// ran out, after printing why.
static int parse_input(sof_config_t *config, printer_t *printer, int name_files, char **paths, int num_paths) {
    static char *standard_input[] = {"-"};
    if (num_paths == 0) {
        paths = standard_input;
        num_paths = 1;
    }
    for (int i = 0; i < num_paths; i++) {
        int is_stdin = strcmp(paths[i], "-") == 0;
        const char *name = is_stdin ? "standard input" : paths[i];
        printer->file = name_files ? name : NULL;
        sof_config_error_t err = is_stdin ? sof_config_parse_file(config, stdin) : sof_config_parse(config, paths[i]);
        switch (err) {
        case SOF_CONFIG_OK:
            break;
        case SOF_CONFIG_NO_OPEN:
        case SOF_CONFIG_NO_READ:
        case SOF_CONFIG_NO_MEM:
            fprintf(stderr, "%s: %s\n", name, sof_config_error_string(err));
            return EXIT_TROUBLE;
        default:
            fprintf(stderr, "%s:%zu:%zu: %s\n", name, config->line, config->column, sof_config_error_string(err));
            return EXIT_TROUBLE;
        }
    }
    return 0;
}

/// @brief The sof_config callback, for -a: prints each assignment as it is
/// parsed, if it is in the section the printer in ctx asks for.
/// @return 0, to go on parsing.
static int print_assignment(const char *section, const char *key, const char *value, void *ctx) {
    print_line(ctx, section, key, value);
    return 0;
}

/// @brief The sof_config_traverse() callback: prints a stored entry, if it
/// is in the section the printer in ctx asks for.
/// @return 0, to go on.
static int print_entry(const sof_config_entry_t *entry, void *ctx) {
    print_line(ctx, entry->section, entry->key, entry->value);
    return 0;
}

/// @brief Prints an assignment as section.key = "value", or key = "value"
/// outside any section, after the input's name if the printer has one, if
/// it is in the section the printer asks for.
static void print_line(printer_t *printer, const char *section, const char *key, const char *value) {
    if (!matches(printer, section)) {
        return;
    }
    if (printer->file != NULL) {
        printf("%s: ", printer->file);
    }
    print_name(stdout, section, key);
    printf(" = ");
    print_quoted(stdout, value);
    putchar('\n');
    printer->printed++;
}

/// @brief Prints a setting's name, as section.key, or key outside any
/// section.
static void print_name(FILE *stream, const char *section, const char *key) {
    if (section != NULL) {
        fprintf(stream, "%s.", section);
    }
    fprintf(stream, "%s", key);
}

/// @brief Prints a value in quotes, with the escapes sof_config reads, so
/// that it takes one line and would be read back as it is.
static void print_quoted(FILE *stream, const char *value) {
    putc('"', stream);
    for (const unsigned char *p = (const unsigned char *)value; *p != '\0'; p++) {
        switch (*p) {
        case '"':
            fputs("\\\"", stream);
            break;
        case '\\':
            fputs("\\\\", stream);
            break;
        case '\a':
            fputs("\\a", stream);
            break;
        case '\b':
            fputs("\\b", stream);
            break;
        case '\f':
            fputs("\\f", stream);
            break;
        case '\n':
            fputs("\\n", stream);
            break;
        case '\r':
            fputs("\\r", stream);
            break;
        case '\t':
            fputs("\\t", stream);
            break;
        case '\v':
            fputs("\\v", stream);
            break;
        default:
            if (*p < 0x20 || *p == 0x7f) {
                fprintf(stream, "\\x%02x", *p);
            } else {
                putc(*p, stream);
            }
            break;
        }
    }
    putc('"', stream);
}

/// @brief Prints the value of key in section, read as type, for -k.
/// @return EXIT_FOUND, or EXIT_NOT_FOUND if there is no such key or its value
/// isn't of the type, after printing why.
static int print_value(const sof_config_t *config, const char *section, const char *key, value_type_t type) {
    sof_config_error_t err = SOF_CONFIG_OK;
    switch (type) {
    case TYPE_STRING: {
        const char *value = sof_config_get(config, section, key);
        if (value == NULL) {
            err = SOF_CONFIG_NO_MATCH;
        } else {
            printf("%s\n", value);
        }
        break;
    }
    case TYPE_INT: {
        int64_t value;
        err = sof_config_get_int(config, section, key, &value);
        if (err == SOF_CONFIG_OK) {
            printf("%" PRId64 "\n", value);
        }
        break;
    }
    case TYPE_DOUBLE: {
        double value;
        err = sof_config_get_double(config, section, key, &value);
        if (err == SOF_CONFIG_OK) {
            printf("%.15g\n", value);
        }
        break;
    }
    case TYPE_BOOL: {
        int value;
        err = sof_config_get_bool(config, section, key, &value);
        if (err == SOF_CONFIG_OK) {
            printf("%s\n", value ? "true" : "false");
        }
        break;
    }
    }
    if (err == SOF_CONFIG_OK) {
        return EXIT_FOUND;
    }
    if (err == SOF_CONFIG_BAD_VALUE) {
        // The value is there, or the getter couldn't have found it wrong.
        const sof_config_entry_t *entry = sof_config_find(config, section, key);
        print_name(stderr, section, key);
        fprintf(stderr, " = ");
        print_quoted(stderr, entry->value);
        fprintf(stderr, ", on line %zu, isn't a valid %s\n", entry->line, type_names[type]);
    } else {
        fprintf(stderr, "no ");
        print_name(stderr, section, key);
        fprintf(stderr, "\n");
    }
    return EXIT_NOT_FOUND;
}

/// @brief Reads the argument of -t, a type's name, into the settings in ctx.
static int type_validate(sof_copt_t *option, void *ctx) {
    settings_t *settings = ctx;
    for (size_t i = 0; i < sizeof(type_names) / sizeof(type_names[0]); i++) {
        if (strcmp(option->value, type_names[i]) == 0) {
            settings->type = (value_type_t)i;
            return 0;
        }
    }
    fprintf(stderr, "%s wants string, int, double or bool, not \"%s\"\n", option->option, option->value);
    return BAD_TYPE;
}

/// @brief Prints the usage message, with the options sof_copt knows.
static void usage(FILE *stream) {
    fprintf(stream, "usage: ini [-h] [-a] [-c] [-s section] [-k key [-t type]] [file]...\n");
    sof_copt_print(stream);
    fprintf(stream,
        "Reads the .ini files, or standard input if there are none or a file is \"-\",\n"
        "each overriding those before it, and prints every setting as\n"
        "section.key = \"value\", or the value of the key given with -k. Exits with 0,\n"
        "1 if the key or section isn't there or the value isn't of its type, or 2 on\n"
        "any other error.\n");
}

int main(int ac, char **av) {
    settings_t settings = {TYPE_STRING};
    int index;
    sof_copt_t *option;
    sof_copt_error_t err = sof_copt_parse(ac, av, &settings, &index, &option);
    switch (err) {
    case SOF_COPT_OK:
        break;
    case SOF_COPT_NO_MATCH:
        fprintf(stderr, "unknown option %s\n", av[index]);
        usage(stderr);
        return EXIT_TROUBLE;
    case SOF_COPT_MISSING_ARGUMENT:
        fprintf(stderr, "%s needs an argument\n", option->option);
        usage(stderr);
        return EXIT_TROUBLE;
    case SOF_COPT_UNEXPECTED_ARGUMENT:
        fprintf(stderr, "%s takes no argument\n", option->option);
        usage(stderr);
        return EXIT_TROUBLE;
    case SOF_COPT_STOPPED:
        // The callback returned BAD_TYPE, the only code, and said why.
        return EXIT_TROUBLE;
    default:
        fprintf(stderr, "can't read the options (error %d)\n", (int)err);
        return EXIT_TROUBLE;
    }
    if (opt_help.present) {
        usage(stdout);
        return EXIT_FOUND;
    }
    if (opt_type.present && !opt_key.present) {
        fprintf(stderr, "-t needs -k\n");
        return EXIT_TROUBLE;
    }
    if (opt_key.present && opt_all.present) {
        fprintf(stderr, "-k and -a can't be used together\n");
        return EXIT_TROUBLE;
    }

    printer_t printer = {opt_section.present, NULL, NULL, 0};
    if (opt_section.present && opt_section.value[0] != '\0') {
        printer.section = opt_section.value;
    }
    // With -a, assignments are printed by the callback as they are parsed.
    int as_parsed = opt_all.present && !opt_check.present;
    sof_config_t config;
    if (sof_config_init(&config, as_parsed ? print_assignment : NULL, &printer, NULL) != SOF_CONFIG_OK) {
        fprintf(stderr, "out of memory\n");
        return EXIT_TROUBLE;
    }
    int rc = parse_input(&config, &printer, as_parsed && ac - index > 1, av + index, ac - index);
    if (rc == 0 && !opt_check.present) {
        if (opt_key.present) {
            rc = print_value(&config, printer.section, opt_key.value, settings.type);
        } else {
            if (!as_parsed) {
                sof_config_traverse(&config, print_entry, &printer);
            }
            if (printer.filter && printer.printed == 0) {
                if (printer.section != NULL) {
                    fprintf(stderr, "no section %s\n", printer.section);
                } else {
                    fprintf(stderr, "no assignments outside a section\n");
                }
                rc = EXIT_NOT_FOUND;
            }
        }
    }
    sof_config_free(&config);
    return rc;
}
