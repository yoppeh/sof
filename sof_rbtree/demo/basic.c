/**
 * @file basic.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief A BASIC interpreter in c, for the basic program.
 * @details Lines are split into tokens by basic_scan.c when they are
 * entered. Running a line walks its tokens: each statement is parsed and
 * done as it is met, and expressions are evaluated by recursive descent,
 * one function per level of precedence. A place in the program is a line,
 * its node in the tree of lines, and the index of a token in it, so GOTO,
 * GOSUB, RETURN, NEXT and CONT only move the place.
 * @version 0.0.1
 * @date 2026-11-04
 * @copyright Copyright (c) 2026
 */

#include <errno.h>
#include <float.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "basic.h"
#include "basic_scan.h"

/// @brief What execute() returns when the program ran an END statement or
/// ran off its last line.
#define FLOW_END 100

/// @brief What execute() returns when the program ran a STOP statement.
#define FLOW_STOP 101

/// @brief How deeply expressions, parentheses and DEF functions can nest.
#define DEPTH_MAX 200

/// @brief The most GOSUBs without a RETURN, and the most FOR loops running.
#define STACK_MAX 10000

/// @brief The most elements an array can have.
#define ARRAY_MAX 10000000

/// @brief The most subscripts an array can have.
#define DIMS_MAX 4

/// @brief The most parameters a DEF function can have.
#define PARAMS_MAX 8

/// @brief The longest string, in bytes.
#define STRING_MAX 1048576

/// @brief The room left in a message for what goes after "line N: ".
#define TEXT_SIZE (BASIC_MESSAGE_SIZE - 40)

/// @brief A simple variable: a number, or a string if its name ends in '$'.
typedef struct var_t {
    double number;
    /// @brief The string, dead (and so empty) until one is assigned.
    sof_string_t string;
} var_t;

/// @brief An array, of numbers or of strings.
typedef struct array_t {
    int is_string;
    size_t dims;
    /// @brief The lowest subscript, OPTION BASE's when it was made.
    int base;
    /// @brief The highest subscript of each dimension.
    size_t upper[DIMS_MAX];
    size_t count;
    /// @brief count numbers, or count strings, dead until assigned.
    double *numbers;
    sof_string_t *strings;
} array_t;

/// @brief Where a DEF function is defined.
typedef struct def_t {
    sof_rbtree_node_t *node;
    basic_line_t *line;
    /// @brief The token its expression starts at.
    size_t pc;
    size_t params;
    /// @brief The tokens naming its parameters.
    size_t param_tokens[PARAMS_MAX];
} def_t;

/// @brief A place in the program: a token of a line. node is NULL for the
/// line being run at once.
typedef struct place_t {
    sof_rbtree_node_t *node;
    basic_line_t *line;
    size_t pc;
} place_t;

/// @brief A GOSUB waiting for its RETURN.
typedef struct gosub_t {
    /// @brief Where RETURN goes back to.
    place_t place;
    /// @brief The FOR loops running when it was made, which RETURN goes back
    /// to as well.
    size_t loops;
} gosub_t;

/// @brief A FOR loop running.
typedef struct loop_t {
    char name[BASIC_NAME_MAX + 1];
    double *control;
    double limit;
    double step;
    /// @brief Just after the FOR statement, where NEXT goes back to.
    place_t place;
} loop_t;

/// @brief A DATA item: its line and token.
typedef struct datum_t {
    basic_line_t *line;
    size_t token;
} datum_t;

/// @brief The value of an expression.
typedef struct value_t {
    int is_string;
    double number;
    /// @brief The string, owned by the value, if is_string is set.
    sof_string_t string;
} value_t;

/// @brief A variable or array element to assign to.
typedef struct target_t {
    int is_string;
    double *number;
    sof_string_t *string;
} target_t;

static int accept_op(basic_t *b, int op);
static basic_error_t args(basic_t *b, value_t *v, size_t min, size_t max, size_t *count, const char *what);
static basic_error_t array_create(basic_t *b, const char *name, size_t dims, const size_t *upper, array_t **result);
static void array_delete(sof_hashmap_element_t *element, void *ctx);
static basic_error_t array_element(basic_t *b, const char *name, array_t **result, size_t *index);
static basic_error_t assign(basic_t *b, const target_t *t, value_t *v);
static int at_end(const basic_t *b);
static int at_kw(const basic_t *b, int kw);
static int at_op(const basic_t *b, int op);
static basic_error_t builtin(basic_t *b, value_t *out);
static basic_error_t check_types(basic_t *b, value_t *v, size_t count, const char *types);
static void clear_program(basic_t *b);
static void clear_variables(basic_t *b);
static int compare_lines(const void *a, const void *b, void *ctx);
static void def_delete(sof_hashmap_element_t *element, void *ctx);
static void describe(const basic_t *b, char *buffer, size_t size);
static basic_error_t end_of_statement(basic_t *b);
static int execute(basic_t *b);
static basic_error_t expect_op(basic_t *b, int op);
static basic_error_t expected(basic_t *b, const char *what);
static basic_error_t expr(basic_t *b, value_t *out);
static basic_error_t expr_add(basic_t *b, value_t *out);
static basic_error_t expr_and(basic_t *b, value_t *out);
static basic_error_t expr_mul(basic_t *b, value_t *out);
static basic_error_t expr_not(basic_t *b, value_t *out);
static basic_error_t expr_or(basic_t *b, value_t *out);
static basic_error_t expr_pow(basic_t *b, value_t *out);
static basic_error_t expr_rel(basic_t *b, value_t *out);
static basic_error_t expr_unary(basic_t *b, value_t *out);
static basic_error_t fail(basic_t *b, basic_error_t code, const char *format, ...);
static sof_rbtree_node_t *find_line(const basic_t *b, unsigned long number);
static basic_error_t finish(basic_t *b, int flow);
static basic_error_t fn_call(basic_t *b, value_t *out);
static void forget_immediate(basic_t *b);
static basic_error_t function_number(basic_t *b, int kw, double x, value_t *out);
static basic_error_t function_string(basic_t *b, int kw, value_t *v, size_t count, value_t *out);
static basic_error_t immediate_only(basic_t *b, int kw);
static void invalidate(basic_t *b);
static int is_string_name(const char *name);
static void line_delete(sof_rbtree_node_t *node, void *ctx);
static basic_error_t line_number(basic_t *b, unsigned long *number);
static basic_error_t list_lines(basic_t *b, unsigned long first, unsigned long last);
static basic_error_t load_file(basic_t *b, const char *path);
static size_t loop_index(basic_t *b, const char *name);
static basic_error_t mismatch(basic_t *b);
static basic_error_t number_result(basic_t *b, double x, value_t *out);
static basic_error_t numbered_line(basic_t *b, const char *text, size_t length);
static basic_error_t numeric_expr(basic_t *b, double *x);
static basic_error_t out(basic_t *b, const char *text, size_t length);
static basic_error_t out_item(basic_t *b, const char *text, size_t length);
static basic_error_t parse_def(basic_t *b);
static const basic_token_t *peek(const basic_t *b);
static basic_error_t power(basic_t *b, double x, double y, value_t *out);
static basic_error_t prepare(basic_t *b);
static basic_error_t primary(basic_t *b, value_t *out);
static basic_error_t print_number(basic_t *b, double x);
static int read_line(FILE *f, sof_string_t *line);
static void release(basic_t *b);
static void remove_loop(basic_t *b, const char *name);
static double round_half(double x);
static basic_error_t run_immediate(basic_t *b, const char *text, size_t length);
static basic_error_t save_file(basic_t *b, const char *path);
static basic_error_t scalar_get(basic_t *b, const char *name, value_t *out);
static basic_error_t scalar_var(basic_t *b, const char *name, var_t **result);
static void set_place(basic_t *b, sof_rbtree_node_t *node, size_t pc);
static basic_error_t skip_to_next(basic_t *b, const char *name);
static basic_error_t spaces(basic_t *b, size_t count);
static int statement(basic_t *b);
static int stmt_cont(basic_t *b);
static int stmt_dim(basic_t *b);
static int stmt_for(basic_t *b);
static int stmt_gosub(basic_t *b);
static int stmt_goto(basic_t *b);
static int stmt_if(basic_t *b);
static int stmt_input(basic_t *b);
static int stmt_let(basic_t *b);
static int stmt_list(basic_t *b);
static int stmt_load_save(basic_t *b, int kw);
static int stmt_next(basic_t *b);
static int stmt_on(basic_t *b);
static int stmt_option(basic_t *b);
static int stmt_print(basic_t *b);
static int stmt_randomize(basic_t *b);
static int stmt_read(basic_t *b);
static int stmt_restore(basic_t *b);
static int stmt_return(basic_t *b);
static int stmt_run(basic_t *b);
static basic_error_t store_line(basic_t *b, unsigned long number, const char *text, size_t length);
static basic_error_t string_value(basic_t *b, value_t *out, const char *data, size_t length);
static basic_error_t tab(basic_t *b, double x);
static basic_error_t target(basic_t *b, target_t *t);
static const basic_token_t *token_at(const basic_line_t *line, size_t i);
static void value_free(value_t *v);
static void var_delete(sof_hashmap_element_t *element, void *ctx);
static void warn(basic_t *b, const char *format, ...);
static basic_error_t zone(basic_t *b);

/// @brief Moves past the operator op if it is next, and returns 1, or
/// returns 0.
static int accept_op(basic_t *b, int op) {
    if (at_op(b, op)) {
        b->pc++;
        return 1;
    }
    return 0;
}

/// @brief Evaluates a function's arguments, "(a, b, ...)", into v, from min
/// to max of them, and sets *count to their number. On an error, nothing is
/// left in v to free.
static basic_error_t args(basic_t *b, value_t *v, size_t min, size_t max, size_t *count, const char *what) {
    basic_error_t rc;
    size_t n = 0;
    *count = 0;
    if ((rc = expect_op(b, '(')) != BASIC_OK) {
        return rc;
    }
    for (;;) {
        if ((rc = expr(b, &v[n])) != BASIC_OK) {
            break;
        }
        n++;
        if (n == max || !accept_op(b, ',')) {
            rc = expect_op(b, ')');
            break;
        }
    }
    if (rc == BASIC_OK && n < min) {
        rc = fail(b, BASIC_SYNTAX, "syntax error: %s needs %zu arguments", what, min);
    }
    if (rc != BASIC_OK) {
        for (size_t i = 0; i < n; i++) {
            value_free(&v[i]);
        }
        return rc;
    }
    *count = n;
    return BASIC_OK;
}

/// @brief Makes an array, with dims dimensions whose highest subscripts are
/// upper, each at least the base.
static basic_error_t array_create(basic_t *b, const char *name, size_t dims, const size_t *upper, array_t **result) {
    size_t count = 1;
    for (size_t d = 0; d < dims; d++) {
        size_t n = upper[d] - (size_t)b->base + 1;
        if (n > ARRAY_MAX / count) {
            return fail(b, BASIC_RUNTIME, "array %s is too large", name);
        }
        count *= n;
    }
    array_t *a = sof_alloc_calloc(&b->alloc, 1, sizeof(array_t));
    if (a == NULL) {
        return fail(b, BASIC_NO_MEM, "out of memory");
    }
    a->is_string = is_string_name(name);
    a->dims = dims;
    a->base = b->base;
    a->count = count;
    memcpy(a->upper, upper, dims * sizeof(size_t));
    if (a->is_string) {
        a->strings = sof_alloc_calloc(&b->alloc, count, sizeof(sof_string_t));
    } else {
        a->numbers = sof_alloc_calloc(&b->alloc, count, sizeof(double));
    }
    sof_hashmap_element_t *e = NULL;
    if (a->strings != NULL || a->numbers != NULL) {
        e = sof_hashmap_insert(&b->arrays, name, NULL);
    }
    if (e == NULL) {
        sof_alloc_free(&b->alloc, a->strings, count * sizeof(sof_string_t));
        sof_alloc_free(&b->alloc, a->numbers, count * sizeof(double));
        sof_alloc_free(&b->alloc, a, sizeof(array_t));
        return fail(b, BASIC_NO_MEM, "out of memory");
    }
    e->data = a;
    *result = a;
    return BASIC_OK;
}

/// @brief Frees an array, for the arrays hashmap.
static void array_delete(sof_hashmap_element_t *element, void *ctx) {
    basic_t *b = ctx;
    array_t *a = element->data;
    if (a == NULL) {
        return;
    }
    if (a->strings != NULL) {
        for (size_t i = 0; i < a->count; i++) {
            sof_string_free(&a->strings[i]);
        }
        sof_alloc_free(&b->alloc, a->strings, a->count * sizeof(sof_string_t));
    }
    sof_alloc_free(&b->alloc, a->numbers, a->count * sizeof(double));
    sof_alloc_free(&b->alloc, a, sizeof(array_t));
}

/// @brief Reads the subscripts "(i, j, ...)" of an element of array name,
/// making the array with upper bounds of 10 if it doesn't exist, and finds
/// the element's index.
static basic_error_t array_element(basic_t *b, const char *name, array_t **result, size_t *index) {
    double subscripts[DIMS_MAX];
    size_t n = 0;
    basic_error_t rc;
    b->pc++;
    for (;;) {
        double x;
        if ((rc = numeric_expr(b, &x)) != BASIC_OK) {
            return rc;
        }
        if (n == DIMS_MAX) {
            return fail(b, BASIC_SYNTAX, "syntax error: more than %d subscripts", DIMS_MAX);
        }
        subscripts[n++] = round_half(x);
        if (!accept_op(b, ',')) {
            break;
        }
    }
    if ((rc = expect_op(b, ')')) != BASIC_OK) {
        return rc;
    }
    sof_hashmap_element_t *e = sof_hashmap_find(&b->arrays, name);
    array_t *a;
    if (e == NULL) {
        size_t upper[DIMS_MAX] = {10, 10, 10, 10};
        if ((rc = array_create(b, name, n, upper, &a)) != BASIC_OK) {
            return rc;
        }
    } else {
        a = e->data;
    }
    if (a->dims != n) {
        return fail(b, BASIC_RUNTIME, "%s has %zu subscript%s, not %zu", name, a->dims, a->dims == 1 ? "" : "s", n);
    }
    size_t i = 0;
    for (size_t d = 0; d < n; d++) {
        if (subscripts[d] < a->base || subscripts[d] > (double)a->upper[d]) {
            return fail(b, BASIC_RUNTIME, "subscript out of range");
        }
        i = i * (a->upper[d] - (size_t)a->base + 1) + (size_t)subscripts[d] - (size_t)a->base;
    }
    *result = a;
    *index = i;
    return BASIC_OK;
}

/// @brief Assigns a value to a target, taking over its string.
static basic_error_t assign(basic_t *b, const target_t *t, value_t *v) {
    if (t->is_string != v->is_string) {
        value_free(v);
        return mismatch(b);
    }
    if (t->is_string) {
        sof_string_free(t->string);
        *t->string = v->string;
        v->is_string = 0;
    } else {
        *t->number = v->number;
    }
    return BASIC_OK;
}

/// @brief Returns 1 at the end of a statement: a colon or the end of the
/// line.
static int at_end(const basic_t *b) {
    const basic_token_t *t = peek(b);
    return t == NULL || (t->type == BASIC_TOKEN_OP && t->id == ':');
}

/// @brief Returns 1 if the keyword kw is next.
static int at_kw(const basic_t *b, int kw) {
    const basic_token_t *t = peek(b);
    return t != NULL && t->type == BASIC_TOKEN_KEYWORD && t->id == kw;
}

/// @brief Returns 1 if the operator op is next.
static int at_op(const basic_t *b, int op) {
    const basic_token_t *t = peek(b);
    return t != NULL && t->type == BASIC_TOKEN_OP && t->id == op;
}

/// @brief Evaluates a call of a built-in function, whose keyword is next.
static basic_error_t builtin(basic_t *b, value_t *out) {
    const basic_token_t *t = peek(b);
    int kw = t->id;
    const char *what = basic_keyword_name(kw);
    value_t v[3];
    size_t n;
    basic_error_t rc;
    memset(v, 0, sizeof(v));
    switch (kw) {
    case BASIC_KW_RND:
        b->pc++;
        // RND takes no argument in ECMA-55; one is allowed, and ignored.
        if (at_op(b, '(')) {
            if ((rc = args(b, v, 1, 1, &n, what)) != BASIC_OK || (rc = check_types(b, v, n, "N")) != BASIC_OK) {
                return rc;
            }
        }
        out->is_string = 0;
        out->number = sof_random_double(&b->random);
        return BASIC_OK;
    case BASIC_KW_ABS:
    case BASIC_KW_ATN:
    case BASIC_KW_CHR:
    case BASIC_KW_COS:
    case BASIC_KW_EXP:
    case BASIC_KW_INT:
    case BASIC_KW_LOG:
    case BASIC_KW_SGN:
    case BASIC_KW_SIN:
    case BASIC_KW_SQR:
    case BASIC_KW_STR:
    case BASIC_KW_TAN:
        b->pc++;
        if ((rc = args(b, v, 1, 1, &n, what)) != BASIC_OK || (rc = check_types(b, v, n, "N")) != BASIC_OK) {
            return rc;
        }
        return function_number(b, kw, v[0].number, out);
    case BASIC_KW_ASC:
    case BASIC_KW_LEN:
    case BASIC_KW_VAL:
        b->pc++;
        if ((rc = args(b, v, 1, 1, &n, what)) != BASIC_OK || (rc = check_types(b, v, n, "S")) != BASIC_OK) {
            return rc;
        }
        break;
    case BASIC_KW_LEFT:
    case BASIC_KW_RIGHT:
        b->pc++;
        if ((rc = args(b, v, 2, 2, &n, what)) != BASIC_OK || (rc = check_types(b, v, n, "SN")) != BASIC_OK) {
            return rc;
        }
        break;
    case BASIC_KW_MID:
        b->pc++;
        if ((rc = args(b, v, 2, 3, &n, what)) != BASIC_OK || (rc = check_types(b, v, n, "SNN")) != BASIC_OK) {
            return rc;
        }
        break;
    case BASIC_KW_INSTR:
        b->pc++;
        if ((rc = args(b, v, 2, 3, &n, what)) != BASIC_OK || (rc = check_types(b, v, n, n == 3 ? "NSS" : "SS")) != BASIC_OK) {
            return rc;
        }
        break;
    case BASIC_KW_TAB:
        return fail(b, BASIC_SYNTAX, "syntax error: TAB can only be used in PRINT");
    default:
        return expected(b, "an expression");
    }
    rc = function_string(b, kw, v, n, out);
    for (size_t i = 0; i < n; i++) {
        value_free(&v[i]);
    }
    return rc;
}

/// @brief Checks that count values are of the types given, 'S' for a string
/// and 'N' for a number, freeing them all if not.
static basic_error_t check_types(basic_t *b, value_t *v, size_t count, const char *types) {
    for (size_t i = 0; i < count; i++) {
        if (v[i].is_string != (types[i] == 'S')) {
            for (size_t j = 0; j < count; j++) {
                value_free(&v[j]);
            }
            return mismatch(b);
        }
    }
    return BASIC_OK;
}

/// @brief Deletes the program and its variables.
static void clear_program(basic_t *b) {
    sof_rbtree_clear(&b->lines, NULL);
    clear_variables(b);
    invalidate(b);
}

/// @brief Deletes the variables and arrays, and the FOR loops and GOSUBs
/// that refer to them, and sets OPTION BASE back to 0.
static void clear_variables(basic_t *b) {
    sof_stack_clear(&b->loops);
    sof_stack_clear(&b->gosubs);
    sof_hashmap_clear(&b->scalars);
    sof_hashmap_clear(&b->arrays);
    b->base = 0;
}

/// @brief Orders line numbers, which are the keys of the lines tree.
static int compare_lines(const void *a, const void *b, void *ctx) {
    (void)ctx;
    uintptr_t x = (uintptr_t)a, y = (uintptr_t)b;
    return x < y ? -1 : x > y;
}

/// @brief Frees a DEF function, for the functions hashmap.
static void def_delete(sof_hashmap_element_t *element, void *ctx) {
    basic_t *b = ctx;
    sof_alloc_free(&b->alloc, element->data, sizeof(def_t));
}

/// @brief Describes the next token for a message.
static void describe(const basic_t *b, char *buffer, size_t size) {
    const basic_token_t *t = peek(b);
    if (t == NULL) {
        snprintf(buffer, size, "the end of the line");
        return;
    }
    const char *text = b->line->text.data + t->offset;
    int length = t->length > 20 ? 20 : (int)t->length;
    const char *more = t->length > 20 ? "..." : "";
    if (t->type == BASIC_TOKEN_STRING) {
        snprintf(buffer, size, "\"%.*s%s\"", length, text, more);
    } else {
        snprintf(buffer, size, "'%.*s%s'", length, text, more);
    }
}

/// @brief Checks that the statement ends here.
static basic_error_t end_of_statement(basic_t *b) {
    if (at_end(b)) {
        return BASIC_OK;
    }
    return expected(b, "the end of the statement");
}

/// @brief Runs statements from the place set, until the line run at once
/// ends, or the program ends, stops or fails.
/// @return BASIC_OK, FLOW_END, FLOW_STOP or an error code.
static int execute(basic_t *b) {
    for (;;) {
        const basic_token_t *t = peek(b);
        if (t == NULL) {
            if (b->node == NULL) {
                return BASIC_OK;
            }
            sof_rbtree_node_t *next = sof_rbtree_next(&b->lines, b->node);
            if (next == NULL) {
                return FLOW_END;
            }
            set_place(b, next, 0);
            continue;
        }
        if (t->type == BASIC_TOKEN_OP && t->id == ':') {
            b->pc++;
            continue;
        }
        b->jumped = 0;
        int rc = statement(b);
        if (rc != BASIC_OK) {
            return rc;
        }
        if (!b->jumped && (rc = end_of_statement(b)) != BASIC_OK) {
            return rc;
        }
    }
}

/// @brief Moves past the operator op, or fails if it isn't next.
static basic_error_t expect_op(basic_t *b, int op) {
    if (accept_op(b, op)) {
        return BASIC_OK;
    }
    char what[8];
    snprintf(what, sizeof(what), "'%c'", op);
    return expected(b, what);
}

/// @brief Fails with "syntax error: expected what, found" the next token.
static basic_error_t expected(basic_t *b, const char *what) {
    char found[40];
    describe(b, found, sizeof(found));
    return fail(b, BASIC_SYNTAX, "syntax error: expected %s, found %s", what, found);
}

/// @brief Evaluates an expression. On an error, out holds nothing to free.
static basic_error_t expr(basic_t *b, value_t *out) {
    if (b->depth >= DEPTH_MAX) {
        return fail(b, BASIC_RUNTIME, "expression too complex");
    }
    b->depth++;
    basic_error_t rc = expr_or(b, out);
    b->depth--;
    return rc;
}

/// @brief Evaluates a sum: terms joined by + and -, or strings joined by +.
static basic_error_t expr_add(basic_t *b, value_t *out) {
    basic_error_t rc = expr_mul(b, out);
    while (rc == BASIC_OK && (at_op(b, '+') || at_op(b, '-'))) {
        int op = peek(b)->id;
        value_t r = {0};
        b->pc++;
        if ((rc = expr_mul(b, &r)) != BASIC_OK) {
            value_free(out);
            return rc;
        }
        if (out->is_string != r.is_string || (out->is_string && op == '-')) {
            value_free(out);
            value_free(&r);
            return mismatch(b);
        }
        if (out->is_string) {
            if (out->string.length + r.string.length > STRING_MAX) {
                rc = fail(b, BASIC_RUNTIME, "string too long");
            } else if (sof_string_append(&out->string, &r.string) != SOF_STRING_OK) {
                rc = fail(b, BASIC_NO_MEM, "out of memory");
            }
            value_free(&r);
            if (rc != BASIC_OK) {
                value_free(out);
            }
        } else {
            rc = number_result(b, op == '+' ? out->number + r.number : out->number - r.number, out);
        }
    }
    return rc;
}

/// @brief Evaluates conditions joined by AND.
static basic_error_t expr_and(basic_t *b, value_t *out) {
    basic_error_t rc = expr_not(b, out);
    while (rc == BASIC_OK && at_kw(b, BASIC_KW_AND)) {
        value_t r = {0};
        b->pc++;
        if ((rc = expr_not(b, &r)) != BASIC_OK) {
            value_free(out);
            return rc;
        }
        if (out->is_string || r.is_string) {
            value_free(out);
            value_free(&r);
            return mismatch(b);
        }
        out->number = out->number != 0 && r.number != 0 ? -1 : 0;
    }
    return rc;
}

/// @brief Evaluates a product: factors joined by *, / and MOD.
static basic_error_t expr_mul(basic_t *b, value_t *out) {
    basic_error_t rc = expr_unary(b, out);
    while (rc == BASIC_OK && (at_op(b, '*') || at_op(b, '/') || at_kw(b, BASIC_KW_MOD))) {
        const basic_token_t *t = peek(b);
        int op = t->type == BASIC_TOKEN_KEYWORD ? 'M' : t->id;
        value_t r = {0};
        b->pc++;
        if ((rc = expr_unary(b, &r)) != BASIC_OK) {
            value_free(out);
            return rc;
        }
        if (out->is_string || r.is_string) {
            value_free(out);
            value_free(&r);
            return mismatch(b);
        }
        double x = out->number, y = r.number;
        if (op == '*') {
            rc = number_result(b, x * y, out);
        } else if (y == 0) {
            if (op == 'M') {
                return fail(b, BASIC_RUNTIME, "division by zero in MOD");
            }
            // ECMA-55: report it and go on with the largest number.
            warn(b, "division by zero");
            out->number = x < 0 ? -DBL_MAX : DBL_MAX;
        } else if (op == '/') {
            rc = number_result(b, x / y, out);
        } else {
            rc = number_result(b, x - y * floor(x / y), out);
        }
    }
    return rc;
}

/// @brief Evaluates NOT, as many times as it is given, before a comparison.
static basic_error_t expr_not(basic_t *b, value_t *out) {
    size_t nots = 0;
    while (at_kw(b, BASIC_KW_NOT)) {
        b->pc++;
        nots++;
    }
    basic_error_t rc = expr_rel(b, out);
    if (rc != BASIC_OK || nots == 0) {
        return rc;
    }
    if (out->is_string) {
        value_free(out);
        return mismatch(b);
    }
    if (nots % 2 == 1) {
        out->number = out->number == 0 ? -1 : 0;
    } else {
        out->number = out->number != 0 ? -1 : 0;
    }
    return BASIC_OK;
}

/// @brief Evaluates conditions joined by OR: the lowest precedence.
static basic_error_t expr_or(basic_t *b, value_t *out) {
    basic_error_t rc = expr_and(b, out);
    while (rc == BASIC_OK && at_kw(b, BASIC_KW_OR)) {
        value_t r = {0};
        b->pc++;
        if ((rc = expr_and(b, &r)) != BASIC_OK) {
            value_free(out);
            return rc;
        }
        if (out->is_string || r.is_string) {
            value_free(out);
            value_free(&r);
            return mismatch(b);
        }
        out->number = out->number != 0 || r.number != 0 ? -1 : 0;
    }
    return rc;
}

/// @brief Evaluates powers, which group from left to right as in ECMA-55, so
/// 2^3^2 is 64. The exponent may have a sign of its own.
static basic_error_t expr_pow(basic_t *b, value_t *out) {
    basic_error_t rc = primary(b, out);
    while (rc == BASIC_OK && at_op(b, '^')) {
        b->pc++;
        int negative = 0;
        while (at_op(b, '-') || at_op(b, '+')) {
            negative ^= peek(b)->id == '-';
            b->pc++;
        }
        value_t r = {0};
        if ((rc = primary(b, &r)) != BASIC_OK) {
            value_free(out);
            return rc;
        }
        if (out->is_string || r.is_string) {
            value_free(out);
            value_free(&r);
            return mismatch(b);
        }
        rc = power(b, out->number, negative ? -r.number : r.number, out);
    }
    return rc;
}

/// @brief Evaluates comparisons, of two numbers or two strings, which are
/// -1 if true and 0 if false.
static basic_error_t expr_rel(basic_t *b, value_t *out) {
    basic_error_t rc = expr_add(b, out);
    for (;;) {
        const basic_token_t *t = peek(b);
        if (rc != BASIC_OK || t == NULL || t->type != BASIC_TOKEN_OP) {
            return rc;
        }
        int op = t->id;
        if (op != '=' && op != '<' && op != '>' && op != BASIC_OP_LE && op != BASIC_OP_GE && op != BASIC_OP_NE) {
            return rc;
        }
        value_t r = {0};
        b->pc++;
        if ((rc = expr_add(b, &r)) != BASIC_OK) {
            value_free(out);
            return rc;
        }
        if (out->is_string != r.is_string) {
            value_free(out);
            value_free(&r);
            return mismatch(b);
        }
        int c;
        if (out->is_string) {
            c = sof_string_cmp(&out->string, &r.string);
        } else {
            c = out->number < r.number ? -1 : out->number > r.number;
        }
        value_free(out);
        value_free(&r);
        int truth = op == '=' ? c == 0 : op == '<' ? c < 0 : op == '>' ? c > 0 : op == BASIC_OP_LE ? c <= 0 : op == BASIC_OP_GE ? c >= 0 : c != 0;
        out->number = truth ? -1 : 0;
    }
}

/// @brief Evaluates signs before a power, so -2^2 is -4.
static basic_error_t expr_unary(basic_t *b, value_t *out) {
    int negative = 0, signs = 0;
    while (at_op(b, '-') || at_op(b, '+')) {
        negative ^= peek(b)->id == '-';
        b->pc++;
        signs++;
    }
    basic_error_t rc = expr_pow(b, out);
    if (rc != BASIC_OK || signs == 0) {
        return rc;
    }
    if (out->is_string) {
        value_free(out);
        return mismatch(b);
    }
    if (negative) {
        out->number = -out->number;
    }
    return BASIC_OK;
}

/// @brief Sets the message, after "line N: " when a program line is
/// running, and returns code.
static basic_error_t fail(basic_t *b, basic_error_t code, const char *format, ...) {
    char text[TEXT_SIZE];
    va_list ap;
    va_start(ap, format);
    vsnprintf(text, sizeof(text), format, ap);
    va_end(ap);
    if (b->line != NULL && b->line->number != 0) {
        snprintf(b->message, sizeof(b->message), "line %lu: %s", b->line->number, text);
    } else {
        snprintf(b->message, sizeof(b->message), "%s", text);
    }
    return code;
}

/// @brief Returns the node of a line, or NULL if there is no such line.
static sof_rbtree_node_t *find_line(const basic_t *b, unsigned long number) {
    return sof_rbtree_find(&b->lines, (const void *)(uintptr_t)number);
}

/// @brief Ends the running of a line run at once: turns execute()'s result
/// into an error code, ends a partly written output line and frees the
/// line.
static basic_error_t finish(basic_t *b, int flow) {
    basic_error_t code;
    if (flow == BASIC_OK || flow == FLOW_END) {
        code = BASIC_OK;
        b->message[0] = '\0';
    } else if (flow == FLOW_STOP) {
        code = BASIC_STOPPED;
        if (b->line != NULL && b->line->number != 0) {
            snprintf(b->message, sizeof(b->message), "stopped at line %lu", b->line->number);
        } else {
            snprintf(b->message, sizeof(b->message), "stopped");
        }
    } else {
        code = (basic_error_t)flow;
    }
    forget_immediate(b);
    if (b->column > 0) {
        if (b->write_func != NULL) {
            b->write_func("\n", 1, b->ctx);
        }
        b->column = 0;
    }
    b->line = NULL;
    b->node = NULL;
    b->pc = 0;
    b->depth = 0;
    if (b->immediate != NULL) {
        basic_scan_free(b->immediate);
        sof_alloc_free(&b->alloc, b->immediate, sizeof(basic_line_t));
        b->immediate = NULL;
    }
    return code;
}

/// @brief Evaluates a call of a DEF function, whose name is next. The
/// parameters are set to the arguments while its expression is evaluated,
/// and set back after.
static basic_error_t fn_call(basic_t *b, value_t *out) {
    char name[BASIC_NAME_MAX + 1];
    memcpy(name, peek(b)->u.name, sizeof(name));
    b->pc++;
    basic_error_t rc = prepare(b);
    if (rc != BASIC_OK) {
        return rc;
    }
    sof_hashmap_element_t *e = sof_hashmap_find(&b->functions, name);
    if (e == NULL) {
        return fail(b, BASIC_RUNTIME, "function %s isn't defined", name);
    }
    const def_t *d = e->data;
    value_t v[PARAMS_MAX];
    size_t n = 0;
    memset(v, 0, sizeof(v));
    if (at_op(b, '(') && (rc = args(b, v, 1, PARAMS_MAX, &n, name)) != BASIC_OK) {
        return rc;
    }
    if (n != d->params) {
        for (size_t i = 0; i < n; i++) {
            value_free(&v[i]);
        }
        return fail(b, BASIC_SYNTAX, "syntax error: %s takes %zu argument%s", name, d->params, d->params == 1 ? "" : "s");
    }
    var_t *vars[PARAMS_MAX];
    var_t saved[PARAMS_MAX];
    size_t bound = 0;
    for (; bound < n; bound++) {
        const char *param = token_at(d->line, d->param_tokens[bound])->u.name;
        if (is_string_name(param) != v[bound].is_string) {
            rc = mismatch(b);
            break;
        }
        if ((rc = scalar_var(b, param, &vars[bound])) != BASIC_OK) {
            break;
        }
        saved[bound] = *vars[bound];
        if (v[bound].is_string) {
            vars[bound]->string = v[bound].string;
            v[bound].is_string = 0;
        } else {
            vars[bound]->number = v[bound].number;
        }
    }
    if (rc == BASIC_OK) {
        place_t here = {b->node, b->line, b->pc};
        b->node = d->node;
        b->line = d->line;
        b->pc = d->pc;
        rc = expr(b, out);
        if (rc == BASIC_OK && !at_end(b)) {
            value_free(out);
            rc = expected(b, "the end of the DEF statement");
        }
        b->node = here.node;
        b->line = here.line;
        b->pc = here.pc;
        if (rc == BASIC_OK && out->is_string != is_string_name(name)) {
            value_free(out);
            rc = mismatch(b);
        }
    }
    while (bound > 0) {
        bound--;
        sof_string_free(&vars[bound]->string);
        *vars[bound] = saved[bound];
    }
    for (size_t i = 0; i < n; i++) {
        value_free(&v[i]);
    }
    return rc;
}

/// @brief Drops the FOR loops and GOSUBs that would go back to the line
/// run at once, which is about to be freed, with any made after them.
static void forget_immediate(basic_t *b) {
    if (b->immediate == NULL) {
        return;
    }
    size_t keep = b->loops.count, i = b->loops.count;
    for (loop_t *l = sof_stack_peek(&b->loops); l != NULL; l = sof_stack_next(&b->loops, l)) {
        i--;
        if (l->place.line == b->immediate) {
            keep = i;
        }
    }
    sof_stack_truncate(&b->loops, keep);
    keep = i = b->gosubs.count;
    for (gosub_t *g = sof_stack_peek(&b->gosubs); g != NULL; g = sof_stack_next(&b->gosubs, g)) {
        i--;
        if (g->place.line == b->immediate) {
            keep = i;
        }
    }
    sof_stack_truncate(&b->gosubs, keep);
}

/// @brief Evaluates a built-in function of one number.
static basic_error_t function_number(basic_t *b, int kw, double x, value_t *out) {
    char text[BASIC_NUMBER_SIZE];
    out->is_string = 0;
    switch (kw) {
    case BASIC_KW_ABS:
        out->number = fabs(x);
        return BASIC_OK;
    case BASIC_KW_ATN:
        out->number = atan(x);
        return BASIC_OK;
    case BASIC_KW_CHR: {
        double c = round_half(x);
        if (c < 0 || c > 255) {
            basic_format_number(x, text);
            return fail(b, BASIC_RUNTIME, "CHR$ of %s, which isn't from 0 to 255", text);
        }
        char byte = (char)(unsigned char)c;
        return string_value(b, out, &byte, 1);
    }
    case BASIC_KW_COS:
        out->number = cos(x);
        return BASIC_OK;
    case BASIC_KW_EXP:
        return number_result(b, exp(x), out);
    case BASIC_KW_INT:
        out->number = floor(x);
        return BASIC_OK;
    case BASIC_KW_LOG:
        if (x <= 0) {
            return fail(b, BASIC_RUNTIME, "logarithm of zero or a negative number");
        }
        out->number = log(x);
        return BASIC_OK;
    case BASIC_KW_SGN:
        out->number = x > 0 ? 1 : x < 0 ? -1 : 0;
        return BASIC_OK;
    case BASIC_KW_SIN:
        out->number = sin(x);
        return BASIC_OK;
    case BASIC_KW_SQR:
        if (x < 0) {
            return fail(b, BASIC_RUNTIME, "square root of a negative number");
        }
        out->number = sqrt(x);
        return BASIC_OK;
    case BASIC_KW_STR:
        return string_value(b, out, text, basic_format_number(x, text));
    default:
        return number_result(b, tan(x), out);
    }
}

/// @brief Evaluates a built-in function of strings, whose count arguments
/// have been checked. The arguments are left for the caller to free.
static basic_error_t function_string(basic_t *b, int kw, value_t *v, size_t count, value_t *out) {
    const sof_string_t *s = &v[0].string;
    double length = (double)s->length;
    out->is_string = 0;
    switch (kw) {
    case BASIC_KW_ASC:
        if (s->length == 0) {
            return fail(b, BASIC_RUNTIME, "ASC of an empty string");
        }
        out->number = (unsigned char)s->data[0];
        return BASIC_OK;
    case BASIC_KW_LEN:
        out->number = length;
        return BASIC_OK;
    case BASIC_KW_VAL:
        if (basic_parse_prefix(s->data, s->length, &out->number) != 0) {
            return fail(b, BASIC_RUNTIME, "number out of range in VAL");
        }
        return BASIC_OK;
    case BASIC_KW_LEFT:
    case BASIC_KW_RIGHT: {
        double n = round_half(v[1].number);
        if (n < 0) {
            return fail(b, BASIC_RUNTIME, "%s with a negative length", basic_keyword_name(kw));
        }
        size_t k = n > length ? s->length : (size_t)n;
        return string_value(b, out, kw == BASIC_KW_LEFT ? s->data : s->data + s->length - k, k);
    }
    case BASIC_KW_MID: {
        double start = round_half(v[1].number);
        double n = count == 3 ? round_half(v[2].number) : length;
        if (start < 1 || n < 0) {
            return fail(b, BASIC_RUNTIME, "MID$ with a start below 1 or a negative length");
        }
        if (start > length) {
            return string_value(b, out, s->data, 0);
        }
        size_t from = (size_t)start - 1;
        size_t k = n > (double)(s->length - from) ? s->length - from : (size_t)n;
        return string_value(b, out, s->data + from, k);
    }
    default: {
        // INSTR([start,] string, sought): where sought starts, from 1, or 0.
        double start = count == 3 ? round_half(v[0].number) : 1;
        const sof_string_t *in = &v[count - 2].string, *sought = &v[count - 1].string;
        if (start < 1) {
            return fail(b, BASIC_RUNTIME, "INSTR with a start below 1");
        }
        out->number = 0;
        if (start > (double)in->length + 1) {
            return BASIC_OK;
        }
        for (size_t i = (size_t)start - 1; i + sought->length <= in->length; i++) {
            if (memcmp(in->data + i, sought->data, sought->length) == 0) {
                out->number = (double)(i + 1);
                break;
            }
        }
        return BASIC_OK;
    }
    }
}

/// @brief Fails if a command is used in a program rather than run at once.
static basic_error_t immediate_only(basic_t *b, int kw) {
    if (b->line != b->immediate) {
        return fail(b, BASIC_SYNTAX, "syntax error: %s can't be used in a program", basic_keyword_name(kw));
    }
    return BASIC_OK;
}

/// @brief Forgets everything that points into the program, which has
/// changed: GOSUBs, FOR loops, where CONT goes, the DEF functions and the
/// DATA items.
static void invalidate(basic_t *b) {
    sof_stack_clear(&b->gosubs);
    sof_stack_clear(&b->loops);
    sof_hashmap_clear(&b->functions);
    sof_vector_clear(&b->data);
    b->data_next = 0;
    b->can_continue = 0;
    b->changed = 1;
}

/// @brief Returns 1 if name is that of a string: if it ends in '$'.
static int is_string_name(const char *name) {
    size_t length = strlen(name);
    return length > 0 && name[length - 1] == '$';
}

/// @brief Frees a line, for the lines tree.
static void line_delete(sof_rbtree_node_t *node, void *ctx) {
    basic_t *b = ctx;
    basic_line_t *line = node->data;
    if (line != NULL) {
        basic_scan_free(line);
        sof_alloc_free(&b->alloc, line, sizeof(basic_line_t));
    }
}

/// @brief Reads a line number, which must be next.
static basic_error_t line_number(basic_t *b, unsigned long *number) {
    const basic_token_t *t = peek(b);
    if (t == NULL || t->type != BASIC_TOKEN_NUMBER || t->u.number != floor(t->u.number) || t->u.number < 1 || t->u.number > BASIC_LINE_MAX) {
        return expected(b, "a line number");
    }
    *number = (unsigned long)t->u.number;
    b->pc++;
    return BASIC_OK;
}

/// @brief Writes the lines from first to last.
static basic_error_t list_lines(basic_t *b, unsigned long first, unsigned long last) {
    basic_error_t rc = BASIC_OK;
    for (sof_rbtree_node_t *n = sof_rbtree_minimum(&b->lines, NULL); n != NULL && rc == BASIC_OK; n = sof_rbtree_next(&b->lines, n)) {
        basic_line_t *line = n->data;
        if (line->number < first || line->number > last) {
            continue;
        }
        char number[24];
        int length = snprintf(number, sizeof(number), "%lu ", line->number);
        if ((rc = out(b, number, (size_t)length)) == BASIC_OK && (rc = out(b, line->text.data, line->text.length)) == BASIC_OK) {
            rc = out(b, "\n", 1);
        }
    }
    return rc;
}

/// @brief Replaces the program with the lines of a file.
static basic_error_t load_file(basic_t *b, const char *path) {
    FILE *f = fopen(path, "r");
    if (f == NULL) {
        return fail(b, BASIC_RUNTIME, "can't open %s: %s", path, strerror(errno));
    }
    clear_program(b);
    sof_string_t text;
    if (sof_string_init(&text, 128, &b->alloc) != SOF_STRING_OK) {
        fclose(f);
        return fail(b, BASIC_NO_MEM, "out of memory");
    }
    basic_error_t rc = BASIC_OK;
    size_t number = 0;
    while (rc == BASIC_OK) {
        int got = read_line(f, &text);
        if (got < 0) {
            rc = fail(b, BASIC_NO_MEM, "out of memory");
            break;
        }
        if (got == 0) {
            break;
        }
        number++;
        size_t i = 0;
        while (i < text.length && (text.data[i] == ' ' || text.data[i] == '\t' || text.data[i] == '\r' || text.data[i] == '\n')) {
            i++;
        }
        if (i == text.length) {
            continue;
        }
        if (text.data[i] < '0' || text.data[i] > '9') {
            rc = fail(b, BASIC_SYNTAX, "%s:%zu: line number expected", path, number);
        } else if ((rc = numbered_line(b, text.data + i, text.length - i)) != BASIC_OK) {
            char why[TEXT_SIZE];
            size_t n = strlen(b->message) < sizeof(why) - 1 ? strlen(b->message) : sizeof(why) - 1;
            memcpy(why, b->message, n);
            why[n] = '\0';
            fail(b, rc, "%s:%zu: %s", path, number, why);
        }
    }
    if (rc == BASIC_OK && ferror(f)) {
        rc = fail(b, BASIC_RUNTIME, "error reading %s", path);
    }
    fclose(f);
    sof_string_free(&text);
    return rc;
}

/// @brief Finds the FOR loop of variable name, the innermost if there are
/// several.
/// @return Its index on the stack of loops plus 1, or 0 if there is none.
static size_t loop_index(basic_t *b, const char *name) {
    size_t i = b->loops.count;
    for (loop_t *l = sof_stack_peek(&b->loops); l != NULL; l = sof_stack_next(&b->loops, l)) {
        if (strcmp(l->name, name) == 0) {
            return i;
        }
        i--;
    }
    return 0;
}

/// @brief Fails because a string and a number were mixed.
static basic_error_t mismatch(basic_t *b) {
    return fail(b, BASIC_SYNTAX, "syntax error: type mismatch");
}

/// @brief Sets out to the result of arithmetic. An infinite result is an
/// overflow, which ECMA-55 reports and goes on after with the largest number.
static basic_error_t number_result(basic_t *b, double x, value_t *out) {
    if (isnan(x)) {
        return fail(b, BASIC_RUNTIME, "arithmetic result isn't a number");
    }
    if (isinf(x)) {
        warn(b, "overflow");
        x = x < 0 ? -DBL_MAX : DBL_MAX;
    }
    out->is_string = 0;
    out->number = x;
    return BASIC_OK;
}

/// @brief Stores or deletes a line that starts with its number.
static basic_error_t numbered_line(basic_t *b, const char *text, size_t length) {
    unsigned long number = 0;
    size_t i = 0;
    while (i < length && text[i] >= '0' && text[i] <= '9') {
        if (number <= BASIC_LINE_MAX) {
            number = number * 10 + (unsigned long)(text[i] - '0');
        }
        i++;
    }
    if (number < 1 || number > BASIC_LINE_MAX) {
        return fail(b, BASIC_SYNTAX, "line number out of range: it must be from 1 to %d", BASIC_LINE_MAX);
    }
    return store_line(b, number, text + i, length - i);
}

/// @brief Evaluates an expression that must be a number.
static basic_error_t numeric_expr(basic_t *b, double *x) {
    value_t v = {0};
    basic_error_t rc = expr(b, &v);
    if (rc != BASIC_OK) {
        return rc;
    }
    if (v.is_string) {
        value_free(&v);
        return mismatch(b);
    }
    *x = v.number;
    return BASIC_OK;
}

/// @brief Writes output through the write callback, keeping track of the
/// column.
static basic_error_t out(basic_t *b, const char *text, size_t length) {
    if (length == 0) {
        return BASIC_OK;
    }
    if (b->write_func != NULL && b->write_func(text, length, b->ctx) != 0) {
        return fail(b, BASIC_RUNTIME, "can't write the output");
    }
    for (size_t i = 0; i < length; i++) {
        b->column = text[i] == '\n' ? 0 : b->column + 1;
    }
    return BASIC_OK;
}

/// @brief Writes an item of PRINT: on a new line if it doesn't fit on this
/// one, and over as many lines as it needs if it is longer than the margin.
static basic_error_t out_item(basic_t *b, const char *text, size_t length) {
    basic_error_t rc = BASIC_OK;
    if (b->column > 0 && b->column + length > BASIC_MARGIN) {
        rc = out(b, "\n", 1);
    }
    while (rc == BASIC_OK && length > 0) {
        size_t room = BASIC_MARGIN - b->column;
        if (room == 0) {
            rc = out(b, "\n", 1);
            continue;
        }
        size_t n = length < room ? length : room;
        rc = out(b, text, n);
        text += n;
        length -= n;
    }
    return rc;
}

/// @brief Reads the DEF statement after the DEF keyword at the place set,
/// and adds its function.
static basic_error_t parse_def(basic_t *b) {
    const basic_token_t *t = peek(b);
    def_t d;
    memset(&d, 0, sizeof(d));
    if (t == NULL || t->type != BASIC_TOKEN_FN) {
        return expected(b, "a function name such as FNA");
    }
    const char *name = t->u.name;
    b->pc++;
    basic_error_t rc;
    if (accept_op(b, '(')) {
        for (;;) {
            const basic_token_t *p = peek(b);
            if (p == NULL || p->type != BASIC_TOKEN_NAME) {
                return expected(b, "a parameter name");
            }
            if (d.params == PARAMS_MAX) {
                return fail(b, BASIC_SYNTAX, "syntax error: more than %d parameters", PARAMS_MAX);
            }
            for (size_t i = 0; i < d.params; i++) {
                if (strcmp(token_at(b->line, d.param_tokens[i])->u.name, p->u.name) == 0) {
                    return fail(b, BASIC_SYNTAX, "syntax error: parameter %s is given twice", p->u.name);
                }
            }
            d.param_tokens[d.params++] = b->pc++;
            if (!accept_op(b, ',')) {
                break;
            }
        }
        if ((rc = expect_op(b, ')')) != BASIC_OK) {
            return rc;
        }
    }
    if ((rc = expect_op(b, '=')) != BASIC_OK) {
        return rc;
    }
    if (at_end(b)) {
        return expected(b, "an expression");
    }
    d.node = b->node;
    d.line = b->line;
    d.pc = b->pc;
    int created;
    sof_hashmap_element_t *e = sof_hashmap_insert(&b->functions, name, &created);
    if (e == NULL) {
        return fail(b, BASIC_NO_MEM, "out of memory");
    }
    if (!created) {
        return fail(b, BASIC_SYNTAX, "syntax error: %s is defined twice", name);
    }
    def_t *copy = sof_alloc_malloc(&b->alloc, sizeof(def_t));
    if (copy == NULL) {
        sof_hashmap_remove(&b->functions, name);
        return fail(b, BASIC_NO_MEM, "out of memory");
    }
    *copy = d;
    e->data = copy;
    return BASIC_OK;
}

/// @brief Returns the next token, or NULL at the end of the line.
static const basic_token_t *peek(const basic_t *b) {
    return sof_vector_at(&b->line->tokens, b->pc);
}

/// @brief Raises x to the power y, as ECMA-55 has it: zero to a negative
/// power is reported and gives the largest number, and a negative number to
/// a power that isn't whole is a fatal exception.
static basic_error_t power(basic_t *b, double x, double y, value_t *out) {
    if (x == 0 && y < 0) {
        warn(b, "zero raised to a negative power");
        out->is_string = 0;
        out->number = DBL_MAX;
        return BASIC_OK;
    }
    if (x < 0 && y != floor(y)) {
        return fail(b, BASIC_RUNTIME, "negative number raised to a power that isn't whole");
    }
    return number_result(b, pow(x, y), out);
}

/// @brief Gathers the program's DEF functions and DATA items, if the
/// program has changed since they were last gathered. READ starts again
/// from the first item when they are.
static basic_error_t prepare(basic_t *b) {
    if (!b->changed) {
        return BASIC_OK;
    }
    sof_hashmap_clear(&b->functions);
    sof_vector_clear(&b->data);
    b->data_next = 0;
    place_t here = {b->node, b->line, b->pc};
    basic_error_t rc = BASIC_OK;
    for (sof_rbtree_node_t *n = sof_rbtree_minimum(&b->lines, NULL); n != NULL && rc == BASIC_OK; n = sof_rbtree_next(&b->lines, n)) {
        b->node = n;
        b->line = n->data;
        for (size_t i = 0; i < b->line->tokens.count && rc == BASIC_OK; i++) {
            const basic_token_t *t = token_at(b->line, i);
            if (t->type == BASIC_TOKEN_DATUM) {
                datum_t datum = {b->line, i};
                if (sof_vector_push(&b->data, &datum) != SOF_VECTOR_OK) {
                    rc = fail(b, BASIC_NO_MEM, "out of memory");
                }
            } else if (t->type == BASIC_TOKEN_KEYWORD && t->id == BASIC_KW_DEF) {
                b->pc = i + 1;
                rc = parse_def(b);
            }
        }
    }
    b->node = here.node;
    b->line = here.line;
    b->pc = here.pc;
    if (rc != BASIC_OK) {
        sof_hashmap_clear(&b->functions);
        sof_vector_clear(&b->data);
        return rc;
    }
    b->changed = 0;
    return BASIC_OK;
}

/// @brief Evaluates a constant, a variable, an array element, a function
/// call or an expression in parentheses.
static basic_error_t primary(basic_t *b, value_t *out) {
    const basic_token_t *t = peek(b);
    basic_error_t rc;
    if (t == NULL) {
        return expected(b, "an expression");
    }
    switch (t->type) {
    case BASIC_TOKEN_NUMBER:
        b->pc++;
        out->is_string = 0;
        out->number = t->u.number;
        return BASIC_OK;
    case BASIC_TOKEN_STRING:
        b->pc++;
        return string_value(b, out, b->line->text.data + t->offset, t->length);
    case BASIC_TOKEN_NAME: {
        char name[BASIC_NAME_MAX + 1];
        memcpy(name, t->u.name, sizeof(name));
        b->pc++;
        if (!at_op(b, '(')) {
            return scalar_get(b, name, out);
        }
        array_t *a;
        size_t i;
        if ((rc = array_element(b, name, &a, &i)) != BASIC_OK) {
            return rc;
        }
        if (a->is_string) {
            return string_value(b, out, a->strings[i].data, a->strings[i].length);
        }
        out->is_string = 0;
        out->number = a->numbers[i];
        return BASIC_OK;
    }
    case BASIC_TOKEN_FN:
        return fn_call(b, out);
    case BASIC_TOKEN_KEYWORD:
        return builtin(b, out);
    default:
        if (t->type == BASIC_TOKEN_OP && t->id == '(') {
            b->pc++;
            if ((rc = expr(b, out)) != BASIC_OK) {
                return rc;
            }
            if ((rc = expect_op(b, ')')) != BASIC_OK) {
                value_free(out);
            }
            return rc;
        }
        return expected(b, "an expression");
    }
}

/// @brief Writes a number as PRINT does: with a space before it, or a minus
/// sign, and a space after it.
static basic_error_t print_number(basic_t *b, double x) {
    char text[BASIC_NUMBER_SIZE + 2];
    size_t n = 0;
    if (!(x < 0)) {
        text[n++] = ' ';
    }
    n += basic_format_number(x, text + n);
    text[n++] = ' ';
    return out_item(b, text, n);
}

/// @brief Reads a line of any length from a file, with its newline.
/// @return 1 if a line was read, 0 at the end of the file, or -1 if memory
/// ran out.
static int read_line(FILE *f, sof_string_t *line) {
    char buffer[256];
    int got = 0;
    sof_string_clear(line);
    while (fgets(buffer, sizeof(buffer), f) != NULL) {
        got = 1;
        if (sof_string_append_cstr(line, buffer) != SOF_STRING_OK) {
            return -1;
        }
        if (line->length > 0 && line->data[line->length - 1] == '\n') {
            break;
        }
    }
    return got;
}

/// @brief Frees everything the interpreter allocated and leaves it empty.
static void release(basic_t *b) {
    if (b->immediate != NULL) {
        basic_scan_free(b->immediate);
        sof_alloc_free(&b->alloc, b->immediate, sizeof(basic_line_t));
    }
    sof_rbtree_free(&b->lines);
    sof_hashmap_free(&b->functions);
    sof_hashmap_free(&b->scalars);
    sof_hashmap_free(&b->arrays);
    sof_stack_free(&b->gosubs);
    sof_stack_free(&b->loops);
    sof_vector_free(&b->data);
    sof_string_free(&b->input);
    memset(b, 0, sizeof(*b));
}

/// @brief Drops the FOR loop of variable name, and any inside it, if there
/// is one.
static void remove_loop(basic_t *b, const char *name) {
    size_t i = loop_index(b, name);
    if (i != 0) {
        sof_stack_truncate(&b->loops, i - 1);
    }
}

/// @brief Rounds to the nearest whole number, as ECMA-55 rounds subscripts.
static double round_half(double x) {
    return floor(x + 0.5);
}

/// @brief Runs a line typed without a number.
static basic_error_t run_immediate(basic_t *b, const char *text, size_t length) {
    basic_line_t *line = sof_alloc_calloc(&b->alloc, 1, sizeof(basic_line_t));
    if (line == NULL) {
        return fail(b, BASIC_NO_MEM, "out of memory");
    }
    basic_error_t rc = basic_scan_line(line, text, length, 0, &b->alloc, b->message, sizeof(b->message));
    if (rc != BASIC_OK) {
        sof_alloc_free(&b->alloc, line, sizeof(basic_line_t));
        return rc;
    }
    b->immediate = line;
    b->node = NULL;
    b->line = line;
    b->pc = 0;
    b->depth = 0;
    return finish(b, execute(b));
}

/// @brief Writes the program to a file.
static basic_error_t save_file(basic_t *b, const char *path) {
    FILE *f = fopen(path, "w");
    if (f == NULL) {
        return fail(b, BASIC_RUNTIME, "can't create %s: %s", path, strerror(errno));
    }
    for (sof_rbtree_node_t *n = sof_rbtree_minimum(&b->lines, NULL); n != NULL; n = sof_rbtree_next(&b->lines, n)) {
        basic_line_t *line = n->data;
        fprintf(f, "%lu ", line->number);
        fwrite(line->text.data, 1, line->text.length, f);
        fputc('\n', f);
    }
    int failed = ferror(f);
    if (fclose(f) != 0 || failed) {
        return fail(b, BASIC_RUNTIME, "error writing %s", path);
    }
    return BASIC_OK;
}

/// @brief Reads a simple variable: 0 or the empty string if it was never
/// assigned.
static basic_error_t scalar_get(basic_t *b, const char *name, value_t *out) {
    sof_hashmap_element_t *e = sof_hashmap_find(&b->scalars, name);
    const var_t *v = e != NULL ? e->data : NULL;
    if (is_string_name(name)) {
        return string_value(b, out, v != NULL ? v->string.data : NULL, v != NULL ? v->string.length : 0);
    }
    out->is_string = 0;
    out->number = v != NULL ? v->number : 0;
    return BASIC_OK;
}

/// @brief Finds a simple variable, making it if it doesn't exist.
static basic_error_t scalar_var(basic_t *b, const char *name, var_t **result) {
    sof_hashmap_element_t *e = sof_hashmap_find(&b->scalars, name);
    if (e == NULL) {
        e = sof_hashmap_insert(&b->scalars, name, NULL);
        if (e == NULL) {
            return fail(b, BASIC_NO_MEM, "out of memory");
        }
        e->data = sof_alloc_calloc(&b->alloc, 1, sizeof(var_t));
        if (e->data == NULL) {
            sof_hashmap_remove(&b->scalars, name);
            return fail(b, BASIC_NO_MEM, "out of memory");
        }
    }
    *result = e->data;
    return BASIC_OK;
}

/// @brief Moves to a token of a program line. Running a program line means
/// CONT can no longer go on from where the program last stopped.
static void set_place(basic_t *b, sof_rbtree_node_t *node, size_t pc) {
    b->node = node;
    b->line = node->data;
    b->pc = pc;
    b->can_continue = 0;
}

/// @brief Moves past the NEXT that ends the FOR loop of variable name, which
/// is to run no times: the first NEXT, after the place set, of that
/// variable or of none, that isn't inside another loop.
static basic_error_t skip_to_next(basic_t *b, const char *name) {
    sof_rbtree_node_t *node = b->node;
    const basic_line_t *line = b->line;
    size_t pc = b->pc, depth = 0;
    for (;;) {
        const basic_token_t *t = token_at(line, pc);
        if (t == NULL) {
            node = node != NULL ? sof_rbtree_next(&b->lines, node) : NULL;
            if (node == NULL) {
                return fail(b, BASIC_RUNTIME, "FOR without NEXT");
            }
            line = node->data;
            pc = 0;
            continue;
        }
        if (t->type == BASIC_TOKEN_KEYWORD && t->id == BASIC_KW_FOR) {
            depth++;
        } else if (t->type == BASIC_TOKEN_KEYWORD && t->id == BASIC_KW_NEXT) {
            const basic_token_t *v = token_at(line, pc + 1);
            int named = v != NULL && v->type == BASIC_TOKEN_NAME;
            if (depth == 0 && (!named || strcmp(v->u.name, name) == 0)) {
                if (node != NULL) {
                    set_place(b, node, pc + 1 + (size_t)named);
                } else {
                    b->pc = pc + 1 + (size_t)named;
                }
                b->jumped = 1;
                return BASIC_OK;
            }
            if (depth > 0) {
                depth--;
            }
        }
        pc++;
    }
}

/// @brief Writes count spaces.
static basic_error_t spaces(basic_t *b, size_t count) {
    static const char blanks[] = "                ";
    basic_error_t rc = BASIC_OK;
    while (rc == BASIC_OK && count > 0) {
        size_t n = count < sizeof(blanks) - 1 ? count : sizeof(blanks) - 1;
        rc = out(b, blanks, n);
        count -= n;
    }
    return rc;
}

/// @brief Runs the statement at the place set.
/// @return BASIC_OK, FLOW_END, FLOW_STOP or an error code.
static int statement(basic_t *b) {
    const basic_token_t *t = peek(b);
    if (t->type == BASIC_TOKEN_NAME) {
        return stmt_let(b);
    }
    if (t->type != BASIC_TOKEN_KEYWORD) {
        return expected(b, "a statement");
    }
    int kw = t->id;
    basic_error_t rc;
    switch (kw) {
    case BASIC_KW_CONT:
        return stmt_cont(b);
    case BASIC_KW_DATA:
        b->pc++;
        while ((t = peek(b)) != NULL && t->type == BASIC_TOKEN_DATUM) {
            b->pc++;
        }
        return BASIC_OK;
    case BASIC_KW_DEF:
        // DEFs are gathered before the program uses them; running one
        // does nothing.
        if (b->line == b->immediate) {
            return fail(b, BASIC_SYNTAX, "syntax error: DEF can only be used in a program");
        }
        while (!at_end(b)) {
            b->pc++;
        }
        return BASIC_OK;
    case BASIC_KW_DIM:
        return stmt_dim(b);
    case BASIC_KW_END:
        b->pc++;
        return (rc = end_of_statement(b)) != BASIC_OK ? (int)rc : FLOW_END;
    case BASIC_KW_FOR:
        return stmt_for(b);
    case BASIC_KW_GOSUB:
        return stmt_gosub(b);
    case BASIC_KW_GOTO:
        return stmt_goto(b);
    case BASIC_KW_IF:
        return stmt_if(b);
    case BASIC_KW_INPUT:
        return stmt_input(b);
    case BASIC_KW_LET:
        return stmt_let(b);
    case BASIC_KW_LIST:
        return stmt_list(b);
    case BASIC_KW_LOAD:
    case BASIC_KW_SAVE:
        return stmt_load_save(b, kw);
    case BASIC_KW_NEW:
        if ((rc = immediate_only(b, kw)) != BASIC_OK) {
            return rc;
        }
        b->pc++;
        if ((rc = end_of_statement(b)) != BASIC_OK) {
            return rc;
        }
        clear_program(b);
        return BASIC_OK;
    case BASIC_KW_NEXT:
        return stmt_next(b);
    case BASIC_KW_ON:
        return stmt_on(b);
    case BASIC_KW_OPTION:
        return stmt_option(b);
    case BASIC_KW_PRINT:
        return stmt_print(b);
    case BASIC_KW_RANDOMIZE:
        return stmt_randomize(b);
    case BASIC_KW_READ:
        return stmt_read(b);
    case BASIC_KW_REM:
        b->pc = b->line->tokens.count;
        return BASIC_OK;
    case BASIC_KW_RESTORE:
        return stmt_restore(b);
    case BASIC_KW_RETURN:
        return stmt_return(b);
    case BASIC_KW_RUN:
        return stmt_run(b);
    case BASIC_KW_STOP:
        b->pc++;
        if ((rc = end_of_statement(b)) != BASIC_OK) {
            return rc;
        }
        if (b->node != NULL) {
            b->cont_node = b->node;
            b->cont_pc = b->pc;
            b->can_continue = 1;
        }
        return FLOW_STOP;
    default:
        return expected(b, "a statement");
    }
}

/// @brief CONT: goes on from where the program stopped.
static int stmt_cont(basic_t *b) {
    basic_error_t rc;
    if ((rc = immediate_only(b, BASIC_KW_CONT)) != BASIC_OK) {
        return rc;
    }
    b->pc++;
    if ((rc = end_of_statement(b)) != BASIC_OK) {
        return rc;
    }
    if (!b->can_continue) {
        return fail(b, BASIC_RUNTIME, "can't continue: the program hasn't stopped, or has changed since");
    }
    set_place(b, b->cont_node, b->cont_pc);
    b->jumped = 1;
    return BASIC_OK;
}

/// @brief DIM name(bounds), ...: makes arrays.
static int stmt_dim(basic_t *b) {
    basic_error_t rc;
    b->pc++;
    for (;;) {
        const basic_token_t *t = peek(b);
        if (t == NULL || t->type != BASIC_TOKEN_NAME) {
            return expected(b, "an array name");
        }
        char name[BASIC_NAME_MAX + 1];
        memcpy(name, t->u.name, sizeof(name));
        b->pc++;
        if ((rc = expect_op(b, '(')) != BASIC_OK) {
            return rc;
        }
        size_t upper[DIMS_MAX], n = 0;
        for (;;) {
            double x;
            if ((rc = numeric_expr(b, &x)) != BASIC_OK) {
                return rc;
            }
            if (n == DIMS_MAX) {
                return fail(b, BASIC_SYNTAX, "syntax error: more than %d dimensions", DIMS_MAX);
            }
            x = round_half(x);
            if (x < b->base || x > ARRAY_MAX) {
                return fail(b, BASIC_RUNTIME, "array %s can't have a bound of %.0f", name, x);
            }
            upper[n++] = (size_t)x;
            if (!accept_op(b, ',')) {
                break;
            }
        }
        if ((rc = expect_op(b, ')')) != BASIC_OK) {
            return rc;
        }
        if (sof_hashmap_find(&b->arrays, name) != NULL) {
            return fail(b, BASIC_RUNTIME, "array %s already exists", name);
        }
        array_t *a;
        if ((rc = array_create(b, name, n, upper, &a)) != BASIC_OK) {
            return rc;
        }
        if (!accept_op(b, ',')) {
            return BASIC_OK;
        }
    }
}

/// @brief FOR v = first TO limit [STEP step]: starts a loop, or skips past
/// its NEXT if it is to run no times.
static int stmt_for(basic_t *b) {
    basic_error_t rc;
    b->pc++;
    const basic_token_t *t = peek(b);
    if (t == NULL || t->type != BASIC_TOKEN_NAME || is_string_name(t->u.name)) {
        return expected(b, "a numeric variable");
    }
    char name[BASIC_NAME_MAX + 1];
    memcpy(name, t->u.name, sizeof(name));
    b->pc++;
    double first, limit, step = 1;
    if ((rc = expect_op(b, '=')) != BASIC_OK || (rc = numeric_expr(b, &first)) != BASIC_OK) {
        return rc;
    }
    if (!at_kw(b, BASIC_KW_TO)) {
        return expected(b, "TO");
    }
    b->pc++;
    if ((rc = numeric_expr(b, &limit)) != BASIC_OK) {
        return rc;
    }
    if (at_kw(b, BASIC_KW_STEP)) {
        b->pc++;
        if ((rc = numeric_expr(b, &step)) != BASIC_OK) {
            return rc;
        }
    }
    if ((rc = end_of_statement(b)) != BASIC_OK) {
        return rc;
    }
    var_t *v;
    if ((rc = scalar_var(b, name, &v)) != BASIC_OK) {
        return rc;
    }
    v->number = first;
    remove_loop(b, name);
    double sign = step > 0 ? 1 : step < 0 ? -1 : 0;
    if ((first - limit) * sign > 0) {
        return skip_to_next(b, name);
    }
    if (b->loops.count >= STACK_MAX) {
        return fail(b, BASIC_RUNTIME, "too many FOR loops");
    }
    loop_t loop;
    memset(&loop, 0, sizeof(loop));
    memcpy(loop.name, name, sizeof(name));
    loop.control = &v->number;
    loop.limit = limit;
    loop.step = step;
    loop.place.node = b->node;
    loop.place.line = b->line;
    loop.place.pc = b->pc;
    if (sof_stack_push(&b->loops, &loop) != SOF_STACK_OK) {
        return fail(b, BASIC_NO_MEM, "out of memory");
    }
    return BASIC_OK;
}

/// @brief GOSUB line: goes to a subroutine, which RETURN comes back from.
static int stmt_gosub(basic_t *b) {
    basic_error_t rc;
    unsigned long number;
    b->pc++;
    if ((rc = line_number(b, &number)) != BASIC_OK || (rc = end_of_statement(b)) != BASIC_OK) {
        return rc;
    }
    sof_rbtree_node_t *node = find_line(b, number);
    if (node == NULL) {
        return fail(b, BASIC_RUNTIME, "there is no line %lu", number);
    }
    if (b->gosubs.count >= STACK_MAX) {
        return fail(b, BASIC_RUNTIME, "too many GOSUBs without RETURN");
    }
    gosub_t g = {{b->node, b->line, b->pc}, b->loops.count};
    if (sof_stack_push(&b->gosubs, &g) != SOF_STACK_OK) {
        return fail(b, BASIC_NO_MEM, "out of memory");
    }
    set_place(b, node, 0);
    b->jumped = 1;
    return BASIC_OK;
}

/// @brief GOTO line.
static int stmt_goto(basic_t *b) {
    basic_error_t rc;
    unsigned long number;
    b->pc++;
    if ((rc = line_number(b, &number)) != BASIC_OK || (rc = end_of_statement(b)) != BASIC_OK) {
        return rc;
    }
    sof_rbtree_node_t *node = find_line(b, number);
    if (node == NULL) {
        return fail(b, BASIC_RUNTIME, "there is no line %lu", number);
    }
    set_place(b, node, 0);
    b->jumped = 1;
    return BASIC_OK;
}

/// @brief IF condition THEN line, IF condition GOTO line, or IF condition
/// THEN statements. A false condition skips the rest of the line.
static int stmt_if(basic_t *b) {
    basic_error_t rc;
    double condition;
    b->pc++;
    if ((rc = numeric_expr(b, &condition)) != BASIC_OK) {
        return rc;
    }
    if (at_kw(b, BASIC_KW_THEN)) {
        b->pc++;
        const basic_token_t *t = peek(b);
        if (t == NULL || (t->type == BASIC_TOKEN_OP && t->id == ':')) {
            return expected(b, "a line number or a statement");
        }
        if (t->type != BASIC_TOKEN_NUMBER) {
            if (condition == 0) {
                b->pc = b->line->tokens.count;
            }
            b->jumped = 1;
            return BASIC_OK;
        }
    } else if (at_kw(b, BASIC_KW_GOTO)) {
        b->pc++;
    } else {
        return expected(b, "THEN");
    }
    unsigned long number;
    if ((rc = line_number(b, &number)) != BASIC_OK || (rc = end_of_statement(b)) != BASIC_OK) {
        return rc;
    }
    b->jumped = 1;
    if (condition == 0) {
        b->pc = b->line->tokens.count;
        return BASIC_OK;
    }
    sof_rbtree_node_t *node = find_line(b, number);
    if (node == NULL) {
        return fail(b, BASIC_RUNTIME, "there is no line %lu", number);
    }
    set_place(b, node, 0);
    return BASIC_OK;
}

/// @brief INPUT ["prompt";] variables: reads a line of values, asking again
/// while the reply doesn't fit the variables.
static int stmt_input(basic_t *b) {
    basic_error_t rc = BASIC_OK;
    const char *prompt = NULL;
    size_t prompt_length = 0;
    int question = 1;
    b->pc++;
    const basic_token_t *t = peek(b), *after = token_at(b->line, b->pc + 1);
    if (t != NULL && t->type == BASIC_TOKEN_STRING && after != NULL && after->type == BASIC_TOKEN_OP && (after->id == ';' || after->id == ',')) {
        prompt = b->line->text.data + t->offset;
        prompt_length = t->length;
        question = after->id == ';';
        b->pc += 2;
    }
    sof_vector_t targets, items;
    sof_vector_init(&targets, sizeof(target_t), 0, NULL, NULL, &b->alloc);
    sof_vector_init(&items, sizeof(basic_token_t), 0, NULL, NULL, &b->alloc);
    for (;;) {
        target_t tg;
        if ((rc = target(b, &tg)) != BASIC_OK) {
            goto done;
        }
        if (sof_vector_push(&targets, &tg) != SOF_VECTOR_OK) {
            rc = fail(b, BASIC_NO_MEM, "out of memory");
            goto done;
        }
        if (!accept_op(b, ',')) {
            break;
        }
    }
    if ((rc = end_of_statement(b)) != BASIC_OK) {
        goto done;
    }
    for (;;) {
        if ((rc = out(b, prompt, prompt_length)) != BASIC_OK || (question && (rc = out(b, "? ", 2)) != BASIC_OK)) {
            goto done;
        }
        sof_string_clear(&b->input);
        if (b->read_func == NULL || b->read_func(&b->input, b->ctx) != 0) {
            rc = fail(b, BASIC_RUNTIME, "end of input");
            goto done;
        }
        b->column = 0;
        sof_vector_clear(&items);
        char why[TEXT_SIZE];
        const char *text = b->input.data != NULL ? b->input.data : "";
        rc = basic_scan_items(text, b->input.length, &items, why, sizeof(why));
        if (rc == BASIC_NO_MEM) {
            rc = fail(b, BASIC_NO_MEM, "out of memory");
            goto done;
        }
        if (rc != BASIC_OK) {
            warn(b, "bad input (%s), type it again", why);
            continue;
        }
        if (items.count != targets.count) {
            warn(b, "%zu value%s needed, type %s again", targets.count, targets.count == 1 ? "" : "s", targets.count == 1 ? "it" : "them");
            continue;
        }
        // Every item is checked before any is assigned.
        size_t bad = 0;
        for (size_t i = 0; i < targets.count && bad == 0; i++) {
            const target_t *tg = sof_vector_at(&targets, i);
            const basic_token_t *item = sof_vector_at(&items, i);
            double x;
            if (!tg->is_string && (item->id == 1 || basic_parse_number(text + item->offset, item->length, &x) != 0)) {
                bad = i + 1;
            }
        }
        if (bad != 0) {
            const basic_token_t *item = sof_vector_at(&items, bad - 1);
            warn(b, "\"%.*s\" isn't a number, type the line again", item->length > 20 ? 20 : (int)item->length, text + item->offset);
            continue;
        }
        for (size_t i = 0; i < targets.count && rc == BASIC_OK; i++) {
            const target_t *tg = sof_vector_at(&targets, i);
            const basic_token_t *item = sof_vector_at(&items, i);
            value_t v = {0};
            if (tg->is_string) {
                rc = string_value(b, &v, text + item->offset, item->length);
            } else {
                basic_parse_number(text + item->offset, item->length, &v.number);
            }
            if (rc == BASIC_OK) {
                rc = assign(b, tg, &v);
            }
        }
        break;
    }
done:
    sof_vector_free(&targets);
    sof_vector_free(&items);
    return rc;
}

/// @brief [LET] variable = expression.
static int stmt_let(basic_t *b) {
    basic_error_t rc;
    target_t t;
    value_t v = {0};
    if (at_kw(b, BASIC_KW_LET)) {
        b->pc++;
    }
    if ((rc = target(b, &t)) != BASIC_OK || (rc = expect_op(b, '=')) != BASIC_OK || (rc = expr(b, &v)) != BASIC_OK) {
        return rc;
    }
    return assign(b, &t, &v);
}

/// @brief LIST [first][-[last]]: writes the program, or some of its lines.
static int stmt_list(basic_t *b) {
    basic_error_t rc;
    unsigned long first = 0, last = BASIC_LINE_MAX;
    if ((rc = immediate_only(b, BASIC_KW_LIST)) != BASIC_OK) {
        return rc;
    }
    b->pc++;
    const basic_token_t *t = peek(b);
    if (t != NULL && t->type == BASIC_TOKEN_NUMBER) {
        if ((rc = line_number(b, &first)) != BASIC_OK) {
            return rc;
        }
        last = first;
        if (accept_op(b, '-') || accept_op(b, ',')) {
            t = peek(b);
            last = BASIC_LINE_MAX;
            if (t != NULL && t->type == BASIC_TOKEN_NUMBER && (rc = line_number(b, &last)) != BASIC_OK) {
                return rc;
            }
        }
    } else if (accept_op(b, '-') || accept_op(b, ',')) {
        if ((rc = line_number(b, &last)) != BASIC_OK) {
            return rc;
        }
    }
    if ((rc = end_of_statement(b)) != BASIC_OK) {
        return rc;
    }
    return list_lines(b, first, last);
}

/// @brief LOAD "file" or SAVE "file".
static int stmt_load_save(basic_t *b, int kw) {
    basic_error_t rc;
    value_t v = {0};
    if ((rc = immediate_only(b, kw)) != BASIC_OK) {
        return rc;
    }
    b->pc++;
    if ((rc = expr(b, &v)) != BASIC_OK) {
        return rc;
    }
    if (!v.is_string) {
        return mismatch(b);
    }
    if ((rc = end_of_statement(b)) == BASIC_OK) {
        rc = kw == BASIC_KW_LOAD ? load_file(b, v.string.data) : save_file(b, v.string.data);
    }
    value_free(&v);
    return rc;
}

/// @brief NEXT [variable]: adds the step to the variable of the loop, and
/// goes back to the statement after its FOR unless it has passed the limit.
static int stmt_next(basic_t *b) {
    basic_error_t rc;
    b->pc++;
    const basic_token_t *t = peek(b);
    if (t != NULL && t->type == BASIC_TOKEN_NAME) {
        size_t i = loop_index(b, t->u.name);
        if (i == 0) {
            return fail(b, BASIC_RUNTIME, "NEXT %s without FOR", t->u.name);
        }
        // Loops inside this one that weren't ended are dropped.
        sof_stack_truncate(&b->loops, i);
        b->pc++;
    } else if (b->loops.count == 0) {
        return fail(b, BASIC_RUNTIME, "NEXT without FOR");
    }
    if ((rc = end_of_statement(b)) != BASIC_OK) {
        return rc;
    }
    loop_t *l = sof_stack_peek(&b->loops);
    double x = *l->control + l->step;
    if (isinf(x)) {
        warn(b, "overflow");
        x = x < 0 ? -DBL_MAX : DBL_MAX;
    }
    *l->control = x;
    double sign = l->step > 0 ? 1 : l->step < 0 ? -1 : 0;
    if ((x - l->limit) * sign > 0) {
        sof_stack_pop(&b->loops, NULL);
        return BASIC_OK;
    }
    if (l->place.node != NULL) {
        set_place(b, l->place.node, l->place.pc);
    } else {
        b->node = NULL;
        b->line = l->place.line;
        b->pc = l->place.pc;
    }
    b->jumped = 1;
    return BASIC_OK;
}

/// @brief ON expression GOTO lines, or ON expression GOSUB lines.
static int stmt_on(basic_t *b) {
    basic_error_t rc;
    double x;
    b->pc++;
    if ((rc = numeric_expr(b, &x)) != BASIC_OK) {
        return rc;
    }
    int gosub = at_kw(b, BASIC_KW_GOSUB);
    if (!gosub && !at_kw(b, BASIC_KW_GOTO)) {
        return expected(b, "GOTO or GOSUB");
    }
    b->pc++;
    double choice = round_half(x);
    unsigned long number, chosen = 0;
    size_t count = 0;
    for (;;) {
        if ((rc = line_number(b, &number)) != BASIC_OK) {
            return rc;
        }
        count++;
        if (choice == (double)count) {
            chosen = number;
        }
        if (!accept_op(b, ',')) {
            break;
        }
    }
    if ((rc = end_of_statement(b)) != BASIC_OK) {
        return rc;
    }
    if (chosen == 0) {
        char text[BASIC_NUMBER_SIZE];
        basic_format_number(x, text);
        return fail(b, BASIC_RUNTIME, "ON value %s isn't from 1 to %zu", text, count);
    }
    sof_rbtree_node_t *node = find_line(b, chosen);
    if (node == NULL) {
        return fail(b, BASIC_RUNTIME, "there is no line %lu", chosen);
    }
    if (gosub) {
        if (b->gosubs.count >= STACK_MAX) {
            return fail(b, BASIC_RUNTIME, "too many GOSUBs without RETURN");
        }
        gosub_t g = {{b->node, b->line, b->pc}, b->loops.count};
        if (sof_stack_push(&b->gosubs, &g) != SOF_STACK_OK) {
            return fail(b, BASIC_NO_MEM, "out of memory");
        }
    }
    set_place(b, node, 0);
    b->jumped = 1;
    return BASIC_OK;
}

/// @brief OPTION BASE 0 or 1: the lowest subscript of arrays made after it.
static int stmt_option(basic_t *b) {
    b->pc++;
    if (!at_kw(b, BASIC_KW_BASE)) {
        return expected(b, "BASE");
    }
    b->pc++;
    const basic_token_t *t = peek(b);
    if (t == NULL || t->type != BASIC_TOKEN_NUMBER || (t->u.number != 0 && t->u.number != 1)) {
        return expected(b, "0 or 1");
    }
    if (b->arrays.count > 0) {
        return fail(b, BASIC_RUNTIME, "OPTION BASE must come before any array is used");
    }
    b->base = (int)t->u.number;
    b->pc++;
    return BASIC_OK;
}

/// @brief PRINT items, separated by ',' (to the next zone) or ';' (nothing),
/// and a newline unless one of those ends it.
static int stmt_print(basic_t *b) {
    basic_error_t rc;
    int newline = 1;
    b->pc++;
    while (!at_end(b)) {
        if (accept_op(b, ',')) {
            if ((rc = zone(b)) != BASIC_OK) {
                return rc;
            }
            newline = 0;
            continue;
        }
        if (accept_op(b, ';')) {
            newline = 0;
            continue;
        }
        if (at_kw(b, BASIC_KW_TAB)) {
            double x;
            b->pc++;
            if ((rc = expect_op(b, '(')) != BASIC_OK || (rc = numeric_expr(b, &x)) != BASIC_OK || (rc = expect_op(b, ')')) != BASIC_OK || (rc = tab(b, x)) != BASIC_OK) {
                return rc;
            }
        } else {
            value_t v = {0};
            if ((rc = expr(b, &v)) != BASIC_OK) {
                return rc;
            }
            rc = v.is_string ? out_item(b, v.string.data, v.string.length) : print_number(b, v.number);
            value_free(&v);
            if (rc != BASIC_OK) {
                return rc;
            }
        }
        newline = 1;
        if (!at_end(b) && !at_op(b, ',') && !at_op(b, ';')) {
            return expected(b, "',', ';' or the end of the statement");
        }
    }
    return newline ? out(b, "\n", 1) : BASIC_OK;
}

/// @brief RANDOMIZE [seed]: starts RND again, from the seed given, or from
/// one drawn from the generator itself, which still depends only on the
/// seed the interpreter was given.
static int stmt_randomize(basic_t *b) {
    basic_error_t rc;
    uint64_t seed;
    b->pc++;
    if (!at_end(b)) {
        double x;
        if ((rc = numeric_expr(b, &x)) != BASIC_OK) {
            return rc;
        }
        x += 0.0; // -0 seeds as 0 does.
        memcpy(&seed, &x, sizeof(seed));
    } else {
        seed = sof_random_next(&b->random);
    }
    sof_random_init(&b->random, seed);
    return BASIC_OK;
}

/// @brief READ variables: assigns the next DATA items.
static int stmt_read(basic_t *b) {
    basic_error_t rc;
    b->pc++;
    if ((rc = prepare(b)) != BASIC_OK) {
        return rc;
    }
    for (;;) {
        target_t t;
        if ((rc = target(b, &t)) != BASIC_OK) {
            return rc;
        }
        const datum_t *d = sof_vector_at(&b->data, b->data_next);
        if (d == NULL) {
            return fail(b, BASIC_RUNTIME, "out of data");
        }
        b->data_next++;
        const basic_token_t *item = token_at(d->line, d->token);
        const char *text = d->line->text.data + item->offset;
        value_t v = {0};
        if (t.is_string) {
            rc = string_value(b, &v, text, item->length);
        } else {
            int bad = item->id == 1 ? 1 : basic_parse_number(text, item->length, &v.number);
            if (bad != 0) {
                return fail(b, BASIC_RUNTIME, "READ of \"%.*s\", which isn't a number, into a numeric variable", item->length > 20 ? 20 : (int)item->length, text);
            }
        }
        if (rc != BASIC_OK || (rc = assign(b, &t, &v)) != BASIC_OK) {
            return rc;
        }
        if (!accept_op(b, ',')) {
            return BASIC_OK;
        }
    }
}

/// @brief RESTORE [line]: READ starts again from the first DATA item, or the
/// first at or after a line.
static int stmt_restore(basic_t *b) {
    basic_error_t rc;
    unsigned long number = 0;
    b->pc++;
    if (!at_end(b) && (rc = line_number(b, &number)) != BASIC_OK) {
        return rc;
    }
    if ((rc = prepare(b)) != BASIC_OK) {
        return rc;
    }
    b->data_next = 0;
    while (b->data_next < b->data.count && ((const datum_t *)sof_vector_at(&b->data, b->data_next))->line->number < number) {
        b->data_next++;
    }
    return BASIC_OK;
}

/// @brief RETURN: goes back after the last GOSUB, ending the FOR loops
/// started since.
static int stmt_return(basic_t *b) {
    basic_error_t rc;
    gosub_t g;
    b->pc++;
    if ((rc = end_of_statement(b)) != BASIC_OK) {
        return rc;
    }
    if (sof_stack_pop(&b->gosubs, &g) != SOF_STACK_OK) {
        return fail(b, BASIC_RUNTIME, "RETURN without GOSUB");
    }
    sof_stack_truncate(&b->loops, g.loops);
    if (g.place.node != NULL) {
        set_place(b, g.place.node, g.place.pc);
    } else {
        b->node = NULL;
        b->line = g.place.line;
        b->pc = g.place.pc;
    }
    b->jumped = 1;
    return BASIC_OK;
}

/// @brief RUN [line]: clears the variables and runs the program from its
/// first line, or from a line given.
static int stmt_run(basic_t *b) {
    basic_error_t rc;
    unsigned long number = 0;
    if ((rc = immediate_only(b, BASIC_KW_RUN)) != BASIC_OK) {
        return rc;
    }
    b->pc++;
    if (!at_end(b) && (rc = line_number(b, &number)) != BASIC_OK) {
        return rc;
    }
    if ((rc = end_of_statement(b)) != BASIC_OK) {
        return rc;
    }
    clear_variables(b);
    sof_random_init(&b->random, b->seed);
    b->can_continue = 0;
    if ((rc = prepare(b)) != BASIC_OK) {
        return rc;
    }
    b->data_next = 0;
    sof_rbtree_node_t *node = number != 0 ? find_line(b, number) : sof_rbtree_minimum(&b->lines, NULL);
    if (node == NULL) {
        if (number != 0) {
            return fail(b, BASIC_RUNTIME, "there is no line %lu", number);
        }
        return FLOW_END;
    }
    set_place(b, node, 0);
    b->jumped = 1;
    return BASIC_OK;
}

/// @brief Stores a line in the program, or deletes it if text is blank.
static basic_error_t store_line(basic_t *b, unsigned long number, const char *text, size_t length) {
    size_t i = 0;
    while (i < length && (text[i] == ' ' || text[i] == '\t' || text[i] == '\r' || text[i] == '\n')) {
        i++;
    }
    if (i == length) {
        if (sof_rbtree_remove(&b->lines, (const void *)(uintptr_t)number) == SOF_RBTREE_OK) {
            invalidate(b);
        }
        return BASIC_OK;
    }
    basic_line_t *line = sof_alloc_calloc(&b->alloc, 1, sizeof(basic_line_t));
    if (line == NULL) {
        return fail(b, BASIC_NO_MEM, "out of memory");
    }
    char why[TEXT_SIZE];
    basic_error_t rc = basic_scan_line(line, text, length, number, &b->alloc, why, sizeof(why));
    if (rc != BASIC_OK) {
        sof_alloc_free(&b->alloc, line, sizeof(basic_line_t));
        snprintf(b->message, sizeof(b->message), "line %lu: %s", number, why);
        return rc;
    }
    int created;
    sof_rbtree_node_t *node = sof_rbtree_insert(&b->lines, (void *)(uintptr_t)number, &created);
    if (node == NULL) {
        basic_scan_free(line);
        sof_alloc_free(&b->alloc, line, sizeof(basic_line_t));
        return fail(b, BASIC_NO_MEM, "out of memory");
    }
    if (!created) {
        line_delete(node, b);
    }
    node->data = line;
    invalidate(b);
    return BASIC_OK;
}

/// @brief Sets out to a copy of length bytes of data, NULL meaning none.
static basic_error_t string_value(basic_t *b, value_t *out, const char *data, size_t length) {
    out->is_string = 0;
    if (sof_string_from_bytes(&out->string, data, length, &b->alloc) != SOF_STRING_OK) {
        return fail(b, BASIC_NO_MEM, "out of memory");
    }
    out->is_string = 1;
    return BASIC_OK;
}

/// @brief TAB(x) in PRINT: moves to column x, counting from 1, on a new line
/// if the output is already past it.
static basic_error_t tab(basic_t *b, double x) {
    basic_error_t rc = BASIC_OK;
    x = round_half(x);
    if (x < 1) {
        warn(b, "TAB argument less than 1");
        x = 1;
    }
    size_t column = (size_t)fmod(x - 1, BASIC_MARGIN);
    if (b->column > column) {
        rc = out(b, "\n", 1);
    }
    if (rc == BASIC_OK) {
        rc = spaces(b, column - b->column);
    }
    return rc;
}

/// @brief Reads a variable or array element to assign to, which must be
/// next, making it if it doesn't exist.
static basic_error_t target(basic_t *b, target_t *t) {
    const basic_token_t *tok = peek(b);
    basic_error_t rc;
    if (tok == NULL || tok->type != BASIC_TOKEN_NAME) {
        return expected(b, "a variable");
    }
    char name[BASIC_NAME_MAX + 1];
    memcpy(name, tok->u.name, sizeof(name));
    b->pc++;
    memset(t, 0, sizeof(*t));
    t->is_string = is_string_name(name);
    if (at_op(b, '(')) {
        array_t *a;
        size_t i;
        if ((rc = array_element(b, name, &a, &i)) != BASIC_OK) {
            return rc;
        }
        if (a->is_string) {
            t->string = &a->strings[i];
        } else {
            t->number = &a->numbers[i];
        }
        return BASIC_OK;
    }
    var_t *v;
    if ((rc = scalar_var(b, name, &v)) != BASIC_OK) {
        return rc;
    }
    t->string = &v->string;
    t->number = &v->number;
    return BASIC_OK;
}

/// @brief Returns token i of a line, or NULL past its end.
static const basic_token_t *token_at(const basic_line_t *line, size_t i) {
    return sof_vector_at(&line->tokens, i);
}

/// @brief Frees a value's string, if it has one.
static void value_free(value_t *v) {
    if (v->is_string) {
        sof_string_free(&v->string);
        v->is_string = 0;
    }
}

/// @brief Frees a simple variable, for the scalars hashmap.
static void var_delete(sof_hashmap_element_t *element, void *ctx) {
    basic_t *b = ctx;
    var_t *v = element->data;
    if (v != NULL) {
        sof_string_free(&v->string);
        sof_alloc_free(&b->alloc, v, sizeof(var_t));
    }
}

/// @brief Reports an exception the program goes on after: through the warn
/// callback, or on a line of its own in the output if there is none.
static void warn(basic_t *b, const char *format, ...) {
    char text[TEXT_SIZE], message[BASIC_MESSAGE_SIZE];
    va_list ap;
    va_start(ap, format);
    vsnprintf(text, sizeof(text), format, ap);
    va_end(ap);
    if (b->line != NULL && b->line->number != 0) {
        snprintf(message, sizeof(message), "line %lu: %s", b->line->number, text);
    } else {
        snprintf(message, sizeof(message), "%s", text);
    }
    if (b->warn_func != NULL) {
        b->warn_func(message, b->ctx);
        return;
    }
    if (b->column > 0) {
        out(b, "\n", 1);
    }
    out(b, message, strlen(message));
    out(b, "\n", 1);
}

/// @brief A comma in PRINT: moves to the start of the next print zone, or of
/// the next line if there is no zone left on this one.
static basic_error_t zone(basic_t *b) {
    size_t next = (b->column / BASIC_ZONE + 1) * BASIC_ZONE;
    if (next >= BASIC_MARGIN) {
        return out(b, "\n", 1);
    }
    return spaces(b, next - b->column);
}

basic_error_t basic_free(basic_t *b) {
    if (b == NULL || !b->ready) {
        return BASIC_ERROR;
    }
    release(b);
    return BASIC_OK;
}

basic_error_t basic_init(basic_t *b, basic_write_func_t write_func, basic_read_func_t read_func, basic_warn_func_t warn_func, void *ctx, uint64_t seed, const sof_alloc_t *alloc) {
    if (b == NULL) {
        return BASIC_ERROR;
    }
    memset(b, 0, sizeof(*b));
    if (!sof_alloc_valid(alloc)) {
        return BASIC_ERROR;
    }
    if (alloc != NULL) {
        b->alloc = *alloc;
    }
    b->write_func = write_func;
    b->read_func = read_func;
    b->warn_func = warn_func;
    b->ctx = ctx;
    b->seed = seed;
    b->changed = 1;
    sof_random_init(&b->random, seed);
    if (sof_rbtree_init(&b->lines, compare_lines, line_delete, b, &b->alloc) != SOF_RBTREE_OK || sof_hashmap_init(&b->scalars, &sof_hashmap_string_keys, var_delete, b, 0, &b->alloc) != SOF_HASHMAP_OK || sof_hashmap_init(&b->arrays, &sof_hashmap_string_keys, array_delete, b, 0, &b->alloc) != SOF_HASHMAP_OK || sof_hashmap_init(&b->functions, &sof_hashmap_string_keys, def_delete, b, 0, &b->alloc) != SOF_HASHMAP_OK || sof_stack_init(&b->gosubs, sizeof(gosub_t), 0, NULL, NULL, &b->alloc) != SOF_STACK_OK || sof_stack_init(&b->loops, sizeof(loop_t), 0, NULL, NULL, &b->alloc) != SOF_STACK_OK || sof_vector_init(&b->data, sizeof(datum_t), 0, NULL, NULL, &b->alloc) != SOF_VECTOR_OK || sof_string_init(&b->input, 0, &b->alloc) != SOF_STRING_OK) {
        release(b);
        return BASIC_NO_MEM;
    }
    b->ready = 1;
    return BASIC_OK;
}

basic_error_t basic_line(basic_t *b, const char *text) {
    if (b == NULL || !b->ready || text == NULL) {
        return BASIC_ERROR;
    }
    b->message[0] = '\0';
    size_t length = strlen(text), i = 0;
    while (i < length && (text[i] == ' ' || text[i] == '\t' || text[i] == '\r' || text[i] == '\n')) {
        i++;
    }
    if (i == length) {
        return BASIC_OK;
    }
    if (text[i] >= '0' && text[i] <= '9') {
        return numbered_line(b, text + i, length - i);
    }
    return run_immediate(b, text + i, length - i);
}

basic_error_t basic_list(basic_t *b, unsigned long first, unsigned long last) {
    if (b == NULL || !b->ready) {
        return BASIC_ERROR;
    }
    b->message[0] = '\0';
    return list_lines(b, first, last);
}

basic_error_t basic_load(basic_t *b, const char *path) {
    if (b == NULL || !b->ready || path == NULL) {
        return BASIC_ERROR;
    }
    b->message[0] = '\0';
    return load_file(b, path);
}

const char *basic_message(const basic_t *b) {
    if (b == NULL || !b->ready) {
        return "";
    }
    return b->message;
}

basic_error_t basic_new(basic_t *b) {
    if (b == NULL || !b->ready) {
        return BASIC_ERROR;
    }
    b->message[0] = '\0';
    clear_program(b);
    return BASIC_OK;
}

basic_error_t basic_run(basic_t *b) {
    return basic_line(b, "RUN");
}

basic_error_t basic_save(basic_t *b, const char *path) {
    if (b == NULL || !b->ready || path == NULL) {
        return BASIC_ERROR;
    }
    b->message[0] = '\0';
    return save_file(b, path);
}
