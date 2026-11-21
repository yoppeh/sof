/**
 * @file main.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief An expression calculator: evaluates lines such as "2 * (3 + x)"
 * and prints the results, with variables set by "x = 4".
 * @details Reads expressions from -e options, then from the files, or from
 * standard input if there are no files and no -e. Each line is an
 * expression, whose value is printed, an assignment, which prints nothing,
 * or blank or a comment.
 *
 *     calc [-h] [-p digits] [-e expression]... [file]...
 *
 * A file named "-" is standard input. Its options are read with sof_copt.
 * An error is reported as file:line:column: message, and the lines after it
 * are still evaluated.
 * @version 0.0.1
 * @date 2026-11-04
 * @copyright Copyright (c) 2026
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "calc.h"
#include "sof_copt.h"

/// @brief The most significant digits -p accepts: as many as a double
/// holds.
#define MAX_DIGITS 17

/// @brief What the options ask for, filled in by their callbacks.
typedef struct settings_t {
    /// @brief The significant digits results are printed with.
    int digits;
    /// @brief The expressions given with -e, pointing into argv.
    const char **expressions;
    size_t num_expressions;
} settings_t;

/// @brief What evaluating a line came to.
typedef enum outcome_t {
    /// @brief It was evaluated, or was empty.
    OUTCOME_OK = 0,
    /// @brief It had an error, which was reported.
    OUTCOME_FAILED,
    /// @brief Memory ran out, which was reported; stop.
    OUTCOME_NO_MEM,
} outcome_t;

static int digits_validate(sof_copt_t *option, void *ctx);
static int eval_file(calc_t *calc, const settings_t *settings, const char *path, sof_string_t *line);
static outcome_t eval_line(calc_t *calc, const settings_t *settings, const char *source, size_t number, const char *text);
static int expression_validate(sof_copt_t *option, void *ctx);
static const char *name_of(const char *path);
static int read_line(FILE *f, sof_string_t *line, int *got);
static void usage(FILE *stream);

/// @brief The code the validate callbacks return when an option's argument
/// is wrong, after printing why. sof_copt keeps it in the option's
/// stop_value.
#define BAD_ARGUMENT 1

static sof_copt_t opt_help = {
    .option = "-h",
    .description = "print this help and exit",
};

static sof_copt_t opt_digits = {
    .option = "-p",
    .description = "print results with this many significant digits, 1 to 17 (default 15)",
    .type = SOF_COPT_ARG_REQUIRED,
    .validate = digits_validate,
};

static sof_copt_t opt_expression = {
    .option = "-e",
    .description = "evaluate this line before any file; can be repeated",
    .type = SOF_COPT_ARG_REQUIRED,
    .validate = expression_validate,
};

SOF_COPT_OPTIONS static sof_copt_t *options[] = {
    &opt_help,
    &opt_digits,
    &opt_expression,
};

/// @brief Reads the argument of -p.
static int digits_validate(sof_copt_t *option, void *ctx) {
    settings_t *settings = ctx;
    char *end;
    errno = 0;
    long digits = strtol(option->value, &end, 10);
    if (errno != 0 || end == option->value || *end != '\0' || digits < 1 || digits > MAX_DIGITS) {
        fprintf(stderr, "%s needs a number of digits from 1 to %d, not \"%s\"\n", option->option, MAX_DIGITS, option->value);
        return BAD_ARGUMENT;
    }
    settings->digits = (int)digits;
    return 0;
}

/// @brief Evaluates one line, printing its value or why it failed.
/// @param source The file it came from, or "-e", for messages.
/// @param number Its line number, or for -e, which -e it is, counting from 1.
static outcome_t eval_line(calc_t *calc, const settings_t *settings, const char *source, size_t number, const char *text) {
    double value;
    int assigned;
    calc_error_t err = calc_eval(calc, text, &value, &assigned);
    if (err != CALC_OK && err != CALC_EMPTY) {
        // So the message comes after the results before it, when both go
        // to the same place.
        fflush(stdout);
    }
    switch (err) {
    case CALC_OK:
        if (!assigned) {
            // Prints -0 as 0.
            printf("%.*g\n", settings->digits, value == 0 ? 0.0 : value);
        }
        return OUTCOME_OK;
    case CALC_EMPTY:
        return OUTCOME_OK;
    case CALC_NO_MEM:
        fprintf(stderr, "%s:%zu: out of memory\n", source, number);
        return OUTCOME_NO_MEM;
    default:
        if (calc->column > 0) {
            fprintf(stderr, "%s:%zu:%zu: %s\n", source, number, calc->column, calc_message(calc));
        } else {
            fprintf(stderr, "%s:%zu: %s\n", source, number, calc_message(calc));
        }
        return OUTCOME_FAILED;
    }
}

/// @brief Evaluates each line of a file, or of standard input for "-".
/// @param line A string to read lines into.
/// @return 0, 1 if a line had an error or the file couldn't be read, after
/// printing why, or 2 if memory ran out, after printing that.
static int eval_file(calc_t *calc, const settings_t *settings, const char *path, sof_string_t *line) {
    int is_stdin = strcmp(path, "-") == 0;
    FILE *f = is_stdin ? stdin : fopen(path, "r");
    if (f == NULL) {
        fprintf(stderr, "error opening %s: %s\n", path, strerror(errno));
        return 1;
    }
    int rc = 0, got;
    size_t number = 0;
    while (rc != 2) {
        if (read_line(f, line, &got) != 0) {
            fprintf(stderr, "%s:%zu: out of memory\n", name_of(path), number + 1);
            rc = 2;
            break;
        }
        if (!got) {
            break;
        }
        number++;
        outcome_t outcome = eval_line(calc, settings, name_of(path), number, sof_string_cstr(line));
        if (outcome == OUTCOME_NO_MEM) {
            rc = 2;
        } else if (outcome == OUTCOME_FAILED) {
            rc = 1;
        }
    }
    if (ferror(f)) {
        fprintf(stderr, "error reading %s\n", name_of(path));
        if (rc == 0) {
            rc = 1;
        }
    }
    if (!is_stdin) {
        fclose(f);
    }
    return rc;
}

/// @brief Adds the argument of -e, each time it is given, to the
/// expressions to evaluate.
static int expression_validate(sof_copt_t *option, void *ctx) {
    settings_t *settings = ctx;
    settings->expressions[settings->num_expressions++] = option->value;
    return 0;
}

/// @brief Returns a file's name for messages: "standard input" for "-".
static const char *name_of(const char *path) {
    return strcmp(path, "-") == 0 ? "standard input" : path;
}

/// @brief Reads a line of any length into line, without its newline, or
/// its carriage return and newline.
/// @param got Set to 1 if a line was read, or 0 at the end of the file.
/// @return 0, or 1 if memory ran out.
static int read_line(FILE *f, sof_string_t *line, int *got) {
    char buffer[256];
    sof_string_clear(line);
    *got = 0;
    while (fgets(buffer, sizeof(buffer), f) != NULL) {
        *got = 1;
        if (sof_string_append_cstr(line, buffer) != SOF_STRING_OK) {
            return 1;
        }
        if (line->length > 0 && line->data[line->length - 1] == '\n') {
            break;
        }
    }
    if (line->length > 0 && line->data[line->length - 1] == '\n') {
        sof_string_delete(line, line->length - 1, 1);
    }
    if (line->length > 0 && line->data[line->length - 1] == '\r') {
        sof_string_delete(line, line->length - 1, 1);
    }
    return 0;
}

/// @brief Prints the usage message, with the options sof_copt knows.
static void usage(FILE *stream) {
    fprintf(stream, "usage: calc [-h] [-p digits] [-e expression]... [file]...\n");
    sof_copt_print(stream);
    fprintf(stream,
        "Evaluates each line of the -e options, then of the files, or of standard\n"
        "input if there are neither or a file is \"-\", and prints the results.\n"
        "A line is an expression, such as 2 * (3 + x), or an assignment, such as\n"
        "x = 4. Operators: + - * / %% and ^ (power), with unary - and parentheses.\n"
        "Functions: abs ceil cos exp floor ln log round sin sqrt tan. Variables:\n"
        "pi, e, and ans, the last result. # starts a comment.\n");
}

int main(int ac, char **av) {
    // Every argument could be a -e, so there is room for that many.
    settings_t settings = {15, sof_alloc_calloc(NULL, (size_t)ac, sizeof(char *)), 0};
    if (settings.expressions == NULL) {
        fprintf(stderr, "out of memory\n");
        return 1;
    }
    int index, rc = 2;
    sof_copt_t *option;
    sof_copt_error_t err = sof_copt_parse(ac, av, &settings, &index, &option);
    switch (err) {
    case SOF_COPT_OK:
        break;
    case SOF_COPT_NO_MATCH:
        fprintf(stderr, "unknown option %s\n", av[index]);
        usage(stderr);
        goto done;
    case SOF_COPT_MISSING_ARGUMENT:
        fprintf(stderr, "%s needs an argument\n", option->option);
        usage(stderr);
        goto done;
    case SOF_COPT_UNEXPECTED_ARGUMENT:
        fprintf(stderr, "%s takes no argument\n", option->option);
        usage(stderr);
        goto done;
    case SOF_COPT_STOPPED:
        goto done;
    default:
        fprintf(stderr, "can't read the options (error %d)\n", (int)err);
        goto done;
    }
    if (opt_help.present) {
        usage(stdout);
        rc = 0;
        goto done;
    }

    calc_t calc;
    sof_string_t line;
    if (calc_init(&calc, NULL) != CALC_OK || sof_string_init(&line, 256, NULL) != SOF_STRING_OK) {
        fprintf(stderr, "out of memory\n");
        calc_free(&calc);
        rc = 1;
        goto done;
    }
    rc = 0;
    for (size_t i = 0; i < settings.num_expressions && rc != 2; i++) {
        outcome_t outcome = eval_line(&calc, &settings, "-e", i + 1, settings.expressions[i]);
        if (outcome == OUTCOME_NO_MEM) {
            rc = 2;
        } else if (outcome == OUTCOME_FAILED) {
            rc = 1;
        }
    }
    if (index == ac && settings.num_expressions == 0) {
        rc = eval_file(&calc, &settings, "-", &line);
    }
    for (int i = index; i < ac && rc != 2; i++) {
        int file_rc = eval_file(&calc, &settings, av[i], &line);
        if (file_rc > rc) {
            rc = file_rc;
        }
    }
    // Running out of memory is an error like any other.
    if (rc == 2) {
        rc = 1;
    }
    sof_string_free(&line);
    calc_free(&calc);
done:
    sof_alloc_free(NULL, settings.expressions, (size_t)ac * sizeof(char *));
    return rc;
}
