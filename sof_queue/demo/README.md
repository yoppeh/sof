# bank

A simulation of customers queuing for tellers at a bank in c.

`bank` simulates a day at a bank with one line and a number of tellers. Customers come in while the doors are open, join the line, are called by the first free teller, are served and leave; those still inside when the doors close are served too. The times between customers and the service times are drawn from exponential distributions with the means given, or are fixed at those means. The setup comes from a config file, with any of it overridden on the command line, and the program prints how many customers came, how many waited and for how long, how long the line was on average and at its longest, and how busy the tellers were. The same setup and seed give the same day, on any machine.

_main.c_ is the program. The simulation is done by _bank.c_, declared in _bank.h_, which keeps a `bank_t` that the program provides and sets up with `bank_init()`, giving it a `bank_options_t`: the mean time between arrivals and how it is drawn, the mean service time and how it is drawn, the number of tellers, how long the doors are open, and the seed. `bank_step()` simulates the next event and says what happened, and `bank_run()` simulates every event to the end, calling a callback with each. The counts kept in the `bank_t` as it goes give the results. `bank_clear()` starts the simulation over, and `bank_free()` releases the bank's memory.

It is a discrete-event simulation: nothing happens between events, so the clock jumps from one to the next. The events still to come, each customer's arrival and each teller's finishing, wait in a red-black tree ordered by time, and at the same time departures come before arrivals, then events in the order they were made, so the order is always the same. Each arrival schedules the next. The customers wait in a first-in, first-out queue, and whenever a teller is free and someone is waiting, the teller calls them before anything else happens. Exponential times come from `sof_random_exponential()`, which works them out with integers only, in fixed point, rather than with `log()`, whose last bit can differ between c libraries, so a run gives the same times everywhere. The arrivals and the service times are drawn from separate random streams, the second jumped far ahead of the first, so with the same seed the same customers come at the same times and take the same time to serve however many tellers there are, and runs with different numbers of tellers can be compared.

_bank.ini_ is an example config file. Pending events are kept in the [sof_rbtree](../../sof_rbtree/) library in _../../sof_rbtree/_ (_sof_rbtree.c_ and _sof_rbtree.h_), and the line in the [sof_queue](../../sof_queue/) library in _../../sof_queue/_ (_sof_queue.c_ and _sof_queue.h_). The events and the customers, records of one size each made and dropped all day, come from pools of the [sof_mem](../../sof_mem/) library in _../../sof_mem/_ (_sof_mem_pool.c_, _sof_mem.h_ and _sof_mem_internal.h_), whose slabs come from the bank's allocator. Random numbers come from the [sof_random](../../sof_random/) library in _../../sof_random/_ (_sof_random.c_ and _sof_random.h_), and memory is allocated through the [sof_alloc](../../sof_alloc/) library in _../../sof_alloc/_ (_sof_alloc.c_ and _sof_alloc.h_). The program reads its options with the [sof_copt](../../sof_copt/) library in _../../sof_copt/_ (_sof_copt.c_ and _sof_copt.h_) and its config file with the [sof_config](../../sof_config/) library in _../../sof_config/_ (_sof_config.c_ and _sof_config.h_), which uses the [sof_hashmap](../../sof_hashmap/) library in _../../sof_hashmap/_ (_sof_hashmap.c_ and _sof_hashmap.h_), the [sof_hash](../../sof_hash/) library in _../../sof_hash/_ (_sof_hash.c_, _sof_hash.h_ and _rapidhash.h_, which is [rapidhash](https://github.com/Nicoshev/rapidhash), under its own MIT license) and the [sof_utf](../../sof_utf/) library in _../../sof_utf/_ (_sof_utf.h_). Beyond those, it needs nothing but the c standard library.

    bank_options_t options = {60000, BANK_EXPONENTIAL, 150000, BANK_EXPONENTIAL, 3, 28800000, 1};
    bank_t b;                                  // a customer a minute, 2.5 minutes each, 3 tellers, 8 hours
    bank_init(&b, &options, NULL);             // no allocator: the standard functions
    bank_run(&b, NULL, NULL);                  // the whole day, with no callback
    double mean_wait = (double)b.wait_total / b.served;
    bank_free(&b);

## Building

    make            # builds ./bank
    make debug=1    # builds with debug info and without optimization
    ./bank bank.ini           # a day at the bank in bank.ini
    ./bank -t 4 bank.ini      # the same day, with a fourth teller

`make` in the library's directory, _../_, builds it with the library, as do `make sof_queue` and `make` from the root of the project.

To build it in another project, copy _main.c_, _bank.c_ and _bank.h_, with _sof_rbtree.c_ and _sof_rbtree.h_ from _../../sof_rbtree/_, _sof_queue.c_ and _sof_queue.h_ from _../../sof_queue/_, _sof_mem_pool.c_, _sof_mem.h_ and _sof_mem_internal.h_ from _../../sof_mem/_, _sof_random.c_ and _sof_random.h_ from _../../sof_random/_, _sof_alloc.c_ and _sof_alloc.h_ from _../../sof_alloc/_, _sof_copt.c_ and _sof_copt.h_ from _../../sof_copt/_, _sof_config.c_ and _sof_config.h_ from _../../sof_config/_, _sof_hashmap.c_ and _sof_hashmap.h_ from _../../sof_hashmap/_, _sof_hash.c_, _sof_hash.h_ and _rapidhash.h_ from _../../sof_hash/_, and _sof_utf.h_ from _../../sof_utf/_, into it. To use only the simulation, _bank.c_ and _bank.h_ need just the _sof_rbtree_, _sof_queue_, _sof_mem_, _sof_random_ and _sof_alloc_ files.

## Usage

    bank [-h] [-v] [-t tellers] [-a minutes] [-s minutes] [-o minutes] [-r seed] [config]

- `-h`: print the usage message, with the list `sof_copt_print()` makes, and exit.
- `-v`: print every event as it happens: each arrival, with how many are then waiting; each call of a customer by a teller, with how long they waited; and each departure, with how long the customer took to serve. The time of each, in minutes, comes first.
- `-t` _tellers_: the number of tellers, from 1 to 100000.
- `-a` _minutes_: the mean time between arrivals, above 0.
- `-s` _minutes_: the mean service time, 0 or more.
- `-o` _minutes_: how long the doors are open, 0 or more. Customers come at times before it.
- `-r` _seed_: the seed of the random draws, a whole number. The same seed gives the same customers, at the same times, taking the same time to serve.
- _config_: the config file, read with [sof_config](../../sof_config/); `-` reads it from standard input. Without one, the program uses its defaults. The options override what it sets.

The config file can set these, each in its section, and nothing else:

    [bank]
    tellers = 3             ; the number of tellers (default 3)
    open = 480              ; minutes the doors are open (default 480)
    seed = 1                ; the seed of the random draws (default 1)

    [arrivals]
    mean = 1.0              ; mean minutes between arrivals (default 1)
    draw = exponential      ; exponential or fixed (default exponential)

    [service]
    mean = 2.5              ; mean minutes of service (default 2.5)
    draw = exponential      ; exponential or fixed (default exponential)

Exponential arrivals make a Poisson process, customers coming independently at random; fixed arrivals come exactly _mean_ minutes apart, the first at _mean_. Times are simulated to the millisecond.

It exits with **0**, **1** if the config file couldn't be read or a setting in it is wrong or out of range, or memory ran out, or **2** if the options are wrong.

For example, a day at the bank in _bank.ini_, which is the defaults:

    $ ./bank bank.ini
    tellers:        3
    open:           480.00 minutes
    arrivals:       exponential, a mean of 1.00 minutes apart
    service:        exponential, a mean of 2.50 minutes
    seed:           1

    customers:      493
    waited:         330 (66.9%)
    mean wait:      3.00 minutes
      if they did:  4.48 minutes
    longest wait:   13.87 minutes
    mean service:   2.43 minutes
    mean line:      3.05
    longest line:   14
    tellers busy:   82.1%
    last left at:   485.78 minutes

_waited_ counts the customers who found no teller free, and _if they did_ is the mean wait of those alone. _mean line_ is the number waiting, averaged over the time from opening to the last departure, and _tellers busy_ the share of that time the tellers were serving. The same customers with a fourth teller:

    $ ./bank -t 4 bank.ini | tail -10
    customers:      493
    waited:         155 (31.4%)
    mean wait:      0.43 minutes
      if they did:  1.37 minutes
    longest wait:   4.84 minutes
    mean service:   2.43 minutes
    mean line:      0.44
    longest line:   8
    tellers busy:   61.6%
    last left at:   485.78 minutes

and a short run with fixed times, from standard input, showing each event:

    $ printf '[arrivals]\ndraw = fixed\n[service]\ndraw = fixed\n' | ./bank -v -t 1 -a 2 -s 3 -o 7 - | head -9
         2.00  customer 1 arrives; 0 waiting
         2.00  teller 1 calls customer 1, who waited 0.00
         4.00  customer 2 arrives; 1 waiting
         5.00  customer 1 leaves teller 1 after 3.00
         5.00  teller 1 calls customer 2, who waited 1.00
         6.00  customer 3 arrives; 1 waiting
         8.00  customer 2 leaves teller 1 after 3.00
         8.00  teller 1 calls customer 3, who waited 2.00
        11.00  customer 3 leaves teller 1 after 3.00

The results are worked out from whole milliseconds with single divisions, so they print the same on every machine.

## Functions

`bank_error_t bank_init(bank_t *b, const bank_options_t *options, const sof_alloc_t *alloc)`

Initialize the bank _b_ the caller provides, to simulate what _options_ say (see Types); they are copied, so they can be on the stack. _alloc_ is the allocator for all of the bank's memory, **NULL** meaning the standard functions (see [sof_alloc](../../sof_alloc/)); it is copied into the bank, so it can be on the stack. Nothing is simulated yet. The pending events and the line point back to _b_, so it must not move while it is initialized. Returns **BANK_OK**, **BANK_NO_MEM** if memory couldn't be allocated, or **BANK_ERROR** if _b_ or _options_ is **NULL**, _alloc_ has a _malloc_ but no _free_ or _free_sized_, there are no tellers, _arrival_ is **0**, _arrival_, _service_ or _length_ is above **BANK_TIME_MAX**, or a draw isn't a `bank_draw_t`; an error leaves _b_ empty.

`bank_error_t bank_step(bank_t *b, bank_event_t *event)`

Simulate the next event and, if _event_ isn't **NULL**, set it to what happened (see Types). If a customer is waiting and a teller is free, the lowest-numbered free teller calls the customer at the front of the line (**BANK_START**). Otherwise the earliest pending event happens: a customer arrives and joins the line (**BANK_ARRIVAL**), and the next arrival is scheduled if it comes before the doors close, or a teller finishes and the customer leaves (**BANK_DEPARTURE**). At the same time, departures come before arrivals, so a teller who finishes as a customer comes in serves them without a wait. Returns **BANK_OK**; **BANK_NO_MATCH** if the simulation is over, with no customers left and none to come; **BANK_NO_MEM** if memory ran out, which leaves the bank as it was, random streams included, so the simulation can go on once memory is free and gives the same results; or **BANK_ERROR** if _b_ is **NULL** or not initialized. _event_ is changed only when **BANK_OK** is returned.

`bank_error_t bank_run(bank_t *b, bank_event_func_t cb, void *ctx)`

Simulate events until the simulation is over, calling _cb_, unless it is **NULL**, with each and _ctx_. If _cb_ returns a non-zero value, the run stops just after that event, and `bank_run()` or `bank_step()` can go on from there. Returns **BANK_OK** once the simulation is over (at once, if it already was), **BANK_STOPPED** if _cb_ stopped it, **BANK_NO_MEM** if memory ran out, as for `bank_step()`, or **BANK_ERROR** if _b_ is **NULL** or not initialized.

`bank_error_t bank_clear(bank_t *b)`

Start the simulation over, with the same options and seed: every pending event and every customer in the line is dropped, and the counts are zeroed, so it runs again just as it did the first time. It allocates nothing. Returns **BANK_OK**.

`bank_error_t bank_free(bank_t *b)`

Free everything the bank allocated. The `bank_t` itself belongs to the caller and isn't freed; it is left empty. Returns **BANK_OK**.

Every function accepts a **NULL** _b_ and returns **BANK_ERROR**. A bank that was freed, or whose `bank_init()` failed, is empty and stays that way: every function returns **BANK_ERROR** for it, until it is initialized again.

## Types

    typedef enum bank_error_t {
        BANK_OK = 0,
        BANK_NO_MEM,
        BANK_NO_MATCH,
        BANK_ERROR,
        BANK_STOPPED,       // bank_run()'s callback stopped it
    } bank_error_t;

    typedef enum bank_draw_t {
        BANK_EXPONENTIAL = 0,   // drawn from an exponential distribution with the mean
        BANK_FIXED,             // always the mean
    } bank_draw_t;

    #define BANK_TIME_MAX ((uint64_t)1 << 40)

    typedef struct bank_options_t {
        uint64_t arrival;           // mean time between arrivals, 1 to BANK_TIME_MAX
        bank_draw_t arrival_draw;
        uint64_t service;           // mean service time, 0 to BANK_TIME_MAX
        bank_draw_t service_draw;
        size_t tellers;             // at least 1
        uint64_t length;            // customers arrive before this, 0 to BANK_TIME_MAX
        uint64_t seed;
    } bank_options_t;

What `bank_init()` is to simulate. Times are whole ticks, in a unit the application chooses; the program uses milliseconds. **BANK_TIME_MAX**, 2^40, is about 35 years in milliseconds, and keeps every sum the bank keeps far from overflowing in any run that finishes. An exponential time is _mean_ times -ln _U_, rounded to a tick, where _U_ is uniform in (0, 1] in steps of 2^-32, so it is at most about 22 times the mean.

    typedef enum bank_event_type_t {
        BANK_ARRIVAL = 0,   // a customer came in and joined the line
        BANK_START,         // a teller called the customer at the front of the line
        BANK_DEPARTURE,     // a teller finished with a customer, who left
    } bank_event_type_t;

    typedef struct bank_event_t {
        bank_event_type_t type;
        uint64_t time;
        uint64_t customer;  // numbered from 1 in order of arrival
        size_t teller;      // numbered from 1; 0 for BANK_ARRIVAL
        uint64_t wait;      // BANK_START: how long the customer waited; else 0
        uint64_t service;   // BANK_START and BANK_DEPARTURE: the service time; else 0
        size_t waiting;     // customers waiting for a teller after the event
        size_t busy;        // tellers busy after the event
    } bank_event_t;

What happened at an event, as `bank_step()` and `bank_run()` report it. A customer arriving to a free teller is in the line only until the teller calls them, at the same time, so isn't counted as waiting.

    typedef int (*bank_event_func_t)(const bank_event_t *event, void *ctx)

Called by `bank_run()` with each event and the _ctx_ given to it. Return **0** to go on, or a non-zero value to stop the run, which then returns **BANK_STOPPED**. It must not call `bank_step()`, `bank_run()`, `bank_clear()` or `bank_free()` on the same bank.

    typedef struct bank_t {
        bank_options_t options;     // a copy of those given to bank_init()
        sof_rbtree_t events;        // pending arrivals and departures, by time
        sof_queue_t line;           // the customers in line, oldest first
        sof_random_t arrivals;      // the random streams of arrivals
        sof_random_t services;      // and of service times
        unsigned char *tellers;     // non-zero for each busy teller
        ...
        uint64_t now;               // the time of the last event
        uint64_t arrived;           // customers who have arrived,
        uint64_t started;           // been called by a teller,
        uint64_t served;            // and left
        uint64_t waited;            // customers who waited at all
        uint64_t wait_total;        // the sum of their waits
        uint64_t wait_max;          // and the longest
        uint64_t service_total;     // the sum of the service times of those called
        size_t waiting;             // customers waiting now
        size_t waiting_max;         // and the most there have been
        size_t busy;                // tellers busy now
        uint64_t waiting_area;      // the sum over time of waiting
        uint64_t busy_area;         // and of busy
        sof_alloc_t alloc;          // copy of the allocator; zeroed for the standard functions
        int ready;                  // 1 once bank_init() has succeeded
    } bank_t;

The fields can be read but must not be changed by the application. _waiting_area_ / _now_ is the mean number waiting, and _busy_area_ / (_now_ × _tellers_) the share of the time the tellers were busy. Once the simulation is over, _waiting_area_ equals _wait_total_, by Little's law, and _busy_area_ equals _service_total_.

## Threading

_bank.c_ has no globals and does no locking, so separate banks can be simulated on separate threads freely, each with its own `bank_t`. Every function except reading the fields writes the bank, so a bank shared between threads needs a lock around `bank_step()`, `bank_run()`, `bank_clear()` and `bank_free()`; with a readers-writer lock, readers of the fields take the read lock, and those functions the write lock. The allocator is called only from those functions, on the thread calling them. The program is single-threaded.
