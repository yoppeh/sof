/**
 * @file test_module.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief A second module of options for test.c.
 * @details Its options are defined apart from test.c's, as a program's
 * modules would, so the tests check that the linker merges arrays from
 * different object files into one table.
 * @version 0.0.1
 * @date 2026-11-03
 * @copyright Copyright (c) 2026
 */

#include <stddef.h>

#include "sof_copt.h"

extern int test_validate(sof_copt_t *option, void *ctx);
extern int test_missing(sof_copt_t *option, void *ctx);

sof_copt_t opt_module_heading = {
    .description = "Module options:",
};

sof_copt_t opt_level = {
    .option = "--level",
    .description = "set the level",
    .type = SOF_COPT_ARG_REQUIRED,
    .validate = test_validate,
    .missing = test_missing,
};

sof_copt_t opt_quiet = {
    .option = "-q",
    .type = SOF_COPT_ARG_NONE,
    .validate = test_validate,
};

SOF_COPT_OPTIONS static sof_copt_t *module_options[] = {
    &opt_module_heading,
    &opt_level,
    &opt_quiet,
};
