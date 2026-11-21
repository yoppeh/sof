/**
 * @file main.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief Finds the n-grams in text: the most frequent, and how often any
 * given one occurs.
 * @details Counts every n-gram of characters or words, for a range of
 * lengths, in files or standard input, and prints how many there are and the
 * most frequent of each length, and the counts of any grams asked for.
 *
 *     ngram [-h] [-w] [-i] [-n length] [-c chars] [-t top] [-j threads] [-f gram]... [file]...
 *
 * With no file, or a file named "-", the text is read from standard input.
 * Its options are read with sof_copt.
 *
 * The main thread reads the text and cuts it into chunks, each just after a
 * character that isn't in the set, and hands them through a queue to worker
 * threads. Each worker counts its chunks in a counter of its own, starting
 * each with the words before it as context (see ngram_context()), so no gram
 * is lost at a cut, and the workers' counts are merged once the text is read.
 * @version 0.0.1
 * @date 2026-11-03
 * @copyright Copyright (c) 2026
 */

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sof_copt.h"
#include "ngram.h"
#include "sof_queue.h"
#include "sof_sort.h"

/// @brief The size of each piece of input read, and so roughly of each chunk
/// handed to a worker.
#define CHUNK_SIZE 262144

/// @brief The most threads -j accepts.
#define MAX_THREADS 256

/// @brief What the options ask for, filled in by their callbacks.
typedef struct settings_t {
    /// @brief Passed to ngram_init().
    ngram_options_t options;
    /// @brief The number of most frequent grams to print of each length, or
    /// 0 for all of them.
    size_t top;
    /// @brief The grams given with -f, pointing into argv.
    const char **lookups;
    size_t num_lookups;
    /// @brief The number of worker threads.
    size_t threads;
} settings_t;

/// @brief A gram and its count, for sorting.
typedef struct gram_count_t {
    const char *gram;
    size_t count;
} gram_count_t;

/// @brief A chunk of text for a worker, allocated with its text after it:
/// context bytes that are read but not counted, then the bytes to count.
typedef struct job_t {
    /// @brief The bytes of text after the job_t, context included.
    size_t length;
    /// @brief The bytes of context at the start of the text.
    size_t context;
    /// @brief The file the text came from, for messages.
    const char *path;
} job_t;

/// @brief What the main thread and the workers share, under lock.
typedef struct work_t {
    /// @brief The jobs not yet taken by a worker.
    sof_queue_t jobs;
    pthread_mutex_t lock;
    /// @brief Signalled when a job is queued, or done is set.
    pthread_cond_t ready;
    /// @brief Signalled when a job is taken, or failed is set.
    pthread_cond_t space;
    /// @brief The most jobs queued at once, so the input isn't read far ahead
    /// of the workers.
    size_t limit;
    /// @brief Set when no more jobs will be queued.
    int done;
    /// @brief Set when memory ran out, so jobs are dropped, not counted.
    int failed;
} work_t;

/// @brief A worker thread and its own counter.
typedef struct worker_t {
    work_t *work;
    pthread_t thread;
    ngram_t ng;
    /// @brief The file being counted when memory ran out, or NULL.
    const char *failed;
} worker_t;

static int by_count(const void *a, const void *b, void *ctx);
static int chars_validate(sof_copt_t *option, void *ctx);
static size_t context_start(const ngram_t *ng, const char *text, size_t end);
static int count_input(ngram_t *ng, const settings_t *settings, char **paths, int num_paths);
static ngram_error_t count_job(ngram_t *ng, const job_t *job);
static int is_break(const ngram_t *ng, char c);
static int length_validate(sof_copt_t *option, void *ctx);
static int lookup_validate(sof_copt_t *option, void *ctx);
static const char *name_of(const char *path);
static const char *parse_size(const char *s, size_t *n);
static int print_top(const ngram_t *ng, size_t n, size_t top);
static int queue_file(work_t *work, const ngram_t *ng, const char *path);
static int queue_job(work_t *work, const char *text, size_t length, size_t context, const char *path);
static int threads_validate(sof_copt_t *option, void *ctx);
static int top_validate(sof_copt_t *option, void *ctx);
static void usage(FILE *stream);
static void *work_thread(void *arg);

/// @brief A code the validate callbacks return, which sof_copt keeps in the
/// option's stop_value: its argument is wrong. The callback has printed why.
#define BAD_ARGUMENT 1

static sof_copt_t opt_help = {
    .option = "-h",
    .description = "print this help and exit",
};

static sof_copt_t opt_words = {
    .option = "-w",
    .description = "count grams of words, not characters",
};

static sof_copt_t opt_fold = {
    .option = "-i",
    .description = "ignore case: fold letters to lowercase",
};

static sof_copt_t opt_length = {
    .option = "-n",
    .description = "gram length, n or min-max (default 1-3)",
    .type = SOF_COPT_ARG_REQUIRED,
    .validate = length_validate,
};

static sof_copt_t opt_chars = {
    .option = "-c",
    .description = "the characters grams are made of (default A-Za-z0-9)",
    .type = SOF_COPT_ARG_REQUIRED,
    .validate = chars_validate,
};

static sof_copt_t opt_top = {
    .option = "-t",
    .description = "print this many of the most frequent grams (default 10; 0 for all)",
    .type = SOF_COPT_ARG_REQUIRED,
    .validate = top_validate,
};

static sof_copt_t opt_threads = {
    .option = "-j",
    .description = "count on this many threads (default 1)",
    .type = SOF_COPT_ARG_REQUIRED,
    .validate = threads_validate,
};

static sof_copt_t opt_lookup = {
    .option = "-f",
    .description = "print how often this gram occurs; can be repeated",
    .type = SOF_COPT_ARG_REQUIRED,
    .validate = lookup_validate,
};

SOF_COPT_OPTIONS static sof_copt_t *options[] = {
    &opt_help,
    &opt_words,
    &opt_fold,
    &opt_length,
    &opt_chars,
    &opt_top,
    &opt_threads,
    &opt_lookup,
};

/// @brief Orders grams by count, highest first, then by gram.
static int by_count(const void *a, const void *b, void *ctx) {
    const gram_count_t *ga = a, *gb = b;
    (void)ctx;
    if (ga->count != gb->count) {
        return ga->count > gb->count ? -1 : 1;
    }
    return strcmp(ga->gram, gb->gram);
}

/// @brief Reads the argument of -c.
static int chars_validate(sof_copt_t *option, void *ctx) {
    settings_t *settings = ctx;
    if (option->value[0] == '\0') {
        fprintf(stderr, "%s needs at least one character\n", option->option);
        return BAD_ARGUMENT;
    }
    settings->options.chars = option->value;
    return 0;
}

/// @brief Returns where the context of the text after end starts: at least
/// the last max - 1 words before end, for word grams, or end itself for
/// character grams, whose chunks are cut where no gram goes on. If there are
/// fewer words, the context is all of text, which starts where a gram can.
/// @details Words are told apart only by bytes for which is_break() is true,
/// so the context starts just after one of those, at a whole character. A
/// character above ASCII that isn't in the set joins two words into one
/// here, which only makes the context longer than it need be.
static size_t context_start(const ngram_t *ng, const char *text, size_t end) {
    if (ng->unit == NGRAM_CHARS) {
        return end;
    }
    size_t i = end;
    for (size_t words = 0; words + 1 < ng->max; words++) {
        while (i > 0 && is_break(ng, text[i - 1])) {
            i--;
        }
        if (i == 0) {
            break;
        }
        while (i > 0 && !is_break(ng, text[i - 1])) {
            i--;
        }
    }
    return i;
}

/// @brief Counts the files, or standard input if there are none, into ng,
/// on settings->threads worker threads.
/// @return 0, or 1 if a file couldn't be read or counted, after printing
/// why.
static int count_input(ngram_t *ng, const settings_t *settings, char **paths, int num_paths) {
    work_t work = {.limit = 2 * settings->threads};
    worker_t *workers = sof_alloc_calloc(NULL, settings->threads, sizeof(worker_t));
    if (workers == NULL) {
        fprintf(stderr, "out of memory\n");
        return 1;
    }
    sof_queue_init(&work.jobs, NULL, NULL, NULL);
    pthread_mutex_init(&work.lock, NULL);
    pthread_cond_init(&work.ready, NULL);
    pthread_cond_init(&work.space, NULL);
    int rc = 0;
    size_t started = 0;
    for (; started < settings->threads; started++) {
        worker_t *w = &workers[started];
        w->work = &work;
        if (ngram_init(&w->ng, &settings->options, NULL) != NGRAM_OK) {
            fprintf(stderr, "out of memory\n");
            rc = 1;
            break;
        }
        if (pthread_create(&w->thread, NULL, work_thread, w) != 0) {
            fprintf(stderr, "can't start a thread\n");
            ngram_free(&w->ng);
            rc = 1;
            break;
        }
    }
    if (rc == 0 && num_paths == 0) {
        rc = queue_file(&work, ng, "-");
    }
    for (int i = 0; i < num_paths && rc == 0; i++) {
        rc = queue_file(&work, ng, paths[i]);
    }
    pthread_mutex_lock(&work.lock);
    work.done = 1;
    pthread_cond_broadcast(&work.ready);
    pthread_mutex_unlock(&work.lock);

    for (size_t i = 0; i < started; i++) {
        pthread_join(workers[i].thread, NULL);
    }
    for (size_t i = 0; i < started; i++) {
        if (workers[i].failed != NULL) {
            fprintf(stderr, "out of memory counting %s\n", name_of(workers[i].failed));
            rc = 1;
        }
    }
    // The counts are sums, so the order the chunks were counted in, and
    // the order they are merged in, doesn't change them.
    for (size_t i = 0; i < started; i++) {
        if (rc == 0 && ngram_merge(ng, &workers[i].ng) != NGRAM_OK) {
            fprintf(stderr, "out of memory\n");
            rc = 1;
        }
        ngram_free(&workers[i].ng);
    }
    // The workers took every job before they stopped, so the queue is empty.
    sof_queue_free(&work.jobs);
    pthread_cond_destroy(&work.space);
    pthread_cond_destroy(&work.ready);
    pthread_mutex_destroy(&work.lock);
    sof_alloc_free(NULL, workers, settings->threads * sizeof(worker_t));
    return rc;
}

/// @brief Counts one job's chunk of text in a worker's counter.
static ngram_error_t count_job(ngram_t *ng, const job_t *job) {
    const char *text = (const char *)(job + 1);
    ngram_error_t err = ngram_context(ng, text, job->context);
    if (err == NGRAM_OK) {
        err = ngram_add(ng, text + job->context, job->length - job->context);
    }
    if (err == NGRAM_OK) {
        // The chunk ends where no word goes on, or at the end of its file.
        err = ngram_flush(ng);
    }
    return err;
}

/// @brief Returns 1 if c is an ASCII character that isn't in ng's set, so
/// text can be cut just after it: it is a whole character, and no gram goes
/// on across it.
static int is_break(const ngram_t *ng, char c) {
    unsigned char b = (unsigned char)c;
    return b < 0x80 && ngram_member(ng, b) == 0;
}

/// @brief Reads the argument of -n: a length, or a range such as 2-4.
static int length_validate(sof_copt_t *option, void *ctx) {
    settings_t *settings = ctx;
    size_t min, max;
    const char *end = parse_size(option->value, &min);
    if (end != NULL && *end == '-') {
        end = parse_size(end + 1, &max);
    } else {
        max = min;
    }
    if (end == NULL || *end != '\0' || min == 0 || max < min) {
        fprintf(stderr, "%s needs a length, such as 3, or a range, such as 1-4, not \"%s\"\n", option->option, option->value);
        return BAD_ARGUMENT;
    }
    settings->options.min = min;
    settings->options.max = max;
    return 0;
}

/// @brief Adds the argument of -f, each time it is given, to the grams to
/// look up.
static int lookup_validate(sof_copt_t *option, void *ctx) {
    settings_t *settings = ctx;
    settings->lookups[settings->num_lookups++] = option->value;
    return 0;
}

/// @brief Returns a file's name for messages: "standard input" for "-".
static const char *name_of(const char *path) {
    return strcmp(path, "-") == 0 ? "standard input" : path;
}

/// @brief Reads a non-negative decimal number from the start of s.
/// @return Where the number ends, or NULL if s doesn't start with one or it
/// is too big.
static const char *parse_size(const char *s, size_t *n) {
    if (*s < '0' || *s > '9') {
        return NULL;
    }
    char *end;
    errno = 0;
    unsigned long long value = strtoull(s, &end, 10);
    if (errno != 0 || value > SIZE_MAX) {
        return NULL;
    }
    *n = (size_t)value;
    return end;
}

/// @brief Prints how many grams of length n there are, then the top most
/// frequent of them (all of them if top is 0).
/// @return 0, or 1 if memory ran out, after printing why.
static int print_top(const ngram_t *ng, size_t n, size_t top) {
    size_t distinct = ngram_distinct(ng, n);
    printf("\n%zu-grams: %zu distinct, %zu in all\n", n, distinct, ngram_total(ng, n));
    if (distinct == 0) {
        return 0;
    }
    gram_count_t *grams = sof_alloc_calloc(NULL, distinct, sizeof(gram_count_t));
    if (grams == NULL) {
        fprintf(stderr, "out of memory\n");
        return 1;
    }
    size_t count = 0, position = 0;
    const char *gram;
    while ((gram = ngram_next(ng, n, &position, &grams[count].count)) != NULL) {
        grams[count++].gram = gram;
    }
    sof_sort_quick(grams, count, sizeof(gram_count_t), by_count, NULL);
    if (top == 0 || top > count) {
        top = count;
    }
    for (size_t i = 0; i < top; i++) {
        printf("%10zu  %s\n", grams[i].count, grams[i].gram);
    }
    sof_alloc_free(NULL, grams, distinct * sizeof(gram_count_t));
    return 0;
}

/// @brief Reads one file, or standard input for "-", and queues it for the
/// workers in chunks, each cut just after an ASCII character that isn't in
/// ng's set (see is_break()).
/// @return 0, or 1 if the file couldn't be read or memory ran out, after
/// printing why unless a worker ran out (count_input() prints that).
static int queue_file(work_t *work, const ngram_t *ng, const char *path) {
    int is_stdin = strcmp(path, "-") == 0;
    FILE *f = is_stdin ? stdin : fopen(path, "rb");
    if (f == NULL) {
        fprintf(stderr, "error opening %s: %s\n", path, strerror(errno));
        return 1;
    }
    // piece holds the context for the next chunk, then the text read after
    // it that isn't queued yet.
    char *piece = NULL;
    size_t size = 0, used = 0, context = 0;
    int rc = 0, end = 0;
    while (!end && rc == 0) {
        if (size - used < CHUNK_SIZE) {
            size_t new_size = size == 0 ? 2 * CHUNK_SIZE : 2 * size;
            char *grown = sof_alloc_realloc(NULL, piece, size, new_size);
            if (grown == NULL) {
                fprintf(stderr, "out of memory\n");
                rc = 1;
                break;
            }
            piece = grown;
            size = new_size;
        }
        size_t got = fread(piece + used, 1, CHUNK_SIZE, f);
        used += got;
        end = got < CHUNK_SIZE;
        size_t cut = used;
        if (!end) {
            while (cut > context && !is_break(ng, piece[cut - 1])) {
                cut--;
            }
            if (cut == context) {
                // No place to cut in the piece: read on.
                continue;
            }
        }
        if (cut > context) {
            rc = queue_job(work, piece, cut, context, path);
        }
        size_t keep = context_start(ng, piece, cut);
        memmove(piece, piece + keep, used - keep);
        used -= keep;
        context = cut - keep;
    }
    if (ferror(f)) {
        fprintf(stderr, "error reading %s\n", name_of(path));
        rc = 1;
    }
    if (!is_stdin) {
        fclose(f);
    }
    sof_alloc_free(NULL, piece, size);
    return rc;
}

/// @brief Queues a copy of length bytes of text, the first context of them
/// context, waiting while the queue is full.
/// @return 0, or 1 if memory ran out here, after printing why, or in a
/// worker.
static int queue_job(work_t *work, const char *text, size_t length, size_t context, const char *path) {
    job_t *job = sof_alloc_malloc(NULL, sizeof(job_t) + length);
    if (job == NULL) {
        fprintf(stderr, "out of memory\n");
        return 1;
    }
    job->length = length;
    job->context = context;
    job->path = path;
    memcpy(job + 1, text, length);
    pthread_mutex_lock(&work->lock);
    while (work->jobs.count >= work->limit && !work->failed) {
        pthread_cond_wait(&work->space, &work->lock);
    }
    int failed = work->failed, queued = 0;
    if (!failed) {
        queued = sof_queue_enqueue(&work->jobs, job) == SOF_QUEUE_OK;
        if (queued) {
            pthread_cond_signal(&work->ready);
        } else {
            work->failed = 1;
        }
    }
    pthread_mutex_unlock(&work->lock);
    if (!queued) {
        if (!failed) {
            fprintf(stderr, "out of memory\n");
        }
        sof_alloc_free(NULL, job, sizeof(job_t) + length);
        return 1;
    }
    return 0;
}

/// @brief Reads the argument of -j.
static int threads_validate(sof_copt_t *option, void *ctx) {
    settings_t *settings = ctx;
    const char *end = parse_size(option->value, &settings->threads);
    if (end == NULL || *end != '\0' || settings->threads == 0 || settings->threads > MAX_THREADS) {
        fprintf(stderr, "%s needs a number of threads from 1 to %d, not \"%s\"\n", option->option, MAX_THREADS, option->value);
        return BAD_ARGUMENT;
    }
    return 0;
}

/// @brief Reads the argument of -t.
static int top_validate(sof_copt_t *option, void *ctx) {
    settings_t *settings = ctx;
    const char *end = parse_size(option->value, &settings->top);
    if (end == NULL || *end != '\0') {
        fprintf(stderr, "%s needs a non-negative number, not \"%s\"\n", option->option, option->value);
        return BAD_ARGUMENT;
    }
    return 0;
}

/// @brief Prints the usage message, with the options sof_copt knows.
static void usage(FILE *stream) {
    fprintf(stream, "usage: ngram [-h] [-w] [-i] [-n length] [-c chars] [-t top] [-j threads] [-f gram]... [file]...\n");
    sof_copt_print(stream);
    fprintf(stream,
        "Counts the n-grams of characters, or of words, in the files, or in standard\n"
        "input if there are none or a file is \"-\". Text is UTF-8. chars is a set\n"
        "such as a-z0-9' or a-zà-ÿ: any other character breaks a run of characters,\n"
        "or separates words.\n");
}

/// @brief A worker thread: counts the jobs it takes from the queue in its
/// own counter, until reading is done and the queue is empty.
static void *work_thread(void *arg) {
    worker_t *w = arg;
    work_t *work = w->work;
    while (1) {
        void *data;
        pthread_mutex_lock(&work->lock);
        sof_queue_error_t rc;
        while ((rc = sof_queue_dequeue(&work->jobs, &data)) == SOF_QUEUE_NO_MATCH && !work->done) {
            pthread_cond_wait(&work->ready, &work->lock);
        }
        int failed = work->failed;
        pthread_cond_signal(&work->space);
        pthread_mutex_unlock(&work->lock);
        if (rc != SOF_QUEUE_OK) {
            return NULL;
        }
        job_t *job = data;
        // Once memory has run out the result is thrown away, so the jobs
        // left are only freed.
        if (!failed && count_job(&w->ng, job) != NGRAM_OK) {
            w->failed = job->path;
            pthread_mutex_lock(&work->lock);
            work->failed = 1;
            pthread_cond_broadcast(&work->space);
            pthread_mutex_unlock(&work->lock);
        }
        sof_alloc_free(NULL, job, sizeof(job_t) + job->length);
    }
}

int main(int ac, char **av) {
    // Every argument could be a -f, so there is room for that many grams.
    settings_t settings = {{NGRAM_CHARS, 1, 3, NULL, 0}, 10, sof_alloc_calloc(NULL, (size_t)ac, sizeof(char *)), 0, 1};
    if (settings.lookups == NULL) {
        fprintf(stderr, "out of memory\n");
        return 1;
    }
    int index, rc = 2;
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
    case SOF_COPT_STOPPED:
        goto done;
    default:
        fprintf(stderr, "can't read the options (error %d)\n", (int)err);
        goto done;
    }
    if (opt_help.present) {
        usage(stdout);
        rc = 0;
        goto done;
    }
    settings.options.unit = opt_words.present ? NGRAM_WORDS : NGRAM_CHARS;
    settings.options.fold_case = opt_fold.present;

    ngram_t ng;
    ngram_error_t ng_err = ngram_init(&ng, &settings.options, NULL);
    if (ng_err != NGRAM_OK) {
        fprintf(stderr, ng_err == NGRAM_NO_MEM ? "out of memory\n" : "the set given with -c is empty or isn't UTF-8\n");
        rc = 1;
        goto done;
    }
    rc = count_input(&ng, &settings, av + index, ac - index);
    if (rc == 0) {
        printf("%s: %zu\n", ng.unit == NGRAM_WORDS ? "words" : "characters", ng.units);
        for (size_t n = ng.min; n <= ng.max && rc == 0; n++) {
            rc = print_top(&ng, n, settings.top);
        }
    }
    if (rc == 0 && settings.num_lookups > 0) {
        printf("\nlookups:\n");
        for (size_t i = 0; i < settings.num_lookups; i++) {
            printf("%10zu  %s\n", ngram_find(&ng, settings.lookups[i]), settings.lookups[i]);
        }
    }
    ngram_free(&ng);
done:
    sof_alloc_free(NULL, settings.lookups, (size_t)ac * sizeof(char *));
    return rc;
}
