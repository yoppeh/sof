/**
 * @file main.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief Simulates customers queuing for tellers at a bank, and reports
 * their waits and the length of the line.
 * @details Reads the bank's setup from a config file, or standard input,
 * with any of it overridden on the command line, runs the simulation with
 * bank.c, and prints what happened.
 *
 *     bank [-h] [-v] [-t tellers] [-a minutes] [-s minutes] [-o minutes] [-r seed] [config]
 *
 * Its options are read with sof_copt and its config file with sof_config.
 * Times are given and printed in minutes, and simulated in milliseconds.
 * @version 0.0.1
 * @date 2026-11-04
 * @copyright Copyright (c) 2026
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bank.h"
#include "sof_config.h"
#include "sof_copt.h"

/// @brief Milliseconds in a minute.
#define MINUTE 60000

/// @brief The most tellers the program accepts.
#define MAX_TELLERS 100000

/// @brief A config file's section and key.
typedef struct setting_t {
    const char *section;
    const char *key;
} setting_t;

/// @brief The bank's setup, in the units it is given in: from the
/// defaults, then the config file, then the options.
typedef struct settings_t {
    int64_t tellers;
    double open;
    int64_t seed;
    double arrival;
    bank_draw_t arrival_draw;
    double service;
    bank_draw_t service_draw;
} settings_t;

static int check_keys(const sof_config_entry_t *entry, void *ctx);
static const char *draw_name(bank_draw_t draw);
static int minutes_validate(sof_copt_t *option, void *ctx);
static const char *parse_minutes(const char *s, double *minutes);
static int print_event(const bank_event_t *event, void *ctx);
static int read_config(settings_t *settings, const char *path);
static int read_draw(const sof_config_t *config, const char *path, const char *section, bank_draw_t *draw);
static int read_int(const sof_config_t *config, const char *path, const char *section, const char *key, int64_t *value);
static int read_minutes(const sof_config_t *config, const char *path, const char *section, double *minutes);
static void report(const bank_t *b, const settings_t *settings);
static int seed_validate(sof_copt_t *option, void *ctx);
static int tellers_validate(sof_copt_t *option, void *ctx);
static int to_options(const settings_t *settings, bank_options_t *options);
static int to_ticks(double minutes, uint64_t *ticks);
static void usage(FILE *stream);

/// @brief A code the validate callbacks return, which sof_copt keeps in the
/// option's stop_value: its argument is wrong. The callback has printed why.
#define BAD_ARGUMENT 1

/// @brief The settings a config file can hold.
static const setting_t known[] = {
    {"bank", "tellers"},
    {"bank", "open"},
    {"bank", "seed"},
    {"arrivals", "mean"},
    {"arrivals", "draw"},
    {"service", "mean"},
    {"service", "draw"},
};

static sof_copt_t opt_help = {
    .option = "-h",
    .description = "print this help and exit",
};

static sof_copt_t opt_verbose = {
    .option = "-v",
    .description = "print every arrival, call and departure",
};

static sof_copt_t opt_tellers = {
    .option = "-t",
    .description = "the number of tellers",
    .type = SOF_COPT_ARG_REQUIRED,
    .validate = tellers_validate,
};

static sof_copt_t opt_arrival = {
    .option = "-a",
    .description = "the mean minutes between arrivals",
    .type = SOF_COPT_ARG_REQUIRED,
    .validate = minutes_validate,
};

static sof_copt_t opt_service = {
    .option = "-s",
    .description = "the mean minutes a customer takes to serve",
    .type = SOF_COPT_ARG_REQUIRED,
    .validate = minutes_validate,
};

static sof_copt_t opt_open = {
    .option = "-o",
    .description = "the minutes the doors are open",
    .type = SOF_COPT_ARG_REQUIRED,
    .validate = minutes_validate,
};

static sof_copt_t opt_seed = {
    .option = "-r",
    .description = "the seed of the random draws",
    .type = SOF_COPT_ARG_REQUIRED,
    .validate = seed_validate,
};

SOF_COPT_OPTIONS static sof_copt_t *options[] = {
    &opt_help,
    &opt_verbose,
    &opt_tellers,
    &opt_arrival,
    &opt_service,
    &opt_open,
    &opt_seed,
};

/// @brief Fails, after printing why, for an entry the program doesn't read.
static int check_keys(const sof_config_entry_t *entry, void *ctx) {
    const char *path = ctx;
    for (size_t i = 0; i < sizeof(known) / sizeof(known[0]); i++) {
        if (entry->section != NULL && strcmp(entry->section, known[i].section) == 0 && strcmp(entry->key, known[i].key) == 0) {
            return 0;
        }
    }
    fprintf(stderr, "%s:%zu: unknown setting %s%s%s%s\n", path, entry->line, entry->section == NULL ? "" : "[", entry->section == NULL ? "" : entry->section, entry->section == NULL ? "" : "] ", entry->key);
    return 1;
}

/// @brief Returns the name of a draw, as a config file gives it.
static const char *draw_name(bank_draw_t draw) {
    return draw == BANK_FIXED ? "fixed" : "exponential";
}

/// @brief Reads the argument of -a, -s or -o.
static int minutes_validate(sof_copt_t *option, void *ctx) {
    settings_t *settings = ctx;
    double minutes;
    const char *end = parse_minutes(option->value, &minutes);
    uint64_t ticks;
    if (end == NULL || *end != '\0' || !to_ticks(minutes, &ticks) || (option == &opt_arrival && ticks == 0)) {
        fprintf(stderr, "%s needs a number of minutes%s, not \"%s\"\n", option->option, option == &opt_arrival ? " above 0" : "", option->value);
        return BAD_ARGUMENT;
    }
    if (option == &opt_arrival) {
        settings->arrival = minutes;
    } else if (option == &opt_service) {
        settings->service = minutes;
    } else {
        settings->open = minutes;
    }
    return 0;
}

/// @brief Reads a decimal number of minutes from the start of s.
/// @return Where the number ends, or NULL if s doesn't start with one.
static const char *parse_minutes(const char *s, double *minutes) {
    if ((*s < '0' || *s > '9') && *s != '.') {
        return NULL;
    }
    char *end;
    errno = 0;
    *minutes = strtod(s, &end);
    return errno != 0 || end == s ? NULL : end;
}

/// @brief Prints an event, for -v.
static int print_event(const bank_event_t *event, void *ctx) {
    (void)ctx;
    double minutes = (double)event->time / MINUTE;
    switch (event->type) {
    case BANK_ARRIVAL:
        printf("%9.2f  customer %llu arrives; %zu waiting\n", minutes, (unsigned long long)event->customer, event->waiting);
        break;
    case BANK_START:
        printf("%9.2f  teller %zu calls customer %llu, who waited %.2f\n", minutes, event->teller, (unsigned long long)event->customer, (double)event->wait / MINUTE);
        break;
    case BANK_DEPARTURE:
        printf("%9.2f  customer %llu leaves teller %zu after %.2f\n", minutes, (unsigned long long)event->customer, event->teller, (double)event->service / MINUTE);
        break;
    }
    return 0;
}

/// @brief Reads the settings in a config file, or standard input for "-".
/// @return 0, or 1 after printing why the file couldn't be read or a setting
/// is wrong.
static int read_config(settings_t *settings, const char *path) {
    sof_config_t config;
    if (sof_config_init(&config, NULL, NULL, NULL) != SOF_CONFIG_OK) {
        fprintf(stderr, "out of memory\n");
        return 1;
    }
    int is_stdin = strcmp(path, "-") == 0;
    const char *name = is_stdin ? "standard input" : path;
    sof_config_error_t err = is_stdin ? sof_config_parse_file(&config, stdin) : sof_config_parse(&config, path);
    int rc = 0;
    if (err == SOF_CONFIG_NO_OPEN) {
        fprintf(stderr, "error opening %s: %s\n", path, strerror(errno));
        rc = 1;
    } else if (err != SOF_CONFIG_OK) {
        fprintf(stderr, "%s:%zu:%zu: %s\n", name, config.line, config.column, sof_config_error_string(err));
        rc = 1;
    }
    if (rc == 0 && sof_config_traverse(&config, check_keys, (void *)name) != 0) {
        rc = 1;
    }
    if (rc == 0) {
        rc = read_int(&config, name, "bank", "tellers", &settings->tellers) || read_minutes(&config, name, "bank", &settings->open) || read_int(&config, name, "bank", "seed", &settings->seed) || read_minutes(&config, name, "arrivals", &settings->arrival) || read_draw(&config, name, "arrivals", &settings->arrival_draw) || read_minutes(&config, name, "service", &settings->service) || read_draw(&config, name, "service", &settings->service_draw);
    }
    sof_config_free(&config);
    return rc;
}

/// @brief Reads the draw of a section, if it is set.
/// @return 0, or 1 after printing why it is wrong.
static int read_draw(const sof_config_t *config, const char *path, const char *section, bank_draw_t *draw) {
    const sof_config_entry_t *entry = sof_config_find(config, section, "draw");
    if (entry == NULL) {
        return 0;
    }
    if (strcmp(entry->value, "exponential") == 0) {
        *draw = BANK_EXPONENTIAL;
    } else if (strcmp(entry->value, "fixed") == 0) {
        *draw = BANK_FIXED;
    } else {
        fprintf(stderr, "%s:%zu: [%s] draw must be exponential or fixed, not \"%s\"\n", path, entry->line, section, entry->value);
        return 1;
    }
    return 0;
}

/// @brief Reads an integer, if it is set.
/// @return 0, or 1 after printing why it is wrong.
static int read_int(const sof_config_t *config, const char *path, const char *section, const char *key, int64_t *value) {
    if (sof_config_get_int(config, section, key, value) == SOF_CONFIG_BAD_VALUE) {
        const sof_config_entry_t *entry = sof_config_find(config, section, key);
        fprintf(stderr, "%s:%zu: [%s] %s must be a whole number, not \"%s\"\n", path, entry->line, section, key, entry->value);
        return 1;
    }
    return 0;
}

/// @brief Reads the minutes of a section, if they are set: its mean, or for
/// [bank], how long it is open.
/// @return 0, or 1 after printing why they are wrong.
static int read_minutes(const sof_config_t *config, const char *path, const char *section, double *minutes) {
    const char *key = strcmp(section, "bank") == 0 ? "open" : "mean";
    if (sof_config_get_double(config, section, key, minutes) == SOF_CONFIG_BAD_VALUE) {
        const sof_config_entry_t *entry = sof_config_find(config, section, key);
        fprintf(stderr, "%s:%zu: [%s] %s must be a number of minutes, not \"%s\"\n", path, entry->line, section, key, entry->value);
        return 1;
    }
    return 0;
}

/// @brief Prints the setup and the results of a finished run.
static void report(const bank_t *b, const settings_t *settings) {
    const bank_options_t *o = &b->options;
    printf("tellers:        %zu\n", o->tellers);
    printf("open:           %.2f minutes\n", (double)o->length / MINUTE);
    printf("arrivals:       %s, a mean of %.2f minutes apart\n", draw_name(o->arrival_draw), (double)o->arrival / MINUTE);
    printf("service:        %s, a mean of %.2f minutes\n", draw_name(o->service_draw), (double)o->service / MINUTE);
    printf("seed:           %lld\n", (long long)settings->seed);
    printf("\n");
    printf("customers:      %llu\n", (unsigned long long)b->served);
    if (b->served == 0) {
        return;
    }
    printf("waited:         %llu (%.1f%%)\n", (unsigned long long)b->waited, 100.0 * (double)b->waited / (double)b->served);
    printf("mean wait:      %.2f minutes\n", (double)b->wait_total / (double)b->served / MINUTE);
    if (b->waited > 0) {
        printf("  if they did:  %.2f minutes\n", (double)b->wait_total / (double)b->waited / MINUTE);
    }
    printf("longest wait:   %.2f minutes\n", (double)b->wait_max / MINUTE);
    printf("mean service:   %.2f minutes\n", (double)b->service_total / (double)b->served / MINUTE);
    printf("mean line:      %.2f\n", b->now == 0 ? 0.0 : (double)b->waiting_area / (double)b->now);
    printf("longest line:   %zu\n", b->waiting_max);
    printf("tellers busy:   %.1f%%\n", b->now == 0 ? 0.0 : 100.0 * (double)b->busy_area / ((double)b->now * (double)o->tellers));
    printf("last left at:   %.2f minutes\n", (double)b->now / MINUTE);
}

/// @brief Reads the argument of -r.
static int seed_validate(sof_copt_t *option, void *ctx) {
    settings_t *settings = ctx;
    char *end;
    errno = 0;
    long long seed = strtoll(option->value, &end, 10);
    if (errno != 0 || end == option->value || *end != '\0') {
        fprintf(stderr, "%s needs a whole number, not \"%s\"\n", option->option, option->value);
        return BAD_ARGUMENT;
    }
    settings->seed = seed;
    return 0;
}

/// @brief Reads the argument of -t.
static int tellers_validate(sof_copt_t *option, void *ctx) {
    settings_t *settings = ctx;
    char *end;
    errno = 0;
    long long tellers = strtoll(option->value, &end, 10);
    if (errno != 0 || end == option->value || *end != '\0' || tellers < 1 || tellers > MAX_TELLERS) {
        fprintf(stderr, "%s needs a number of tellers from 1 to %d, not \"%s\"\n", option->option, MAX_TELLERS, option->value);
        return BAD_ARGUMENT;
    }
    settings->tellers = tellers;
    return 0;
}

/// @brief Turns the settings into the bank's options.
/// @return 0, or 1 after printing which setting is out of range.
static int to_options(const settings_t *settings, bank_options_t *options) {
    options->arrival_draw = settings->arrival_draw;
    options->service_draw = settings->service_draw;
    options->seed = (uint64_t)settings->seed;
    if (settings->tellers < 1 || settings->tellers > MAX_TELLERS) {
        fprintf(stderr, "the tellers must be from 1 to %d, not %lld\n", MAX_TELLERS, (long long)settings->tellers);
        return 1;
    }
    options->tellers = (size_t)settings->tellers;
    if (!to_ticks(settings->arrival, &options->arrival) || options->arrival == 0) {
        fprintf(stderr, "the mean minutes between arrivals must be above 0, and not too large\n");
        return 1;
    }
    if (!to_ticks(settings->service, &options->service)) {
        fprintf(stderr, "the mean minutes of service must be 0 or more, and not too large\n");
        return 1;
    }
    if (!to_ticks(settings->open, &options->length)) {
        fprintf(stderr, "the minutes the doors are open must be 0 or more, and not too large\n");
        return 1;
    }
    return 0;
}

/// @brief Turns minutes into milliseconds, rounded.
/// @details The multiplication and the addition are kept in separate
/// statements so no compiler fuses them into one rounding, which would give
/// different times on different machines.
/// @return 1, or 0 if minutes is negative, not a number, or more than
/// BANK_TIME_MAX milliseconds.
static int to_ticks(double minutes, uint64_t *ticks) {
    if (!(minutes >= 0) || minutes > (double)BANK_TIME_MAX / MINUTE) {
        return 0;
    }
    double ms = minutes * MINUTE;
    ms += 0.5;
    *ticks = (uint64_t)ms;
    return *ticks <= BANK_TIME_MAX;
}

/// @brief Prints the usage message, with the options sof_copt knows.
static void usage(FILE *stream) {
    fprintf(stream, "usage: bank [-h] [-v] [-t tellers] [-a minutes] [-s minutes] [-o minutes] [-r seed] [config]\n");
    sof_copt_print(stream);
    fprintf(stream,
        "Simulates customers queuing for tellers at a bank, set up by the config\n"
        "file (standard input if it is \"-\") and then the options, and prints\n"
        "their waits and the length of the line. The config file can set:\n"
        "  [bank]      tellers, open (minutes), seed\n"
        "  [arrivals]  mean (minutes apart), draw (exponential or fixed)\n"
        "  [service]   mean (minutes), draw (exponential or fixed)\n");
}

int main(int ac, char **av) {
    settings_t settings = {3, 480, 1, 1, BANK_EXPONENTIAL, 2.5, BANK_EXPONENTIAL};
    // What the options give, used only for those that are present.
    settings_t given = settings;
    int index;
    sof_copt_t *option;
    sof_copt_error_t err = sof_copt_parse(ac, av, &given, &index, &option);
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
    if (ac - index > 1) {
        fprintf(stderr, "only one config file can be given\n");
        usage(stderr);
        return 2;
    }
    if (index < ac && read_config(&settings, av[index]) != 0) {
        return 1;
    }
    // The options override the config file.
    if (opt_tellers.present) {
        settings.tellers = given.tellers;
    }
    if (opt_arrival.present) {
        settings.arrival = given.arrival;
    }
    if (opt_service.present) {
        settings.service = given.service;
    }
    if (opt_open.present) {
        settings.open = given.open;
    }
    if (opt_seed.present) {
        settings.seed = given.seed;
    }
    bank_options_t options;
    if (to_options(&settings, &options) != 0) {
        return 1;
    }
    bank_t b;
    if (bank_init(&b, &options, NULL) != BANK_OK) {
        fprintf(stderr, "out of memory\n");
        return 1;
    }
    int rc = 0;
    if (bank_run(&b, opt_verbose.present ? print_event : NULL, NULL) != BANK_OK) {
        fprintf(stderr, "out of memory\n");
        rc = 1;
    } else {
        if (opt_verbose.present) {
            printf("\n");
        }
        report(&b, &settings);
    }
    bank_free(&b);
    return rc;
}
