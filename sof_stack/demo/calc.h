/**
 * @file calc.h
 * @author Warren Mann (warren@nonvol.io)
 * @brief An expression evaluator with variables in c, for the calc program.
 * @details A calc_t evaluates lines such as "2 * (3 + x)" and "x = 4". Numbers
 * are doubles, written as in c ("42", "1.5", ".5", "6.02e23"). The operators
 * are, from lowest precedence to highest: + and -; *, / and %; unary - and
 * +; and ^ (or **), the power, which groups right to left, so -2^2 is -4 and
 * 2^3^2 is 512. The functions abs, ceil, cos, exp, floor, ln, log (base 10),
 * round, sin, sqrt and tan each take one argument in parentheses. A line
 * "name = expression" sets a variable; pi and e are set by calc_init(), and
 * ans holds the value of the last expression that wasn't an assignment. A #
 * starts a comment that runs to the end of the line.
 *
 * Expressions are evaluated by the shunting-yard algorithm, with operators
 * on one sof_stack and values on another, and the variables are kept in a
 * sof_hashmap. The line is held in a sof_string and split into tokens as
 * it is read.
 * @version 0.0.1
 * @date 2026-11-04
 * @copyright Copyright (c) 2026
 */

#ifndef CALC_H
#define CALC_H

#include <stddef.h>

#include "sof_alloc.h"
#include "sof_hashmap.h"
#include "sof_stack.h"
#include "sof_string.h"

/// @brief The longest name a variable can have, in bytes. Longer names are
/// a syntax error in calc_eval(), and rejected by calc_set().
#define CALC_NAME_MAX 63

/// @brief The size of calc_t's message buffer, NUL included.
#define CALC_MESSAGE_SIZE 128

/**
 * @brief Error codes for the calc functions.
 */
typedef enum calc_error_t {
    CALC_OK = 0,
    CALC_NO_MEM,
    /// @brief calc_eval() met a variable that isn't set, or calc_get() or
    /// calc_unset() was given one.
    CALC_NO_MATCH,
    CALC_ERROR,
    /// @brief calc_eval() was given a line that isn't an expression or an
    /// assignment.
    CALC_SYNTAX,
    /// @brief calc_eval() met a division or modulo by zero, a number too
    /// large for a double, or an operation whose result isn't a finite
    /// number, such as sqrt(-1).
    CALC_MATH,
    /// @brief calc_eval() was given a line with nothing but spaces or a
    /// comment.
    CALC_EMPTY,
} calc_error_t;

/**
 * @brief An expression evaluator and its variables.
 * @details The application provides the calc_t (on the stack, in another
 * structure or on the heap) and sets it up with calc_init(). vars maps each
 * variable's name to its value, a double allocated with alloc that the data
 * pointer points to. values and ops are the two stacks of the shunting-yard
 * algorithm and line a copy of the line being evaluated, all kept between
 * calls so their memory is reused. After calc_eval() fails, message says why and column is the
 * 1-based byte offset in the line where the problem was found, or 0 if it
 * has no place (as when memory runs out). ready is 1 once calc_init() has
 * succeeded and 0 when it was freed or failed. These fields can be read, but
 * should not be changed by the application. Nothing points into the calc_t,
 * so it can be moved or copied between calls, as long as only one copy is
 * used afterwards.
 *
 * Every function accepts a NULL calc_t, and one that was freed or whose
 * calc_init() failed, as described for each.
 *
 * The calc_t does no locking. calc_get(), calc_message() and calc_next()
 * only read it; every other function writes it.
 */
typedef struct calc_t {
    sof_hashmap_t vars;
    sof_stack_t values;
    sof_stack_t ops;
    sof_string_t line;
    size_t column;
    char message[CALC_MESSAGE_SIZE];
    sof_alloc_t alloc;
    int ready;
} calc_t;

/**
 * @brief Evaluates one line: an expression, or an assignment to a variable.
 * @details An expression's value is stored in the variable ans as well as
 * returned. An assignment, "name = expression", sets the variable; ans is
 * left alone. On any error, no variable is changed, and calc_message() and
 * column say what went wrong and where.
 * @param calc The evaluator.
 * @param line The line, NUL-terminated, without its newline.
 * @param value If not NULL, set to the value of the expression, or of the
 * one assigned, on CALC_OK; left alone otherwise.
 * @param assigned If not NULL, set to 1 if the line was an assignment, or 0
 * otherwise.
 * @return CALC_OK, CALC_EMPTY if the line holds no expression, CALC_SYNTAX,
 * CALC_NO_MATCH if a variable isn't set, CALC_MATH, CALC_NO_MEM, or
 * CALC_ERROR if calc or line is NULL or calc isn't initialized.
 */
extern calc_error_t calc_eval(calc_t *calc, const char *line, double *value, int *assigned);

/**
 * @brief Frees everything the evaluator allocated, variables included.
 * @details The calc_t itself isn't freed (it belongs to the caller); it is
 * left empty, and acts like one whose calc_init() failed until it is
 * initialized again.
 * @param calc The evaluator.
 * @return CALC_OK, or CALC_ERROR if calc is NULL or not initialized.
 */
extern calc_error_t calc_free(calc_t *calc);

/**
 * @brief Gets a variable's value.
 * @param calc The evaluator.
 * @param name The variable's name, NUL-terminated.
 * @param value If not NULL, set to the value if the variable is set.
 * @return CALC_OK, CALC_NO_MATCH if the variable isn't set, or CALC_ERROR if
 * calc or name is NULL or calc isn't initialized.
 */
extern calc_error_t calc_get(const calc_t *calc, const char *name, double *value);

/**
 * @brief Initializes an evaluator in a calc_t the caller provides.
 * @details The variables pi and e are set.
 * @param calc The evaluator to initialize. It can be anywhere: on the stack,
 * in another structure or on the heap.
 * @param alloc The memory functions the evaluator uses for everything, or
 * NULL for the standard functions. The
 * evaluator keeps a copy, so it can be on the stack.
 * @return CALC_OK, CALC_NO_MEM, or CALC_ERROR if calc is NULL or alloc has a
 * malloc but no free; on an error, calc is left empty.
 */
extern calc_error_t calc_init(calc_t *calc, const sof_alloc_t *alloc);

/**
 * @brief Returns the message saying why the last calc_eval() failed.
 * @param calc The evaluator.
 * @return The message, such as "unknown variable 'y'", or "" if the last
 * calc_eval() succeeded or there was none, or calc is NULL or not
 * initialized. It is good until the next calc_eval() or calc_free().
 */
extern const char *calc_message(const calc_t *calc);

/**
 * @brief Returns the next variable, for looping over them.
 * @details Set *position to 0 to start, then call this until it returns NULL.
 * The variables come in no particular order. Don't call calc_eval(),
 * calc_set() or calc_unset() until the loop ends.
 *
 *     size_t position = 0;
 *     double value;
 *     const char *name;
 *     while ((name = calc_next(&calc, &position, &value)) != NULL) {
 *         ...
 *     }
 *
 * @param calc The evaluator.
 * @param position Where the loop is; updated on each call.
 * @param value If not NULL, set to the variable's value.
 * @return The next variable's name, good until the variable is unset or calc
 * is freed, or NULL when there are no more, or calc or position is NULL or
 * calc isn't initialized.
 */
extern const char *calc_next(const calc_t *calc, size_t *position, double *value);

/**
 * @brief Sets a variable, creating it if it isn't set.
 * @param calc The evaluator.
 * @param name The name, NUL-terminated: a letter or underscore, then
 * letters, digits and underscores, at most CALC_NAME_MAX bytes, and not the
 * name of a function. The evaluator keeps a copy.
 * @param value The value.
 * @return CALC_OK, CALC_NO_MEM, leaving the variable as it was, or
 * CALC_ERROR if calc or name is NULL, calc isn't initialized, or name isn't
 * a name a variable can have.
 */
extern calc_error_t calc_set(calc_t *calc, const char *name, double value);

/**
 * @brief Removes a variable.
 * @param calc The evaluator.
 * @param name The variable's name, NUL-terminated.
 * @return CALC_OK, CALC_NO_MATCH if the variable isn't set, or CALC_ERROR if
 * calc or name is NULL or calc isn't initialized.
 */
extern calc_error_t calc_unset(calc_t *calc, const char *name);

#endif // CALC_H
