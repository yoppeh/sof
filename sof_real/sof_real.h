/**
 * @file sof_real.h
 * @author Warren Mann (warren@nonvol.io)
 * @brief Shortest round-trip formatting and exact parsing of floating-point
 * numbers in c.
 * @details sof_real_format() writes a double with the fewest significant
 * digits that read back as the same double, and sof_real_format_float() a
 * float with the fewest that read back as the same float; where several
 * decimals of that length would, the one closest to the value is written.
 * sof_real_decimal() and sof_real_decimal_float() give those digits and
 * their power of ten as integers, for callers that write numbers their own
 * way. sof_real_parse() and sof_real_parse_float() read a decimal number
 * and round it correctly, to the nearest double or float, with ties to the
 * one whose last bit is 0, whatever its length; sof_real_from_decimal() and
 * sof_real_from_decimal_float() do the same for digits and a power of ten
 * a caller has already taken apart.
 *
 * Formatting is Raffaello Giulietti's Schubfach ("The Schubfach way to
 * render doubles", 2020), which gives the same digits as Ryu and as Java's
 * Double.toString(). Parsing is Clinger's fast path for short numbers,
 * then Daniel Lemire's form of the Eisel-Lemire algorithm ("Number Parsing
 * at a Gigabyte per Second", 2021), and, for the few numbers that leaves
 * undecided, an exact comparison with the halfway point between two
 * candidates, in integers that take about two kilobytes of stack. Neither
 * depends on the locale: the decimal point is always '.'. Neither
 * allocates, uses the maths library or keeps any state.
 *
 * The grammar sof_real_parse() reads is
 *
 *     number   = [ sign ] mantissa [ exponent ]
 *     sign     = "+" / "-"
 *     mantissa = digits [ "." [ digits ] ] / "." digits
 *     exponent = ( "e" / "E" ) [ sign ] digits
 *     digits   = 1*( "0" - "9" )
 *
 * so "1", "-0.5", ".5", "5.", "+1e10" and "1E-7" are numbers; JSON's and
 * TOML's numbers, without TOML's underscores, are a part of it. There is no
 * white space before the number, no hexadecimal form, and no infinity or
 * NaN, which callers that want them read themselves. Reading stops at the
 * first byte that can't continue the number, which needn't be a NUL: an
 * 'e' that no digits follow, as in "2em", is left unread, and the number
 * is 2.
 *
 * sof_real_format() writes as printf()'s %g does with the shortest
 * precision that reads back, except that a number from 1e-4 up to, but not
 * including, 1e17 is never written with an exponent, so 100 is "100" and
 * 1e16 "10000000000000000": "0.0001", "0.1", "1.5", "100", "1e+17",
 * "1.5e-07", "-0", "5e-324". Infinities are "inf" and "-inf", and every NaN
 * is "nan".
 * @version 0.0.1
 * @date 2026-11-20
 * @copyright Copyright (c) 2026
 */

#ifndef SOF_REAL_H
#define SOF_REAL_H

#include <stddef.h>
#include <stdint.h>

/**
 * @brief The buffer size sof_real_format() and sof_real_format_float()
 * need for any value, the NUL included.
 * @details 25 bytes: the longest text, such as "-1.2345678901234567e-308",
 * is 24 characters.
 */
#define SOF_REAL_FORMAT_SIZE 25

/**
 * @brief Error codes for the sof_real functions.
 * @details SOF_REAL_NO_MEM is never returned, since the library allocates
 * nothing; it is there so the codes match those of the other libraries.
 */
typedef enum sof_real_error_t {
    /// @brief No error.
    SOF_REAL_OK = 0,
    /// @brief Not returned by sof_real, which never allocates.
    SOF_REAL_NO_MEM,
    /// @brief The text doesn't start with a number.
    SOF_REAL_NO_MATCH,
    /// @brief A NULL pointer where one is needed, or a value that has no
    /// digits, an infinity or a NaN.
    SOF_REAL_ERROR,
    /// @brief The number is too large for the type: the value is an
    /// infinity of its sign.
    SOF_REAL_RANGE,
} sof_real_error_t;

/**
 * @brief Find the shortest decimal that reads back as a double.
 * @details The decimal is digits times ten to the power exponent, where
 * digits has the fewest significant digits, at most 17, of any decimal that
 * rounds to value, and no trailing zeros; of those of that length, it is
 * the closest to value, and of two equally close, the even one. The sign
 * isn't included: digits is of value's magnitude, and signbit() gives the
 * sign. 0 and -0 give 0 and 0.
 * @param value The double.
 * @param digits Where the digits go, as an integer.
 * @param exponent Where the power of ten goes, from -324 to 308.
 * @return SOF_REAL_OK, or SOF_REAL_ERROR, changing nothing, if value is an
 * infinity or a NaN, or digits or exponent is NULL.
 */
extern sof_real_error_t sof_real_decimal(double value, uint64_t *digits, int *exponent);

/**
 * @brief Find the shortest decimal that reads back as a float.
 * @details As sof_real_decimal(), for a float: digits has at most 9
 * significant digits, and exponent is from -45 to 38.
 * @param value The float.
 * @param digits Where the digits go, as an integer.
 * @param exponent Where the power of ten goes.
 * @return SOF_REAL_OK, or SOF_REAL_ERROR, changing nothing, if value is an
 * infinity or a NaN, or digits or exponent is NULL.
 */
extern sof_real_error_t sof_real_decimal_float(float value, uint64_t *digits, int *exponent);

/**
 * @brief Write a double with the fewest digits that read back as it.
 * @details The digits are sof_real_decimal()'s, written as the header's
 * description shows: without an exponent from 1e-4 up to 1e17, and
 * otherwise as d.ddde+XX, with at least two digits of exponent. The decimal
 * point is always '.'. The text is NUL-terminated.
 * @param value The double, which may be an infinity ("inf", "-inf") or a
 * NaN ("nan").
 * @param buffer Where the text goes.
 * @param size The size of buffer, NUL included. SOF_REAL_FORMAT_SIZE is
 * always enough.
 * @return The length of the text, not counting the NUL, or 0 if it doesn't
 * fit in size bytes, when buffer holds an empty string if size isn't 0,
 * or if buffer is NULL.
 */
extern size_t sof_real_format(double value, char *buffer, size_t size);

/**
 * @brief Write a float with the fewest digits that read back as it.
 * @details As sof_real_format(), with sof_real_decimal_float()'s digits:
 * at most 9, so 0.1f is "0.1" rather than the double's
 * "0.10000000149011612".
 * @param value The float.
 * @param buffer Where the text goes.
 * @param size The size of buffer, NUL included. SOF_REAL_FORMAT_SIZE is
 * always enough.
 * @return The length of the text, or 0 if it doesn't fit, as for
 * sof_real_format().
 */
extern size_t sof_real_format_float(float value, char *buffer, size_t size);

/**
 * @brief Convert digits times a power of ten to the nearest double.
 * @details The value is digits times ten to the power exponent, negated if
 * negative isn't 0, rounded to the nearest double, with ties to the one
 * whose last bit is 0. It is the inverse of sof_real_decimal(): the digits
 * and exponent that gives for any finite double convert back to it.
 * @param digits The digits, as an integer; 0 gives 0 or -0, whatever the
 * exponent.
 * @param exponent The power of ten, which may be any int.
 * @param negative Non-zero for a negative value.
 * @param value Where the double goes.
 * @return SOF_REAL_OK, including for a value too small for a double, which
 * is 0 or a subnormal; SOF_REAL_RANGE if it is too large, with an infinity
 * of its sign in value; or SOF_REAL_ERROR if value is NULL.
 */
extern sof_real_error_t sof_real_from_decimal(uint64_t digits, int exponent, int negative, double *value);

/**
 * @brief Convert digits times a power of ten to the nearest float.
 * @details As sof_real_from_decimal(), rounding to a float. The value is
 * rounded once, straight to the float, never to a double first.
 * @param digits The digits, as an integer.
 * @param exponent The power of ten.
 * @param negative Non-zero for a negative value.
 * @param value Where the float goes.
 * @return As sof_real_from_decimal().
 */
extern sof_real_error_t sof_real_from_decimal_float(uint64_t digits, int exponent, int negative, float *value);

/**
 * @brief Read a number and round it to the nearest double.
 * @details The number is read by the header's grammar from the start of
 * text and rounded correctly, to the nearest double, with ties to the one
 * whose last bit is 0, however many digits it has. strtod() gives the
 * same double, in the "C" locale, for any text both read, but this reads
 * no white space, hexadecimal, infinity or NaN, and ignores the locale.
 * @param text The text, which needn't be NUL-terminated.
 * @param length The bytes of text that may be read.
 * @param value Where the double goes: the number, 0 when there is none.
 * @param used Where the number of bytes read goes, 0 when there is no
 * number; may be NULL. A caller that wants the whole text to be the number
 * checks that this is length.
 * @return SOF_REAL_OK, including for a number too small for a double,
 * which is 0 or a subnormal of its sign; SOF_REAL_RANGE if it is too
 * large, with an infinity of its sign in value; SOF_REAL_NO_MATCH if text
 * doesn't start with a number; or SOF_REAL_ERROR, changing nothing, if
 * value is NULL, or text is NULL and length isn't 0.
 */
extern sof_real_error_t sof_real_parse(const char *text, size_t length, double *value, size_t *used);

/**
 * @brief Read a number and round it to the nearest float.
 * @details As sof_real_parse(), rounding to a float. The number is rounded
 * once, straight to the float, as strtof() does, never to a double first.
 * @param text The text, which needn't be NUL-terminated.
 * @param length The bytes of text that may be read.
 * @param value Where the float goes.
 * @param used Where the number of bytes read goes; may be NULL.
 * @return As sof_real_parse().
 */
extern sof_real_error_t sof_real_parse_float(const char *text, size_t length, float *value, size_t *used);

#endif // SOF_REAL_H
