/**
 * @file main.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief Solves the Towers of Hanoi, printing each move, and in a debug
 * build traces its recursion.
 * @details
 *
 *     hanoi [-h] [-c] [-p] [disks]
 *
 * Moves a tower of disks, 3 unless another number is given, from peg A to
 * peg C, one disk at a time and never a disk onto a smaller one, and prints
 * each move, or with -c only how many there are. With -p it prints the pegs
 * after each move. The pegs are sof_stack stacks of disks. Built with DEBUG
 * defined (make debug=1), it traces each call of the recursion, indented by
 * its depth, to stderr through sof_debug, between the moves it makes. Its options
 * are read with sof_copt. It exits with 0, or 2 if the options are wrong or
 * memory runs out.
 * @version 0.0.1
 * @date 2026-11-19
 * @copyright Copyright (c) 2026
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "sof_copt.h"
#include "sof_debug.h"
#include "sof_stack.h"

/// @brief The exit codes.
#define EXIT_DONE 0
#define EXIT_TROUBLE 2

/// @brief The most disks: a tower of 64 takes 2^64 - 1 moves, as many as a
/// uint64_t counts.
#define DISKS_MAX 64

/// @brief The number of pegs.
#define PEGS 3

/// @brief The three pegs and the moves made.
typedef struct towers_t {
    /// @brief Each peg's disks, bottom first, numbered by size from 1, the
    /// smallest.
    sof_stack_t pegs[PEGS];
    /// @brief The moves made so far.
    uint64_t moves;
} towers_t;

static int move_disk(towers_t *t, int from, int to);
static int move_tower(towers_t *t, unsigned disks, int from, int to, int via);
static int parse_disks(const char *s, unsigned *disks);
static void print_pegs(const towers_t *t);
static void usage(FILE *stream);

static sof_copt_t opt_help = {
    .option = "-h",
    .description = "print this help and exit",
};

static sof_copt_t opt_count = {
    .option = "-c",
    .description = "print only the number of moves, without making them",
};

static sof_copt_t opt_pegs = {
    .option = "-p",
    .description = "print the pegs at the start and after each move",
};

SOF_COPT_OPTIONS static sof_copt_t *options[] = {
    &opt_help,
    &opt_count,
    &opt_pegs,
};

/// @brief Moves the top disk of peg from to peg to, and prints the move.
/// @return 0, or EXIT_TROUBLE if the move isn't allowed, which the recursion
/// never asks for, after printing why.
static int move_disk(towers_t *t, int from, int to) {
    unsigned disk;
    const unsigned *below = sof_stack_peek(&t->pegs[to]);
    const unsigned *top = sof_stack_peek(&t->pegs[from]);
    if (top == NULL || (below != NULL && *below < *top)) {
        fprintf(stderr, "can't move from %c to %c\n", 'A' + from, 'A' + to);
        return EXIT_TROUBLE;
    }
    sof_stack_pop(&t->pegs[from], &disk);
    // The pegs have room for every disk, so this doesn't allocate.
    sof_stack_push(&t->pegs[to], &disk);
    t->moves++;
    printf("%llu: disk %u from %c to %c\n", (unsigned long long)t->moves, disk, 'A' + from, 'A' + to);
    if (opt_pegs.present) {
        print_pegs(t);
    }
    return 0;
}

/// @brief Moves the top disks disks of peg from to peg to, using peg via:
/// all but the bottom one to via, the bottom one to to, then the rest from
/// via onto it.
/// @return 0, or EXIT_TROUBLE from move_disk().
static int move_tower(towers_t *t, unsigned disks, int from, int to, int via) {
    sof_debug_enter();
    sof_debug("%u disk%s from %c to %c, by %c\n", disks, disks == 1 ? "" : "s", 'A' + from, 'A' + to, 'A' + via);
    if (disks == 1) {
        sof_debug_return(move_disk(t, from, to));
    }
    int rc = move_tower(t, disks - 1, from, via, to);
    if (rc == 0) {
        rc = move_disk(t, from, to);
    }
    if (rc == 0) {
        rc = move_tower(t, disks - 1, via, to, from);
    }
    sof_debug_return(rc);
}

/// @brief Reads the number of disks, from 0 to DISKS_MAX.
/// @return 1, or 0 if s isn't such a number.
static int parse_disks(const char *s, unsigned *disks) {
    if (*s < '0' || *s > '9') {
        return 0;
    }
    char *end;
    errno = 0;
    unsigned long value = strtoul(s, &end, 10);
    if (errno != 0 || *end != '\0' || value > DISKS_MAX) {
        return 0;
    }
    *disks = (unsigned)value;
    return 1;
}

/// @brief Prints each peg's disks, bottom first, on one line, as
/// "    A: 3 2    B:    C: 1".
static void print_pegs(const towers_t *t) {
    printf("   ");
    for (int p = 0; p < PEGS; p++) {
        const sof_stack_t *peg = &t->pegs[p];
        // The stack's elements can be read, bottom first.
        const unsigned *disks = (const unsigned *)peg->elements;
        printf(" %c:", 'A' + p);
        for (size_t i = 0; i < peg->count; i++) {
            printf(" %u", disks[i]);
        }
        printf(p < PEGS - 1 ? "   " : "\n");
    }
}

/// @brief Prints the usage message, with the options sof_copt knows.
static void usage(FILE *stream) {
    fprintf(stream, "usage: hanoi [-h] [-c] [-p] [disks]\n");
    sof_copt_print(stream);
    fprintf(stream,
            "Moves a tower of disks, from 0 to 64 (default 3), from peg A to peg C, one\n"
            "at a time and never onto a smaller disk, and prints each move. Built with\n"
            "make debug=1, it traces its recursion to standard error.\n");
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
        return EXIT_TROUBLE;
    case SOF_COPT_UNEXPECTED_ARGUMENT:
        fprintf(stderr, "%s takes no argument\n", option->option);
        usage(stderr);
        return EXIT_TROUBLE;
    default:
        fprintf(stderr, "can't read the options (error %d)\n", (int)err);
        return EXIT_TROUBLE;
    }
    if (opt_help.present) {
        usage(stdout);
        return EXIT_DONE;
    }
    unsigned disks = 3;
    if (ac - index > 1 || (ac - index == 1 && !parse_disks(av[index], &disks))) {
        fprintf(stderr, "give one number of disks, from 0 to %d\n", DISKS_MAX);
        usage(stderr);
        return EXIT_TROUBLE;
    }
    if (opt_count.present) {
        uint64_t moves = disks == 64 ? UINT64_MAX : ((uint64_t)1 << disks) - 1;
        printf("%llu\n", (unsigned long long)moves);
        return EXIT_DONE;
    }
#ifdef DEBUG
    // The moves go to standard output and the trace to standard error, so
    // keep them in order when both go to one place.
    setvbuf(stdout, NULL, _IOLBF, 0);
#endif

    towers_t t = {0};
    int rc = EXIT_DONE;
    for (int p = 0; p < PEGS && rc == EXIT_DONE; p++) {
        // Room for every disk on each peg, so moving them never allocates.
        if (sof_stack_init(&t.pegs[p], sizeof(unsigned), disks == 0 ? 1 : disks, NULL, NULL, NULL) != SOF_STACK_OK) {
            fprintf(stderr, "out of memory\n");
            rc = EXIT_TROUBLE;
        }
    }
    for (unsigned disk = disks; disk > 0 && rc == EXIT_DONE; disk--) {
        sof_stack_push(&t.pegs[0], &disk);
    }
    if (rc == EXIT_DONE && opt_pegs.present) {
        print_pegs(&t);
    }
    if (rc == EXIT_DONE && disks > 0) {
        rc = move_tower(&t, disks, 0, 2, 1);
    }
    for (int p = 0; p < PEGS; p++) {
        sof_stack_free(&t.pegs[p]);
    }
    return rc;
}
