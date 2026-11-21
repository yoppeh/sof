/**
 * @file calc.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief An expression evaluator with variables in c, for the calc program.
 * @details See calc.h. A line is split into tokens by token_length(), and
 * each token is handled as it comes by the shunting-yard algorithm:
 * values are pushed onto one stack and operators onto another, and an
 * operator is applied to the values on top of the value stack as soon as
 * the operators after it show that its operands are complete. Built with
 * DEBUG defined (make debug=1), it traces each line, token, push and
 * operator applied to stderr through sof_debug.
 * @version 0.0.1
 * @date 2026-11-04
 * @copyright Copyright (c) 2026
 */

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "calc.h"
#include "sof_debug.h"

/// @brief The longest number calc_eval() reads, in bytes.
#define NUMBER_MAX 255

/// @brief The most bytes of a token quoted in a message.
#define QUOTE_MAX 24

/// @brief What a token is.
typedef enum token_kind_t {
    TOKEN_END = 0,
    TOKEN_NUMBER,
    TOKEN_NAME,
    TOKEN_OPERATOR,
    TOKEN_LPAREN,
    TOKEN_RPAREN,
    TOKEN_ASSIGN,
    TOKEN_BAD,
} token_kind_t;

/// @brief What an entry on the operator stack does.
typedef enum op_kind_t {
    OP_ADD = 0,
    OP_SUB,
    OP_MUL,
    OP_DIV,
    OP_MOD,
    OP_POW,
    OP_NEG,
    OP_FUNC,
    OP_PAREN,
} op_kind_t;

/// @brief A token: its kind, the operator if it is one, and where it is in
/// the line.
typedef struct token_t {
    token_kind_t kind;
    op_kind_t op;
    size_t offset;
    size_t length;
} token_t;

/// @brief An entry on the operator stack: an operator, a function waiting
/// for its argument, or an open parenthesis, with where it is in the line,
/// for messages.
typedef struct calc_op_t {
    op_kind_t kind;
    /// @brief For OP_FUNC, the index of the function in functions.
    size_t func;
    size_t offset;
    size_t length;
} calc_op_t;

/// @brief A function of one argument.
typedef struct function_t {
    const char *name;
    double (*func)(double);
} function_t;

/// @brief The functions, in alphabetical order.
static const function_t functions[] = {
    {"abs", fabs}, {"ceil", ceil}, {"cos", cos},   {"exp", exp},   {"floor", floor}, {"ln", log},
    {"log", log10}, {"round", round}, {"sin", sin}, {"sqrt", sqrt}, {"tan", tan},
};

/// @brief The number of functions.
#define FUNCTION_COUNT (sizeof(functions) / sizeof(functions[0]))

static calc_error_t apply(calc_t *calc, const calc_op_t *op);
static calc_error_t evaluate(calc_t *calc, size_t *position, token_t *token, double *result);
static size_t exponent_end(const char *s, size_t n, size_t i);
static calc_error_t fail(calc_t *calc, calc_error_t err, size_t offset, const char *format, ...) __attribute__((format(printf, 4, 5)));
static size_t find_function(const char *name);
static int is_alpha(unsigned char c);
static int is_digit(unsigned char c);
static int is_space(unsigned char c);
static calc_error_t next_token(calc_t *calc, size_t *position, token_t *token);
static calc_error_t no_mem(calc_t *calc);
static int precedence(op_kind_t kind);
static calc_error_t push_op(calc_t *calc, op_kind_t kind, size_t func, size_t offset, size_t length);
static calc_error_t push_value(calc_t *calc, double value);
static void quote(const calc_t *calc, const token_t *token, char *buffer, size_t size);
static calc_error_t reduce(calc_t *calc, int min_precedence);
static void release(calc_t *calc);
static size_t token_length(const char *s, size_t n);
static int valid_name(const char *name);

/// @brief Pops an operator's operands off the value stack, applies it, and
/// pushes the result.
static calc_error_t apply(calc_t *calc, const calc_op_t *op) {
    double a, b = 0, r;
    int binary = op->kind <= OP_POW;
    if (binary && sof_stack_pop(&calc->values, &b) != SOF_STACK_OK) {
        return fail(calc, CALC_ERROR, op->offset, "internal error: no operand");
    }
    if (sof_stack_pop(&calc->values, &a) != SOF_STACK_OK) {
        return fail(calc, CALC_ERROR, op->offset, "internal error: no operand");
    }
    switch (op->kind) {
    case OP_ADD:
        r = a + b;
        break;
    case OP_SUB:
        r = a - b;
        break;
    case OP_MUL:
        r = a * b;
        break;
    case OP_DIV:
        if (b == 0) {
            return fail(calc, CALC_MATH, op->offset, "division by zero");
        }
        r = a / b;
        break;
    case OP_MOD:
        if (b == 0) {
            return fail(calc, CALC_MATH, op->offset, "modulo by zero");
        }
        r = fmod(a, b);
        break;
    case OP_POW:
        r = pow(a, b);
        break;
    case OP_NEG:
        r = -a;
        break;
    case OP_FUNC:
        r = functions[op->func].func(a);
        break;
    default:
        return fail(calc, CALC_ERROR, op->offset, "internal error: bad operator");
    }
    const char *text = calc->line.data + op->offset;
    if (isnan(r)) {
        return fail(calc, CALC_MATH, op->offset, "result of '%.*s' is undefined", (int)op->length, text);
    }
    if (isinf(r)) {
        return fail(calc, CALC_MATH, op->offset, "result of '%.*s' is out of range", (int)op->length, text);
    }
    sof_debug("apply '%.*s': %g\n", (int)op->length, text, r);
    return push_value(calc, r);
}

/// @brief Evaluates the expression starting with *token, the token before
/// *position, up to the end of the line.
static calc_error_t evaluate(calc_t *calc, size_t *position, token_t *token, double *result) {
    char text[QUOTE_MAX + 32];
    char name[NUMBER_MAX + 1];
    // Whether a value (a number, a name, a unary operator or an open
    // parenthesis) is expected next, rather than a binary operator or a
    // close parenthesis.
    int expect = 1;
    while (1) {
        calc_error_t err = CALC_OK;
        quote(calc, token, text, sizeof(text));
        sof_debug("token %s\n", text);
        switch (token->kind) {
        case TOKEN_NUMBER:
        case TOKEN_NAME:
            if (!expect) {
                return fail(calc, CALC_SYNTAX, token->offset, "expected an operator, not %s", text);
            }
            if (token->length > (token->kind == TOKEN_NAME ? CALC_NAME_MAX : NUMBER_MAX)) {
                return fail(calc, CALC_SYNTAX, token->offset, "%s too long", token->kind == TOKEN_NAME ? "name" : "number");
            }
            memcpy(name, calc->line.data + token->offset, token->length);
            name[token->length] = '\0';
            if (token->kind == TOKEN_NUMBER) {
                double value = strtod(name, NULL);
                if (isinf(value)) {
                    return fail(calc, CALC_MATH, token->offset, "number %s out of range", text);
                }
                err = push_value(calc, value);
                expect = 0;
                break;
            }
            size_t func = find_function(name);
            if (func < FUNCTION_COUNT) {
                // A function: it must be followed by its argument in
                // parentheses, and is applied when they close.
                size_t after = *position;
                token_t paren;
                err = next_token(calc, &after, &paren);
                if (err != CALC_OK) {
                    return err;
                }
                if (paren.kind != TOKEN_LPAREN) {
                    return fail(calc, CALC_SYNTAX, token->offset, "%s is a function: write %s(x)", text, name);
                }
                err = push_op(calc, OP_FUNC, func, token->offset, token->length);
                if (err == CALC_OK) {
                    err = push_op(calc, OP_PAREN, 0, paren.offset, paren.length);
                }
                *position = after;
                break;
            }
            double value;
            if (calc_get(calc, name, &value) != CALC_OK) {
                return fail(calc, CALC_NO_MATCH, token->offset, "unknown variable %s", text);
            }
            err = push_value(calc, value);
            expect = 0;
            break;
        case TOKEN_LPAREN:
            if (!expect) {
                return fail(calc, CALC_SYNTAX, token->offset, "expected an operator, not %s", text);
            }
            err = push_op(calc, OP_PAREN, 0, token->offset, token->length);
            break;
        case TOKEN_RPAREN:
            if (expect) {
                return fail(calc, CALC_SYNTAX, token->offset, "expected a value, not %s", text);
            }
            err = reduce(calc, 0);
            if (err != CALC_OK) {
                return err;
            }
            calc_op_t *top = sof_stack_peek(&calc->ops);
            if (top == NULL) {
                return fail(calc, CALC_SYNTAX, token->offset, "unmatched ')'");
            }
            sof_stack_pop(&calc->ops, NULL);
            top = sof_stack_peek(&calc->ops);
            if (top != NULL && top->kind == OP_FUNC) {
                calc_op_t op = *top;
                sof_stack_pop(&calc->ops, NULL);
                err = apply(calc, &op);
            }
            break;
        case TOKEN_OPERATOR:
            if (expect) {
                if (token->op == OP_SUB) {
                    // A unary minus applies to the value after it, so it
                    // is pushed without applying anything before it.
                    err = push_op(calc, OP_NEG, 0, token->offset, token->length);
                    break;
                }
                if (token->op == OP_ADD) {
                    // A unary plus does nothing.
                    break;
                }
                return fail(calc, CALC_SYNTAX, token->offset, "expected a value, not %s", text);
            }
            // The operators before this one whose precedence is higher, or
            // the same for one that groups left to right, have their
            // operands.
            err = reduce(calc, precedence(token->op) + (token->op == OP_POW ? 1 : 0));
            if (err == CALC_OK) {
                err = push_op(calc, token->op, 0, token->offset, token->length);
            }
            expect = 1;
            break;
        case TOKEN_ASSIGN:
            return fail(calc, CALC_SYNTAX, token->offset, "'=' can only follow a name at the start of the line");
        case TOKEN_BAD:
            return fail(calc, CALC_SYNTAX, token->offset, "unknown character %s", text);
        case TOKEN_END:
            if (expect) {
                return fail(calc, CALC_SYNTAX, token->offset, "expected a value, not %s", text);
            }
            err = reduce(calc, 0);
            if (err != CALC_OK) {
                return err;
            }
            calc_op_t *paren = sof_stack_peek(&calc->ops);
            if (paren != NULL) {
                return fail(calc, CALC_SYNTAX, paren->offset, "unmatched '('");
            }
            if (sof_stack_pop(&calc->values, result) != SOF_STACK_OK || calc->values.count != 0) {
                return fail(calc, CALC_ERROR, token->offset, "internal error: values left over");
            }
            sof_debug("result %g\n", *result);
            return CALC_OK;
        }
        if (err != CALC_OK) {
            return err;
        }
        err = next_token(calc, position, token);
        if (err != CALC_OK) {
            return err;
        }
    }
}

/// @brief Sets the message, and the column to offset + 1, and returns err.
/// @brief Returns where the exponent ([eE][-+]?[0-9]+) at s[i] of the n bytes
/// at s ends, or i if there isn't a whole one there.
static size_t exponent_end(const char *s, size_t n, size_t i) {
    if (i >= n || (s[i] != 'e' && s[i] != 'E')) {
        return i;
    }
    size_t j = i + 1;
    if (j < n && (s[j] == '-' || s[j] == '+')) {
        j++;
    }
    if (j >= n || !is_digit((unsigned char)s[j])) {
        return i;
    }
    while (j < n && is_digit((unsigned char)s[j])) {
        j++;
    }
    return j;
}

static calc_error_t fail(calc_t *calc, calc_error_t err, size_t offset, const char *format, ...) {
    va_list args;
    va_start(args, format);
    vsnprintf(calc->message, sizeof(calc->message), format, args);
    va_end(args);
    calc->column = offset + 1;
    return err;
}

/// @brief Returns the index of the function called name, or FUNCTION_COUNT
/// if there is none.
static size_t find_function(const char *name) {
    for (size_t i = 0; i < FUNCTION_COUNT; i++) {
        if (strcmp(functions[i].name, name) == 0) {
            return i;
        }
    }
    return FUNCTION_COUNT;
}

/// @brief Returns 1 if c can start a name: an ASCII letter or underscore.
/// Written out rather than with isalpha(), which depends on the locale.
static int is_alpha(unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

/// @brief Returns 1 if c is an ASCII digit.
static int is_digit(unsigned char c) {
    return c >= '0' && c <= '9';
}

/// @brief Returns 1 if c is ASCII white space: a space, tab, newline,
/// vertical tab, form feed or carriage return, whatever the locale.
static int is_space(unsigned char c) {
    return c == ' ' || (c >= '\t' && c <= '\r');
}

/// @brief Reads the token at *position in the line, and moves *position past
/// it. At the end of the line, or at a comment, the token is TOKEN_END.
static calc_error_t next_token(calc_t *calc, size_t *position, token_t *token) {
    const char *s = calc->line.data;
    size_t n = calc->line.length, offset = *position;
    while (offset < n && is_space((unsigned char)s[offset])) {
        offset++;
    }
    token->op = OP_ADD;
    if (offset >= n) {
        token->kind = TOKEN_END;
        token->offset = n;
        token->length = 0;
        return CALC_OK;
    }
    size_t length = token_length(s + offset, n - offset);
    *position = offset + length;
    token->offset = offset;
    token->length = length;
    unsigned char c = (unsigned char)calc->line.data[offset];
    if (c == '#') {
        token->kind = TOKEN_END;
        token->length = 0;
    } else if (is_digit(c) || (c == '.' && length > 1)) {
        token->kind = TOKEN_NUMBER;
    } else if (is_alpha(c)) {
        token->kind = TOKEN_NAME;
    } else if (c == '(') {
        token->kind = TOKEN_LPAREN;
    } else if (c == ')') {
        token->kind = TOKEN_RPAREN;
    } else if (c == '=') {
        token->kind = TOKEN_ASSIGN;
    } else {
        token->kind = TOKEN_OPERATOR;
        switch (length == 1 ? c : (length == 2 && c == '*' ? '^' : 0)) {
        case '+':
            token->op = OP_ADD;
            break;
        case '-':
            token->op = OP_SUB;
            break;
        case '*':
            token->op = OP_MUL;
            break;
        case '/':
            token->op = OP_DIV;
            break;
        case '%':
            token->op = OP_MOD;
            break;
        case '^':
            token->op = OP_POW;
            break;
        default:
            token->kind = TOKEN_BAD;
            break;
        }
    }
    return CALC_OK;
}

/// @brief Sets the message to "out of memory", which has no column, and
/// returns CALC_NO_MEM.
static calc_error_t no_mem(calc_t *calc) {
    snprintf(calc->message, sizeof(calc->message), "out of memory");
    calc->column = 0;
    return CALC_NO_MEM;
}

/// @brief Returns an operator's precedence: higher binds tighter. Open
/// parentheses and functions have none, so nothing is applied past them.
static int precedence(op_kind_t kind) {
    switch (kind) {
    case OP_ADD:
    case OP_SUB:
        return 1;
    case OP_MUL:
    case OP_DIV:
    case OP_MOD:
        return 2;
    case OP_NEG:
        return 3;
    case OP_POW:
        return 4;
    default:
        return 0;
    }
}

/// @brief Pushes an entry onto the operator stack.
static calc_error_t push_op(calc_t *calc, op_kind_t kind, size_t func, size_t offset, size_t length) {
    calc_op_t op = {kind, func, offset, length};
    sof_debug("push '%.*s'\n", (int)length, calc->line.data + offset);
    if (sof_stack_push(&calc->ops, &op) != SOF_STACK_OK) {
        return no_mem(calc);
    }
    return CALC_OK;
}

/// @brief Pushes a value onto the value stack.
static calc_error_t push_value(calc_t *calc, double value) {
    sof_debug("push %g\n", value);
    if (sof_stack_push(&calc->values, &value) != SOF_STACK_OK) {
        return no_mem(calc);
    }
    return CALC_OK;
}

/// @brief Writes a token as messages show it: in quotes, cut short if it is
/// long, or "the end of the line".
static void quote(const calc_t *calc, const token_t *token, char *buffer, size_t size) {
    if (token->kind == TOKEN_END) {
        snprintf(buffer, size, "the end of the line");
        return;
    }
    size_t length = token->length > QUOTE_MAX ? QUOTE_MAX : token->length;
    snprintf(buffer, size, "'%.*s%s'", (int)length, calc->line.data + token->offset, length < token->length ? "..." : "");
}

/// @brief Applies the operators on top of the operator stack while their
/// precedence is at least min_precedence, stopping at an open parenthesis.
/// With min_precedence 0, everything down to an open parenthesis is applied.
/// Traced as a call, so the operators it applies are indented under it.
static calc_error_t reduce(calc_t *calc, int min_precedence) {
    sof_debug_enter();
    calc_op_t *top;
    while ((top = sof_stack_peek(&calc->ops)) != NULL && top->kind != OP_PAREN) {
        int p = precedence(top->kind);
        if (min_precedence > 0 && p < min_precedence) {
            break;
        }
        calc_op_t op = *top;
        sof_stack_pop(&calc->ops, NULL);
        calc_error_t err = apply(calc, &op);
        if (err != CALC_OK) {
            sof_debug_return(err);
        }
    }
    sof_debug_return(CALC_OK);
}

/// @brief Frees whatever parts of calc are set up, and leaves it empty.
static void release(calc_t *calc) {
    size_t position = 0;
    sof_hashmap_element_t *e;
    while ((e = sof_hashmap_next(&calc->vars, &position)) != NULL) {
        sof_alloc_free(&calc->alloc, e->data, sizeof(double));
    }
    sof_hashmap_free(&calc->vars);
    sof_stack_free(&calc->values);
    sof_stack_free(&calc->ops);
    sof_string_free(&calc->line);
    memset(calc, 0, sizeof(calc_t));
}

/// @brief Returns the length of the token at the start of the n bytes at s,
/// which don't start with white space and number at least one. The tokens
/// are tried in this order: a comment (# to the end), a number, a name, the
/// power operator **, a run of bytes above ASCII (so a UTF-8 character is
/// reported whole), or any other single byte.
static size_t token_length(const char *s, size_t n) {
    unsigned char c = (unsigned char)s[0];
    size_t i = 1;
    if (c == '#') {
        return n;
    }
    if (is_digit(c)) {
        while (i < n && is_digit((unsigned char)s[i])) {
            i++;
        }
        if (i < n && s[i] == '.') {
            i++;
        }
        while (i < n && is_digit((unsigned char)s[i])) {
            i++;
        }
        return exponent_end(s, n, i);
    }
    if (c == '.' && n > 1 && is_digit((unsigned char)s[1])) {
        while (i < n && is_digit((unsigned char)s[i])) {
            i++;
        }
        return exponent_end(s, n, i);
    }
    if (is_alpha(c)) {
        while (i < n && (is_alpha((unsigned char)s[i]) || is_digit((unsigned char)s[i]))) {
            i++;
        }
        return i;
    }
    if (c == '*' && n > 1 && s[1] == '*') {
        return 2;
    }
    if (c >= 0x80) {
        while (i < n && (unsigned char)s[i] >= 0x80) {
            i++;
        }
        return i;
    }
    return 1;
}

/// @brief Returns 1 if name is a name a variable can have.
static int valid_name(const char *name) {
    size_t length = strlen(name);
    if (length == 0 || length > CALC_NAME_MAX || !is_alpha((unsigned char)name[0])) {
        return 0;
    }
    for (size_t i = 1; i < length; i++) {
        if (!is_alpha((unsigned char)name[i]) && !is_digit((unsigned char)name[i])) {
            return 0;
        }
    }
    return find_function(name) == FUNCTION_COUNT;
}

calc_error_t calc_eval(calc_t *calc, const char *line, double *value, int *assigned) {
    if (assigned != NULL) {
        *assigned = 0;
    }
    if (calc == NULL || !calc->ready || line == NULL) {
        return CALC_ERROR;
    }
    calc->message[0] = '\0';
    calc->column = 0;
    sof_stack_clear(&calc->values);
    sof_stack_clear(&calc->ops);
    sof_string_clear(&calc->line);
    if (sof_string_append_cstr(&calc->line, line) != SOF_STRING_OK) {
        return no_mem(calc);
    }
    sof_debug("line '%s'\n", line);
    size_t position = 0;
    token_t token;
    calc_error_t err = next_token(calc, &position, &token);
    if (err != CALC_OK) {
        return err;
    }
    if (token.kind == TOKEN_END) {
        return CALC_EMPTY;
    }
    // "name = expression" is an assignment.
    char target[CALC_NAME_MAX + 1];
    int is_assignment = 0;
    if (token.kind == TOKEN_NAME) {
        size_t after = position;
        token_t equals;
        err = next_token(calc, &after, &equals);
        if (err != CALC_OK) {
            return err;
        }
        if (equals.kind == TOKEN_ASSIGN) {
            if (token.length > CALC_NAME_MAX) {
                return fail(calc, CALC_SYNTAX, token.offset, "name too long");
            }
            memcpy(target, calc->line.data + token.offset, token.length);
            target[token.length] = '\0';
            if (find_function(target) < FUNCTION_COUNT) {
                return fail(calc, CALC_SYNTAX, token.offset, "'%s' is a function and can't be assigned", target);
            }
            is_assignment = 1;
            position = after;
            err = next_token(calc, &position, &token);
            if (err != CALC_OK) {
                return err;
            }
        }
    }
    double result;
    err = evaluate(calc, &position, &token, &result);
    if (err != CALC_OK) {
        return err;
    }
    if (calc_set(calc, is_assignment ? target : "ans", result) != CALC_OK) {
        return no_mem(calc);
    }
    if (value != NULL) {
        *value = result;
    }
    if (assigned != NULL) {
        *assigned = is_assignment;
    }
    return CALC_OK;
}

calc_error_t calc_free(calc_t *calc) {
    if (calc == NULL || !calc->ready) {
        return CALC_ERROR;
    }
    release(calc);
    return CALC_OK;
}

calc_error_t calc_get(const calc_t *calc, const char *name, double *value) {
    if (calc == NULL || !calc->ready || name == NULL) {
        return CALC_ERROR;
    }
    const sof_hashmap_element_t *e = sof_hashmap_find(&calc->vars, name);
    if (e == NULL) {
        return CALC_NO_MATCH;
    }
    if (value != NULL) {
        *value = *(const double *)e->data;
    }
    return CALC_OK;
}

calc_error_t calc_init(calc_t *calc, const sof_alloc_t *alloc) {
    if (calc == NULL) {
        return CALC_ERROR;
    }
    memset(calc, 0, sizeof(calc_t));
    if (!sof_alloc_valid(alloc)) {
        return CALC_ERROR;
    }
    if (alloc != NULL) {
        calc->alloc = *alloc;
    }
    const sof_alloc_t *a = &calc->alloc;
    if (sof_hashmap_init(&calc->vars, &sof_hashmap_string_keys, NULL, NULL, 0, a) == SOF_HASHMAP_OK && sof_stack_init(&calc->values, sizeof(double), 16, NULL, NULL, a) == SOF_STACK_OK && sof_stack_init(&calc->ops, sizeof(calc_op_t), 16, NULL, NULL, a) == SOF_STACK_OK && sof_string_init(&calc->line, 80, a) == SOF_STRING_OK) {
        calc->ready = 1;
        if (calc_set(calc, "pi", 3.14159265358979323846) == CALC_OK && calc_set(calc, "e", 2.71828182845904523536) == CALC_OK) {
            return CALC_OK;
        }
    }
    release(calc);
    return CALC_NO_MEM;
}

const char *calc_message(const calc_t *calc) {
    if (calc == NULL || !calc->ready) {
        return "";
    }
    return calc->message;
}

const char *calc_next(const calc_t *calc, size_t *position, double *value) {
    if (calc == NULL || !calc->ready || position == NULL) {
        return NULL;
    }
    const sof_hashmap_element_t *e = sof_hashmap_next(&calc->vars, position);
    if (e == NULL) {
        return NULL;
    }
    if (value != NULL) {
        *value = *(const double *)e->data;
    }
    return e->key;
}

calc_error_t calc_set(calc_t *calc, const char *name, double value) {
    if (calc == NULL || !calc->ready || name == NULL || !valid_name(name)) {
        return CALC_ERROR;
    }
    int created;
    sof_hashmap_element_t *e = sof_hashmap_insert(&calc->vars, name, &created);
    if (e == NULL) {
        return CALC_NO_MEM;
    }
    if (created) {
        e->data = sof_alloc_malloc(&calc->alloc, sizeof(double));
        if (e->data == NULL) {
            sof_hashmap_remove_element(&calc->vars, e);
            return CALC_NO_MEM;
        }
    }
    *(double *)e->data = value;
    return CALC_OK;
}

calc_error_t calc_unset(calc_t *calc, const char *name) {
    if (calc == NULL || !calc->ready || name == NULL) {
        return CALC_ERROR;
    }
    sof_hashmap_element_t *e = sof_hashmap_find(&calc->vars, name);
    if (e == NULL) {
        return CALC_NO_MATCH;
    }
    sof_alloc_free(&calc->alloc, e->data, sizeof(double));
    sof_hashmap_remove_element(&calc->vars, e);
    return CALC_OK;
}
