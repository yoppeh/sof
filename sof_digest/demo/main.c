/**
 * @file main.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief Prints or checks the digests of files, as sha256sum does.
 * @details Prints the SHA-256 digest of each file, or of standard input, as
 * 64 hex digits, two spaces and the file's name, which is GNU sha256sum's
 * format, or with --tag as "SHA256 (name) = digest", BSD's. -a chooses
 * another digest: MD5, SHA-1, SHA-224, SHA-384, SHA-512, SHA-512/256, or
 * the CRC-32 or CRC-32C, printed as 8 hex digits. With -c it reads lists of
 * digests in either format, as this program, sha256sum, md5sum or shasum
 * printed them, and checks each file against its digest, printing
 * "name: OK" or "name: FAILED". A line in the tag format names its digest,
 * so a list can mix them; the others are -a's.
 *
 *     digestsum [-h] [-a algorithm] [-c] [--quiet] [--status] [--tag] [file]...
 *
 * With no file, or a file named "-", it reads standard input. Files are
 * digested a chunk at a time with a stream, such as a sof_digest_sha256_t,
 * so a file of any size takes the same memory; a list for -c is read
 * whole, into a sof_string, to be split into lines. A name with a newline,
 * a carriage return or a backslash in it is written escaped, after a
 * backslash at the start of the line, as sha256sum writes it. Its options
 * are read with sof_copt. It exits with 0, 1 if a file couldn't be read,
 * memory ran out, or a check failed, and 2 if the options are wrong.
 * @version 0.0.1
 * @date 2026-11-19
 * @copyright Copyright (c) 2026
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sof_copt.h"
#include "sof_digest.h"
#include "sof_string.h"

/// @brief The size of the chunks files are read in.
#define CHUNK_SIZE 65536

/// @brief What comes between the name and the digest in the tag format,
/// after the algorithm's tag and " (".
#define TAG_MIDDLE ") = "

/// @brief The digests the program can compute.
typedef enum algorithm_t {
    ALGORITHM_MD5,
    ALGORITHM_SHA1,
    ALGORITHM_SHA224,
    ALGORITHM_SHA256,
    ALGORITHM_SHA384,
    ALGORITHM_SHA512,
    ALGORITHM_SHA512_256,
    ALGORITHM_CRC32,
    ALGORITHM_CRC32C,
    ALGORITHMS,
} algorithm_t;

/// @brief What the program knows of each digest.
typedef struct algorithm_info_t {
    /// @brief The name -a takes.
    const char *name;
    /// @brief The tag the tag format starts with, as BSD's tools write it.
    const char *tag;
    /// @brief The bytes in the digest.
    size_t size;
} algorithm_info_t;

/// @brief A stream of any of the digests.
typedef union stream_t {
    sof_digest_md5_t md5;
    sof_digest_sha1_t sha1;
    sof_digest_sha256_t sha256;
    sof_digest_sha512_t sha512;
    sof_digest_crc32_t crc32;
    sof_digest_crc32c_t crc32c;
} stream_t;

/// @brief What parse_line() found in a line of a list.
typedef enum parse_t {
    /// @brief A digest and a name.
    PARSE_OK,
    /// @brief Not a digest and a name.
    PARSE_MALFORMED,
    /// @brief Memory ran out.
    PARSE_NO_MEM,
} parse_t;

/// @brief The counts of a check, for its summary.
typedef struct tally_t {
    /// @brief Files whose digest didn't match.
    size_t failed;
    /// @brief Files that couldn't be read.
    size_t unread;
    /// @brief Lines that weren't a digest and a name.
    size_t malformed;
} tally_t;

static int algorithm_validate(sof_copt_t *option, void *ctx);
static int check_line(const char *line, size_t length, sof_string_t *name, tally_t *tally);
static int check_list(const char *path, sof_string_t *list, sof_string_t *name, tally_t *tally);
static int digest_file(algorithm_t algorithm, const char *path, unsigned char *digest);
static int needs_escape(const char *name);
static const char *next_line(const char *p, const char *end, size_t *length);
static parse_t parse_line(const char *line, size_t length, algorithm_t *algorithm, unsigned char *digest, sof_string_t *name);
static void print_name(const char *name, int escaped);
static int print_sum(const char *path);
static int read_file(const char *path, sof_string_t *contents);
static void stream_final(algorithm_t algorithm, const stream_t *s, unsigned char *digest);
static void stream_init(algorithm_t algorithm, stream_t *s);
static void stream_update(algorithm_t algorithm, stream_t *s, const void *data, size_t length);
static parse_t unescape(const char *s, size_t length, int escaped, sof_string_t *name);
static void usage(FILE *stream);

/// @brief The digests, in algorithm_t's order. The CRCs are written as their
/// 32-bit values are, high byte first.
static const algorithm_info_t algorithms[ALGORITHMS] = {
    {"md5", "MD5", SOF_DIGEST_MD5_SIZE},
    {"sha1", "SHA1", SOF_DIGEST_SHA1_SIZE},
    {"sha224", "SHA224", SOF_DIGEST_SHA224_SIZE},
    {"sha256", "SHA256", SOF_DIGEST_SHA256_SIZE},
    {"sha384", "SHA384", SOF_DIGEST_SHA384_SIZE},
    {"sha512", "SHA512", SOF_DIGEST_SHA512_SIZE},
    {"sha512-256", "SHA512/256", SOF_DIGEST_SHA512_256_SIZE},
    {"crc32", "CRC32", 4},
    {"crc32c", "CRC32C", 4},
};

/// @brief The digest -a chose, SHA-256 if it wasn't given.
static algorithm_t chosen = ALGORITHM_SHA256;

static sof_copt_t opt_help = {
    .option = "-h",
    .description = "print this help and exit",
};

static sof_copt_t opt_algorithm = {
    .option = "-a",
    .description = "the digest: md5, sha1, sha224, sha256 (the default), sha384, sha512, sha512-256, crc32 or crc32c",
    .type = SOF_COPT_ARG_REQUIRED,
    .validate = algorithm_validate,
};

static sof_copt_t opt_check = {
    .option = "-c",
    .description = "check the digests in the files, in either format",
};

static sof_copt_t opt_quiet = {
    .option = "--quiet",
    .description = "with -c, don't print OK for each file that checks",
};

static sof_copt_t opt_status = {
    .option = "--status",
    .description = "with -c, print nothing; the exit status tells",
};

static sof_copt_t opt_tag = {
    .option = "--tag",
    .description = "print \"SHA256 (name) = digest\", as BSD does, with the digest's tag",
};

SOF_COPT_OPTIONS static sof_copt_t *options[] = {
    &opt_help,
    &opt_algorithm,
    &opt_check,
    &opt_quiet,
    &opt_status,
    &opt_tag,
};

/// @brief Reads -a's argument, the name of a digest, into chosen.
static int algorithm_validate(sof_copt_t *option, void *ctx) {
    (void)ctx;
    for (int i = 0; i < ALGORITHMS; i++) {
        if (strcmp(option->value, algorithms[i].name) == 0) {
            chosen = (algorithm_t)i;
            return 0;
        }
    }
    fprintf(stderr, "unknown digest %s\n", option->value);
    return 1;
}

/// @brief Checks one line of a list, a digest and the name of a file whose
/// digest it must be. Prints "name: OK", or "name: FAILED" with the reason,
/// as --quiet and --status allow, and counts failures in tally.
/// @return 0, or 1 if memory ran out.
static int check_line(const char *line, size_t length, sof_string_t *name, tally_t *tally) {
    unsigned char want[SOF_DIGEST_MAX_SIZE], got[SOF_DIGEST_MAX_SIZE];
    algorithm_t algorithm;
    parse_t parsed = parse_line(line, length, &algorithm, want, name);
    if (parsed == PARSE_NO_MEM) {
        fprintf(stderr, "out of memory\n");
        return 1;
    }
    if (parsed == PARSE_MALFORMED) {
        tally->malformed++;
        return 0;
    }
    const char *result = NULL;
    if (digest_file(algorithm, name->data, got) != 0) {
        result = "FAILED open or read";
        tally->unread++;
    } else if (memcmp(want, got, algorithms[algorithm].size) != 0) {
        result = "FAILED";
        tally->failed++;
    } else if (!opt_quiet.present) {
        result = "OK";
    }
    if (result != NULL && !opt_status.present) {
        int escaped = needs_escape(name->data);
        if (escaped) {
            putchar('\\');
        }
        print_name(name->data, escaped);
        printf(": %s\n", result);
    }
    return 0;
}

/// @brief Checks every digest in one list, or standard input for "-",
/// reading the list into list and each name into name.
/// @return 0, or 1 if the list couldn't be read or memory ran out, after
/// printing why. Failed checks are counted in tally, not returned.
static int check_list(const char *path, sof_string_t *list, sof_string_t *name, tally_t *tally) {
    if (read_file(path, list) != 0) {
        return 1;
    }
    const char *p = list->data, *end = list->data + list->length;
    size_t length;
    int rc = 0;
    while (rc == 0 && p < end) {
        const char *line = p;
        p = next_line(p, end, &length);
        rc = check_line(line, length, name, tally);
    }
    return rc;
}

/// @brief Digests a file, or standard input for "-", a chunk at a time with
/// a stream for algorithm, into digest.
/// @return 0, or 1 if the file couldn't be opened or read, after printing
/// why.
static int digest_file(algorithm_t algorithm, const char *path, unsigned char *digest) {
    int is_stdin = strcmp(path, "-") == 0;
    FILE *f = is_stdin ? stdin : fopen(path, "rb");
    if (f == NULL) {
        fprintf(stderr, "error opening %s: %s\n", path, strerror(errno));
        return 1;
    }
    static char chunk[CHUNK_SIZE];
    stream_t s;
    stream_init(algorithm, &s);
    size_t got;
    while ((got = fread(chunk, 1, sizeof(chunk), f)) > 0) {
        stream_update(algorithm, &s, chunk, got);
    }
    int rc = 0;
    if (ferror(f)) {
        fprintf(stderr, "error reading %s\n", is_stdin ? "standard input" : path);
        rc = 1;
    }
    if (!is_stdin) {
        fclose(f);
    }
    stream_final(algorithm, &s, digest);
    return rc;
}

/// @brief Returns 1 if name has a character that is written escaped: a
/// backslash, a newline or a carriage return.
static int needs_escape(const char *name) {
    return strpbrk(name, "\\\n\r") != NULL;
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

/// @brief Reads one line of a list into algorithm, digest and name:
/// "digest  name" or "digest *name", as sha256sum writes it, with -a's
/// digest, or "TAG (name) = digest", as it writes it with --tag, with the
/// digest the tag names. A backslash at the start says the name is escaped.
static parse_t parse_line(const char *line, size_t length, algorithm_t *algorithm, unsigned char *digest, sof_string_t *name) {
    int escaped = length > 0 && line[0] == '\\';
    if (escaped) {
        line++;
        length--;
    }
    size_t middle = sizeof(TAG_MIDDLE) - 1;
    for (int i = 0; i < ALGORITHMS; i++) {
        size_t tag = strlen(algorithms[i].tag), digits = 2 * algorithms[i].size;
        if (length > tag + 2 + middle + digits && memcmp(line, algorithms[i].tag, tag) == 0 && memcmp(line + tag, " (", 2) == 0) {
            // The name runs to the last ") = ", which is right before the
            // digest.
            const char *hex = line + length - digits;
            if (memcmp(hex - middle, TAG_MIDDLE, middle) != 0 || sof_digest_from_hex(hex, digits, digest) != 0) {
                return PARSE_MALFORMED;
            }
            *algorithm = (algorithm_t)i;
            return unescape(line + tag + 2, length - tag - 2 - middle - digits, escaped, name);
        }
    }
    size_t digits = 2 * algorithms[chosen].size;
    if (length < digits + 3 || line[digits] != ' ' || (line[digits + 1] != ' ' && line[digits + 1] != '*') || sof_digest_from_hex(line, digits, digest) != 0) {
        return PARSE_MALFORMED;
    }
    *algorithm = chosen;
    return unescape(line + digits + 2, length - digits - 2, escaped, name);
}

/// @brief Prints a name, with each backslash, newline and carriage return
/// written as \\, \n and \r if escaped is set.
static void print_name(const char *name, int escaped) {
    if (!escaped) {
        fputs(name, stdout);
        return;
    }
    for (const char *p = name; *p != '\0'; p++) {
        if (*p == '\\') {
            fputs("\\\\", stdout);
        } else if (*p == '\n') {
            fputs("\\n", stdout);
        } else if (*p == '\r') {
            fputs("\\r", stdout);
        } else {
            putchar(*p);
        }
    }
}

/// @brief Prints the digest of one file, or standard input for "-", and its
/// name, in the format the options ask for.
/// @return 0, or 1 if the file couldn't be read, after printing why.
static int print_sum(const char *path) {
    unsigned char digest[SOF_DIGEST_MAX_SIZE];
    if (digest_file(chosen, path, digest) != 0) {
        return 1;
    }
    char hex[2 * SOF_DIGEST_MAX_SIZE + 1];
    sof_digest_to_hex(digest, algorithms[chosen].size, hex);
    int escaped = needs_escape(path);
    if (escaped) {
        putchar('\\');
    }
    if (opt_tag.present) {
        printf("%s (", algorithms[chosen].tag);
        print_name(path, escaped);
        printf("%s%s\n", TAG_MIDDLE, hex);
    } else {
        printf("%s  ", hex);
        print_name(path, escaped);
        putchar('\n');
    }
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

/// @brief Gives the digest of the data given to a stream for algorithm,
/// writing a CRC's 32 bits high byte first.
static void stream_final(algorithm_t algorithm, const stream_t *s, unsigned char *digest) {
    uint32_t crc;
    switch (algorithm) {
    case ALGORITHM_MD5:
        sof_digest_md5_final(&s->md5, digest);
        return;
    case ALGORITHM_SHA1:
        sof_digest_sha1_final(&s->sha1, digest);
        return;
    case ALGORITHM_SHA224:
        sof_digest_sha224_final(&s->sha256, digest);
        return;
    case ALGORITHM_SHA256:
        sof_digest_sha256_final(&s->sha256, digest);
        return;
    case ALGORITHM_SHA384:
        sof_digest_sha384_final(&s->sha512, digest);
        return;
    case ALGORITHM_SHA512:
        sof_digest_sha512_final(&s->sha512, digest);
        return;
    case ALGORITHM_SHA512_256:
        sof_digest_sha512_256_final(&s->sha512, digest);
        return;
    case ALGORITHM_CRC32:
        crc = sof_digest_crc32_final(&s->crc32);
        break;
    default:
        crc = sof_digest_crc32c_final(&s->crc32c);
        break;
    }
    for (int i = 0; i < 4; i++) {
        digest[i] = (unsigned char)(crc >> (24 - 8 * i));
    }
}

/// @brief Sets up a stream for algorithm.
static void stream_init(algorithm_t algorithm, stream_t *s) {
    switch (algorithm) {
    case ALGORITHM_MD5:
        sof_digest_md5_init(&s->md5);
        break;
    case ALGORITHM_SHA1:
        sof_digest_sha1_init(&s->sha1);
        break;
    case ALGORITHM_SHA224:
        sof_digest_sha224_init(&s->sha256);
        break;
    case ALGORITHM_SHA256:
        sof_digest_sha256_init(&s->sha256);
        break;
    case ALGORITHM_SHA384:
        sof_digest_sha384_init(&s->sha512);
        break;
    case ALGORITHM_SHA512:
        sof_digest_sha512_init(&s->sha512);
        break;
    case ALGORITHM_SHA512_256:
        sof_digest_sha512_256_init(&s->sha512);
        break;
    case ALGORITHM_CRC32:
        sof_digest_crc32_init(&s->crc32);
        break;
    default:
        sof_digest_crc32c_init(&s->crc32c);
        break;
    }
}

/// @brief Gives a stream for algorithm the next piece of its data.
static void stream_update(algorithm_t algorithm, stream_t *s, const void *data, size_t length) {
    switch (algorithm) {
    case ALGORITHM_MD5:
        sof_digest_md5_update(&s->md5, data, length);
        break;
    case ALGORITHM_SHA1:
        sof_digest_sha1_update(&s->sha1, data, length);
        break;
    case ALGORITHM_SHA224:
        sof_digest_sha224_update(&s->sha256, data, length);
        break;
    case ALGORITHM_SHA256:
        sof_digest_sha256_update(&s->sha256, data, length);
        break;
    case ALGORITHM_SHA384:
        sof_digest_sha384_update(&s->sha512, data, length);
        break;
    case ALGORITHM_SHA512:
        sof_digest_sha512_update(&s->sha512, data, length);
        break;
    case ALGORITHM_SHA512_256:
        sof_digest_sha512_256_update(&s->sha512, data, length);
        break;
    case ALGORITHM_CRC32:
        sof_digest_crc32_update(&s->crc32, data, length);
        break;
    default:
        sof_digest_crc32c_update(&s->crc32c, data, length);
        break;
    }
}

/// @brief Puts the length bytes at s into name, turning \\, \n and \r back
/// into the characters if escaped is set.
/// @return PARSE_OK; PARSE_MALFORMED if the name is empty, has a NUL, or
/// has a backslash before anything else; or PARSE_NO_MEM.
static parse_t unescape(const char *s, size_t length, int escaped, sof_string_t *name) {
    if (length == 0 || memchr(s, '\0', length) != NULL) {
        return PARSE_MALFORMED;
    }
    sof_string_clear(name);
    for (size_t i = 0; i < length; i++) {
        char c = s[i];
        if (escaped && c == '\\') {
            if (++i == length) {
                return PARSE_MALFORMED;
            }
            c = s[i] == '\\' ? '\\' : s[i] == 'n' ? '\n' : s[i] == 'r' ? '\r' : '\0';
            if (c == '\0') {
                return PARSE_MALFORMED;
            }
        }
        if (sof_string_append_bytes(name, &c, 1) != SOF_STRING_OK) {
            return PARSE_NO_MEM;
        }
    }
    return PARSE_OK;
}

/// @brief Prints the usage message, with the options sof_copt knows.
static void usage(FILE *stream) {
    fprintf(stream, "usage: digestsum [-h] [-a algorithm] [-c] [--quiet] [--status] [--tag] [file]...\n");
    sof_copt_print(stream);
    fprintf(stream,
        "Prints the SHA-256 digest of each file, or -a's, or of standard input if\n"
        "there are none or a file is \"-\", in hex, then its name. With -c, reads\n"
        "lists of digests, as sha256sum prints them, with or without --tag, and\n"
        "checks each file named, printing OK or FAILED; a line with a tag is checked\n"
        "with the digest it names. Exits with 0, 1 if a file can't be read or a\n"
        "check fails, 2 on bad options.\n");
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
        return 2;
    case SOF_COPT_UNEXPECTED_ARGUMENT:
        fprintf(stderr, "%s takes no argument\n", option->option);
        usage(stderr);
        return 2;
    case SOF_COPT_MISSING_ARGUMENT:
        fprintf(stderr, "%s needs an argument\n", option->option);
        usage(stderr);
        return 2;
    case SOF_COPT_STOPPED:
        usage(stderr);
        return 2;
    default:
        fprintf(stderr, "can't read the options (error %d)\n", (int)err);
        return 2;
    }
    if (opt_help.present) {
        usage(stdout);
        return 0;
    }
    if (opt_check.present && opt_tag.present) {
        fprintf(stderr, "--tag is for printing digests; -c reads either format\n");
        usage(stderr);
        return 2;
    }
    if (!opt_check.present && (opt_quiet.present || opt_status.present)) {
        fprintf(stderr, "--quiet and --status are only for -c\n");
        usage(stderr);
        return 2;
    }

    static char *standard_input[] = {"-"};
    char **paths = av + index;
    int num_paths = ac - index;
    if (num_paths == 0) {
        paths = standard_input;
        num_paths = 1;
    }
    // A check reads each list into list, and each name in it into name,
    // which keep their memory from one to the next.
    sof_string_t list, name;
    if (sof_string_init(&list, 0, NULL) != SOF_STRING_OK || sof_string_init(&name, 0, NULL) != SOF_STRING_OK) {
        fprintf(stderr, "out of memory\n");
        sof_string_free(&list);
        return 1;
    }
    int rc = 0;
    tally_t tally = {0, 0, 0};
    for (int i = 0; i < num_paths; i++) {
        int got = opt_check.present ? check_list(paths[i], &list, &name, &tally) : print_sum(paths[i]);
        if (got != 0) {
            rc = 1;
        }
    }
    if (!opt_status.present) {
        if (tally.malformed > 0) {
            fprintf(stderr, "digestsum: %zu line%s not a digest and a name\n", tally.malformed, tally.malformed == 1 ? " is" : "s are");
        }
        if (tally.unread > 0) {
            fprintf(stderr, "digestsum: %zu file%s couldn't be read\n", tally.unread, tally.unread == 1 ? "" : "s");
        }
        if (tally.failed > 0) {
            fprintf(stderr, "digestsum: %zu digest%s didn't match\n", tally.failed, tally.failed == 1 ? "" : "s");
        }
    }
    if (tally.malformed + tally.unread + tally.failed > 0) {
        rc = 1;
    }
    sof_string_free(&name);
    sof_string_free(&list);
    return rc;
}
