/**
 * @file test.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief Tests for sof_copt.c.
 * @details Usage: test. Runs every test, and exits with 0 if they all pass
 * or 1 if any fails. The Makefile's check target builds and runs it.
 *
 * The options tested are defined here and in test_module.c, so the table
 * holds arrays from two object files. They are the only options in the test
 * program, which takes no arguments.
 * @version 0.0.1
 * @date 2026-11-03
 * @copyright Copyright (c) 2026
 */

#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "sof_copt.h"
#include "test.h"

/// @brief Threads, the lookups each makes, and the usage messages each
/// prints, in the threaded test.
#define THREADS 8
#define LOOKUPS 100000
#define PRINTS 200
/// @brief Room for the table's entries and for the usage message.
#define MAX_ENTRIES 16
#define MAX_TEXT 1024

/// @brief The ctx the tests pass to sof_copt_parse(), counting the
/// callbacks.
typedef struct calls_t {
    /// @brief Calls of test_validate().
    int validated;
    /// @brief Calls of test_missing().
    int missing;
    /// @brief The last option either callback was called with.
    sof_copt_t *last;
    /// @brief test_validate() returns fail_result for this option.
    sof_copt_t *fail_on;
    int fail_result;
    /// @brief test_missing() returns missing_result.
    int missing_result;
} calls_t;

int test_validate(sof_copt_t *option, void *ctx);
int test_missing(sof_copt_t *option, void *ctx);

extern sof_copt_t opt_module_heading, opt_level, opt_quiet;

static sof_copt_t opt_verbose = {
    .option = "-v",
    .description = "be verbose",
    .type = SOF_COPT_ARG_NONE,
    .validate = test_validate,
};

static sof_copt_t opt_output = {
    .option = "-o",
    .description = "write to a file",
    .type = SOF_COPT_ARG_REQUIRED,
    .validate = test_validate,
};

static sof_copt_t opt_color = {
    .option = "--color",
    .description = "use color",
    .type = SOF_COPT_ARG_OPTIONAL,
    .validate = test_validate,
};

static sof_copt_t opt_heading = {
    .description = "Commands:",
};

static sof_copt_t opt_add = {
    .option = "add",
    .description = "add something",
    .type = SOF_COPT_ARG_NONE,
    .validate = test_validate,
};

static sof_copt_t opt_need = {
    .option = "-n",
    .description = "a number",
    .type = SOF_COPT_ARG_REQUIRED,
    .missing = test_missing,
};

static sof_copt_t opt_zip = {
    .option = "-z",
    .description = "compress",
    .type = SOF_COPT_ARG_OPTIONAL,
    .validate = test_validate,
};

/// @brief A name that is also a group of two other options.
static sof_copt_t opt_both = {
    .option = "-vq",
    .description = "not -v -q",
    .type = SOF_COPT_ARG_NONE,
    .validate = test_validate,
};

SOF_COPT_OPTIONS static sof_copt_t *options[] = {
    &opt_verbose,
    &opt_output,
    &opt_color,
    &opt_heading,
    &opt_add,
    &opt_need,
    &opt_zip,
    &opt_both,
    NULL,
};

/// @brief Counts the call, and fails for calls->fail_on.
int test_validate(sof_copt_t *option, void *ctx) {
    calls_t *calls = ctx;
    calls->validated++;
    calls->last = option;
    return option == calls->fail_on ? calls->fail_result : 0;
}

/// @brief Counts the call, gives --level a default, and returns
/// calls->missing_result.
int test_missing(sof_copt_t *option, void *ctx) {
    calls_t *calls = ctx;
    calls->missing++;
    calls->last = option;
    if (option == &opt_level) {
        option->value = "1";
    }
    return calls->missing_result;
}

/// @brief Returns the position of option in the table, or -1.
static int position(const sof_copt_t *option) {
    for (size_t i = 0; i < sof_copt_count(); i++) {
        if (sof_copt_get(i) == option) {
            return (int)i;
        }
    }
    return -1;
}

/// @brief Clears calls' counts, keeping what it should fail on.
static void reset(calls_t *calls) {
    sof_copt_t *fail_on = calls->fail_on;
    sof_copt_error_t fail_result = calls->fail_result, missing_result = calls->missing_result;
    memset(calls, 0, sizeof(*calls));
    calls->fail_on = fail_on;
    calls->fail_result = fail_result;
    calls->missing_result = missing_result;
}

/// @brief Parses the argc arguments in argv with a fresh calls_t.
static sof_copt_error_t parse(int argc, char **argv, calls_t *calls, int *index, sof_copt_t **option) {
    reset(calls);
    return sof_copt_parse(argc, argv, calls, index, option);
}

/// @brief Parses the argc arguments in argv in order, with a fresh calls_t.
static sof_copt_error_t parse_ordered(int argc, char *const *argv, calls_t *calls, int *index, sof_copt_t **option) {
    reset(calls);
    return sof_copt_parse_ordered(argc, argv, calls, index, option);
}

/// @brief Returns 1 if the n strings in argv are those in want, in order.
static int args_are(char *const *argv, const char *const *want, int n) {
    for (int i = 0; i < n; i++) {
        if (strcmp(argv[i], want[i]) != 0) {
            return 0;
        }
    }
    return 1;
}

#define ARGC(argv) ((int)(sizeof(argv) / sizeof((argv)[0])))

static void test_table(void) {
    // Eight entries here (the NULL is skipped) and three in test_module.c.
    sof_copt_t *entries[] = {&opt_verbose, &opt_output, &opt_color, &opt_heading, &opt_add, &opt_need,
                              &opt_zip, &opt_both, &opt_module_heading, &opt_level, &opt_quiet};
    CHECK(sof_copt_count() == 11);
    CHECK(sof_copt_get(11) == NULL);
    CHECK(sof_copt_get((size_t)-1) == NULL);
    for (size_t i = 0; i < 11; i++) {
        CHECK(sof_copt_get(i) != NULL);
        CHECK(position(entries[i]) >= 0);
        for (size_t j = 0; j < i; j++) {
            CHECK(position(entries[i]) != position(entries[j]));
        }
    }
    // Each array keeps its order.
    CHECK(position(&opt_verbose) < position(&opt_output));
    CHECK(position(&opt_output) < position(&opt_color));
    CHECK(position(&opt_color) < position(&opt_heading));
    CHECK(position(&opt_heading) < position(&opt_add));
    CHECK(position(&opt_add) < position(&opt_need));
    CHECK(position(&opt_need) < position(&opt_zip));
    CHECK(position(&opt_zip) < position(&opt_both));
    CHECK(position(&opt_module_heading) < position(&opt_level));
    CHECK(position(&opt_level) < position(&opt_quiet));
}

static void test_find(void) {
    CHECK(sof_copt_find("-v") == &opt_verbose);
    CHECK(sof_copt_find("--color") == &opt_color);
    CHECK(sof_copt_find("add") == &opt_add);
    CHECK(sof_copt_find("--level") == &opt_level);
    CHECK(sof_copt_find("-q") == &opt_quiet);
    CHECK(sof_copt_find("-vq") == &opt_both);
    // Groups are for parsing; find matches names only.
    CHECK(sof_copt_find("-qv") == NULL);
    CHECK(sof_copt_find("--col") == NULL);
    CHECK(sof_copt_find("--colors") == NULL);
    CHECK(sof_copt_find("-") == NULL);
    CHECK(sof_copt_find("") == NULL);
    CHECK(sof_copt_find(NULL) == NULL);
}

static void test_parse(void) {
    char *argv[] = {"prog", "-v", "-o", "out", "--color=always", "add", "--level=3", "file", "-q"};
    calls_t calls = {0};
    int index = -1;
    sof_copt_t *option = &opt_add;
    char *out = argv[3], *color = argv[4];
    CHECK(parse(ARGC(argv), argv, &calls, &index, &option) == SOF_COPT_OK);
    CHECK(index == 8);
    CHECK(option == NULL);
    CHECK(opt_verbose.present && opt_verbose.value == NULL);
    CHECK(opt_output.present && opt_output.value == out);
    CHECK(opt_color.present && strcmp(opt_color.value, "always") == 0);
    CHECK(opt_color.value == color + 8);
    CHECK(opt_add.present && opt_add.value == NULL);
    CHECK(opt_level.present && strcmp(opt_level.value, "3") == 0);
    // An option after an operand is read too.
    CHECK(opt_quiet.present);
    CHECK(!opt_need.present && opt_need.value == NULL);
    CHECK(!opt_heading.present);
    CHECK(calls.validated == 6);
    CHECK(calls.missing == 1 && calls.last == &opt_need);
    // The operand is moved to the end; the strings aren't changed.
    static const char *want[] = {"prog", "-v", "-o", "out", "--color=always", "add", "--level=3", "-q", "file"};
    CHECK(args_are(argv, want, ARGC(argv)));
}

static void test_groups(void) {
    calls_t calls = {0};
    int index;
    sof_copt_t *option;
    char *flags[] = {"prog", "-qv", "file"};
    CHECK(parse(ARGC(flags), flags, &calls, &index, &option) == SOF_COPT_OK);
    CHECK(index == 2 && option == NULL);
    CHECK(opt_quiet.present && opt_verbose.present && !opt_both.present);
    CHECK(calls.validated == 2 && calls.missing == 2);
    // Each one is validated, even the same one twice.
    char *twice[] = {"prog", "-vv"};
    CHECK(parse(ARGC(twice), twice, &calls, &index, NULL) == SOF_COPT_OK);
    CHECK(calls.validated == 2 && opt_verbose.present);
    // A name in the table wins over a group, but a longer group is a group.
    char *name[] = {"prog", "-vq"};
    CHECK(parse(ARGC(name), name, &calls, &index, NULL) == SOF_COPT_OK);
    CHECK(opt_both.present && !opt_verbose.present && !opt_quiet.present);
    char *longer[] = {"prog", "-vqv"};
    CHECK(parse(ARGC(longer), longer, &calls, &index, NULL) == SOF_COPT_OK);
    CHECK(!opt_both.present && opt_verbose.present && opt_quiet.present);
    CHECK(calls.validated == 3);
    // A required argument: the rest of the group, after an '=' if there is
    // one, or else the next argument.
    char *attached[] = {"prog", "-vofile", "x"};
    CHECK(parse(ARGC(attached), attached, &calls, &index, NULL) == SOF_COPT_OK);
    CHECK(index == 2 && opt_verbose.present && opt_output.value == attached[1] + 3);
    char *single[] = {"prog", "-n5"};
    CHECK(parse(ARGC(single), single, &calls, &index, NULL) == SOF_COPT_OK);
    CHECK(index == 2 && strcmp(opt_need.value, "5") == 0);
    char *equal[] = {"prog", "-vn=5"};
    CHECK(parse(ARGC(equal), equal, &calls, &index, NULL) == SOF_COPT_OK);
    CHECK(opt_verbose.present && opt_need.value == equal[1] + 4);
    char *empty[] = {"prog", "-vn="};
    CHECK(parse(ARGC(empty), empty, &calls, &index, NULL) == SOF_COPT_OK);
    CHECK(opt_need.present && strcmp(opt_need.value, "") == 0);
    char *detached[] = {"prog", "-qvo", "-v", "file"};
    CHECK(parse(ARGC(detached), detached, &calls, &index, NULL) == SOF_COPT_OK);
    CHECK(index == 3 && opt_output.value == detached[2]);
    CHECK(calls.validated == 3);
    // Letters after one that takes an argument are its argument.
    char *rest[] = {"prog", "-ovq"};
    CHECK(parse(ARGC(rest), rest, &calls, &index, NULL) == SOF_COPT_OK);
    CHECK(strcmp(opt_output.value, "vq") == 0);
    CHECK(!opt_verbose.present && !opt_quiet.present && !opt_both.present);
    // An optional argument: the rest of the group, or none.
    char *zip_none[] = {"prog", "-vz", "9"};
    CHECK(parse(ARGC(zip_none), zip_none, &calls, &index, NULL) == SOF_COPT_OK);
    CHECK(index == 2 && opt_zip.present && opt_zip.value == NULL);
    char *zip_rest[] = {"prog", "-zv"};
    CHECK(parse(ARGC(zip_rest), zip_rest, &calls, &index, NULL) == SOF_COPT_OK);
    CHECK(strcmp(opt_zip.value, "v") == 0 && !opt_verbose.present);
    char *zip_equal[] = {"prog", "-qz=9"};
    CHECK(parse(ARGC(zip_equal), zip_equal, &calls, &index, NULL) == SOF_COPT_OK);
    CHECK(opt_quiet.present && strcmp(opt_zip.value, "9") == 0);
    char *zip_empty[] = {"prog", "-qz="};
    CHECK(parse(ARGC(zip_empty), zip_empty, &calls, &index, NULL) == SOF_COPT_OK);
    CHECK(opt_zip.present && strcmp(opt_zip.value, "") == 0);
}

static void test_group_errors(void) {
    calls_t calls = {0};
    int index;
    sof_copt_t *option;
    // An unknown letter: the letters before it are taken, those after not.
    char *unknown[] = {"prog", "-q", "-vxo", "file"};
    option = &opt_add;
    CHECK(parse(ARGC(unknown), unknown, &calls, &index, &option) == SOF_COPT_NO_MATCH);
    CHECK(index == 2 && option == NULL);
    CHECK(opt_quiet.present && opt_verbose.present && !opt_output.present);
    CHECK(calls.validated == 2 && calls.missing == 0);
    // Only a single '-' starts a group.
    char *dashes[] = {"prog", "--vq"};
    CHECK(parse(ARGC(dashes), dashes, &calls, &index, &option) == SOF_COPT_NO_MATCH);
    CHECK(index == 1 && option == NULL && !opt_verbose.present);
    char *letter[] = {"prog", "-a"};
    CHECK(parse(ARGC(letter), letter, &calls, &index, &option) == SOF_COPT_NO_MATCH);
    CHECK(index == 1 && !opt_add.present);
    char *equal_first[] = {"prog", "-=v"};
    CHECK(parse(ARGC(equal_first), equal_first, &calls, &index, &option) == SOF_COPT_NO_MATCH);
    CHECK(index == 1 && option == NULL);
    // A required argument missing at the end of a group on the last argument.
    char *missing[] = {"prog", "file", "-qo"};
    CHECK(parse(ARGC(missing), missing, &calls, &index, &option) == SOF_COPT_MISSING_ARGUMENT);
    CHECK(index == 2 && option == &opt_output && opt_quiet.present);
    CHECK(strcmp(missing[index], "-qo") == 0);
    char *ended[] = {"prog", "-qo", NULL};
    CHECK(parse(3, ended, &calls, &index, &option) == SOF_COPT_MISSING_ARGUMENT);
    CHECK(index == 1 && option == &opt_output);
    // An argument for an option that takes none.
    char *assigned[] = {"prog", "-qv=1"};
    CHECK(parse(ARGC(assigned), assigned, &calls, &index, &option) == SOF_COPT_UNEXPECTED_ARGUMENT);
    CHECK(index == 1 && option == &opt_verbose && calls.validated == 1);
    // A validate callback stops the group.
    calls.fail_on = &opt_verbose;
    calls.fail_result = 1;
    char *stopped[] = {"prog", "-qvz"};
    CHECK(parse(ARGC(stopped), stopped, &calls, &index, &option) == SOF_COPT_STOPPED);
    CHECK(index == 1 && option == &opt_verbose && opt_verbose.stop_value == 1);
    CHECK(opt_quiet.present && opt_verbose.present && !opt_zip.present);
    calls.fail_on = NULL;
    // An invalid type in a group.
    opt_quiet.type = (sof_copt_arg_t)99;
    char *bad_type[] = {"prog", "-vq", "-vqv"};
    sof_copt_error_t err = parse(ARGC(bad_type), bad_type, &calls, &index, &option);
    opt_quiet.type = SOF_COPT_ARG_NONE;
    CHECK(err == SOF_COPT_ERROR);
    CHECK(index == 2 && option == &opt_quiet && opt_both.present);
}

static void test_permute(void) {
    calls_t calls = {0};
    int index;
    sof_copt_t *option;
    // Options anywhere; the operands keep their order at the end.
    char *mixed[] = {"prog", "a", "-v", "b", "-o", "x", "c", "-qn5", "d", "--level=2"};
    CHECK(parse(ARGC(mixed), mixed, &calls, &index, &option) == SOF_COPT_OK);
    static const char *mixed_want[] = {"prog", "-v", "-o", "x", "-qn5", "--level=2", "a", "b", "c", "d"};
    CHECK(index == 6 && option == NULL);
    CHECK(args_are(mixed, mixed_want, ARGC(mixed)));
    CHECK(strcmp(opt_output.value, "x") == 0 && strcmp(opt_need.value, "5") == 0);
    CHECK(opt_verbose.present && opt_quiet.present && strcmp(opt_level.value, "2") == 0);
    CHECK(calls.validated == 4 && calls.missing == 0);
    // "--" ends the options, and stays in front of the operands.
    char *dashes[] = {"prog", "a", "-v", "--", "-q", "b"};
    CHECK(parse(ARGC(dashes), dashes, &calls, &index, NULL) == SOF_COPT_OK);
    static const char *dashes_want[] = {"prog", "-v", "--", "a", "-q", "b"};
    CHECK(index == 3 && args_are(dashes, dashes_want, ARGC(dashes)));
    CHECK(opt_verbose.present && !opt_quiet.present);
    // Only the first "--" is special.
    char *two[] = {"prog", "--", "--", "-v"};
    CHECK(parse(ARGC(two), two, &calls, &index, NULL) == SOF_COPT_OK);
    CHECK(index == 2 && !opt_verbose.present);
    // "-" is an operand.
    char *dash[] = {"prog", "-", "-v", "-"};
    CHECK(parse(ARGC(dash), dash, &calls, &index, NULL) == SOF_COPT_OK);
    static const char *dash_want[] = {"prog", "-v", "-", "-"};
    CHECK(index == 2 && opt_verbose.present && args_are(dash, dash_want, ARGC(dash)));
    // Names that don't start with '-' are options anywhere too.
    char *word[] = {"prog", "remove", "add", "x"};
    CHECK(parse(ARGC(word), word, &calls, &index, NULL) == SOF_COPT_OK);
    static const char *word_want[] = {"prog", "add", "remove", "x"};
    CHECK(index == 2 && opt_add.present && args_are(word, word_want, ARGC(word)));
    // A required argument is taken from the next argument, whatever it is.
    char *value[] = {"prog", "a", "-o", "-", "b", "-n", "--", "c"};
    CHECK(parse(ARGC(value), value, &calls, &index, NULL) == SOF_COPT_OK);
    static const char *value_want[] = {"prog", "-o", "-", "-n", "--", "a", "b", "c"};
    CHECK(index == 5 && args_are(value, value_want, ARGC(value)));
    // Only operands.
    char *operands[] = {"prog", "a", "b"};
    CHECK(parse(ARGC(operands), operands, &calls, &index, NULL) == SOF_COPT_OK);
    CHECK(index == 1 && strcmp(operands[1], "a") == 0 && strcmp(operands[2], "b") == 0);
    // On an error, index is where the argument is now.
    char *unknown[] = {"prog", "a", "-v", "b", "-x", "-q"};
    CHECK(parse(ARGC(unknown), unknown, &calls, &index, &option) == SOF_COPT_NO_MATCH);
    CHECK(index == 4 && strcmp(unknown[index], "-x") == 0 && option == NULL);
    CHECK(opt_verbose.present && !opt_quiet.present && calls.missing == 0);
    char *missing[] = {"prog", "a", "-v", "-o"};
    CHECK(parse(ARGC(missing), missing, &calls, &index, &option) == SOF_COPT_MISSING_ARGUMENT);
    CHECK(index == 3 && strcmp(missing[index], "-o") == 0 && option == &opt_output);
    calls.fail_on = &opt_output;
    calls.fail_result = -3;
    char *failed[] = {"prog", "a", "b", "-o", "x"};
    CHECK(parse(ARGC(failed), failed, &calls, &index, &option) == SOF_COPT_STOPPED);
    CHECK(opt_output.stop_value == -3);
    CHECK(index == 3 && strcmp(failed[index], "-o") == 0 && option == &opt_output);
    calls.fail_on = NULL;
    char *hole[] = {"prog", "a", "-v", NULL, "-q"};
    CHECK(parse(5, hole, &calls, &index, &option) == SOF_COPT_ERROR);
    CHECK(index == 3 && option == NULL);
}

static void test_ordered(void) {
    calls_t calls = {0};
    int index;
    sof_copt_t *option = &opt_add;
    // The old behaviour: stop at the first operand, and leave argv alone.
    char *argv[] = {"prog", "-v", "-o", "out", "--color=always", "add", "--level=3", "file", "-q"};
    char *copy[ARGC(argv)];
    memcpy(copy, argv, sizeof(argv));
    CHECK(parse_ordered(ARGC(argv), argv, &calls, &index, &option) == SOF_COPT_OK);
    CHECK(index == 7 && option == NULL);
    CHECK(memcmp(copy, argv, sizeof(argv)) == 0);
    CHECK(opt_verbose.present && opt_output.value == argv[3] && opt_add.present);
    CHECK(opt_color.value == argv[4] + 8 && strcmp(opt_level.value, "3") == 0);
    // Operands aren't parsed, even if they look like options.
    CHECK(!opt_quiet.present);
    CHECK(calls.validated == 5 && calls.missing == 1 && calls.last == &opt_need);
    // Groups work the same.
    char *group[] = {"prog", "-qvo", "x", "-zn5", "cmd", "-v", "--", "-x"};
    memcpy(copy, group, sizeof(group));
    CHECK(parse_ordered(ARGC(group), group, &calls, &index, NULL) == SOF_COPT_OK);
    CHECK(index == 4 && memcmp(copy, group, sizeof(group)) == 0);
    CHECK(opt_quiet.present && opt_verbose.present && opt_output.value == group[2]);
    CHECK(strcmp(opt_zip.value, "n5") == 0 && !opt_need.present);
    // "--" is skipped; "-" is an operand.
    char *dashes[] = {"prog", "-v", "--", "-q"};
    CHECK(parse_ordered(ARGC(dashes), dashes, &calls, &index, NULL) == SOF_COPT_OK);
    CHECK(index == 3 && opt_verbose.present && !opt_quiet.present);
    char *dash[] = {"prog", "-", "-v"};
    CHECK(parse_ordered(ARGC(dash), dash, &calls, &index, NULL) == SOF_COPT_OK);
    CHECK(index == 1 && !opt_verbose.present);
    char *last[] = {"prog", "-q", "--"};
    CHECK(parse_ordered(ARGC(last), last, &calls, &index, NULL) == SOF_COPT_OK);
    CHECK(index == 3 && opt_quiet.present);
    // Errors.
    char *unknown[] = {"prog", "-v", "-x", "file"};
    CHECK(parse_ordered(ARGC(unknown), unknown, &calls, &index, &option) == SOF_COPT_NO_MATCH);
    CHECK(index == 2 && option == NULL);
    char *missing[] = {"prog", "-vo"};
    CHECK(parse_ordered(ARGC(missing), missing, &calls, &index, &option) == SOF_COPT_MISSING_ARGUMENT);
    CHECK(index == 1 && option == &opt_output);
    CHECK(parse_ordered(2, NULL, &calls, &index, &option) == SOF_COPT_ERROR);
    CHECK(index == 0 && option == NULL);
    char *hole[] = {"prog", "-v", NULL};
    CHECK(parse_ordered(3, hole, &calls, &index, &option) == SOF_COPT_ERROR);
    CHECK(index == 2 && option == NULL);
    char *none[] = {"prog"};
    CHECK(parse_ordered(0, none, &calls, &index, NULL) == SOF_COPT_OK);
    CHECK(index == 0);
    CHECK(parse_ordered(-1, NULL, &calls, NULL, NULL) == SOF_COPT_OK);
    CHECK(parse_ordered(ARGC(unknown), unknown, &calls, NULL, NULL) == SOF_COPT_NO_MATCH);
    // Missing callbacks are called, and can stop it.
    calls.missing_result = 2;
    CHECK(parse_ordered(ARGC(none), none, &calls, &index, &option) == SOF_COPT_STOPPED);
    CHECK(index == 1 && calls.missing == 1 && option == calls.last);
    CHECK(option->stop_value == 2 && !option->present);
    calls.missing_result = 0;
}

static void test_arguments(void) {
    calls_t calls = {0};
    int index;
    char *equals[] = {"prog", "-o=x=y", "-n=", "--color"};
    CHECK(parse(ARGC(equals), equals, &calls, &index, NULL) == SOF_COPT_OK);
    CHECK(index == 4);
    CHECK(strcmp(opt_output.value, "x=y") == 0);
    CHECK(strcmp(opt_need.value, "") == 0);
    CHECK(opt_color.present && opt_color.value == NULL);
    // A required argument is taken from the next argument, whatever it is.
    char *next[] = {"prog", "-o", "-v", "-n", "--"};
    CHECK(parse(ARGC(next), next, &calls, &index, NULL) == SOF_COPT_OK);
    CHECK(index == 5);
    CHECK(opt_output.value == next[2] && opt_need.value == next[4]);
    CHECK(!opt_verbose.present);
    // An optional argument is only taken after '='.
    char *optional[] = {"prog", "--color", "add"};
    CHECK(parse(ARGC(optional), optional, &calls, &index, NULL) == SOF_COPT_OK);
    CHECK(index == 3 && opt_color.value == NULL && opt_add.present);
    // The last of several wins, and each is validated.
    char *twice[] = {"prog", "-o", "a", "-o=b", "-v", "-v"};
    CHECK(parse(ARGC(twice), twice, &calls, &index, NULL) == SOF_COPT_OK);
    CHECK(strcmp(opt_output.value, "b") == 0);
    CHECK(calls.validated == 4);
}

static void test_operands(void) {
    calls_t calls = {0};
    int index;
    char *dashes[] = {"prog", "-v", "--", "-o", "x"};
    CHECK(parse(ARGC(dashes), dashes, &calls, &index, NULL) == SOF_COPT_OK);
    CHECK(index == 3 && opt_verbose.present && !opt_output.present);
    char *dash[] = {"prog", "-", "-v"};
    CHECK(parse(ARGC(dash), dash, &calls, &index, NULL) == SOF_COPT_OK);
    CHECK(index == 2 && opt_verbose.present && strcmp(dash[2], "-") == 0);
    char *word[] = {"prog", "-q", "remove", "-v"};
    CHECK(parse(ARGC(word), word, &calls, &index, NULL) == SOF_COPT_OK);
    CHECK(index == 3 && opt_quiet.present && opt_verbose.present && strcmp(word[3], "remove") == 0);
    char *none[] = {"prog"};
    CHECK(parse(ARGC(none), none, &calls, &index, NULL) == SOF_COPT_OK);
    CHECK(index == 1);
    char *last[] = {"prog", "--"};
    CHECK(parse(ARGC(last), last, &calls, &index, NULL) == SOF_COPT_OK);
    CHECK(index == 2);
    CHECK(parse(0, none, &calls, &index, NULL) == SOF_COPT_OK);
    CHECK(index == 0);
    CHECK(parse(-1, NULL, &calls, &index, NULL) == SOF_COPT_OK);
    CHECK(index == 0);
    CHECK(calls.missing == 2);
}

static void test_errors(void) {
    calls_t calls = {0};
    int index;
    sof_copt_t *option;
    char *unknown[] = {"prog", "-v", "-x", "-o"};
    option = &opt_add;
    CHECK(parse(ARGC(unknown), unknown, &calls, &index, &option) == SOF_COPT_NO_MATCH);
    CHECK(index == 2 && option == NULL);
    CHECK(opt_verbose.present && calls.validated == 1 && calls.missing == 0);
    char *prefix[] = {"prog", "--col"};
    CHECK(parse(ARGC(prefix), prefix, &calls, &index, &option) == SOF_COPT_NO_MATCH);
    CHECK(index == 1);
    char *longer[] = {"prog", "-vqx"};
    CHECK(parse(ARGC(longer), longer, &calls, &index, &option) == SOF_COPT_NO_MATCH);
    char *number[] = {"prog", "-1"};
    CHECK(parse(ARGC(number), number, &calls, &index, &option) == SOF_COPT_NO_MATCH);
    CHECK(index == 1 && option == NULL);
    char *assigned[] = {"prog", "-v=1"};
    CHECK(parse(ARGC(assigned), assigned, &calls, &index, &option) == SOF_COPT_UNEXPECTED_ARGUMENT);
    CHECK(index == 1 && option == &opt_verbose && calls.validated == 0);
    char *missing[] = {"prog", "-v", "-o"};
    CHECK(parse(ARGC(missing), missing, &calls, &index, &option) == SOF_COPT_MISSING_ARGUMENT);
    CHECK(index == 2 && option == &opt_output && calls.validated == 1);
    char *ended[] = {"prog", "-o", NULL};
    CHECK(parse(3, ended, &calls, &index, &option) == SOF_COPT_MISSING_ARGUMENT);
    CHECK(index == 1 && option == &opt_output);
    char *hole[] = {"prog", "-v", NULL, "-q"};
    CHECK(parse(4, hole, &calls, &index, &option) == SOF_COPT_ERROR);
    CHECK(index == 2 && option == NULL);
    CHECK(parse(2, NULL, &calls, &index, &option) == SOF_COPT_ERROR);
    CHECK(index == 0 && option == NULL);
    opt_quiet.type = (sof_copt_arg_t)99;
    char *bad_type[] = {"prog", "-q"};
    sof_copt_error_t err = parse(ARGC(bad_type), bad_type, &calls, &index, &option);
    opt_quiet.type = SOF_COPT_ARG_NONE;
    CHECK(err == SOF_COPT_ERROR);
    CHECK(index == 1 && option == &opt_quiet);
    // index and option may be NULL.
    CHECK(parse(ARGC(unknown), unknown, &calls, NULL, NULL) == SOF_COPT_NO_MATCH);
}

static void test_validate_stops(void) {
    char *argv[] = {"prog", "-v", "-o", "x", "-q"};
    calls_t calls = {0};
    int index;
    sof_copt_t *option;
    calls.fail_on = &opt_output;
    // Each program's codes are its own; any non-zero value stops parsing, and
    // is kept, unchanged, in the option it was returned for.
    static const int codes[] = {1, 2, 42, -1, INT_MAX, INT_MIN};
    for (size_t i = 0; i < sizeof(codes) / sizeof(codes[0]); i++) {
        char *args[] = {"prog", "-v", "-o", "x", "-q"};
        calls.validated = 0;
        calls.fail_result = codes[i];
        CHECK(parse(ARGC(args), args, &calls, &index, &option) == SOF_COPT_STOPPED);
        CHECK(index == 2 && option == &opt_output);
        CHECK(opt_output.stop_value == codes[i]);
        CHECK(opt_verbose.stop_value == 0);
        CHECK(opt_output.present && opt_output.value == args[3]);
        CHECK(!opt_quiet.present);
        CHECK(calls.validated == 2 && calls.last == &opt_output);
        CHECK(calls.missing == 0);
    }
    // The next parse clears it.
    calls.fail_on = NULL;
    CHECK(parse(ARGC(argv), argv, &calls, &index, &option) == SOF_COPT_OK);
    CHECK(opt_output.stop_value == 0 && option == NULL);
}

static void test_missing_callbacks(void) {
    calls_t calls = {0};
    int index;
    sof_copt_t *option;
    char *none[] = {"prog", "file"};
    CHECK(parse(ARGC(none), none, &calls, &index, &option) == SOF_COPT_OK);
    CHECK(index == 1 && option == NULL);
    CHECK(calls.missing == 2);
    // A missing callback can give a default.
    CHECK(!opt_level.present && strcmp(opt_level.value, "1") == 0);
    char *level[] = {"prog", "--level", "2"};
    CHECK(parse(ARGC(level), level, &calls, &index, &option) == SOF_COPT_OK);
    CHECK(calls.missing == 1 && calls.last == &opt_need);
    CHECK(strcmp(opt_level.value, "2") == 0);
    char *both[] = {"prog", "--level", "2", "-n", "3"};
    CHECK(parse(ARGC(both), both, &calls, &index, &option) == SOF_COPT_OK);
    CHECK(calls.missing == 0);
    // The first non-zero return stops parsing.
    calls.missing_result = 7;
    CHECK(parse(ARGC(none), none, &calls, &index, &option) == SOF_COPT_STOPPED);
    CHECK(calls.missing == 1 && option == calls.last);
    CHECK(option == &opt_need || option == &opt_level);
    CHECK(option->stop_value == 7 && !option->present);
    CHECK(index == 1);
    calls.missing_result = 0;
}

static void test_reset(void) {
    calls_t calls = {0};
    char *given[] = {"prog", "-v", "-o", "x"};
    char *none[] = {"prog"};
    CHECK(parse(ARGC(given), given, &calls, NULL, NULL) == SOF_COPT_OK);
    CHECK(opt_verbose.present && opt_output.value != NULL);
    CHECK(parse(ARGC(none), none, &calls, NULL, NULL) == SOF_COPT_OK);
    CHECK(!opt_verbose.present && !opt_output.present && opt_output.value == NULL);
    // Even when parsing fails.
    CHECK(parse(ARGC(given), given, &calls, NULL, NULL) == SOF_COPT_OK);
    CHECK(parse(2, NULL, &calls, NULL, NULL) == SOF_COPT_ERROR);
    CHECK(!opt_verbose.present && opt_output.value == NULL);
}

/// @brief Returns the line of text that equals line, or NULL.
static const char *find_line(const char *text, const char *line) {
    size_t length = strlen(line);
    for (const char *p = text; p != NULL && *p != '\0'; p = strchr(p, '\n'), p = p ? p + 1 : NULL) {
        if (strncmp(p, line, length) == 0 && p[length] == '\n') {
            return p;
        }
    }
    return NULL;
}

static void test_print(void) {
    // The longest are "--color[=arg]" and "--level=<arg>", 13 characters.
    static const char *lines[] = {
        "  -v ............. be verbose",
        "  -o=<arg> ....... write to a file",
        "  --color[=arg] .. use color",
        "Commands:",
        "  add ............ add something",
        "  -n=<arg> ....... a number",
        "  -z[=arg] ....... compress",
        "  -vq ............ not -v -q",
        "Module options:",
        "  --level=<arg> .. set the level",
        "  -q",
    };
    char text[1024];
    FILE *f = tmpfile();
    CHECK(f != NULL);
    sof_copt_error_t err = sof_copt_print(f);
    rewind(f);
    size_t length = fread(text, 1, sizeof(text) - 1, f);
    fclose(f);
    text[length] = '\0';
    CHECK(err == SOF_COPT_OK);
    size_t total = 0;
    for (size_t i = 0; i < sizeof(lines) / sizeof(lines[0]); i++) {
        CHECK(find_line(text, lines[i]) != NULL);
        total += strlen(lines[i]) + 1;
    }
    CHECK(length == total);
    // Each array's lines in order.
    for (size_t i = 1; i < 8; i++) {
        CHECK(find_line(text, lines[i - 1]) < find_line(text, lines[i]));
    }
    CHECK(find_line(text, lines[8]) < find_line(text, lines[9]));
    CHECK(find_line(text, lines[9]) < find_line(text, lines[10]));
}

static void test_print_errors(void) {
    CHECK(sof_copt_print(NULL) == SOF_COPT_ERROR);
    FILE *f = fopen("/dev/null", "r");
    CHECK(f != NULL);
    sof_copt_error_t err = sof_copt_print(f);
    fclose(f);
    CHECK(err == SOF_COPT_ERROR);
}

/// @brief What one sof_copt_parse() left in the table, as the main thread
/// saw it before starting the threads.
typedef struct snapshot_t {
    size_t count;
    sof_copt_t *entries[MAX_ENTRIES];
    const char *values[MAX_ENTRIES];
    bool present[MAX_ENTRIES];
} snapshot_t;

/// @brief One thread's state and results, checked by the main thread after
/// joining, since CHECK isn't safe on other threads.
typedef struct reader_t {
    const snapshot_t *expected;
    uint64_t seed;
    /// @brief The thread's own file for sof_copt_print().
    FILE *out;
    size_t lookups;
    /// @brief Results that differ from the snapshot, and failed prints.
    size_t errors;
} reader_t;

/// @brief Looks the table up LOOKUPS times in random ways, comparing what
/// it finds with the snapshot, then prints the usage message PRINTS times.
static void *read_table(void *arg) {
    reader_t *r = arg;
    const snapshot_t *e = r->expected;
    uint64_t random = r->seed;
    for (size_t n = 0; n < LOOKUPS; n++) {
        uint64_t x = next_random(&random);
        size_t i = (size_t)(x >> 8) % e->count;
        sof_copt_t *entry = e->entries[i];
        int ok = 1;
        switch (x % 4) {
        case 0:
            ok = sof_copt_count() == e->count && sof_copt_get(e->count) == NULL;
            break;
        case 1:
            ok = sof_copt_get(i) == entry;
            break;
        case 2:
            // Entries with a name are found by it; headings have none.
            ok = entry->option == NULL ? sof_copt_find(NULL) == NULL : sof_copt_find(entry->option) == entry;
            ok = ok && sof_copt_find("--no-such-option") == NULL;
            break;
        default:
            ok = entry->value == e->values[i] && entry->present == e->present[i];
            break;
        }
        if (!ok) {
            r->errors++;
        }
        r->lookups++;
    }
    for (size_t n = 0; n < PRINTS; n++) {
        if (sof_copt_print(r->out) != SOF_COPT_OK) {
            r->errors++;
        }
    }
    return NULL;
}

static void test_threads(void) {
    // One parse on the main thread, then threads that only read: they look
    // the table up and print it at once, and must all see what the main
    // thread saw.
    char *argv[] = {"prog", "-v", "-o", "out", "--color=always", "--level=3", "file"};
    calls_t calls = {0};
    CHECK(parse(ARGC(argv), argv, &calls, NULL, NULL) == SOF_COPT_OK);
    static snapshot_t expected;
    expected.count = sof_copt_count();
    CHECK(expected.count > 0 && expected.count <= MAX_ENTRIES);
    for (size_t i = 0; i < expected.count; i++) {
        expected.entries[i] = sof_copt_get(i);
        expected.values[i] = expected.entries[i]->value;
        expected.present[i] = expected.entries[i]->present;
    }
    CHECK(opt_verbose.present && opt_output.value == argv[3] && opt_level.value == argv[5] + 8);
    char text[MAX_TEXT];
    FILE *f = tmpfile();
    CHECK(f != NULL);
    sof_copt_error_t err = sof_copt_print(f);
    rewind(f);
    size_t length = fread(text, 1, sizeof(text), f);
    fclose(f);
    CHECK(err == SOF_COPT_OK);
    CHECK(length > 0 && length < sizeof(text));

    reader_t readers[THREADS] = {{0}};
    pthread_t threads[THREADS];
    size_t opened = 0, started = 0;
    for (; opened < THREADS; opened++) {
        if ((readers[opened].out = tmpfile()) == NULL) {
            break;
        }
    }
    if (opened == THREADS) {
        for (; started < THREADS; started++) {
            readers[started].expected = &expected;
            readers[started].seed = 0x2545f4914f6cdd1dULL + started;
            if (pthread_create(&threads[started], NULL, read_table, &readers[started]) != 0) {
                break;
            }
        }
    }
    size_t joined = 0;
    for (size_t i = 0; i < started; i++) {
        joined += pthread_join(threads[i], NULL) == 0;
    }

    // Each thread's file holds PRINTS copies of the message.
    size_t bad_files = 0;
    char copy[MAX_TEXT];
    for (size_t i = 0; i < started; i++) {
        rewind(readers[i].out);
        size_t copies = 0;
        while (fread(copy, 1, length, readers[i].out) == length && memcmp(copy, text, length) == 0) {
            copies++;
        }
        if (copies != PRINTS || !feof(readers[i].out)) {
            bad_files++;
        }
    }
    for (size_t i = 0; i < opened; i++) {
        fclose(readers[i].out);
    }
    CHECK(opened == THREADS);
    CHECK(started == THREADS);
    CHECK(joined == THREADS);
    for (size_t i = 0; i < THREADS; i++) {
        CHECK(readers[i].errors == 0);
        CHECK(readers[i].lookups == LOOKUPS);
    }
    CHECK(bad_files == 0);
    // Nothing the threads did changed the options.
    for (size_t i = 0; i < expected.count; i++) {
        CHECK(expected.entries[i]->value == expected.values[i]);
        CHECK(expected.entries[i]->present == expected.present[i]);
    }
}

int main(void) {
    printf("sof_copt:\n");
    run("table merges arrays from two files", test_table);
    run("sof_copt_find", test_find);
    run("sof_copt_parse", test_parse);
    run("option arguments", test_arguments);
    run("operands", test_operands);
    run("parse errors", test_errors);
    run("groups of short options", test_groups);
    run("errors in groups", test_group_errors);
    run("options after operands", test_permute);
    run("sof_copt_parse_ordered", test_ordered);
    run("validate stops parsing", test_validate_stops);
    run("missing callbacks", test_missing_callbacks);
    run("each parse starts afresh", test_reset);
    run("sof_copt_print", test_print);
    run("sof_copt_print errors", test_print_errors);
    run("threads reading the table after one parse", test_threads);
    return test_report();
}
