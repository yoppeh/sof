/**
 * @file main.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief Prints the last lines, or bytes, of files, as tail does.
 * @details Reads each file, or standard input, to its end, keeping its last
 * lines in a sof_ring that overwrites its oldest line when it is full, and
 * then prints what the ring holds. Each line is a sof_string_t that the
 * ring takes over when it is pushed and frees, through its delete function,
 * when the line is overwritten or the ring is cleared. With -c, the ring
 * holds the last bytes instead, one byte to an element.
 *
 *     tail [-h] [-q] [-n lines | -c bytes] [file]...
 *
 * With no file, or a file named "-", it reads standard input. With more
 * than one file, each is headed "==> name <==", as tail heads them. Its
 * options are read with sof_copt. It exits with 0, 1 if a file couldn't be
 * read or memory ran out, and 2 for a usage error.
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
#include "sof_ring.h"
#include "sof_string.h"

/// @brief The exit codes.
#define EXIT_DONE 0
#define EXIT_TROUBLE 1
#define EXIT_USAGE 2

/// @brief The code count_validate() returns for an argument that isn't a
/// non-negative number, after printing why. sof_copt keeps it in the
/// option's stop_value.
#define BAD_NUMBER 1

/// @brief The size of each read.
#define CHUNK 65536

/// @brief The bytes a line can hold before it first grows.
#define LINE_CAPACITY 80

/// @brief The number of lines printed when neither -n nor -c is given.
#define DEFAULT_LINES 10

static int count_validate(sof_copt_t *option, void *ctx);
static void free_line(void *element, void *ctx);
static int print_line(void *element, void *ctx);
static int read_count(const char *s, size_t *count);
static int tail_bytes(FILE *f, sof_ring_t *ring);
static int tail_file(FILE *f, sof_ring_t *ring, int bytes);
static int tail_lines(FILE *f, sof_ring_t *ring);
static void usage(FILE *stream);

static sof_copt_t opt_help = {
    .option = "-h",
    .description = "print this help and exit",
};

static sof_copt_t opt_quiet = {
    .option = "-q",
    .description = "never print the \"==> name <==\" headers",
};

static sof_copt_t opt_lines = {
    .option = "-n",
    .description = "print the last this many lines (default 10)",
    .type = SOF_COPT_ARG_REQUIRED,
    .validate = count_validate,
};

static sof_copt_t opt_bytes = {
    .option = "-c",
    .description = "print the last this many bytes instead of lines",
    .type = SOF_COPT_ARG_REQUIRED,
    .validate = count_validate,
};

SOF_COPT_OPTIONS static sof_copt_t *options[] = {
    &opt_help,
    &opt_quiet,
    &opt_lines,
    &opt_bytes,
};

/// @brief Reads the argument of -n or -c into the count in ctx, printing
/// why if it isn't a non-negative number.
static int count_validate(sof_copt_t *option, void *ctx) {
    if (read_count(option->value, ctx) != 0) {
        fprintf(stderr, "tail: %s needs a non-negative number, not \"%s\"\n", option->option, option->value);
        return BAD_NUMBER;
    }
    return 0;
}

/// @brief The ring's delete function, for a ring of lines: frees the
/// sof_string_t that the ring took over when the line was pushed.
static void free_line(void *element, void *ctx) {
    (void)ctx;
    sof_string_free(element);
}

/// @brief The sof_ring_traverse() callback for a ring of lines: writes the
/// line, which holds its newline unless it ended the input without one.
/// @return 0 to go on, or 1 if the write failed, which stops the traversal.
static int print_line(void *element, void *ctx) {
    (void)ctx;
    const sof_string_t *line = element;
    return fwrite(line->data, 1, line->length, stdout) == line->length ? 0 : 1;
}

/// @brief Reads a non-negative decimal number from s into *count.
/// @return 0, or -1 if s isn't one or it is too large.
static int read_count(const char *s, size_t *count) {
    char *end;
    errno = 0;
    unsigned long long value = strtoull(s, &end, 10);
    if (*s < '0' || *s > '9' || *end != '\0' || errno != 0 || value > SIZE_MAX) {
        return -1;
    }
    *count = (size_t)value;
    return 0;
}

/// @brief Reads f to its end, pushing every byte onto ring, of one-byte
/// elements, so that it is left holding the last ring->capacity bytes.
/// @return 0, or -1 if f couldn't be read.
static int tail_bytes(FILE *f, sof_ring_t *ring) {
    static unsigned char buffer[CHUNK];
    size_t n;
    while ((n = fread(buffer, 1, sizeof(buffer), f)) > 0) {
        // Only the last capacity bytes of a chunk can end up in the ring.
        size_t skip = n > ring->capacity ? n - ring->capacity : 0;
        for (size_t i = skip; i < n; i++) {
            sof_ring_push_overwrite(ring, &buffer[i]);
        }
    }
    return ferror(f) ? -1 : 0;
}

/// @brief Prints the last lines, or with bytes set the last bytes, of f.
/// @details The ring is empty before and after.
/// @return 0, -1 if f couldn't be read, or -2 if memory ran out.
static int tail_file(FILE *f, sof_ring_t *ring, int bytes) {
    int rc = bytes ? tail_bytes(f, ring) : tail_lines(f, ring);
    if (rc == 0) {
        if (bytes) {
            for (unsigned char *b = sof_ring_peek(ring); b != NULL; b = sof_ring_next(ring, b)) {
                putchar(*b);
            }
        } else {
            sof_ring_traverse(ring, print_line, NULL);
        }
    }
    // Lines still held go to free_line().
    sof_ring_clear(ring);
    return rc;
}

/// @brief Reads f to its end, keeping its last ring->capacity lines in
/// ring.
/// @details Each line is gathered in a sof_string_t, newline included, and
/// pushed whole; the ring then owns the string, and a new one is started for
/// the next line. When the ring is full, pushing frees its oldest line
/// through free_line().
/// @return 0, -1 if f couldn't be read, or -2 if memory ran out.
static int tail_lines(FILE *f, sof_ring_t *ring) {
    static char buffer[CHUNK];
    sof_string_t line;
    if (sof_string_init(&line, LINE_CAPACITY, NULL) != SOF_STRING_OK) {
        return -2;
    }
    int rc = 0;
    size_t n;
    while (rc == 0 && (n = fread(buffer, 1, sizeof(buffer), f)) > 0) {
        const char *p = buffer, *end = buffer + n;
        while (rc == 0 && p < end) {
            const char *newline = memchr(p, '\n', (size_t)(end - p));
            const char *stop = newline == NULL ? end : newline + 1;
            if (sof_string_append_bytes(&line, p, (size_t)(stop - p)) != SOF_STRING_OK) {
                rc = -2;
            } else if (newline != NULL) {
                sof_ring_push_overwrite(ring, &line);
                if (sof_string_init(&line, LINE_CAPACITY, NULL) != SOF_STRING_OK) {
                    rc = -2;
                }
            }
            p = stop;
        }
    }
    if (rc == 0 && ferror(f)) {
        rc = -1;
    }
    if (rc == 0 && line.length > 0) {
        // The last line had no newline.
        sof_ring_push_overwrite(ring, &line);
    } else {
        sof_string_free(&line);
    }
    return rc;
}

/// @brief Prints the usage message, with the options sof_copt knows.
static void usage(FILE *stream) {
    fprintf(stream, "usage: tail [-h] [-q] [-n lines | -c bytes] [file]...\n");
    sof_copt_print(stream);
    fprintf(stream,
        "Prints the last lines, or bytes, of each file, or of standard input if\n"
        "there are none or a file is \"-\". With several files, each is headed\n"
        "\"==> name <==\". Exits with 0, 1 if a file can't be read, 2 for bad options.\n");
}

int main(int ac, char **av) {
    // -n and -c each read their argument into this.
    size_t count = DEFAULT_LINES;
    int index;
    sof_copt_t *option;
    sof_copt_error_t err = sof_copt_parse(ac, av, &count, &index, &option);
    switch (err) {
    case SOF_COPT_OK:
        break;
    case SOF_COPT_NO_MATCH:
        fprintf(stderr, "tail: unknown option %s\n", av[index]);
        usage(stderr);
        return EXIT_USAGE;
    case SOF_COPT_MISSING_ARGUMENT:
        fprintf(stderr, "tail: %s needs an argument\n", option->option);
        usage(stderr);
        return EXIT_USAGE;
    case SOF_COPT_UNEXPECTED_ARGUMENT:
        fprintf(stderr, "tail: %s takes no argument\n", option->option);
        usage(stderr);
        return EXIT_USAGE;
    case SOF_COPT_STOPPED:
        return EXIT_USAGE;
    default:
        fprintf(stderr, "tail: can't read the options (error %d)\n", (int)err);
        return EXIT_USAGE;
    }
    if (opt_help.present) {
        usage(stdout);
        return EXIT_DONE;
    }
    if (opt_lines.present && opt_bytes.present) {
        fprintf(stderr, "tail: give -n or -c, not both\n");
        usage(stderr);
        return EXIT_USAGE;
    }
    int bytes = opt_bytes.present;

    // A ring can't have a capacity of 0, so with -n 0 or -c 0 there is no
    // ring, and the files aren't read.
    sof_ring_t ring;
    if (count > 0) {
        sof_ring_error_t rc = bytes ? sof_ring_init(&ring, 1, count, NULL, NULL, NULL) : sof_ring_init(&ring, sizeof(sof_string_t), count, free_line, NULL, NULL);
        if (rc != SOF_RING_OK) {
            fprintf(stderr, "tail: out of memory\n");
            return EXIT_TROUBLE;
        }
    }
    static char *standard_input[] = {"-"};
    char **paths = av + index;
    int num_paths = ac - index;
    if (num_paths == 0) {
        paths = standard_input;
        num_paths = 1;
    }
    int status = EXIT_DONE, printed = 0;
    for (int i = 0; i < num_paths; i++) {
        int is_stdin = strcmp(paths[i], "-") == 0;
        const char *name = is_stdin ? "standard input" : paths[i];
        FILE *f = is_stdin ? stdin : fopen(paths[i], "rb");
        if (f == NULL) {
            fprintf(stderr, "tail: %s: %s\n", name, strerror(errno));
            status = EXIT_TROUBLE;
            continue;
        }
        if (num_paths > 1 && !opt_quiet.present) {
            printf("%s==> %s <==\n", printed ? "\n" : "", name);
            printed = 1;
        }
        int rc = count > 0 ? tail_file(f, &ring, bytes) : 0;
        if (rc == -1) {
            fprintf(stderr, "tail: %s: read error\n", name);
            status = EXIT_TROUBLE;
        } else if (rc == -2) {
            fprintf(stderr, "tail: out of memory reading %s\n", name);
            status = EXIT_TROUBLE;
        }
        if (is_stdin) {
            clearerr(stdin);
        } else {
            fclose(f);
        }
    }
    if (count > 0) {
        sof_ring_free(&ring);
    }
    if (fflush(stdout) != 0) {
        fprintf(stderr, "tail: write error\n");
        status = EXIT_TROUBLE;
    }
    return status;
}
