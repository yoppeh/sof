/**
 * @file main.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief Prints the lines of files, numbered, squeezed or cut to a range,
 * with each feature's options defined in its own source file.
 * @details The options of the program are spread over its modules: -n and
 * --number in number.c, -f and -c in range.c, -s in squeeze.c, and only -h here. Each
 * module lists its own in a SOF_COPT_OPTIONS array, which the linker merges
 * into one table, so this file parses the command line and prints the usage
 * message without knowing what the options are.
 *
 *     lines [-h] [-n] [--number[=width]] [-s] [-f first] [-c count] [file]...
 *
 * With no file, or a file named "-", it reads standard input. Options may
 * come after the files, and one-letter ones may be combined, as in
 * "lines -sn file". It exits with 0, 1 if a file couldn't be read, or 2 if
 * the options are wrong.
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
#include "sof_string.h"
#include "lines.h"

/// @brief The exit codes.
#define EXIT_DONE 0
#define EXIT_NO_READ 1
#define EXIT_BAD_OPTIONS 2

/// @brief What print_file() leaves to do.
typedef enum progress_t {
    /// @brief Go on to the next file.
    PROGRESS_MORE = 0,
    /// @brief The range is over: read no more files.
    PROGRESS_STOP,
    /// @brief A file couldn't be read, or memory ran out.
    PROGRESS_FAILED,
} progress_t;

static progress_t print_file(FILE *f, const char *name, sof_string_t *line, size_t *number);
static int read_line(FILE *f, sof_string_t *line);
static void usage(FILE *stream);

static sof_copt_t opt_help = {
    .option = "-h",
    .description = "print this help and exit",
};

SOF_COPT_OPTIONS static sof_copt_t *options[] = {
    &opt_help,
};

int lines_number(const sof_copt_t *option, size_t min, size_t max, size_t *n) {
    const char *s = option->value;
    char *end;
    errno = 0;
    unsigned long long value = s[0] >= '0' && s[0] <= '9' ? strtoull(s, &end, 10) : 0;
    if (s[0] < '0' || s[0] > '9' || errno != 0 || *end != '\0' || value < min || value > max) {
        if (max == SIZE_MAX) {
            fprintf(stderr, "%s wants a number from %zu up, not \"%s\"\n", option->option, min, s);
        } else {
            fprintf(stderr, "%s wants a number from %zu to %zu, not \"%s\"\n", option->option, min, max, s);
        }
        return LINES_BAD_ARGUMENT;
    }
    *n = (size_t)value;
    return 0;
}

/// @brief Prints the lines of f that the modules choose, as they ask.
/// @param name The file's name, for messages.
/// @param line A string to read each line into.
/// @param number The number of lines read before this file, which is
/// counted on, since the files are numbered as one input.
/// @return What is left to do.
static progress_t print_file(FILE *f, const char *name, sof_string_t *line, size_t *number) {
    int got;
    while ((got = read_line(f, line)) > 0) {
        (*number)++;
        range_t range = range_select(*number);
        if (range == RANGE_STOP) {
            return PROGRESS_STOP;
        }
        if (range == RANGE_PRINT && squeeze_keep(line)) {
            number_print(stdout, *number);
            fwrite(line->data, 1, line->length, stdout);
        }
    }
    if (got < 0) {
        fprintf(stderr, "out of memory\n");
        return PROGRESS_FAILED;
    }
    if (ferror(f)) {
        fprintf(stderr, "%s: %s\n", name, strerror(errno));
        return PROGRESS_FAILED;
    }
    return PROGRESS_MORE;
}

/// @brief Reads a line of any length into line, with its line ending, if it
/// has one.
/// @return 1 if a line was read, 0 at the end of the file or on an error,
/// or -1 if memory ran out.
static int read_line(FILE *f, sof_string_t *line) {
    char buffer[256];
    sof_string_clear(line);
    while (fgets(buffer, sizeof(buffer), f) != NULL) {
        if (sof_string_append_cstr(line, buffer) != SOF_STRING_OK) {
            return -1;
        }
        if (line->data[line->length - 1] == '\n') {
            break;
        }
    }
    return line->length > 0;
}

/// @brief Prints the usage message, with the options sof_copt knows.
static void usage(FILE *stream) {
    fprintf(stream, "usage: lines [-h] [-n] [--number[=width]] [-s] [-f first] [-c count] [file]...\n");
    sof_copt_print(stream);
    fprintf(stream,
        "Prints the lines of the files, or of standard input if there are none or a\n"
        "file is \"-\", as one input. Exits with 0, 1 if a file couldn't be read, or\n"
        "2 if the options are wrong.\n");
}

int main(int ac, char **av) {
    int index;
    sof_copt_t *option;
    sof_copt_error_t err = sof_copt_parse(ac, av, NULL, &index, &option);
    switch (err) {
    case SOF_COPT_OK:
        break;
    case SOF_COPT_NO_MATCH:
        fprintf(stderr, "unknown option %s\n", av[index]);
        usage(stderr);
        return EXIT_BAD_OPTIONS;
    case SOF_COPT_MISSING_ARGUMENT:
        fprintf(stderr, "%s needs an argument\n", option->option);
        usage(stderr);
        return EXIT_BAD_OPTIONS;
    case SOF_COPT_UNEXPECTED_ARGUMENT:
        fprintf(stderr, "%s takes no argument\n", option->option);
        usage(stderr);
        return EXIT_BAD_OPTIONS;
    case SOF_COPT_STOPPED:
        // The option's callback returned LINES_BAD_ARGUMENT, its only code,
        // and has said why.
        return EXIT_BAD_OPTIONS;
    default:
        fprintf(stderr, "can't read the options (error %d)\n", (int)err);
        return EXIT_BAD_OPTIONS;
    }
    if (opt_help.present) {
        usage(stdout);
        return EXIT_DONE;
    }

    static char *standard_input[] = {"-"};
    char **paths = av + index;
    int num_paths = ac - index;
    if (num_paths == 0) {
        paths = standard_input;
        num_paths = 1;
    }
    sof_string_t line;
    if (sof_string_init(&line, 0, NULL) != SOF_STRING_OK) {
        fprintf(stderr, "out of memory\n");
        return EXIT_NO_READ;
    }
    int rc = EXIT_DONE;
    size_t number = 0;
    progress_t progress = PROGRESS_MORE;
    for (int i = 0; i < num_paths && progress != PROGRESS_STOP; i++) {
        int is_stdin = strcmp(paths[i], "-") == 0;
        const char *name = is_stdin ? "standard input" : paths[i];
        FILE *f = is_stdin ? stdin : fopen(paths[i], "r");
        if (f == NULL) {
            fprintf(stderr, "%s: %s\n", name, strerror(errno));
            rc = EXIT_NO_READ;
            continue;
        }
        progress = print_file(f, name, &line, &number);
        if (progress == PROGRESS_FAILED) {
            rc = EXIT_NO_READ;
        }
        if (!is_stdin) {
            fclose(f);
        }
    }
    sof_string_free(&line);
    return rc;
}
