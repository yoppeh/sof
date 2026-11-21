/**
 * @file main.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief Prints an order to build targets in, from a file of what each
 * depends on, or reports a cycle.
 * @details Reads .ini style files, in which each assignment, such as
 * "app = libnet libui", names a target and what it depends on, and prints
 * the targets one to a line, each after everything it depends on.
 *
 *     deps [-h] [-v] [-r] [-t target]... [file]...
 *
 * With no file, or a file named "-", the input is read from standard input.
 * Its options are read with sof_copt, the files parsed with sof_config,
 * and what it does is logged with sof_log when -v is given. It exits with
 * 0 once the order is printed, 1 if there is a cycle, and 2 on any other
 * error.
 * @version 0.0.1
 * @date 2026-11-04
 * @copyright Copyright (c) 2026
 */

#include <stdio.h>
#include <string.h>

#include "sof_alloc.h"
#include "sof_config.h"
#include "sof_copt.h"
#include "sof_log.h"
#include "sof_queue.h"
#include "sof_set.h"
#include "deps.h"

/// @brief The exit codes.
#define EXIT_ORDERED 0
#define EXIT_CYCLE 1
#define EXIT_TROUBLE 2

/// @brief What the options ask for, filled in by their callbacks.
typedef struct settings_t {
    /// @brief The targets given with -t, pointing into argv.
    const char **targets;
    size_t num_targets;
} settings_t;

static int add_assignment(const char *section, const char *key, const char *value, void *ctx);
static int need(sof_set_t *needed, sof_queue_t *queue, deps_node_t *node);
static int print_cycle(const deps_t *graph);
static int read_input(deps_t *graph, const sof_log_t *log, char **paths, int num_paths);
static int select_targets(deps_t *graph, const settings_t *settings, deps_t *chosen);
static int target_validate(sof_copt_t *option, void *ctx);
static void usage(FILE *stream);

static sof_copt_t opt_help = {
    .option = "-h",
    .description = "print this help and exit",
};

static sof_copt_t opt_verbose = {
    .option = "-v",
    .description = "log what is read and found to standard error",
};

static sof_copt_t opt_reverse = {
    .option = "-r",
    .description = "print the order reversed: each target before what it depends on",
};

static sof_copt_t opt_target = {
    .option = "-t",
    .description = "order only this target and what it needs; can be repeated",
    .type = SOF_COPT_ARG_REQUIRED,
    .validate = target_validate,
};

SOF_COPT_OPTIONS static sof_copt_t *options[] = {
    &opt_help,
    &opt_verbose,
    &opt_reverse,
    &opt_target,
};

/// @brief The sof_config callback: adds an assignment's target and list of
/// dependencies to the graph in ctx.
/// @return 0, or the deps_error_t that stopped it, which sof_config keeps
/// in stop_value.
static int add_assignment(const char *section, const char *key, const char *value, void *ctx) {
    (void)section;
    return (int)deps_add_list(ctx, key, value);
}

/// @brief Adds node to the targets needed, and to the queue of those still
/// to visit, unless it is needed already.
/// @return 0, or EXIT_TROUBLE if memory ran out, after printing why.
static int need(sof_set_t *needed, sof_queue_t *queue, deps_node_t *node) {
    int created;
    if (sof_set_add(needed, node, &created) == NULL || (created && sof_queue_enqueue(queue, node) != SOF_QUEUE_OK)) {
        fprintf(stderr, "out of memory\n");
        return EXIT_TROUBLE;
    }
    return 0;
}

/// @brief Prints a cycle in the graph to standard error, as
/// "cycle: a -> b -> a", each target depending on the next.
/// @return EXIT_CYCLE, or EXIT_TROUBLE if memory ran out.
static int print_cycle(const deps_t *graph) {
    deps_node_t **cycle = sof_alloc_calloc(NULL, graph->count, sizeof(deps_node_t *));
    size_t length;
    if (cycle == NULL || deps_cycle(graph, cycle, &length) != DEPS_OK) {
        // The sort found a cycle, so only memory can be missing.
        sof_alloc_free(NULL, cycle, graph->count * sizeof(deps_node_t *));
        fprintf(stderr, "out of memory\n");
        return EXIT_TROUBLE;
    }
    fprintf(stderr, "cycle:");
    for (size_t i = 0; i < length; i++) {
        fprintf(stderr, " %s ->", cycle[i]->key);
    }
    fprintf(stderr, " %s\n", cycle[0]->key);
    sof_alloc_free(NULL, cycle, graph->count * sizeof(deps_node_t *));
    return EXIT_CYCLE;
}

/// @brief Parses the files, or standard input if there are none, into the
/// graph.
/// @return 0, or EXIT_TROUBLE if a file couldn't be read or parsed, or memory
/// ran out, after printing why.
static int read_input(deps_t *graph, const sof_log_t *log, char **paths, int num_paths) {
    sof_config_t config;
    if (sof_config_init(&config, add_assignment, graph, NULL) != SOF_CONFIG_OK) {
        fprintf(stderr, "out of memory\n");
        return EXIT_TROUBLE;
    }
    static char *standard_input[] = {"-"};
    if (num_paths == 0) {
        paths = standard_input;
        num_paths = 1;
    }
    int rc = 0;
    for (int i = 0; i < num_paths && rc == 0; i++) {
        int is_stdin = strcmp(paths[i], "-") == 0;
        const char *name = is_stdin ? "standard input" : paths[i];
        size_t before = graph->count, edges = graph->edges;
        sof_config_error_t err = is_stdin ? sof_config_parse_file(&config, stdin) : sof_config_parse(&config, paths[i]);
        switch (err) {
        case SOF_CONFIG_OK:
            sof_log_info(log, "%s: %zu new targets, %zu new dependencies", name, graph->count - before, graph->edges - edges);
            break;
        case SOF_CONFIG_STOPPED:
            fprintf(stderr, "out of memory reading %s\n", name);
            rc = EXIT_TROUBLE;
            break;
        case SOF_CONFIG_NO_OPEN:
        case SOF_CONFIG_NO_READ:
        case SOF_CONFIG_NO_MEM:
            fprintf(stderr, "%s: %s\n", name, sof_config_error_string(err));
            rc = EXIT_TROUBLE;
            break;
        default:
            fprintf(stderr, "%s:%zu:%zu: %s\n", name, config.line, config.column, sof_config_error_string(err));
            rc = EXIT_TROUBLE;
            break;
        }
        // The graph has what it needs from the assignments.
        sof_config_clear(&config);
    }
    sof_config_free(&config);
    return rc;
}

/// @brief Copies the targets given with -t, and everything they need, from
/// graph into chosen, which is initialized here.
/// @details The targets needed are found by a walk of what they depend on,
/// with a sof_set of the targets needed, compared by pointer, and a
/// sof_queue of those still to visit. They are then added to chosen in their
/// order in graph, and
/// their dependencies after them in the order they were added, so chosen
/// orders them as graph would.
/// @return 0, or EXIT_TROUBLE if a target isn't in the graph or memory ran
/// out, after printing why.
static int select_targets(deps_t *graph, const settings_t *settings, deps_t *chosen) {
    if (deps_init(chosen, NULL) != DEPS_OK) {
        fprintf(stderr, "out of memory\n");
        return EXIT_TROUBLE;
    }
    sof_set_t needed;
    if (sof_set_init(&needed, NULL, NULL, 0, NULL) != SOF_SET_OK) {
        fprintf(stderr, "out of memory\n");
        deps_free(chosen);
        return EXIT_TROUBLE;
    }
    sof_queue_t queue;
    sof_queue_init(&queue, NULL, NULL, NULL);
    int rc = 0;
    for (size_t i = 0; i < settings->num_targets && rc == 0; i++) {
        deps_node_t *node = deps_find(graph, settings->targets[i]);
        if (node == NULL) {
            fprintf(stderr, "no target %s\n", settings->targets[i]);
            rc = EXIT_TROUBLE;
        } else {
            rc = need(&needed, &queue, node);
        }
    }
    void *data;
    while (rc == 0 && sof_queue_dequeue(&queue, &data) == SOF_QUEUE_OK) {
        deps_node_t *node = data;
        for (size_t i = 0; i < node->dep_count && rc == 0; i++) {
            rc = need(&needed, &queue, node->deps[i]);
        }
    }
    sof_queue_free(&queue);
    for (size_t i = 0; i < graph->count && rc == 0; i++) {
        if (sof_set_find(&needed, graph->nodes[i]) != NULL && deps_add(chosen, graph->nodes[i]->key, NULL) == NULL) {
            fprintf(stderr, "out of memory\n");
            rc = EXIT_TROUBLE;
        }
    }
    for (size_t i = 0; i < graph->count && rc == 0; i++) {
        deps_node_t *node = graph->nodes[i];
        int is_needed = sof_set_find(&needed, node) != NULL;
        for (size_t j = 0; is_needed && j < node->dep_count && rc == 0; j++) {
            if (deps_depend(chosen, node->key, node->deps[j]->key) != DEPS_OK) {
                fprintf(stderr, "out of memory\n");
                rc = EXIT_TROUBLE;
            }
        }
    }
    sof_set_free(&needed);
    if (rc != 0) {
        deps_free(chosen);
    }
    return rc;
}

/// @brief Adds the argument of -t, each time it is given, to the targets to
/// order.
static int target_validate(sof_copt_t *option, void *ctx) {
    settings_t *settings = ctx;
    settings->targets[settings->num_targets++] = option->value;
    return 0;
}

/// @brief Prints the usage message, with the options sof_copt knows.
static void usage(FILE *stream) {
    fprintf(stream, "usage: deps [-h] [-v] [-r] [-t target]... [file]...\n");
    sof_copt_print(stream);
    fprintf(stream,
        "Reads the files, or standard input if there are none or a file is \"-\":\n"
        "lines of \"target = dependency...\" in .ini syntax. Prints the targets one\n"
        "to a line, each after everything it depends on, or the targets in a cycle.\n"
        "Exits with 0 once the order is printed, 1 for a cycle, 2 on any other error.\n");
}

int main(int ac, char **av) {
    // Every argument could be a -t, so there is room for that many targets.
    settings_t settings = {sof_alloc_calloc(NULL, (size_t)ac, sizeof(char *)), 0};
    if (settings.targets == NULL) {
        fprintf(stderr, "out of memory\n");
        return EXIT_TROUBLE;
    }
    int index, rc = EXIT_TROUBLE;
    sof_copt_t *option;
    sof_copt_error_t err = sof_copt_parse(ac, av, &settings, &index, &option);
    switch (err) {
    case SOF_COPT_OK:
        break;
    case SOF_COPT_NO_MATCH:
        fprintf(stderr, "unknown option %s\n", av[index]);
        usage(stderr);
        goto done;
    case SOF_COPT_MISSING_ARGUMENT:
        fprintf(stderr, "%s needs an argument\n", option->option);
        usage(stderr);
        goto done;
    case SOF_COPT_UNEXPECTED_ARGUMENT:
        fprintf(stderr, "%s takes no argument\n", option->option);
        usage(stderr);
        goto done;
    default:
        fprintf(stderr, "can't read the options (error %d)\n", (int)err);
        goto done;
    }
    if (opt_help.present) {
        usage(stdout);
        rc = EXIT_ORDERED;
        goto done;
    }

    sof_log_t log;
    if (sof_log_init(&log, stderr, opt_verbose.present ? SOF_LOG_LEVEL_INFO : SOF_LOG_LEVEL_WARN, "deps", NULL) != SOF_LOG_OK) {
        fprintf(stderr, "out of memory\n");
        goto done;
    }
    deps_t graph, chosen;
    deps_t *sorting = &graph;
    deps_node_t **order = NULL;
    size_t count = 0;
    if (deps_init(&graph, NULL) != DEPS_OK) {
        fprintf(stderr, "out of memory\n");
        goto free_log;
    }
    if (read_input(&graph, &log, av + index, ac - index) != 0) {
        goto free_graph;
    }
    sof_log_info(&log, "%zu targets, %zu dependencies", graph.count, graph.edges);
    if (settings.num_targets > 0) {
        if (select_targets(&graph, &settings, &chosen) != 0) {
            goto free_graph;
        }
        sorting = &chosen;
        sof_log_info(&log, "%zu targets needed by those given with -t", chosen.count);
    }
    order = sof_alloc_calloc(NULL, sorting->count == 0 ? 1 : sorting->count, sizeof(deps_node_t *));
    deps_error_t sorted = order == NULL ? DEPS_NO_MEM : deps_sort(sorting, order, &count);
    if (sorted == DEPS_OK) {
        for (size_t i = 0; i < count; i++) {
            printf("%s\n", order[opt_reverse.present ? count - 1 - i : i]->key);
        }
        rc = EXIT_ORDERED;
    } else if (sorted == DEPS_CYCLE) {
        sof_log_info(&log, "%zu of %zu targets can't be ordered", sorting->count - count, sorting->count);
        rc = print_cycle(sorting);
    } else {
        fprintf(stderr, "out of memory\n");
    }
    sof_alloc_free(NULL, order, (sorting->count == 0 ? 1 : sorting->count) * sizeof(deps_node_t *));
    if (sorting == &chosen) {
        deps_free(&chosen);
    }
free_graph:
    deps_free(&graph);
free_log:
    sof_log_free(&log);
done:
    sof_alloc_free(NULL, settings.targets, (size_t)ac * sizeof(char *));
    return rc;
}
