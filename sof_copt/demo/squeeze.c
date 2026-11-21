/**
 * @file squeeze.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief The lines program's squeezing of blank lines, and its option, -s.
 * @details -s prints one blank line where the input has several in a row.
 * A blank line is an empty one, ended by a newline or a carriage return and
 * a newline.
 * @version 0.0.1
 * @date 2026-11-19
 * @copyright Copyright (c) 2026
 */

#include <string.h>

#include "sof_copt.h"
#include "sof_string.h"
#include "lines.h"

static int is_blank(const sof_string_t *line);

/// @brief 1 if the last line squeeze_keep() saw was blank.
static int last_blank;

static sof_copt_t opt_heading = {
    .description = "Blank lines:",
};

static sof_copt_t opt_squeeze = {
    .option = "-s",
    .description = "print one blank line in place of several in a row",
};

SOF_COPT_OPTIONS static sof_copt_t *options[] = {
    &opt_heading,
    &opt_squeeze,
};

/// @brief Tells whether a line is blank: only a line ending.
/// @return 1 if it is, or 0.
static int is_blank(const sof_string_t *line) {
    return (line->length == 1 && line->data[0] == '\n') || (line->length == 2 && memcmp(line->data, "\r\n", 2) == 0);
}

int squeeze_keep(const sof_string_t *line) {
    int blank = is_blank(line);
    int keep = !(opt_squeeze.present && blank && last_blank);
    last_blank = blank;
    return keep;
}
