/**
 * @file main.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief Prints the primes up to a number, counts them, or says whether
 * given numbers are prime, with a sieve of Eratosthenes over a bit array.
 * @details
 *
 *     sieve [-h] [-c] [-m] [-f from] limit
 *     sieve [-h] [-m] -t number...
 *
 * Prints the primes up to limit, one to a line, or with -f only those from
 * from on, or with -c only how many there are. With -t, says for each number
 * whether it is prime, and if not, its smallest prime factor, sieving only
 * up to the square root of the largest. -m reports the memory the sieve took
 * against one byte per number. Its options are read with sof_copt. It exits
 * with 0, or with -t 0 if every number is prime and 1 if any isn't, and 2 if
 * the options are wrong or memory runs out.
 * @version 0.0.1
 * @date 2026-11-19
 * @copyright Copyright (c) 2026
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "sof_bit.h"
#include "sof_copt.h"
#include "sieve.h"

/// @brief The exit codes.
#define EXIT_DONE 0
#define EXIT_NOT_PRIME 1
#define EXIT_TROUBLE 2

/// @brief An application code the validate callback returns when an
/// option's argument is wrong, once it has printed why; sof_copt keeps it
/// in the option's stop_value.
#define BAD_ARGUMENT 1

/// @brief What the options ask for, filled in by their callbacks.
typedef struct settings_t {
    /// @brief The smallest prime to print, from -f.
    uint64_t from;
} settings_t;

static int from_validate(sof_copt_t *option, void *ctx);
static uint64_t isqrt(uint64_t n);
static int list_primes(const settings_t *settings, const char *operand);
static int parse_u64(const char *s, uint64_t *n);
static void report_memory(const sieve_t *s);
static uint64_t smallest_factor(const sieve_t *s, uint64_t n);
static int test_numbers(char **operands, int count);
static void usage(FILE *stream);

static sof_copt_t opt_help = {
    .option = "-h",
    .description = "print this help and exit",
};

static sof_copt_t opt_count = {
    .option = "-c",
    .description = "print only the number of primes",
};

static sof_copt_t opt_from = {
    .option = "-f",
    .description = "print only the primes from this number on",
    .type = SOF_COPT_ARG_REQUIRED,
    .validate = from_validate,
};

static sof_copt_t opt_memory = {
    .option = "-m",
    .description = "report the sieve's memory, against one byte per number, to standard error",
};

static sof_copt_t opt_test = {
    .option = "-t",
    .description = "say whether each number given is prime",
};

SOF_COPT_OPTIONS static sof_copt_t *options[] = {
    &opt_help,
    &opt_count,
    &opt_from,
    &opt_memory,
    &opt_test,
};

/// @brief Reads the argument of -f.
static int from_validate(sof_copt_t *option, void *ctx) {
    settings_t *settings = ctx;
    if (!parse_u64(option->value, &settings->from)) {
        fprintf(stderr, "%s needs a non-negative number, not \"%s\"\n", option->option, option->value);
        return BAD_ARGUMENT;
    }
    return 0;
}

/// @brief Returns the square root of n, rounded down, a bit pair at a time.
static uint64_t isqrt(uint64_t n) {
    uint64_t root = 0;
    for (uint64_t bit = (uint64_t)1 << 62; bit != 0; bit >>= 2) {
        if (n >= root + bit) {
            n -= root + bit;
            root = (root >> 1) + bit;
        } else {
            root >>= 1;
        }
    }
    return root;
}

/// @brief Sieves up to the limit in operand and prints the primes, or how
/// many there are.
/// @return EXIT_DONE, or EXIT_TROUBLE after printing why.
static int list_primes(const settings_t *settings, const char *operand) {
    uint64_t limit;
    if (!parse_u64(operand, &limit)) {
        fprintf(stderr, "the limit must be a non-negative number, not \"%s\"\n", operand);
        return EXIT_TROUBLE;
    }
    sieve_t s;
    if (sieve_init(&s, limit, NULL) != SIEVE_OK) {
        fprintf(stderr, "out of memory for a sieve up to %llu\n", (unsigned long long)limit);
        return EXIT_TROUBLE;
    }
    if (opt_count.present) {
        printf("%llu\n", (unsigned long long)sieve_count(&s, settings->from, limit));
    } else {
        for (uint64_t p = sieve_next(&s, settings->from); p != 0; p = sieve_next(&s, p + 1)) {
            printf("%llu\n", (unsigned long long)p);
        }
    }
    if (opt_memory.present) {
        report_memory(&s);
    }
    sieve_free(&s);
    return EXIT_DONE;
}

/// @brief Reads a whole non-negative decimal number.
/// @return 1, or 0 if s isn't one or it is too big.
static int parse_u64(const char *s, uint64_t *n) {
    if (*s < '0' || *s > '9') {
        return 0;
    }
    char *end;
    errno = 0;
    unsigned long long value = strtoull(s, &end, 10);
    if (errno != 0 || *end != '\0') {
        return 0;
    }
    *n = (uint64_t)value;
    return 1;
}

/// @brief Prints, to standard error, the memory the sieve's bits take, and
/// what an array of one byte per number, from 0 to the limit, would take.
static void report_memory(const sieve_t *s) {
    unsigned long long bytes = (unsigned long long)s->word_count * sizeof(sof_bit_t);
    unsigned long long naive = (unsigned long long)s->limit + 1;
    // The report comes after the primes, even when both go to one place.
    fflush(stdout);
    fprintf(stderr, "%llu primes up to %llu, sieved in %llu bytes (%zu words of %u bits, a bit for each odd number);\n", (unsigned long long)s->count, (unsigned long long)s->limit, bytes, s->word_count, SOF_BIT_BITS);
    fprintf(stderr, "one byte per number would take %llu bytes, %.1f times as much\n", naive, (double)naive / (double)bytes);
}

/// @brief Finds the smallest prime factor of n, from the primes in the
/// sieve, which must go up to the square root of n at least.
/// @return The factor, or 0 if n is prime, 1 for 0 and 1, which have none
/// and aren't prime.
static uint64_t smallest_factor(const sieve_t *s, uint64_t n) {
    if (n < 2) {
        return 1;
    }
    uint64_t root = isqrt(n);
    for (uint64_t p = sieve_next(s, 2); p != 0 && p <= root; p = sieve_next(s, p + 1)) {
        if (n % p == 0) {
            return p;
        }
    }
    return 0;
}

/// @brief Says, for each number in operands, whether it is prime, sieving up
/// to the square root of the largest.
/// @return EXIT_DONE if every number is prime, EXIT_NOT_PRIME if any isn't,
/// or EXIT_TROUBLE after printing why.
static int test_numbers(char **operands, int count) {
    uint64_t largest = 0, n;
    for (int i = 0; i < count; i++) {
        if (!parse_u64(operands[i], &n)) {
            fprintf(stderr, "\"%s\" isn't a non-negative number\n", operands[i]);
            return EXIT_TROUBLE;
        }
        if (n > largest) {
            largest = n;
        }
    }
    sieve_t s;
    uint64_t limit = isqrt(largest);
    if (sieve_init(&s, limit, NULL) != SIEVE_OK) {
        fprintf(stderr, "out of memory for a sieve up to %llu\n", (unsigned long long)limit);
        return EXIT_TROUBLE;
    }
    int rc = EXIT_DONE;
    for (int i = 0; i < count; i++) {
        parse_u64(operands[i], &n);
        // Numbers the sieve reaches are looked up; the rest are divided by
        // the primes up to their square root.
        int prime = sieve_is_prime(&s, n);
        uint64_t factor = prime == 1 ? 0 : smallest_factor(&s, n);
        if (factor == 0) {
            printf("%llu is prime\n", (unsigned long long)n);
        } else if (factor == 1) {
            printf("%llu is not prime\n", (unsigned long long)n);
            rc = EXIT_NOT_PRIME;
        } else {
            printf("%llu is not prime: %llu x %llu\n", (unsigned long long)n, (unsigned long long)factor, (unsigned long long)(n / factor));
            rc = EXIT_NOT_PRIME;
        }
    }
    if (opt_memory.present) {
        report_memory(&s);
    }
    sieve_free(&s);
    return rc;
}

/// @brief Prints the usage message, with the options sof_copt knows.
static void usage(FILE *stream) {
    fprintf(stream, "usage: sieve [-h] [-c] [-m] [-f from] limit\n"
                    "       sieve [-h] [-m] -t number...\n");
    sof_copt_print(stream);
    fprintf(stream,
            "Prints the primes up to limit, one to a line, found by a sieve of\n"
            "Eratosthenes that keeps a bit for each odd number. With -t, says whether\n"
            "each number is prime, and if not, gives its smallest factor. Exits with 0,\n"
            "or with -t 0 if every number is prime and 1 if any isn't, and 2 on errors.\n");
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
        // The callback returned BAD_ARGUMENT, the only code, and said why.
        return EXIT_TROUBLE;
    default:
        fprintf(stderr, "can't read the options (error %d)\n", (int)err);
        return EXIT_TROUBLE;
    }
    if (opt_help.present) {
        usage(stdout);
        return EXIT_DONE;
    }
    if (opt_test.present) {
        if (opt_count.present || opt_from.present) {
            fprintf(stderr, "-c and -f can't be used with -t\n");
            usage(stderr);
            return EXIT_TROUBLE;
        }
        if (index == ac) {
            fprintf(stderr, "-t needs at least one number\n");
            usage(stderr);
            return EXIT_TROUBLE;
        }
        return test_numbers(av + index, ac - index);
    }
    if (ac - index != 1) {
        fprintf(stderr, "give one limit\n");
        usage(stderr);
        return EXIT_TROUBLE;
    }
    return list_primes(&settings, av[index]);
}
