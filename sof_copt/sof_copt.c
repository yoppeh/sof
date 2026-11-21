/**
 * @file sof_copt.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief Command line options defined in any module, merged at link time.
 * @details Every SOF_COPT_OPTIONS array is placed in a section named
 * sof_copt, and the linker puts them one after another. On ELF platforms it
 * defines __start_sof_copt and __stop_sof_copt around a section whose name
 * is a c identifier; Apple's linker does the same for
 * section$start$__DATA$sof_copt and section$end$__DATA$sof_copt. The
 * section is walked as one array of pointers, skipping NULL entries: those that applications add, and any
 * zero padding the compiler or linker leaves between arrays to align them.
 *
 * sof_copt_parse() keeps the options it has read, and their arguments, at
 * the front of argv: each one found after an operand is moved down past the
 * operands, which keep their order, so when it is done the operands are
 * together at the end.
 * @version 0.0.1
 * @date 2026-11-03
 * @copyright Copyright (c) 2026
 */

#include <string.h>

#include "sof_copt.h"

#ifdef __APPLE__
extern sof_copt_t *const sof_copt_start[] __asm("section$start$__DATA$sof_copt");
extern sof_copt_t *const sof_copt_stop[] __asm("section$end$__DATA$sof_copt");
#else
extern sof_copt_t *const __start_sof_copt[];
extern sof_copt_t *const __stop_sof_copt[];
#define sof_copt_start __start_sof_copt
#define sof_copt_stop __stop_sof_copt
#endif

static sof_copt_error_t sof_copt_argument(int argc, char *const argv[], int i, void *ctx, int *used, sof_copt_t **opt);
static sof_copt_error_t sof_copt_group(int argc, char *const argv[], int i, void *ctx, int *used, sof_copt_t **opt);
static sof_copt_t *sof_copt_lookup(const char *name, size_t length);
static void sof_copt_move(char **argv, int to, int from, int count);
static sof_copt_error_t sof_copt_run(int argc, char **argv, bool permute, void *ctx, int *index, sof_copt_t **option);
static sof_copt_t *const *sof_copt_skip(sof_copt_t *const *p);
static sof_copt_error_t sof_copt_take(sof_copt_t *opt, const char *value, void *ctx);

/// @brief Makes sure the section exists, so the program links even if it
/// defines no options.
SOF_COPT_OPTIONS static sof_copt_t *sof_copt_none[] = {NULL};

size_t sof_copt_count(void) {
    size_t count = 0;
    for (sof_copt_t *const *p = sof_copt_skip(sof_copt_start); p < sof_copt_stop; p = sof_copt_skip(p + 1)) {
        count++;
    }
    return count;
}

sof_copt_t *sof_copt_find(const char *name) {
    if (name == NULL) {
        return NULL;
    }
    return sof_copt_lookup(name, strlen(name));
}

sof_copt_t *sof_copt_get(size_t index) {
    for (sof_copt_t *const *p = sof_copt_skip(sof_copt_start); p < sof_copt_stop; p = sof_copt_skip(p + 1)) {
        if (index == 0) {
            return *p;
        }
        index--;
    }
    return NULL;
}

sof_copt_error_t sof_copt_parse(int argc, char *argv[], void *ctx, int *index, sof_copt_t **option) {
    return sof_copt_run(argc, argv, true, ctx, index, option);
}

sof_copt_error_t sof_copt_parse_ordered(int argc, char *const argv[], void *ctx, int *index, sof_copt_t **option) {
    // Without permuting, sof_copt_run() never moves an argument, so argv
    // isn't written through the cast.
    return sof_copt_run(argc, (char **)argv, false, ctx, index, option);
}

sof_copt_error_t sof_copt_print(FILE *stream) {
    sof_copt_t *const *p;
    size_t width = 0;
    if (stream == NULL) {
        return SOF_COPT_ERROR;
    }
    for (p = sof_copt_skip(sof_copt_start); p < sof_copt_stop; p = sof_copt_skip(p + 1)) {
        if ((*p)->option != NULL) {
            size_t length = strlen((*p)->option) + ((*p)->type == SOF_COPT_ARG_NONE ? 0 : 6);
            if (length > width) {
                width = length;
            }
        }
    }
    for (p = sof_copt_skip(sof_copt_start); p < sof_copt_stop; p = sof_copt_skip(p + 1)) {
        const sof_copt_t *opt = *p;
        if (opt->option == NULL) {
            if (opt->description != NULL) {
                fprintf(stream, "%s\n", opt->description);
            }
            continue;
        }
        size_t length = strlen(opt->option);
        fprintf(stream, "  %s", opt->option);
        if (opt->type == SOF_COPT_ARG_REQUIRED) {
            fputs("=<arg>", stream);
            length += 6;
        } else if (opt->type == SOF_COPT_ARG_OPTIONAL) {
            fputs("[=arg]", stream);
            length += 6;
        }
        if (opt->description != NULL) {
            fputc(' ', stream);
            for (size_t dots = width - length + 2; dots > 0; dots--) {
                fputc('.', stream);
            }
            fprintf(stream, " %s", opt->description);
        }
        fputc('\n', stream);
    }
    return ferror(stream) ? SOF_COPT_ERROR : SOF_COPT_OK;
}

/// @brief Reads the option, or group of options, at argv[i].
/// @param used Set to the number of arguments taken: 1, or 2 when an option's
/// argument is the next one; or 0 if argv[i] is an operand.
/// @param opt Set to the option last found, which on an error is the option
/// the error was found with, or NULL.
/// @return SOF_COPT_OK or the error that stops parsing.
static sof_copt_error_t sof_copt_argument(int argc, char *const argv[], int i, void *ctx, int *used, sof_copt_t **opt) {
    const char *arg = argv[i];
    const char *equal = strchr(arg, '=');
    size_t length = equal != NULL ? (size_t)(equal - arg) : strlen(arg);
    *used = 1;
    *opt = sof_copt_lookup(arg, length);
    if (*opt == NULL) {
        if (arg[0] != '-' || arg[1] == '\0') {
            *used = 0;
            return SOF_COPT_OK;
        }
        if (arg[1] == '-') {
            return SOF_COPT_NO_MATCH;
        }
        return sof_copt_group(argc, argv, i, ctx, used, opt);
    }
    switch ((*opt)->type) {
    case SOF_COPT_ARG_NONE:
        if (equal != NULL) {
            (*opt)->present = true;
            return SOF_COPT_UNEXPECTED_ARGUMENT;
        }
        return sof_copt_take(*opt, NULL, ctx);
    case SOF_COPT_ARG_OPTIONAL:
        return sof_copt_take(*opt, equal != NULL ? equal + 1 : NULL, ctx);
    case SOF_COPT_ARG_REQUIRED:
        if (equal != NULL) {
            return sof_copt_take(*opt, equal + 1, ctx);
        }
        if (i + 1 < argc && argv[i + 1] != NULL) {
            *used = 2;
            return sof_copt_take(*opt, argv[i + 1], ctx);
        }
        (*opt)->present = true;
        return SOF_COPT_MISSING_ARGUMENT;
    default:
        (*opt)->present = true;
        return SOF_COPT_ERROR;
    }
}

/// @brief Reads argv[i], which starts with a single '-' and isn't an option's
/// name, as a group of one-character options: "-in" is "-i -n".
/// @details An option that takes an argument takes the rest of the group,
/// after an '=' if it starts with one ("-n5", "-in5", "-in=5"). If nothing is
/// left, a SOF_COPT_ARG_REQUIRED option takes the next argument and a
/// SOF_COPT_ARG_OPTIONAL one has none. Arguments as for sof_copt_argument().
static sof_copt_error_t sof_copt_group(int argc, char *const argv[], int i, void *ctx, int *used, sof_copt_t **opt) {
    const char *arg = argv[i];
    char name[3] = {'-', '\0', '\0'};
    for (size_t j = 1; arg[j] != '\0'; j++) {
        name[1] = arg[j];
        *opt = sof_copt_lookup(name, 2);
        if (*opt == NULL) {
            return SOF_COPT_NO_MATCH;
        }
        const char *rest = arg + j + 1;
        bool equal = *rest == '=';
        if (equal) {
            rest++;
        }
        switch ((*opt)->type) {
        case SOF_COPT_ARG_NONE: {
            if (equal) {
                (*opt)->present = true;
                return SOF_COPT_UNEXPECTED_ARGUMENT;
            }
            sof_copt_error_t err = sof_copt_take(*opt, NULL, ctx);
            if (err != SOF_COPT_OK) {
                return err;
            }
            break;
        }
        case SOF_COPT_ARG_OPTIONAL:
            return sof_copt_take(*opt, equal || *rest != '\0' ? rest : NULL, ctx);
        case SOF_COPT_ARG_REQUIRED:
            if (equal || *rest != '\0') {
                return sof_copt_take(*opt, rest, ctx);
            }
            if (i + 1 < argc && argv[i + 1] != NULL) {
                *used = 2;
                return sof_copt_take(*opt, argv[i + 1], ctx);
            }
            (*opt)->present = true;
            return SOF_COPT_MISSING_ARGUMENT;
        default:
            (*opt)->present = true;
            return SOF_COPT_ERROR;
        }
    }
    return SOF_COPT_OK;
}

/// @brief Returns the first option named by the length characters at name.
static sof_copt_t *sof_copt_lookup(const char *name, size_t length) {
    for (sof_copt_t *const *p = sof_copt_skip(sof_copt_start); p < sof_copt_stop; p = sof_copt_skip(p + 1)) {
        const char *option = (*p)->option;
        if (option != NULL && strncmp(option, name, length) == 0 && option[length] == '\0') {
            return *p;
        }
    }
    return NULL;
}

/// @brief Moves the count arguments (1 or 2) at argv[from] down to argv[to],
/// and those from argv[to] to argv[from - 1] up past them, in order.
static void sof_copt_move(char **argv, int to, int from, int count) {
    if (to < from) {
        char *moving[2] = {argv[from], count > 1 ? argv[from + 1] : NULL};
        memmove(&argv[to + count], &argv[to], (size_t)(from - to) * sizeof(argv[0]));
        for (int k = 0; k < count; k++) {
            argv[to + k] = moving[k];
        }
    }
}

/// @brief Parses the command line, for sof_copt_parse() if permute is true
/// and for sof_copt_parse_ordered() if not. Arguments as for those.
static sof_copt_error_t sof_copt_run(int argc, char **argv, bool permute, void *ctx, int *index, sof_copt_t **option) {
    sof_copt_t *const *p;
    sof_copt_error_t err = SOF_COPT_OK;
    sof_copt_t *opt = NULL;
    // argv[next] is where the next option goes; argv[i] is the next to read.
    // Without permuting they are always the same.
    int i = 1, next = 1;
    for (p = sof_copt_skip(sof_copt_start); p < sof_copt_stop; p = sof_copt_skip(p + 1)) {
        (*p)->value = NULL;
        (*p)->present = false;
        (*p)->stop_value = 0;
    }
    if (argc < 1) {
        next = argc < 0 ? 0 : argc;
    } else if (argv == NULL) {
        i = 0;
        err = SOF_COPT_ERROR;
        goto done;
    }
    while (i < argc) {
        int used;
        opt = NULL;
        if (argv[i] == NULL) {
            err = SOF_COPT_ERROR;
            goto done;
        }
        if (strcmp(argv[i], "--") == 0) {
            sof_copt_move(argv, next, i, 1);
            next++;
            break;
        }
        err = sof_copt_argument(argc, argv, i, ctx, &used, &opt);
        if (err != SOF_COPT_OK) {
            goto done;
        }
        if (used == 0) {
            if (!permute) {
                break;
            }
            i++;
            continue;
        }
        sof_copt_move(argv, next, i, used);
        next += used;
        i += used;
    }
    i = next;
    opt = NULL;
    for (p = sof_copt_skip(sof_copt_start); p < sof_copt_stop; p = sof_copt_skip(p + 1)) {
        if ((*p)->option != NULL && !(*p)->present && (*p)->missing != NULL) {
            int stop = (*p)->missing(*p, ctx);
            if (stop != 0) {
                (*p)->stop_value = stop;
                opt = *p;
                err = SOF_COPT_STOPPED;
                break;
            }
        }
    }
done:
    if (index != NULL) {
        *index = i;
    }
    if (option != NULL) {
        *option = err == SOF_COPT_OK ? NULL : opt;
    }
    return err;
}

/// @brief Returns p, or the first entry after it that isn't NULL, or
/// sof_copt_stop.
static sof_copt_t *const *sof_copt_skip(sof_copt_t *const *p) {
    while (p < sof_copt_stop && *p == NULL) {
        p++;
    }
    return p;
}

/// @brief Records that opt was given, with value, and calls its validate
/// callback.
/// @return SOF_COPT_OK, or SOF_COPT_STOPPED if the callback returned
/// non-zero, which is kept in opt->stop_value.
static sof_copt_error_t sof_copt_take(sof_copt_t *opt, const char *value, void *ctx) {
    opt->present = true;
    opt->value = value;
    int stop = opt->validate != NULL ? opt->validate(opt, ctx) : 0;
    if (stop != 0) {
        opt->stop_value = stop;
        return SOF_COPT_STOPPED;
    }
    return SOF_COPT_OK;
}
