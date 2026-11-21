/**
 * @file main.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief Prints or checks the 64-bit sof_hash of files, or of each line.
 * @details Like md5sum: prints the hash of each file, or of standard input,
 * as 16 hex digits, two spaces and the file's name. With -l it prints the
 * hash of each line instead, followed by the line. With -c it reads lists of
 * sums it printed earlier and checks each file against its sum, printing
 * "name: OK" or "name: FAILED".
 *
 *     hashsum [-h] [-c] [-l] [-s seed] [file]...
 *
 * With no file, or a file named "-", it reads standard input. Files are
 * hashed a chunk at a time with a sof_hash_stream_t, so a file of any size
 * takes the same memory; with -l, each file is read whole, into a
 * sof_string, to be split into lines. Its options are read with sof_copt. It exits with 0,
 * 1 if a file couldn't be read, memory ran out, or a check failed, and 2 if
 * the options are wrong.
 * @version 0.0.1
 * @date 2026-11-19
 * @copyright Copyright (c) 2026
 */

#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sof_copt.h"
#include "sof_hash.h"
#include "sof_string.h"

/// @brief A code the validate callbacks return, which sof_copt keeps in the
/// option's stop_value: its argument is wrong. The callback has printed why.
#define BAD_ARGUMENT 1

/// @brief The size of the chunks files are read in.
#define CHUNK_SIZE 65536

/// @brief The number of hex digits in a sum.
#define DIGITS 16

/// @brief What the options ask for, filled in by their callbacks.
typedef struct settings_t {
    /// @brief The seed every hash is made with, from -s.
    uint64_t seed;
} settings_t;

/// @brief The counts of a check, for its summary.
typedef struct tally_t {
    /// @brief Files whose sum didn't match.
    size_t failed;
    /// @brief Files that couldn't be read.
    size_t unread;
    /// @brief Lines that weren't a sum and a name.
    size_t malformed;
} tally_t;

static int check_list(const char *path, const settings_t *settings, sof_string_t *list, tally_t *tally);
static int check_sum(const char *line, size_t length, const settings_t *settings, tally_t *tally);
static int hash_file(const char *path, const settings_t *settings);
static int hash_lines(const char *path, const settings_t *settings, sof_string_t *contents);
static const char *next_line(const char *p, const char *end, size_t *length);
static int parse_sum(const char *s, uint64_t *sum);
static int read_file(const char *path, sof_string_t *contents);
static int seed_validate(sof_copt_t *option, void *ctx);
static int stream_file(const char *path, uint64_t seed, uint64_t *sum);
static void usage(FILE *stream);

static sof_copt_t opt_help = {
    .option = "-h",
    .description = "print this help and exit",
};

static sof_copt_t opt_check = {
    .option = "-c",
    .description = "check the sums in the files, as hashsum printed them",
};

static sof_copt_t opt_lines = {
    .option = "-l",
    .description = "print the hash of each line, then the line",
};

static sof_copt_t opt_seed = {
    .option = "-s",
    .description = "hash with this seed, decimal or 0x hex (default 0)",
    .type = SOF_COPT_ARG_REQUIRED,
    .validate = seed_validate,
};

SOF_COPT_OPTIONS static sof_copt_t *options[] = {
    &opt_help,
    &opt_check,
    &opt_lines,
    &opt_seed,
};

/// @brief Checks every sum in one list, or standard input for "-", reading
/// the list into list.
/// @return 0, or 1 if the list couldn't be read or memory ran out, after
/// printing why. Failed checks are counted in tally, not returned.
static int check_list(const char *path, const settings_t *settings, sof_string_t *list, tally_t *tally) {
    if (read_file(path, list) != 0) {
        return 1;
    }
    const char *p = list->data, *end = list->data + list->length;
    size_t length;
    int rc = 0;
    while (rc == 0 && p < end) {
        const char *line = p;
        p = next_line(p, end, &length);
        rc = check_sum(line, length, settings, tally);
    }
    return rc;
}

/// @brief Checks one line of a list: 16 hex digits, two spaces and the name
/// of a file, whose hash must be those digits. Prints "name: OK", or
/// "name: FAILED" with the reason, and counts failures in tally.
/// @return 0, or 1 if memory ran out.
static int check_sum(const char *line, size_t length, const settings_t *settings, tally_t *tally) {
    uint64_t sum, hash;
    if (length < DIGITS + 3 || line[DIGITS] != ' ' || line[DIGITS + 1] != ' ' || parse_sum(line, &sum) != 0) {
        tally->malformed++;
        return 0;
    }
    // The name is the rest of the line, and needs a NUL to be opened.
    sof_string_t name;
    if (sof_string_from_bytes(&name, line + DIGITS + 2, length - DIGITS - 2, NULL) != SOF_STRING_OK) {
        fprintf(stderr, "out of memory\n");
        return 1;
    }
    if (stream_file(name.data, settings->seed, &hash) != 0) {
        printf("%s: FAILED open or read\n", name.data);
        tally->unread++;
    } else if (hash != sum) {
        printf("%s: FAILED\n", name.data);
        tally->failed++;
    } else {
        printf("%s: OK\n", name.data);
    }
    sof_string_free(&name);
    return 0;
}

/// @brief Prints the hash of one file, or standard input for "-", and its
/// name.
/// @return 0, or 1 if the file couldn't be read, after printing why.
static int hash_file(const char *path, const settings_t *settings) {
    uint64_t hash;
    if (stream_file(path, settings->seed, &hash) != 0) {
        return 1;
    }
    printf("%016" PRIx64 "  %s\n", hash, path);
    return 0;
}

/// @brief Prints the hash of each line of one file, or standard input for
/// "-", then the line, reading it whole into contents. The hash is of the
/// line without its ending: a newline, or a carriage return and a newline.
/// @return 0, or 1 if the file couldn't be read or memory ran out, after
/// printing why.
static int hash_lines(const char *path, const settings_t *settings, sof_string_t *contents) {
    if (read_file(path, contents) != 0) {
        return 1;
    }
    const char *p = contents->data, *end = contents->data + contents->length;
    size_t length;
    while (p < end) {
        const char *line = p;
        p = next_line(p, end, &length);
        printf("%016" PRIx64 "  ", sof_hash_bytes(line, length, settings->seed));
        fwrite(line, 1, length, stdout);
        putchar('\n');
    }
    return 0;
}

/// @brief Finds the end of the line starting at p, which runs to a newline
/// or to end, and sets *length to its length without the newline or a
/// carriage return before it.
/// @return Where the next line starts: after the newline, or end.
static const char *next_line(const char *p, const char *end, size_t *length) {
    const char *newline = memchr(p, '\n', (size_t)(end - p));
    const char *stop = newline == NULL ? end : newline;
    *length = (size_t)(stop - p);
    if (newline != NULL && *length > 0 && p[*length - 1] == '\r') {
        (*length)--;
    }
    return newline == NULL ? end : newline + 1;
}

/// @brief Reads the 16 hex digits at s, in either case, into *sum.
/// @return 0, or 1 if any of them isn't a hex digit.
static int parse_sum(const char *s, uint64_t *sum) {
    uint64_t value = 0;
    for (int i = 0; i < DIGITS; i++) {
        char c = s[i];
        unsigned digit;
        if (c >= '0' && c <= '9') {
            digit = (unsigned)(c - '0');
        } else if (c >= 'a' && c <= 'f') {
            digit = (unsigned)(c - 'a' + 10);
        } else if (c >= 'A' && c <= 'F') {
            digit = (unsigned)(c - 'A' + 10);
        } else {
            return 1;
        }
        value = value << 4 | digit;
    }
    *sum = value;
    return 0;
}

/// @brief Reads the whole of a file, or standard input for "-", into
/// contents, replacing what it held, in chunks.
/// @return 0, or 1 if the file couldn't be read or memory ran out, after
/// printing why; contents then holds what was read.
static int read_file(const char *path, sof_string_t *contents) {
    int is_stdin = strcmp(path, "-") == 0;
    FILE *f = is_stdin ? stdin : fopen(path, "rb");
    if (f == NULL) {
        fprintf(stderr, "error opening %s: %s\n", path, strerror(errno));
        return 1;
    }
    sof_string_clear(contents);
    static char chunk[CHUNK_SIZE];
    int rc = 0;
    size_t got;
    while (rc == 0 && (got = fread(chunk, 1, sizeof(chunk), f)) > 0) {
        if (sof_string_append_bytes(contents, chunk, got) != SOF_STRING_OK) {
            fprintf(stderr, "out of memory reading %s\n", path);
            rc = 1;
        }
    }
    if (rc == 0 && ferror(f)) {
        fprintf(stderr, "error reading %s\n", is_stdin ? "standard input" : path);
        rc = 1;
    }
    if (!is_stdin) {
        fclose(f);
    }
    return rc;
}

/// @brief Reads the argument of -s: a decimal number, or hex after 0x.
static int seed_validate(sof_copt_t *option, void *ctx) {
    settings_t *settings = ctx;
    const char *s = option->value;
    char *end;
    errno = 0;
    unsigned long long value = strtoull(s, &end, 0);
    if (*s < '0' || *s > '9' || *end != '\0' || errno != 0) {
        fprintf(stderr, "%s needs a seed, such as 42 or 0x2a, not \"%s\"\n", option->option, s);
        return BAD_ARGUMENT;
    }
    settings->seed = (uint64_t)value;
    return 0;
}

/// @brief Hashes a file, or standard input for "-", a chunk at a time with a
/// sof_hash_stream_t, which gives the hash sof_hash_bytes() would of all of
/// it, into *sum.
/// @return 0, or 1 if the file couldn't be opened or read, after printing
/// why.
static int stream_file(const char *path, uint64_t seed, uint64_t *sum) {
    int is_stdin = strcmp(path, "-") == 0;
    FILE *f = is_stdin ? stdin : fopen(path, "rb");
    if (f == NULL) {
        fprintf(stderr, "error opening %s: %s\n", path, strerror(errno));
        return 1;
    }
    static char chunk[CHUNK_SIZE];
    sof_hash_stream_t stream;
    sof_hash_stream_init(&stream, seed);
    size_t got;
    while ((got = fread(chunk, 1, sizeof(chunk), f)) > 0) {
        sof_hash_stream_update(&stream, chunk, got);
    }
    int rc = 0;
    if (ferror(f)) {
        fprintf(stderr, "error reading %s\n", is_stdin ? "standard input" : path);
        rc = 1;
    }
    if (!is_stdin) {
        fclose(f);
    }
    *sum = sof_hash_stream_final(&stream);
    return rc;
}

/// @brief Prints the usage message, with the options sof_copt knows.
static void usage(FILE *stream) {
    fprintf(stream, "usage: hashsum [-h] [-c] [-l] [-s seed] [file]...\n");
    sof_copt_print(stream);
    fprintf(stream,
        "Prints the 64-bit sof_hash of each file, or of standard input if there are\n"
        "none or a file is \"-\", in hex, then its name. With -l, prints the hash of\n"
        "each line, then the line. With -c, reads lists of sums it printed and checks\n"
        "each file named, printing OK or FAILED; -s must give the seed they were made\n"
        "with. Exits with 0, 1 if a file can't be read or a check fails, 2 on bad options.\n");
}

int main(int ac, char **av) {
    settings_t settings = {0};
    int index;
    sof_copt_t *option;
    sof_copt_error_t err = sof_copt_parse(ac, av, &settings, &index, &option);
    switch (err) {
    case SOF_COPT_OK:
        break;
    case SOF_COPT_NO_MATCH:
        fprintf(stderr, "unknown option %s\n", av[index]);
        usage(stderr);
        return 2;
    case SOF_COPT_MISSING_ARGUMENT:
        fprintf(stderr, "%s needs an argument\n", option->option);
        usage(stderr);
        return 2;
    case SOF_COPT_UNEXPECTED_ARGUMENT:
        fprintf(stderr, "%s takes no argument\n", option->option);
        usage(stderr);
        return 2;
    case SOF_COPT_STOPPED:
        return 2;
    default:
        fprintf(stderr, "can't read the options (error %d)\n", (int)err);
        return 2;
    }
    if (opt_help.present) {
        usage(stdout);
        return 0;
    }
    if (opt_check.present && opt_lines.present) {
        fprintf(stderr, "-c checks the sums of files, so it can't be used with -l\n");
        usage(stderr);
        return 2;
    }

    // Files are hashed in chunks. With -l, each is read whole into contents,
    // which keeps its memory from one to the next; a check reads its list
    // into list.
    static char *standard_input[] = {"-"};
    char **paths = av + index;
    int num_paths = ac - index;
    if (num_paths == 0) {
        paths = standard_input;
        num_paths = 1;
    }
    sof_string_t contents, list;
    if (sof_string_init(&contents, 0, NULL) != SOF_STRING_OK || sof_string_init(&list, 0, NULL) != SOF_STRING_OK) {
        fprintf(stderr, "out of memory\n");
        sof_string_free(&contents);
        return 1;
    }
    int rc = 0;
    tally_t tally = {0, 0, 0};
    for (int i = 0; i < num_paths; i++) {
        int got;
        if (opt_check.present) {
            got = check_list(paths[i], &settings, &list, &tally);
        } else if (opt_lines.present) {
            got = hash_lines(paths[i], &settings, &contents);
        } else {
            got = hash_file(paths[i], &settings);
        }
        if (got != 0) {
            rc = 1;
        }
    }
    if (tally.malformed > 0) {
        fprintf(stderr, "hashsum: %zu line%s not a sum and a name\n", tally.malformed, tally.malformed == 1 ? " is" : "s are");
    }
    if (tally.unread > 0) {
        fprintf(stderr, "hashsum: %zu file%s couldn't be read\n", tally.unread, tally.unread == 1 ? "" : "s");
    }
    if (tally.failed > 0) {
        fprintf(stderr, "hashsum: %zu sum%s didn't match\n", tally.failed, tally.failed == 1 ? "" : "s");
    }
    if (tally.malformed + tally.unread + tally.failed > 0) {
        rc = 1;
    }
    sof_string_free(&list);
    sof_string_free(&contents);
    return rc;
}
