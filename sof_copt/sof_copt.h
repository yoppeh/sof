/**
 * @file sof_copt.h
 * @author Warren Mann (warren@nonvol.io)
 * @brief Command line options defined in any module, merged at link time.
 * @details Each source file declares its own options as sof_copt_t
 * structures and lists them in an array marked SOF_COPT_OPTIONS. The linker
 * gathers every such array in the program into one table, which
 * sof_copt_parse() matches the command line against and sof_copt_print()
 * prints a usage message from. No linker script is needed: this works with
 * the GNU and LLVM linkers on ELF platforms and with Apple's linker on macOS.
 *
 * The table is shared by the whole program, and sof_copt_parse() records
 * what it finds in the options themselves, so a program parses its command
 * line once, from main(), before starting any threads.
 *
 * Parsing rules, for sof_copt_parse() and sof_copt_parse_ordered():
 *
 * - An argument is an option when it equals an option's name, or its part
 *   before the first '=' does ("-o=file", "--color=always"). Names are
 *   matched whole, never as prefixes, and need not start with '-'.
 * - Otherwise an argument starting with a single '-' is a group of
 *   one-character options, as with POSIX getopt: "-in" is "-i -n". Each
 *   character c must be the name "-c" of an option. An option in the group
 *   that takes an argument takes the rest of the group, after an '=' if it
 *   starts with one ("-n5", "-in5" and "-in=5" are "-i -n 5"); if nothing
 *   is left, a SOF_COPT_ARG_REQUIRED option takes the next argument and a
 *   SOF_COPT_ARG_OPTIONAL one has none. A name in the table always wins
 *   over a group, so an option named "-in" makes "-in" that option.
 * - "--" ends the options; every argument after it is an operand.
 * - "-" alone, and any other argument that doesn't start with '-', is an
 *   operand. Any other argument starting with '-' is an error.
 *
 * sof_copt_parse() reads options anywhere on the command line, as GNU
 * getopt does: options after operands are still read, and it moves the
 * operands, in their order, to the end of argv. sof_copt_parse_ordered()
 * stops at the first operand, as POSIX getopt does, for programs whose later
 * arguments belong to something else, such as a command to run.
 * @version 0.0.1
 * @date 2026-11-03
 * @copyright Copyright (c) 2026
 */

#ifndef SOF_COPT_H
#define SOF_COPT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

/**
 * @brief Marks an array of option pointers to be merged into the table.
 * @details Put it before the declaration of an array of sof_copt_t
 * pointers, in any number of source files:
 *
 *     SOF_COPT_OPTIONS static sof_copt_t *options[] = {&opt_verbose, &opt_output};
 *
 * Arrays from different files come out in the order the linker places them,
 * which depends on the platform and the order the objects are linked in;
 * the options within an array keep their order. NULL pointers in an array
 * are skipped, so an array may end with one.
 *
 * AddressSanitizer must not put redzones between the arrays, since sof_copt
 * reads them as one. gcc leaves globals with a section alone; clang is told
 * to with no_sanitize. On ELF, retain keeps the section from being discarded
 * by --gc-sections, which lld would otherwise do; Apple's linker keeps
 * anything marked used.
 */
#if defined(__clang__)
#define SOF_COPT_NO_SANITIZE __attribute__((no_sanitize("address")))
#else
#define SOF_COPT_NO_SANITIZE
#endif
#if !defined(__APPLE__) && defined(__has_attribute)
#if __has_attribute(retain)
#define SOF_COPT_RETAIN __attribute__((retain))
#endif
#endif
#ifndef SOF_COPT_RETAIN
#define SOF_COPT_RETAIN
#endif
#ifdef __APPLE__
#define SOF_COPT_OPTIONS __attribute__((section("__DATA,sof_copt"), used)) SOF_COPT_NO_SANITIZE
#else
#define SOF_COPT_OPTIONS __attribute__((section("sof_copt"), used)) SOF_COPT_RETAIN SOF_COPT_NO_SANITIZE
#endif

/**
 * @brief Error codes.
 * @details The validate and missing callbacks don't return these: they
 * return an int of the application's own, 0 to go on, and sof_copt_parse()
 * returns SOF_COPT_STOPPED when one doesn't.
 */
typedef enum sof_copt_error_t {
    /// @brief No error.
    SOF_COPT_OK = 0,
    /// @brief Not returned by sof_copt, which never allocates.
    SOF_COPT_NO_MEM,
    /// @brief An argument starting with '-' matches no option, or has a
    /// character in a group that isn't one.
    SOF_COPT_NO_MATCH,
    /// @brief A NULL argument where one is needed, or an option with an
    /// invalid type.
    SOF_COPT_ERROR,
    /// @brief A SOF_COPT_ARG_REQUIRED option is last, with no argument after
    /// it, alone ("-n") or at the end of a group ("-in").
    SOF_COPT_MISSING_ARGUMENT,
    /// @brief A SOF_COPT_ARG_NONE option was given one, as in "-v=1" or
    /// "-iv=1".
    SOF_COPT_UNEXPECTED_ARGUMENT,
    /// @brief A validate or missing callback returned non-zero, which the
    /// option it was called for keeps in its stop_value.
    SOF_COPT_STOPPED,
} sof_copt_error_t;

/**
 * @brief Whether an option takes an argument.
 */
typedef enum sof_copt_arg_t {
    /// @brief Never takes one: "-v".
    SOF_COPT_ARG_NONE = 0,
    /// @brief Always takes one, after an '=', as the next argument, or as the
    /// rest of a group: "-o=file", "-o file" or "-ofile".
    SOF_COPT_ARG_REQUIRED,
    /// @brief May take one, only after an '=' or as the rest of a group: "-c",
    /// "-c=always" or "-calways".
    SOF_COPT_ARG_OPTIONAL,
} sof_copt_arg_t;

struct sof_copt_t;

/**
 * @brief Called by sof_copt_parse() each time its option is found.
 * @details The option's value and present fields are already set. ctx is
 * the ctx given to sof_copt_parse(). Return 0 to go on, or a non-zero value
 * of the application's choosing, such as a code for what was wrong with the
 * argument, to stop parsing: sof_copt_parse() then keeps the value in the
 * option's stop_value and returns SOF_COPT_STOPPED.
 */
typedef int (*sof_copt_validate_func_t)(struct sof_copt_t *option, void *ctx);

/**
 * @brief Called by sof_copt_parse() for its option if it wasn't given.
 * @details Called once all the arguments are parsed. It can set a default
 * value, or return non-zero for an option the program can't do without.
 * ctx is the ctx given to sof_copt_parse(). Return 0 to go on, or a
 * non-zero value of the application's choosing to stop: sof_copt_parse()
 * then keeps the value in the option's stop_value and returns
 * SOF_COPT_STOPPED. The option's present field, false, tells this from a
 * stop by its validate callback.
 */
typedef int (*sof_copt_missing_func_t)(struct sof_copt_t *option, void *ctx);

/**
 * @brief One command line option.
 * @details The application sets option, description, type, validate and
 * missing, and lists the option in a SOF_COPT_OPTIONS array.
 * sof_copt_parse() sets value, present and stop_value. The library never
 * changes the other fields.
 */
typedef struct sof_copt_t {
    /// @brief The option as it is typed, such as "-v", "--output" or "add".
    /// @details Matched whole, never as a prefix. NULL makes an entry that
    /// isn't an option, whose description sof_copt_print() prints as a
    /// heading.
    const char *option;
    /// @brief One line describing the option, for sof_copt_print(). May be
    /// NULL.
    const char *description;
    /// @brief Whether the option takes an argument.
    sof_copt_arg_t type;
    /// @brief Called each time the option is found. May be NULL.
    sof_copt_validate_func_t validate;
    /// @brief Called if the option isn't given. May be NULL.
    sof_copt_missing_func_t missing;
    /// @brief The option's argument, pointing into one of argv's strings, or
    /// NULL if it had none. Set by sof_copt_parse(); the last one wins if the option is
    /// given more than once.
    const char *value;
    /// @brief Set by sof_copt_parse(): true if the option was given.
    bool present;
    /// @brief Set by sof_copt_parse(): what the option's validate or missing
    /// callback returned if it stopped parsing, or 0.
    int stop_value;
} sof_copt_t;

/**
 * @brief Returns the number of options in the table.
 * @details Headings, entries whose option is NULL, are counted too.
 * @return The number of entries sof_copt_get() returns.
 */
extern size_t sof_copt_count(void);

/**
 * @brief Finds an option by name.
 * @param name The option as typed, such as "-v". NULL finds nothing.
 * @return The first option in the table with that name, or NULL if there is
 * none.
 */
extern sof_copt_t *sof_copt_find(const char *name);

/**
 * @brief Returns an entry of the table by position.
 * @details Loops over the table in the order sof_copt_parse() and
 * sof_copt_print() use, with index from 0 to sof_copt_count() - 1.
 * @param index The entry's position.
 * @return The entry, or NULL if index is sof_copt_count() or more.
 */
extern sof_copt_t *sof_copt_get(size_t index);

/**
 * @brief Parses the command line against the table, reading options
 * anywhere in it.
 * @details First clears every option's value, present and stop_value
 * fields. Then reads
 * the arguments from argv[1] on, by the rules at the top of this file. Each
 * option found sets its value and present fields and calls its validate
 * callback; a group sets its options from left to right. Operands are
 * skipped, so options after them are read too, until "--". The pointers in
 * argv are reordered as it goes: the options, their arguments and any "--"
 * come first, in their order, then the operands, in theirs, so that
 * argv[*index] to argv[argc - 1] are the operands. The strings aren't
 * changed. Once the arguments are parsed, each missing callback is called for
 * an option that wasn't given, in table order. Every callback gets ctx.
 * @param argc The number of arguments, as passed to main().
 * @param argv The arguments, as passed to main(). Their order is changed,
 * as above; on an error, they are left partly reordered.
 * @param ctx Passed to the callbacks; never used by sof_copt.
 * @param index If not NULL, set to the index in argv of the first operand,
 * or argc if there are none; or, if an argument caused an error, to that
 * argument's index in argv as it is left.
 * @param option If not NULL, set to the option the error was found with, or
 * NULL if there was no error or no option (SOF_COPT_NO_MATCH).
 * @return SOF_COPT_OK, SOF_COPT_NO_MATCH, SOF_COPT_MISSING_ARGUMENT,
 * SOF_COPT_UNEXPECTED_ARGUMENT, SOF_COPT_STOPPED if a callback returned
 * non-zero (*option is the option it was called for, which keeps the value in
 * its stop_value), or SOF_COPT_ERROR if argv is NULL (with argc above 0),
 * one of the arguments before argc is NULL, or an option's type isn't one of
 * the sof_copt_arg_t values.
 */
extern sof_copt_error_t sof_copt_parse(int argc, char *argv[], void *ctx, int *index, sof_copt_t **option);

/**
 * @brief Parses the command line against the table, stopping at the first
 * operand.
 * @details The same as sof_copt_parse(), except that parsing stops at "--",
 * which is skipped, or at the first operand: that one and every argument
 * after it, whatever it looks like, are operands. argv isn't changed. For a
 * program that passes the rest of its command line on, as to a command it
 * runs.
 * @param argc The number of arguments, as passed to main().
 * @param argv The arguments, as passed to main(). They aren't changed.
 * @param ctx Passed to the callbacks; never used by sof_copt.
 * @param index If not NULL, set to the index in argv of the first operand,
 * or argc if there are none; or, if an argument caused an error, to that
 * argument's index.
 * @param option As for sof_copt_parse().
 * @return As for sof_copt_parse().
 */
extern sof_copt_error_t sof_copt_parse_ordered(int argc, char *const argv[], void *ctx, int *index, sof_copt_t **option);

/**
 * @brief Prints a usage message listing every option.
 * @details Each option gets a line, indented by two spaces, with its name,
 * "=<arg>" or "[=arg]" if it takes an argument, then its description after a
 * line of dots, so the descriptions line up. An entry whose option is NULL
 * prints its description on a line of its own, not indented, as a heading.
 * @param stream Where to print, such as stdout.
 * @return SOF_COPT_OK, or SOF_COPT_ERROR if stream is NULL or writing failed.
 */
extern sof_copt_error_t sof_copt_print(FILE *stream);

#endif // SOF_COPT_H
