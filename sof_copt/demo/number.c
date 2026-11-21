/**
 * @file number.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief The lines program's numbering, and its options, -n and --number.
 * @details -n numbers the lines, in a field 6 wide. --number does the same,
 * and --number=width numbers them in a field of that width. The width is an
 * optional argument, so it goes on the long option, after an '=', where it
 * can't be mistaken for a file: a short option with an optional argument
 * would read "-n 3" as -n and a file named 3.
 * @version 0.0.1
 * @date 2026-11-19
 * @copyright Copyright (c) 2026
 */

#include <stdio.h>

#include "sof_copt.h"
#include "lines.h"

/// @brief The width of the numbers when none is given, and the widest
/// allowed.
#define DEFAULT_WIDTH 6
#define MAX_WIDTH 20

static int number_long_validate(sof_copt_t *option, void *ctx);
static int number_short_validate(sof_copt_t *option, void *ctx);

/// @brief The width of the numbers, or 0 to print none.
static size_t width;

static sof_copt_t opt_heading = {
    .description = "Numbering:",
};

static sof_copt_t opt_number_short = {
    .option = "-n",
    .description = "number the lines, in a field 6 wide",
    .validate = number_short_validate,
};

static sof_copt_t opt_number_long = {
    .option = "--number",
    .description = "number the lines; =width sets the field's width (default 6)",
    .type = SOF_COPT_ARG_OPTIONAL,
    .validate = number_long_validate,
};

SOF_COPT_OPTIONS static sof_copt_t *options[] = {
    &opt_heading,
    &opt_number_short,
    &opt_number_long,
};

/// @brief Reads --number and its width, if it has one, which wins over -n.
static int number_long_validate(sof_copt_t *option, void *ctx) {
    (void)ctx;
    if (option->value == NULL) {
        width = DEFAULT_WIDTH;
        return 0;
    }
    return lines_number(option, 1, MAX_WIDTH, &width);
}

/// @brief Turns numbering on, at the default width unless --number gave one.
static int number_short_validate(sof_copt_t *option, void *ctx) {
    (void)option;
    (void)ctx;
    if (width == 0) {
        width = DEFAULT_WIDTH;
    }
    return 0;
}

void number_print(FILE *stream, size_t number) {
    if (width > 0) {
        fprintf(stream, "%*zu  ", (int)width, number);
    }
}
