/**
 * @file bank.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief A discrete-event simulation of customers queuing for tellers in c.
 * @details Pending arrivals and departures are events in a sof_rbtree,
 * keyed by time, kind and sequence number; customers wait in a sof_queue.
 * Events and customers are records of one size each, made and dropped all
 * the time, so each comes from a sof_mem_pool_t.
 * Each step either changes the bank completely or, if memory runs out,
 * leaves it as it was, random generator included, so a run can go on once
 * memory is free and still give the same results. Exponential draws come
 * from sof_random_exponential(), which uses integers only, so they are the
 * same on every platform.
 * @version 0.0.1
 * @date 2026-11-04
 * @copyright Copyright (c) 2026
 */

#include <string.h>

#include "bank.h"

/// @brief A pending arrival or departure: the key, and the only data, of a
/// node in the bank's events.
typedef struct event_t {
    uint64_t time;
    /// @brief BANK_ARRIVAL or BANK_DEPARTURE.
    bank_event_type_t type;
    uint64_t sequence;
    uint64_t customer;
    /// @brief Departures: the teller, from 1, and the service time.
    size_t teller;
    uint64_t service;
} event_t;

/// @brief A customer in the line.
typedef struct customer_t {
    uint64_t number;
    uint64_t arrival;
} customer_t;

static void advance(bank_t *b, uint64_t time);
static uint64_t draw(sof_random_t *r, uint64_t mean, bank_draw_t how);
static int event_compare(const void *a, const void *b, void *ctx);
static void event_delete(sof_rbtree_node_t *node, void *ctx);
static void line_delete(sof_queue_element_t *element, void *ctx);
static sof_rbtree_node_t *schedule(bank_t *b, uint64_t time, bank_event_type_t type, uint64_t customer, size_t teller, uint64_t service);
static void seed(bank_t *b);
static bank_error_t start(bank_t *b, bank_event_t *event);
static void update_waiting(bank_t *b);

/// @brief Moves the clock to time, adding to the sums over time.
static void advance(bank_t *b, uint64_t time) {
    uint64_t dt = time - b->now;
    b->waiting_area += (uint64_t)b->waiting * dt;
    b->busy_area += (uint64_t)b->busy * dt;
    b->now = time;
}

/// @brief Returns a time drawn as how says, with the given mean.
static uint64_t draw(sof_random_t *r, uint64_t mean, bank_draw_t how) {
    return how == BANK_FIXED ? mean : sof_random_exponential(r, mean);
}

/// @brief Orders events by time, departures before arrivals at the same
/// time, then by sequence number, so no two are equal.
static int event_compare(const void *a, const void *b, void *ctx) {
    (void)ctx;
    const event_t *ea = a, *eb = b;
    if (ea->time != eb->time) {
        return ea->time < eb->time ? -1 : 1;
    }
    if (ea->type != eb->type) {
        return ea->type == BANK_DEPARTURE ? -1 : 1;
    }
    if (ea->sequence != eb->sequence) {
        return ea->sequence < eb->sequence ? -1 : 1;
    }
    return 0;
}

/// @brief Frees an event as its node is removed.
static void event_delete(sof_rbtree_node_t *node, void *ctx) {
    bank_t *b = ctx;
    sof_mem_pool_release(&b->event_pool, node->key);
}

/// @brief Frees a customer as it is dropped from the line.
static void line_delete(sof_queue_element_t *element, void *ctx) {
    bank_t *b = ctx;
    sof_mem_pool_release(&b->customer_pool, element->data);
}

/// @brief Adds an event to the pending ones.
/// @return Its node, or NULL if memory ran out, leaving the events as they
/// were.
static sof_rbtree_node_t *schedule(bank_t *b, uint64_t time, bank_event_type_t type, uint64_t customer, size_t teller, uint64_t service) {
    event_t *e = sof_mem_pool_alloc(&b->event_pool);
    if (e == NULL) {
        return NULL;
    }
    e->time = time;
    e->type = type;
    e->sequence = b->sequence++;
    e->customer = customer;
    e->teller = teller;
    e->service = service;
    int created;
    sof_rbtree_node_t *node = sof_rbtree_insert(&b->events, e, &created);
    if (node == NULL || !created) {
        // Keys are never equal, so a node not created is one not allocated.
        b->sequence--;
        sof_mem_pool_release(&b->event_pool, e);
        return NULL;
    }
    node->data = e;
    return node;
}

/// @brief Starts the random streams from the seed: the service times'
/// stream is the arrivals' jumped 2^128 steps ahead, so they never overlap.
static void seed(bank_t *b) {
    sof_random_init(&b->arrivals, b->options.seed);
    b->services = b->arrivals;
    sof_random_jump(&b->services);
}

/// @brief Has the lowest-numbered free teller call the customer at the
/// front of the line, filling in event.
/// @return BANK_OK, or BANK_NO_MEM, leaving the bank as it was.
static bank_error_t start(bank_t *b, bank_event_t *event) {
    sof_random_t saved = b->services;
    size_t teller = 0;
    while (b->tellers[teller]) {
        teller++;
    }
    const customer_t *c = sof_queue_peek(&b->line)->data;
    uint64_t number = c->number, wait = b->now - c->arrival;
    uint64_t service = draw(&b->services, b->options.service, b->options.service_draw);
    if (schedule(b, b->now + service, BANK_DEPARTURE, number, teller + 1, service) == NULL) {
        b->services = saved;
        return BANK_NO_MEM;
    }
    // Frees c, through line_delete().
    sof_queue_dequeue(&b->line, NULL);
    b->tellers[teller] = 1;
    b->busy++;
    b->started++;
    b->service_total += service;
    if (wait > 0) {
        b->waited++;
        b->wait_total += wait;
        if (wait > b->wait_max) {
            b->wait_max = wait;
        }
    }
    event->type = BANK_START;
    event->customer = number;
    event->teller = teller + 1;
    event->wait = wait;
    event->service = service;
    return BANK_OK;
}

/// @brief Sets waiting: the customers in the line beyond the free tellers.
static void update_waiting(bank_t *b) {
    size_t free_tellers = b->options.tellers - b->busy;
    b->waiting = b->line.count > free_tellers ? b->line.count - free_tellers : 0;
    if (b->waiting > b->waiting_max) {
        b->waiting_max = b->waiting;
    }
}

bank_error_t bank_clear(bank_t *b) {
    if (b == NULL || !b->ready) {
        return BANK_ERROR;
    }
    sof_rbtree_clear(&b->events, NULL);
    sof_queue_clear(&b->line);
    memset(b->tellers, 0, b->options.tellers);
    seed(b);
    b->sequence = 0;
    b->begun = 0;
    b->now = 0;
    b->arrived = b->started = b->served = 0;
    b->waited = b->wait_total = b->wait_max = b->service_total = 0;
    b->waiting = b->waiting_max = b->busy = 0;
    b->waiting_area = b->busy_area = 0;
    return BANK_OK;
}

bank_error_t bank_free(bank_t *b) {
    if (b == NULL || !b->ready) {
        return BANK_ERROR;
    }
    sof_rbtree_free(&b->events);
    sof_queue_free(&b->line);
    sof_mem_pool_free(&b->event_pool);
    sof_mem_pool_free(&b->customer_pool);
    sof_alloc_free(&b->alloc, b->tellers, b->options.tellers);
    memset(b, 0, sizeof(*b));
    return BANK_OK;
}

bank_error_t bank_init(bank_t *b, const bank_options_t *options, const sof_alloc_t *alloc) {
    if (b == NULL) {
        return BANK_ERROR;
    }
    memset(b, 0, sizeof(*b));
    if (options == NULL || !sof_alloc_valid(alloc) || options->tellers == 0 || options->arrival == 0 || options->arrival > BANK_TIME_MAX || options->service > BANK_TIME_MAX || options->length > BANK_TIME_MAX || (options->arrival_draw != BANK_EXPONENTIAL && options->arrival_draw != BANK_FIXED) || (options->service_draw != BANK_EXPONENTIAL && options->service_draw != BANK_FIXED)) {
        return BANK_ERROR;
    }
    if (alloc != NULL) {
        b->alloc = *alloc;
    }
    b->options = *options;
    b->tellers = sof_alloc_calloc(&b->alloc, options->tellers, 1);
    if (b->tellers == NULL) {
        memset(b, 0, sizeof(*b));
        return BANK_NO_MEM;
    }
    // No init allocates, and each is given what it accepts.
    sof_mem_pool_init(&b->event_pool, sizeof(event_t), 0, &b->alloc);
    sof_mem_pool_init(&b->customer_pool, sizeof(customer_t), 0, &b->alloc);
    sof_rbtree_init(&b->events, event_compare, event_delete, b, &b->alloc);
    sof_queue_init(&b->line, line_delete, b, &b->alloc);
    seed(b);
    b->ready = 1;
    return BANK_OK;
}

bank_error_t bank_run(bank_t *b, bank_event_func_t cb, void *ctx) {
    bank_event_t event;
    bank_error_t err;
    while ((err = bank_step(b, &event)) == BANK_OK) {
        if (cb != NULL && cb(&event, ctx) != 0) {
            return BANK_STOPPED;
        }
    }
    return err == BANK_NO_MATCH ? BANK_OK : err;
}

bank_error_t bank_step(bank_t *b, bank_event_t *event) {
    if (b == NULL || !b->ready) {
        return BANK_ERROR;
    }
    if (!b->begun) {
        sof_random_t saved = b->arrivals;
        uint64_t first = draw(&b->arrivals, b->options.arrival, b->options.arrival_draw);
        if (first < b->options.length && schedule(b, first, BANK_ARRIVAL, 1, 0, 0) == NULL) {
            b->arrivals = saved;
            return BANK_NO_MEM;
        }
        b->begun = 1;
    }
    bank_event_t e = {0};
    if (b->line.count > 0 && b->busy < b->options.tellers) {
        if (start(b, &e) != BANK_OK) {
            return BANK_NO_MEM;
        }
    } else {
        sof_rbtree_node_t *node = sof_rbtree_minimum(&b->events, NULL);
        if (node == NULL) {
            return BANK_NO_MATCH;
        }
        const event_t *pending = node->key;
        e.type = pending->type;
        e.customer = pending->customer;
        if (pending->type == BANK_ARRIVAL) {
            customer_t *c = sof_mem_pool_alloc(&b->customer_pool);
            if (c == NULL) {
                return BANK_NO_MEM;
            }
            c->number = pending->customer;
            c->arrival = pending->time;
            sof_random_t saved = b->arrivals;
            uint64_t next = pending->time + draw(&b->arrivals, b->options.arrival, b->options.arrival_draw);
            sof_rbtree_node_t *scheduled = NULL;
            if (next < b->options.length) {
                scheduled = schedule(b, next, BANK_ARRIVAL, pending->customer + 1, 0, 0);
            }
            if ((next < b->options.length && scheduled == NULL) || sof_queue_enqueue(&b->line, c) != SOF_QUEUE_OK) {
                if (scheduled != NULL) {
                    sof_rbtree_remove_node(&b->events, scheduled);
                    b->sequence--;
                }
                b->arrivals = saved;
                sof_mem_pool_release(&b->customer_pool, c);
                return BANK_NO_MEM;
            }
            advance(b, pending->time);
            b->arrived++;
        } else {
            advance(b, pending->time);
            e.teller = pending->teller;
            e.service = pending->service;
            b->tellers[pending->teller - 1] = 0;
            b->busy--;
            b->served++;
        }
        // Frees pending, through event_delete().
        sof_rbtree_remove_node(&b->events, node);
    }
    e.time = b->now;
    update_waiting(b);
    e.waiting = b->waiting;
    e.busy = b->busy;
    if (event != NULL) {
        *event = e;
    }
    return BANK_OK;
}
