/**
 * @file main.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief Prints statistics of the numbers in files: count, sum, mean,
 * standard deviation, minimum, maximum, median and percentiles.
 * @details Reads numbers separated by whitespace, such as one to a line,
 * into a sof_vector of doubles, which grows as they are read, and sorts it
 * with sof_vector_sort() to find the minimum, maximum, median and
 * percentiles. With -s it prints the sorted values instead, and with -u each
 * distinct value and how often it occurs. -f looks values up: with
 * sof_vector_find() in the order they were read, and with
 * sof_vector_search(), by binary search, in the sorted vector. The
 * percentiles asked for are kept in another vector, sorted as they are
 * added, by sof_vector_search() and sof_vector_insert().
 *
 *     numstat [-h] [-s | -u] [-p list] [-f value]... [file]...
 *
 * With no file, or a file named "-", it reads standard input. Its options
 * are read with sof_copt. It exits with 0, 1 if a file couldn't be read,
 * held something that isn't a number, or memory ran out, and 2 for a usage
 * error.
 * @version 0.0.1
 * @date 2026-11-19
 * @copyright Copyright (c) 2026
 */

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sof_copt.h"
#include "sof_vector.h"

/// @brief The exit codes.
#define EXIT_DONE 0
#define EXIT_TROUBLE 1
#define EXIT_USAGE 2

/// @brief The code a validate callback returns for a bad argument, after
/// printing why. sof_copt keeps it in the option's stop_value.
#define BAD_ARGUMENT 1

/// @brief The code a validate callback returns if memory ran out, after
/// printing so. sof_copt keeps it in the option's stop_value.
#define NO_MEMORY 2

/// @brief The longest number read, in characters.
#define MAX_TOKEN 127

/// @brief The percentiles printed when -p isn't given.
static const double default_percentiles[] = {25, 75, 90, 99};

/// @brief The vectors the options fill, passed to sof_copt_parse() as its
/// ctx.
typedef struct settings_t {
    /// @brief The percentiles from -p, from 0 to 100, sorted and each once.
    sof_vector_t percentiles;
    /// @brief The values from -f, in the order given.
    sof_vector_t wanted;
} settings_t;

static int add_percentile(sof_vector_t *percentiles, double p);
static int compare_doubles(const void *a, const void *b, void *ctx);
static void find_value(const sof_vector_t *input, const sof_vector_t *sorted, double value);
static double percentile(const sof_vector_t *sorted, double p);
static int percentiles_validate(sof_copt_t *option, void *ctx);
static int print_value(void *element, void *ctx);
static void print_summary(const sof_vector_t *sorted, const sof_vector_t *percentiles);
static void print_unique(const sof_vector_t *sorted);
static int read_number(const char *s, double *value);
static int read_numbers(sof_vector_t *values, FILE *f, const char *name);
static void usage(FILE *stream);
static int wanted_validate(sof_copt_t *option, void *ctx);

static sof_copt_t opt_help = {
    .option = "-h",
    .description = "print this help and exit",
};

static sof_copt_t opt_sorted = {
    .option = "-s",
    .description = "print the values sorted, one to a line, instead of the summary",
};

static sof_copt_t opt_unique = {
    .option = "-u",
    .description = "print each distinct value, sorted, and how often it occurs, instead",
};

static sof_copt_t opt_percentiles = {
    .option = "-p",
    .description = "the percentiles to print, such as 50,90,99.9 (default 25,75,90,99)",
    .type = SOF_COPT_ARG_REQUIRED,
    .validate = percentiles_validate,
};

static sof_copt_t opt_find = {
    .option = "-f",
    .description = "say whether and where this value occurs; can be repeated",
    .type = SOF_COPT_ARG_REQUIRED,
    .validate = wanted_validate,
};

SOF_COPT_OPTIONS static sof_copt_t *options[] = {
    &opt_help,
    &opt_sorted,
    &opt_unique,
    &opt_percentiles,
    &opt_find,
};

/// @brief Adds p to the sorted vector of percentiles, where a binary search
/// says it goes, unless it is there already.
/// @return 0, or -1 if memory ran out.
static int add_percentile(sof_vector_t *percentiles, double p) {
    size_t index;
    if (sof_vector_search(percentiles, &p, compare_doubles, NULL, &index) != NULL) {
        return 0;
    }
    return sof_vector_insert(percentiles, index, &p) == SOF_VECTOR_OK ? 0 : -1;
}

/// @brief Compares two doubles, for sof_vector_sort(), sof_vector_find()
/// and sof_vector_search(). None of them is a NaN.
static int compare_doubles(const void *a, const void *b, void *ctx) {
    (void)ctx;
    double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y ? 1 : 0;
}

/// @brief Prints whether value occurs: where it first does in the input, and
/// how often and at which ranks in the sorted values, or, if it doesn't
/// occur, the values it falls between.
static void find_value(const sof_vector_t *input, const sof_vector_t *sorted, double value) {
    size_t first, low, high;
    if (sof_vector_find(input, &value, compare_doubles, NULL, &first) == NULL) {
        sof_vector_search(sorted, &value, compare_doubles, NULL, &low);
        printf("%.12g: not found", value);
        if (low > 0) {
            printf(", above %.12g", *(double *)sof_vector_at(sorted, low - 1));
        }
        if (low < sorted->count) {
            printf("%s below %.12g", low > 0 ? " and" : ",", *(double *)sof_vector_at(sorted, low));
        }
        printf("\n");
        return;
    }
    // The search gives the first element not less than value, which is the
    // first equal to it; the first greater than it follows the last.
    sof_vector_search(sorted, &value, compare_doubles, NULL, &low);
    double next = nextafter(value, INFINITY);
    sof_vector_search(sorted, &next, compare_doubles, NULL, &high);
    printf("%.12g: found %zu time%s, first as number %zu of the input, ranks %zu to %zu of %zu\n", value, high - low, high - low == 1 ? "" : "s", first + 1, low + 1, high, sorted->count);
}

/// @brief Returns percentile p, from 0 to 100, of the sorted values, which
/// aren't empty: the value at rank p / 100 * (count - 1), counting from 0,
/// interpolating between the two values around it.
static double percentile(const sof_vector_t *sorted, double p) {
    double rank = p / 100 * (double)(sorted->count - 1);
    size_t below = (size_t)rank;
    double low = *(double *)sof_vector_at(sorted, below);
    if (below + 1 >= sorted->count) {
        return low;
    }
    double high = *(double *)sof_vector_at(sorted, below + 1);
    return low + (high - low) * (rank - (double)below);
}

/// @brief Reads the comma-separated list of percentiles given with -p into
/// the settings in ctx, printing why if one isn't a number from 0 to 100.
static int percentiles_validate(sof_copt_t *option, void *ctx) {
    settings_t *settings = ctx;
    const char *s = option->value;
    for (;;) {
        const char *comma = strchr(s, ',');
        size_t length = comma == NULL ? strlen(s) : (size_t)(comma - s);
        char token[MAX_TOKEN + 1];
        double p;
        if (length > MAX_TOKEN) {
            length = MAX_TOKEN;
        }
        memcpy(token, s, length);
        token[length] = '\0';
        if (read_number(token, &p) != 0 || p < 0 || p > 100) {
            fprintf(stderr, "numstat: -p needs percentiles from 0 to 100, not \"%s\"\n", token);
            return BAD_ARGUMENT;
        }
        if (add_percentile(&settings->percentiles, p) != 0) {
            fprintf(stderr, "numstat: out of memory\n");
            return NO_MEMORY;
        }
        if (comma == NULL) {
            return 0;
        }
        s = comma + 1;
    }
}

/// @brief The sof_vector_traverse() callback for -s: prints a value.
static int print_value(void *element, void *ctx) {
    (void)ctx;
    printf("%.12g\n", *(double *)element);
    return 0;
}

/// @brief Prints the count, sum, mean, standard deviation, minimum,
/// maximum, median and percentiles of the sorted values.
static void print_summary(const sof_vector_t *sorted, const sof_vector_t *percentiles) {
    printf("count   %zu\n", sorted->count);
    if (sorted->count == 0) {
        return;
    }
    double sum = 0;
    for (const double *x = sof_vector_at(sorted, 0); x != NULL; x = sof_vector_next(sorted, x)) {
        sum += *x;
    }
    double mean = sum / (double)sorted->count, squares = 0;
    for (const double *x = sof_vector_at(sorted, 0); x != NULL; x = sof_vector_next(sorted, x)) {
        squares += (*x - mean) * (*x - mean);
    }
    printf("sum     %.12g\n", sum);
    printf("mean    %.12g\n", mean);
    if (sorted->count > 1) {
        printf("stddev  %.12g\n", sqrt(squares / (double)(sorted->count - 1)));
    }
    printf("min     %.12g\n", *(double *)sof_vector_at(sorted, 0));
    printf("median  %.12g\n", percentile(sorted, 50));
    printf("max     %.12g\n", *(double *)sof_vector_at(sorted, sorted->count - 1));
    for (const double *p = sof_vector_at(percentiles, 0); p != NULL; p = sof_vector_next(percentiles, p)) {
        char label[32];
        snprintf(label, sizeof(label), "p%g", *p);
        printf("%-7s %.12g\n", label, percentile(sorted, *p));
    }
}

/// @brief Prints each distinct value of the sorted values, and how many
/// times it occurs.
static void print_unique(const sof_vector_t *sorted) {
    size_t i = 0;
    while (i < sorted->count) {
        double value = *(double *)sof_vector_at(sorted, i);
        size_t j = i + 1;
        while (j < sorted->count && *(double *)sof_vector_at(sorted, j) == value) {
            j++;
        }
        printf("%.12g\t%zu\n", value, j - i);
        i = j;
    }
}

/// @brief Reads a finite number from all of s into *value.
/// @return 0, or -1 if s isn't one.
static int read_number(const char *s, double *value) {
    char *end;
    errno = 0;
    double x = strtod(s, &end);
    if (*s == '\0' || *end != '\0' || errno == ERANGE || !isfinite(x)) {
        return -1;
    }
    *value = x;
    return 0;
}

/// @brief Reads the numbers in f, separated by whitespace, onto the end of
/// values. A word that isn't a number is reported, with its line, and
/// skipped.
/// @return 0, 1 if f held something that isn't a number or couldn't be read,
/// or -1 if memory ran out, after printing why.
static int read_numbers(sof_vector_t *values, FILE *f, const char *name) {
    char token[MAX_TOKEN + 2];
    size_t length = 0, line = 1;
    int rc = 0, ch;
    do {
        ch = getc(f);
        if (ch != EOF && ch != ' ' && ch != '\t' && ch != '\n' && ch != '\r' && ch != '\f' && ch != '\v') {
            // A token too long to be a number keeps its first MAX_TOKEN + 1
            // characters, which read_number() rejects.
            if (length <= MAX_TOKEN) {
                token[length++] = (char)ch;
            }
            continue;
        }
        if (length > 0) {
            double x;
            token[length] = '\0';
            if (read_number(token, &x) != 0) {
                fprintf(stderr, "numstat: %s:%zu: not a number: \"%s\"\n", name, line, token);
                rc = 1;
            } else if (sof_vector_push(values, &x) != SOF_VECTOR_OK) {
                fprintf(stderr, "numstat: out of memory\n");
                return -1;
            }
            length = 0;
        }
        line += ch == '\n';
    } while (ch != EOF);
    if (ferror(f)) {
        fprintf(stderr, "numstat: %s: read error\n", name);
        rc = 1;
    }
    return rc;
}

/// @brief Prints the usage message, with the options sof_copt knows.
static void usage(FILE *stream) {
    fprintf(stream, "usage: numstat [-h] [-s | -u] [-p list] [-f value]... [file]...\n");
    sof_copt_print(stream);
    fprintf(stream,
        "Reads numbers separated by whitespace from the files, or standard input if\n"
        "there are none or a file is \"-\", and prints their count, sum, mean, standard\n"
        "deviation, minimum, median, maximum and percentiles. Exits with 0, 1 if a file\n"
        "can't be read or holds something that isn't a number, 2 for bad options.\n");
}

/// @brief Adds the value given with -f to the settings in ctx, printing why
/// if it isn't a number.
static int wanted_validate(sof_copt_t *option, void *ctx) {
    settings_t *settings = ctx;
    double value;
    if (read_number(option->value, &value) != 0) {
        fprintf(stderr, "numstat: -f needs a number, not \"%s\"\n", option->value);
        return BAD_ARGUMENT;
    }
    if (sof_vector_push(&settings->wanted, &value) != SOF_VECTOR_OK) {
        fprintf(stderr, "numstat: out of memory\n");
        return NO_MEMORY;
    }
    return 0;
}

int main(int ac, char **av) {
    settings_t settings;
    sof_vector_t input, sorted;
    sof_vector_init(&settings.percentiles, sizeof(double), 0, NULL, NULL, NULL);
    sof_vector_init(&settings.wanted, sizeof(double), 0, NULL, NULL, NULL);
    sof_vector_init(&input, sizeof(double), 0, NULL, NULL, NULL);
    sof_vector_init(&sorted, sizeof(double), 0, NULL, NULL, NULL);
    int index, rc = EXIT_USAGE;
    sof_copt_t *option;
    sof_copt_error_t err = sof_copt_parse(ac, av, &settings, &index, &option);
    switch (err) {
    case SOF_COPT_OK:
        break;
    case SOF_COPT_NO_MATCH:
        fprintf(stderr, "numstat: unknown option %s\n", av[index]);
        usage(stderr);
        goto done;
    case SOF_COPT_MISSING_ARGUMENT:
        fprintf(stderr, "numstat: %s needs an argument\n", option->option);
        usage(stderr);
        goto done;
    case SOF_COPT_UNEXPECTED_ARGUMENT:
        fprintf(stderr, "numstat: %s takes no argument\n", option->option);
        usage(stderr);
        goto done;
    case SOF_COPT_STOPPED:
        if (option->stop_value == NO_MEMORY) {
            rc = EXIT_TROUBLE;
        }
        goto done;
    default:
        fprintf(stderr, "numstat: can't read the options (error %d)\n", (int)err);
        goto done;
    }
    if (opt_help.present) {
        usage(stdout);
        rc = EXIT_DONE;
        goto done;
    }
    if (opt_sorted.present && opt_unique.present) {
        fprintf(stderr, "numstat: give -s or -u, not both\n");
        usage(stderr);
        goto done;
    }
    rc = EXIT_DONE;
    for (size_t i = 0; !opt_percentiles.present && i < sizeof(default_percentiles) / sizeof(double); i++) {
        if (add_percentile(&settings.percentiles, default_percentiles[i]) != 0) {
            fprintf(stderr, "numstat: out of memory\n");
            rc = EXIT_TROUBLE;
            goto done;
        }
    }

    static char *standard_input[] = {"-"};
    char **paths = av + index;
    int num_paths = ac - index;
    if (num_paths == 0) {
        paths = standard_input;
        num_paths = 1;
    }
    for (int i = 0; i < num_paths; i++) {
        int is_stdin = strcmp(paths[i], "-") == 0;
        const char *name = is_stdin ? "standard input" : paths[i];
        FILE *f = is_stdin ? stdin : fopen(paths[i], "rb");
        if (f == NULL) {
            fprintf(stderr, "numstat: %s: %s\n", name, strerror(errno));
            rc = EXIT_TROUBLE;
            continue;
        }
        int read = read_numbers(&input, f, name);
        if (is_stdin) {
            clearerr(stdin);
        } else {
            fclose(f);
        }
        if (read < 0) {
            rc = EXIT_TROUBLE;
            goto done;
        } else if (read > 0) {
            rc = EXIT_TROUBLE;
        }
    }

    // The input is kept in the order it was read, for -f, and copied to be
    // sorted.
    if (sof_vector_resize(&sorted, input.count) != SOF_VECTOR_OK) {
        fprintf(stderr, "numstat: out of memory\n");
        rc = EXIT_TROUBLE;
        goto done;
    }
    if (input.count > 0) {
        memcpy(sorted.elements, input.elements, input.count * sizeof(double));
    }
    sof_vector_sort(&sorted, compare_doubles, NULL);
    if (opt_sorted.present) {
        sof_vector_traverse(&sorted, print_value, NULL);
    } else if (opt_unique.present) {
        print_unique(&sorted);
    } else {
        print_summary(&sorted, &settings.percentiles);
    }
    for (const double *x = sof_vector_at(&settings.wanted, 0); x != NULL; x = sof_vector_next(&settings.wanted, x)) {
        find_value(&input, &sorted, *x);
    }
done:
    sof_vector_free(&sorted);
    sof_vector_free(&input);
    sof_vector_free(&settings.wanted);
    sof_vector_free(&settings.percentiles);
    return rc;
}
