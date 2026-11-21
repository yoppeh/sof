/**
 * @file bank.h
 * @author Warren Mann (warren@nonvol.io)
 * @brief A discrete-event simulation of customers queuing for tellers in c,
 * for the bank program.
 * @details A bank_t simulates a bank with a number of tellers and one line.
 * Customers arrive while the doors are open, wait in the line, a sof_queue,
 * until a teller is free, are served, and leave. The arrivals and departures
 * still to come wait in a sof_rbtree ordered by time, and the simulation
 * goes from one to the next. The times between arrivals and the service
 * times are fixed, or drawn from an exponential distribution with a given
 * mean, using sof_random generators, so the same options and seed give the
 * same run on every platform. Arrivals and service times have streams of
 * their own, so with the same seed, the same customers come at the same
 * times and take the same time to serve whatever the number of tellers,
 * and runs with different numbers of tellers can be compared. Times are whole ticks, in whatever unit the
 * caller chooses; the bank program uses milliseconds. No floating point is
 * used.
 * @version 0.0.1
 * @date 2026-11-04
 * @copyright Copyright (c) 2026
 */

#ifndef BANK_H
#define BANK_H

#include <stddef.h>
#include <stdint.h>

#include "sof_alloc.h"
#include "sof_mem.h"
#include "sof_queue.h"
#include "sof_random.h"
#include "sof_rbtree.h"

/**
 * @brief Error codes for the bank functions.
 * @details BANK_STOPPED is returned by bank_run() when its callback stops
 * the run.
 */
typedef enum bank_error_t {
    BANK_OK = 0,
    BANK_NO_MEM,
    BANK_NO_MATCH,
    BANK_ERROR,
    BANK_STOPPED,
} bank_error_t;

/**
 * @brief How the times between arrivals, or the service times, are drawn.
 */
typedef enum bank_draw_t {
    /// @brief From an exponential distribution with the given mean, so
    /// arrivals are a Poisson process.
    BANK_EXPONENTIAL = 0,
    /// @brief Always the mean itself.
    BANK_FIXED,
} bank_draw_t;

/**
 * @brief What happened at an event.
 */
typedef enum bank_event_type_t {
    /// @brief A customer came in and joined the line.
    BANK_ARRIVAL = 0,
    /// @brief A teller called the customer at the front of the line.
    BANK_START,
    /// @brief A teller finished with a customer, who left.
    BANK_DEPARTURE,
} bank_event_type_t;

/// @brief The longest mean or length bank_init() accepts, in ticks: 2^40,
/// about 35 years in milliseconds, which keeps every sum far from overflow.
#define BANK_TIME_MAX ((uint64_t)1 << 40)

/**
 * @brief What bank_init() is to simulate.
 */
typedef struct bank_options_t {
    /// @brief The mean time between arrivals, from 1 to BANK_TIME_MAX.
    uint64_t arrival;
    /// @brief How the times between arrivals are drawn.
    bank_draw_t arrival_draw;
    /// @brief The mean service time, from 0 to BANK_TIME_MAX.
    uint64_t service;
    /// @brief How service times are drawn.
    bank_draw_t service_draw;
    /// @brief The number of tellers, at least 1.
    size_t tellers;
    /// @brief How long the doors are open, from 0 to BANK_TIME_MAX:
    /// customers arrive at times below it, and those inside when it closes
    /// are still served.
    uint64_t length;
    /// @brief The seed of the random draws.
    uint64_t seed;
} bank_options_t;

/**
 * @brief An event, as bank_step() and bank_run() report it.
 */
typedef struct bank_event_t {
    /// @brief What happened.
    bank_event_type_t type;
    /// @brief When it happened.
    uint64_t time;
    /// @brief The customer, numbered from 1 in order of arrival.
    uint64_t customer;
    /// @brief The teller, numbered from 1, for BANK_START and
    /// BANK_DEPARTURE; 0 for BANK_ARRIVAL.
    size_t teller;
    /// @brief For BANK_START, how long the customer waited; otherwise 0.
    uint64_t wait;
    /// @brief For BANK_START and BANK_DEPARTURE, the customer's service
    /// time; otherwise 0.
    uint64_t service;
    /// @brief Customers waiting for a teller after the event.
    size_t waiting;
    /// @brief Tellers busy after the event.
    size_t busy;
} bank_event_t;

/**
 * @brief Called by bank_run() with each event.
 * @details Return 0 to go on, or a non-zero value to stop the run, which
 * then returns BANK_STOPPED. It must not call bank_step(), bank_run(),
 * bank_clear() or bank_free() on the same bank. ctx is the ctx given to
 * bank_run().
 */
typedef int (*bank_event_func_t)(const bank_event_t *event, void *ctx);

/**
 * @brief A simulated bank.
 * @details The application provides the bank_t and sets it up with
 * bank_init(). The pending events and the line point back to it, so it must
 * not be moved or copied while initialized. The fields can be read, but must
 * not be changed by the application.
 *
 * The counts are kept as the simulation goes. Waiting customers are those
 * in the line with no teller yet free for them; a customer who arrives to a
 * free teller doesn't wait. waiting_area and busy_area are the sums over
 * time of the customers waiting and the tellers busy, so waiting_area / now
 * is the mean length of the line, and busy_area / (now * tellers) the share
 * of the time the tellers were busy. By Little's law, waiting_area equals
 * wait_total once the run is over.
 *
 * Every function accepts a NULL bank, and a bank that was freed or whose
 * bank_init() failed (ready is 0), as described for each.
 */
typedef struct bank_t {
    /// @brief A copy of the options given to bank_init().
    bank_options_t options;
    /// @brief The pending arrivals and departures, by time; at the same time,
    /// departures before arrivals, then in the order they were made.
    sof_rbtree_t events;
    /// @brief The customers in the line, oldest first.
    sof_queue_t line;
    /// @brief Where the pending events and the customers in the line are
    /// allocated: a pool of each, taking slabs from alloc, so the many
    /// small records cost one allocation per slab rather than one each.
    sof_mem_pool_t event_pool;
    sof_mem_pool_t customer_pool;
    /// @brief The random draws of the times between arrivals, and of the
    /// service times: separate streams, the second jumped ahead of the
    /// first, so the customers and their service times are the same
    /// whatever the number of tellers.
    sof_random_t arrivals;
    sof_random_t services;
    /// @brief One byte per teller: non-zero while the teller is busy.
    unsigned char *tellers;
    /// @brief Events made so far, to order those at the same time.
    uint64_t sequence;
    /// @brief Non-zero once the first arrival has been scheduled.
    int begun;
    /// @brief The time of the last event.
    uint64_t now;
    /// @brief Customers who have arrived, been called by a teller, and left.
    uint64_t arrived;
    uint64_t started;
    uint64_t served;
    /// @brief Customers who waited for a teller at all.
    uint64_t waited;
    /// @brief The sum of the customers' waits, and the longest.
    uint64_t wait_total;
    uint64_t wait_max;
    /// @brief The sum of the service times of the customers called.
    uint64_t service_total;
    /// @brief Customers waiting now, and the most there have been.
    size_t waiting;
    size_t waiting_max;
    /// @brief Tellers busy now.
    size_t busy;
    /// @brief The sums over time of waiting and busy.
    uint64_t waiting_area;
    uint64_t busy_area;
    /// @brief A copy of the allocator; zeroed for the standard functions.
    sof_alloc_t alloc;
    /// @brief 1 once bank_init() has succeeded, 0 when freed or failed.
    int ready;
} bank_t;

/**
 * @brief Starts the simulation over, with the same options and seed.
 * @details Every pending event and every customer in the line is dropped and
 * the counts are zeroed, so the bank runs again just as it did the first
 * time. Allocates nothing.
 * @param b The bank.
 * @return BANK_OK, or BANK_ERROR if b is NULL or not initialized.
 */
extern bank_error_t bank_clear(bank_t *b);

/**
 * @brief Frees everything the bank allocated.
 * @details The bank_t itself isn't freed (it belongs to the caller); it is
 * left empty, and acts like a bank whose bank_init() failed until it is
 * initialized again.
 * @param b The bank.
 * @return BANK_OK, or BANK_ERROR if b is NULL or not initialized.
 */
extern bank_error_t bank_free(bank_t *b);

/**
 * @brief Initializes a bank in a bank_t the caller provides.
 * @details Nothing happens until bank_step() or bank_run() is called.
 * @param b The bank to initialize. It can be anywhere: on the stack, in
 * another structure or on the heap, but must not move while initialized.
 * @param options What to simulate. They are copied, so they can be on the
 * stack.
 * @param alloc The memory functions for the bank's tellers, its trees and
 * queues, and the slabs of the pools its customers and events come from, or
 * NULL for the standard functions. The bank keeps a copy, so it
 * can be on the stack.
 * @return BANK_OK, BANK_NO_MEM, or BANK_ERROR if b or options is NULL, alloc
 * has a malloc but no free, there are no tellers, arrival is 0, arrival,
 * service or length is above BANK_TIME_MAX, or a draw isn't a bank_draw_t;
 * on an error, b is left empty.
 */
extern bank_error_t bank_init(bank_t *b, const bank_options_t *options, const sof_alloc_t *alloc);

/**
 * @brief Runs the simulation to its end, calling cb with each event.
 * @details Calls bank_step() until the simulation is over. If cb returns a
 * non-zero value, the run stops just after that event, and bank_run() or
 * bank_step() can go on from there.
 * @param b The bank.
 * @param cb Called with each event and ctx, or NULL.
 * @param ctx Passed to every call of cb.
 * @return BANK_OK once the simulation is over (at once, if it already was),
 * BANK_STOPPED if cb stopped it, BANK_NO_MEM if memory ran out, which leaves
 * the bank as it was before the event that couldn't be simulated, so the run
 * can go on once memory is free, or BANK_ERROR if b is NULL or not
 * initialized.
 */
extern bank_error_t bank_run(bank_t *b, bank_event_func_t cb, void *ctx);

/**
 * @brief Simulates the next event.
 * @details If a customer is waiting and a teller is free, the teller calls
 * the customer (BANK_START). Otherwise the earliest pending event happens: a
 * customer arrives (BANK_ARRIVAL), and the next arrival is scheduled if it
 * comes before the doors close, or a teller finishes (BANK_DEPARTURE).
 * Several events can happen at the same time; at that time, departures come
 * before arrivals, so a teller who finishes as a customer comes in serves
 * them without a wait. A free teller is always the lowest-numbered one.
 * @param b The bank.
 * @param event If not NULL, set to what happened.
 * @return BANK_OK, BANK_NO_MATCH if the simulation is over (no customers
 * are left, and none are to come), BANK_NO_MEM if memory ran out, which
 * leaves the bank, random draws included, as it was, or BANK_ERROR if b is
 * NULL or not initialized. event is unchanged unless BANK_OK is returned.
 */
extern bank_error_t bank_step(bank_t *b, bank_event_t *event);

#endif // BANK_H
