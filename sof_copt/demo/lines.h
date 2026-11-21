/**
 * @file lines.h
 * @author Warren Mann (warren@nonvol.io)
 * @brief What the modules of the lines program offer main.c.
 * @details Each feature of lines is a module of its own, which declares its
 * options in its own SOF_COPT_OPTIONS array, reads their arguments in its
 * own callbacks, and keeps what they ask for to itself. main.c parses the
 * command line once, with sof_copt_parse(), which finds every module's
 * options through the table the linker builds, and then calls the functions
 * below for each line.
 * @version 0.0.1
 * @date 2026-11-19
 * @copyright Copyright (c) 2026
 */

#ifndef LINES_H
#define LINES_H

#include <stddef.h>
#include <stdio.h>

#include "sof_copt.h"
#include "sof_string.h"

/// @brief The application's code for an option's argument that is wrong,
/// which the validate callbacks return once they have printed why, and
/// sof_copt keeps in the option's stop_value.
#define LINES_BAD_ARGUMENT 1

/**
 * @brief What range_select() says to do with a line.
 */
typedef enum range_t {
    /// @brief The line is before the range: skip it.
    RANGE_SKIP = 0,
    /// @brief The line is in the range: print it.
    RANGE_PRINT,
    /// @brief The line is after the range, and so is every line after it:
    /// stop reading.
    RANGE_STOP,
} range_t;

/**
 * @brief Reads an option's argument as a decimal number, for the modules'
 * validate callbacks; defined in main.c.
 * @details Prints why, naming the option, if the argument isn't a number
 * from min to max.
 * @param option The option, whose value is the argument.
 * @param min The smallest number allowed.
 * @param max The largest number allowed.
 * @param n Set to the number; left as it was on an error.
 * @return 0, or LINES_BAD_ARGUMENT, so a validate callback can return it.
 */
extern int lines_number(const sof_copt_t *option, size_t min, size_t max, size_t *n);

/**
 * @brief Prints a line's number in front of it, if -n was given; from
 * number.c.
 * @param stream Where the line is printed.
 * @param number The line's number in the input, from 1.
 */
extern void number_print(FILE *stream, size_t number);

/**
 * @brief Says whether a line is in the range -f and -c ask for; from
 * range.c.
 * @param number The line's number in the input, from 1.
 * @return RANGE_SKIP, RANGE_PRINT or RANGE_STOP.
 */
extern range_t range_select(size_t number);

/**
 * @brief Says whether to print a line, given -s; from squeeze.c.
 * @details Called for every line in the range, in order, since it
 * remembers whether the last one was blank.
 * @param line The line, with its line ending, if it has one.
 * @return 1 to print it, or 0 if it is a blank line after another blank
 * line and -s was given.
 */
extern int squeeze_keep(const sof_string_t *line);

#endif // LINES_H
