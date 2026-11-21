/**
 * @file main.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief A BASIC interpreter: runs a program from a file, or the lines typed
 * or piped on standard input.
 * @details With a file, the program in it is loaded and run, and INPUT reads
 * standard input. Without one, each line of standard input is entered as if
 * it were typed: a line with a number is stored in the program, and any
 * other is run at once, so RUN, LIST, NEW, CONT, LOAD and SAVE work as
 * commands. A prompt is shown only when standard input is a terminal.
 *
 *     basic [-h] [-s seed] [file]
 *
 * Its options are read with sof_copt. Errors go to standard error. When
 * standard input isn't a terminal, the replies INPUT reads are echoed after
 * its prompt, so the output reads as it would on a terminal.
 * @version 0.0.1
 * @date 2026-11-04
 * @copyright Copyright (c) 2026
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "basic.h"
#include "sof_copt.h"

/// @brief What the options ask for, filled in by their callbacks.
typedef struct settings_t {
    /// @brief The seed for RND.
    uint64_t seed;
} settings_t;

/// @brief What the callbacks share.
typedef struct session_t {
    /// @brief Set to echo the replies INPUT reads.
    int echo;
} session_t;

static int read_input(sof_string_t *line, void *ctx);
static int read_line(FILE *f, sof_string_t *line);
static int seed_validate(sof_copt_t *option, void *ctx);
static void usage(FILE *stream);
static void warn_output(const char *message, void *ctx);
static int write_output(const char *text, size_t length, void *ctx);

/// @brief The code the validate callbacks return when an option's argument
/// is wrong, after printing why. sof_copt keeps it in the option's
/// stop_value.
#define BAD_ARGUMENT 1

static sof_copt_t opt_help = {
    .option = "-h",
    .description = "print this help and exit",
};

static sof_copt_t opt_seed = {
    .option = "-s",
    .description = "start RND from this seed, 0 to 18446744073709551615 (default: from the time)",
    .type = SOF_COPT_ARG_REQUIRED,
    .validate = seed_validate,
};

SOF_COPT_OPTIONS static sof_copt_t *options[] = {
    &opt_help,
    &opt_seed,
};

/// @brief Reads a line for INPUT from standard input, echoing it if the
/// session asks for that.
static int read_input(sof_string_t *line, void *ctx) {
    const session_t *session = ctx;
    fflush(stdout);
    if (read_line(stdin, line) != 1) {
        return 1;
    }
    if (session->echo) {
        fwrite(line->data, 1, line->length, stdout);
        fputc('\n', stdout);
    }
    return 0;
}

/// @brief Reads a line of any length into line, without its newline, or
/// its carriage return and newline.
/// @return 1 if a line was read, 0 at the end of the file, or -1 if memory
/// ran out.
static int read_line(FILE *f, sof_string_t *line) {
    char buffer[256];
    int got = 0;
    sof_string_clear(line);
    while (fgets(buffer, sizeof(buffer), f) != NULL) {
        got = 1;
        if (sof_string_append_cstr(line, buffer) != SOF_STRING_OK) {
            return -1;
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
    return got;
}

/// @brief Reads the argument of -s.
static int seed_validate(sof_copt_t *option, void *ctx) {
    settings_t *settings = ctx;
    char *end;
    errno = 0;
    unsigned long long seed = strtoull(option->value, &end, 10);
    if (errno != 0 || end == option->value || *end != '\0' || option->value[0] == '-') {
        fprintf(stderr, "%s needs a number from 0 to 18446744073709551615, not \"%s\"\n", option->option, option->value);
        return BAD_ARGUMENT;
    }
    settings->seed = (uint64_t)seed;
    return 0;
}

/// @brief Prints the usage message, with the options sof_copt knows.
static void usage(FILE *stream) {
    fprintf(stream, "usage: basic [-h] [-s seed] [file]\n");
    sof_copt_print(stream);
    fprintf(stream,
        "Runs the BASIC program in the file, or with no file, enters each line of\n"
        "standard input as if it were typed: a line that starts with a number is\n"
        "stored in the program, and any other is run at once. The commands are RUN,\n"
        "LIST, NEW, CONT, LOAD \"file\" and SAVE \"file\".\n");
}

/// @brief Reports an exception the program goes on after, on standard
/// error.
static void warn_output(const char *message, void *ctx) {
    (void)ctx;
    fflush(stdout);
    fprintf(stderr, "%s\n", message);
}

/// @brief Writes the program's output to standard output.
static int write_output(const char *text, size_t length, void *ctx) {
    (void)ctx;
    return fwrite(text, 1, length, stdout) == length ? 0 : 1;
}

int main(int ac, char **av) {
    settings_t settings = {(uint64_t)time(NULL)};
    int index, rc = 2;
    sof_copt_t *option;
    sof_copt_error_t err = sof_copt_parse(ac, av, &settings, &index, &option);
    switch (err) {
    case SOF_COPT_OK:
        break;
    case SOF_COPT_NO_MATCH:
        fprintf(stderr, "unknown option %s\n", av[index]);
        usage(stderr);
        return rc;
    case SOF_COPT_MISSING_ARGUMENT:
        fprintf(stderr, "%s needs an argument\n", option->option);
        usage(stderr);
        return rc;
    case SOF_COPT_UNEXPECTED_ARGUMENT:
        fprintf(stderr, "%s takes no argument\n", option->option);
        usage(stderr);
        return rc;
    case SOF_COPT_STOPPED:
        return rc;
    default:
        fprintf(stderr, "can't read the options (error %d)\n", (int)err);
        return rc;
    }
    if (opt_help.present) {
        usage(stdout);
        return 0;
    }
    if (ac - index > 1) {
        fprintf(stderr, "only one file can be run\n");
        usage(stderr);
        return rc;
    }

    int interactive = isatty(0);
    session_t session = {!interactive};
    basic_t b;
    sof_string_t line;
    if (basic_init(&b, write_output, read_input, warn_output, &session, settings.seed, NULL) != BASIC_OK) {
        fprintf(stderr, "out of memory\n");
        return 1;
    }
    if (sof_string_init(&line, 256, NULL) != SOF_STRING_OK) {
        fprintf(stderr, "out of memory\n");
        basic_free(&b);
        return 1;
    }
    rc = 0;
    if (index < ac) {
        basic_error_t result = basic_load(&b, av[index]);
        if (result == BASIC_OK) {
            result = basic_run(&b);
        }
        if (result != BASIC_OK && result != BASIC_STOPPED) {
            fflush(stdout);
            fprintf(stderr, "%s\n", basic_message(&b));
            rc = 1;
        }
    } else {
        for (;;) {
            if (interactive) {
                fputs("> ", stdout);
                fflush(stdout);
            }
            int got = read_line(stdin, &line);
            if (got < 0) {
                fprintf(stderr, "out of memory\n");
                rc = 1;
                break;
            }
            if (got == 0) {
                if (interactive) {
                    fputc('\n', stdout);
                }
                break;
            }
            basic_error_t result = basic_line(&b, sof_string_cstr(&line));
            if (result != BASIC_OK) {
                fflush(stdout);
                fprintf(stderr, "%s\n", basic_message(&b));
                if (result != BASIC_STOPPED && !interactive) {
                    rc = 1;
                }
            }
        }
    }
    sof_string_free(&line);
    basic_free(&b);
    return rc;
}
