/**
 * @file main.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief Converts text between UTF-8, UTF-16 and UTF-32, or checks that it
 * is valid.
 * @details
 *
 *     utfconv [-h] [-c] [-d] [-b] [-f encoding] [-t encoding] [file]...
 *
 * Reads each file, or standard input if there are none or a file is "-",
 * in the encoding given with -f (UTF-8 by default), and writes it to
 * standard output in the encoding given with -t (UTF-8 by default), each
 * error in the input written as U+FFFD. With -d, a byte order mark at the
 * start of the input decides its encoding. With -c, it writes nothing but
 * where each error is, and how many code points each file has. Its options
 * are read with sof_copt, and the input is read into a sof_string. It
 * exits with 0 if the input was valid, 1 if it wasn't, and 2 if the options
 * are wrong, a file can't be read or written, or memory runs out.
 * @version 0.0.1
 * @date 2026-11-19
 * @copyright Copyright (c) 2026
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "sof_alloc.h"
#include "sof_copt.h"
#include "sof_string.h"
#include "sof_utf.h"

/// @brief The exit codes.
#define EXIT_VALID 0
#define EXIT_INVALID 1
#define EXIT_TROUBLE 2

/// @brief The code the validate callbacks return when an option's argument
/// is wrong, after printing why. sof_copt keeps it in the option's
/// stop_value.
#define BAD_ARGUMENT 1

/// @brief The size of each piece of input read.
#define CHUNK_SIZE 65536

/// @brief An encoding the program reads or writes.
typedef struct encoding_t {
    /// @brief Its name for -f and -t, matched ignoring case.
    const char *name;
    /// @brief Its name in messages.
    const char *label;
    /// @brief The bytes in a unit: 1, 2 or 4.
    unsigned width;
    /// @brief Non-zero if a unit's most significant byte comes first.
    int big_endian;
    /// @brief Non-zero for UTF-16 and UTF-32 without an order: on input, a
    /// byte order mark of the width, if there is one, sets the order, and
    /// otherwise it is big-endian; on output, one is written.
    int bom;
} encoding_t;

/// @brief The encodings, in the order the usage message lists them.
static const encoding_t encodings[] = {
    {"utf-8", "UTF-8", 1, 0, 0},
    {"utf-16", "UTF-16", 2, 1, 1},
    {"utf-16be", "UTF-16BE", 2, 1, 0},
    {"utf-16le", "UTF-16LE", 2, 0, 0},
    {"utf-32", "UTF-32", 4, 1, 1},
    {"utf-32be", "UTF-32BE", 4, 1, 0},
    {"utf-32le", "UTF-32LE", 4, 0, 0},
};

/// @brief Indexes in encodings.
#define UTF8 0
#define UTF16BE 2
#define UTF16LE 3
#define UTF32BE 5
#define UTF32LE 6

/// @brief What the options ask for, filled in by their callbacks.
typedef struct settings_t {
    /// @brief The input's encoding, from -f.
    const encoding_t *from;
    /// @brief The output's encoding, from -t.
    const encoding_t *to;
} settings_t;

/// @brief One file's text, in units of its encoding, in the machine's
/// order, as sof_utf takes them.
typedef struct text_t {
    /// @brief The encoding it was read in, with its byte order.
    const encoding_t *encoding;
    /// @brief The units: bytes, uint16_t or uint32_t, by the encoding's
    /// width.
    const void *units;
    /// @brief The number of units.
    size_t count;
    /// @brief The bytes before the first unit: the length of a byte order
    /// mark that was skipped, or 0.
    size_t offset;
    /// @brief The bytes after the last whole unit, from 0 to the width - 1.
    size_t extra;
    /// @brief The units, if they were allocated, or NULL.
    void *owned;
} text_t;

static size_t check_text(const text_t *t, const char *name, int print);
static int convert_text(const text_t *t, const encoding_t *to, FILE *out);
static size_t convert_units(unsigned from, const void *data, size_t count, unsigned to, void *buffer, size_t size);
static size_t count_lines(const text_t *t, size_t from, size_t to);
static size_t decode_unit(const text_t *t, size_t position, uint32_t *code);
static const encoding_t *detect_bom(const unsigned char *bytes, size_t size, size_t *length);
static int encoding_validate(sof_copt_t *option, void *ctx);
static void free_text(text_t *t);
static int load_text(const sof_string_t *in, const encoding_t *encoding, int detect, text_t *t);
static const char *name_of(const char *path);
static void print_error(const text_t *t, const char *name, size_t line, size_t position, size_t used, uint32_t code);
static int process_file(const char *path, const settings_t *settings, sof_string_t *in, char *buffer);
static int read_file(const char *path, sof_string_t *in, char *buffer);
static void serialize(void *units, size_t count, unsigned width, int big_endian);
static void usage(FILE *stream);
static size_t validate_units(const text_t *t, size_t position);
static int write_code(uint32_t code, const encoding_t *to, FILE *out);

static sof_copt_t opt_help = {
    .option = "-h",
    .description = "print this help and exit",
};

static sof_copt_t opt_check = {
    .option = "-c",
    .description = "check the input: print each error and count the code points, instead of converting",
};

static sof_copt_t opt_detect = {
    .option = "-d",
    .description = "let a byte order mark at the start of the input decide its encoding",
};

static sof_copt_t opt_bom = {
    .option = "-b",
    .description = "write a byte order mark at the start of the output",
};

static sof_copt_t opt_from = {
    .option = "-f",
    .description = "the input's encoding (default utf-8)",
    .type = SOF_COPT_ARG_REQUIRED,
    .validate = encoding_validate,
};

static sof_copt_t opt_to = {
    .option = "-t",
    .description = "the output's encoding (default utf-8)",
    .type = SOF_COPT_ARG_REQUIRED,
    .validate = encoding_validate,
};

SOF_COPT_OPTIONS static sof_copt_t *options[] = {
    &opt_help,
    &opt_check,
    &opt_detect,
    &opt_bom,
    &opt_from,
    &opt_to,
};

/// @brief Finds each error in a file's text, printing where it is if print
/// is set.
/// @details The errors are found with the encoding's validate function,
/// which gives the position of the next one, and measured with its decode
/// function, which gives the units to skip, just as the conversions replace
/// each with one U+FFFD. Bytes after the last whole unit are one error more.
/// @return The number of errors.
static size_t check_text(const text_t *t, const char *name, int print) {
    size_t errors = 0, position = 0, line = 1;
    while (position < t->count) {
        size_t bad = position + validate_units(t, position);
        line += count_lines(t, position, bad);
        if (bad == t->count) {
            break;
        }
        uint32_t code;
        size_t used = decode_unit(t, bad, &code);
        errors++;
        if (print) {
            print_error(t, name, line, bad, used, code);
        }
        position = bad + used;
    }
    if (t->extra > 0) {
        errors++;
        if (print) {
            printf("%s: line %zu, byte %zu: %zu byte%s at the end, not a whole %s unit\n", name, line, t->offset + t->count * t->encoding->width, t->extra, t->extra == 1 ? "" : "s", t->encoding->label);
        }
    }
    return errors;
}

/// @brief Writes a file's text to out in the encoding to, each error written
/// as U+FFFD.
/// @details Text in another encoding is converted directly by sof_utf, and
/// text in the same one through UTF-32, which replaces the errors; UTF-32 is
/// copied with each unit that isn't a code point replaced. Each conversion
/// is called first without a buffer, to measure the output.
/// @return 0, or EXIT_TROUBLE if memory ran out or the output couldn't be
/// written, after printing why.
static int convert_text(const text_t *t, const encoding_t *to, FILE *out) {
    unsigned width = t->encoding->width;
    const void *data = t->units;
    size_t count = t->count;
    uint32_t *codes = NULL;
    size_t code_count = 0;
    int rc = 0;
    if (width == to->width && width != 4) {
        code_count = convert_units(width, data, count, 4, NULL, 0);
        if (code_count > 0) {
            codes = sof_alloc_calloc(NULL, code_count, sizeof(uint32_t));
            if (codes == NULL) {
                fprintf(stderr, "out of memory\n");
                return EXIT_TROUBLE;
            }
            convert_units(width, data, count, 4, codes, code_count);
        }
        width = 4;
        data = codes;
        count = code_count;
    }
    size_t n = convert_units(width, data, count, to->width, NULL, 0);
    if (n > 0) {
        void *buffer = sof_alloc_calloc(NULL, n, to->width);
        if (buffer == NULL) {
            fprintf(stderr, "out of memory\n");
            rc = EXIT_TROUBLE;
        } else {
            convert_units(width, data, count, to->width, buffer, n);
            serialize(buffer, n, to->width, to->big_endian);
            if (fwrite(buffer, to->width, n, out) != n) {
                fprintf(stderr, "error writing the output: %s\n", strerror(errno));
                rc = EXIT_TROUBLE;
            }
            sof_alloc_free(NULL, buffer, n * to->width);
        }
    }
    if (rc == 0 && t->extra > 0) {
        rc = write_code(SOF_UTF_REPLACEMENT, to, out);
    }
    sof_alloc_free(NULL, codes, code_count * sizeof(uint32_t));
    return rc;
}

/// @brief Converts count units of data, in the encoding of width from, to
/// the encoding of width to, with the sof_utf conversion for the pair,
/// writing at most size units to buffer, or none if it is NULL.
/// @details Converting UTF-8 or UTF-16 to itself isn't one of them; the
/// caller goes through UTF-32. UTF-32 to UTF-32 copies, replacing each unit
/// that isn't a code point.
/// @return The units the whole conversion needs.
static size_t convert_units(unsigned from, const void *data, size_t count, unsigned to, void *buffer, size_t size) {
    switch (from * 10 + to) {
    case 12:
        return sof_utf8_to_utf16(data, count, buffer, size);
    case 14:
        return sof_utf8_to_utf32(data, count, buffer, size);
    case 21:
        return sof_utf16_to_utf8(data, count, buffer, size);
    case 24:
        return sof_utf16_to_utf32(data, count, buffer, size);
    case 41:
        return sof_utf32_to_utf8(data, count, buffer, size);
    case 42:
        return sof_utf32_to_utf16(data, count, buffer, size);
    case 44: {
        const uint32_t *in = data;
        uint32_t *out = buffer;
        for (size_t i = 0; out != NULL && i < count && i < size; i++) {
            out[i] = sof_utf_valid(in[i]) ? in[i] : SOF_UTF_REPLACEMENT;
        }
        return count;
    }
    default:
        return 0;
    }
}

/// @brief Counts the newlines, U+000A, in units from up to, but not
/// including, to.
static size_t count_lines(const text_t *t, size_t from, size_t to) {
    size_t lines = 0;
    for (size_t i = from; i < to; i++) {
        uint32_t unit;
        switch (t->encoding->width) {
        case 1:
            unit = ((const unsigned char *)t->units)[i];
            break;
        case 2:
            unit = ((const uint16_t *)t->units)[i];
            break;
        default:
            unit = ((const uint32_t *)t->units)[i];
            break;
        }
        lines += unit == 0x0a;
    }
    return lines;
}

/// @brief Decodes the code point, or error, at a position in a file's
/// text.
/// @return The units it takes.
static size_t decode_unit(const text_t *t, size_t position, uint32_t *code) {
    switch (t->encoding->width) {
    case 1:
        return sof_utf8_decode((const unsigned char *)t->units + position, t->count - position, code);
    case 2:
        return sof_utf16_decode((const uint16_t *)t->units + position, t->count - position, code);
    default:
        *code = ((const uint32_t *)t->units)[position];
        return 1;
    }
}

/// @brief Finds the byte order mark, if any, at the start of bytes.
/// @details UTF-32LE's is tried before UTF-16LE's, which starts it, so
/// UTF-16LE text that starts with U+0000 after its mark reads as UTF-32LE,
/// as with other programs that guess from the mark.
/// @return The encoding it marks, setting *length to its length in bytes, or
/// NULL, setting it to 0, if there is none.
static const encoding_t *detect_bom(const unsigned char *bytes, size_t size, size_t *length) {
    static const struct {
        unsigned char bytes[4];
        size_t length;
        int encoding;
    } marks[] = {
        {{0xff, 0xfe, 0x00, 0x00}, 4, UTF32LE},
        {{0x00, 0x00, 0xfe, 0xff}, 4, UTF32BE},
        {{0xef, 0xbb, 0xbf}, 3, UTF8},
        {{0xff, 0xfe}, 2, UTF16LE},
        {{0xfe, 0xff}, 2, UTF16BE},
    };
    for (size_t i = 0; i < sizeof(marks) / sizeof(marks[0]); i++) {
        if (size >= marks[i].length && memcmp(bytes, marks[i].bytes, marks[i].length) == 0) {
            *length = marks[i].length;
            return &encodings[marks[i].encoding];
        }
    }
    *length = 0;
    return NULL;
}

/// @brief Reads the argument of -f or -t, an encoding's name, ignoring case.
static int encoding_validate(sof_copt_t *option, void *ctx) {
    settings_t *settings = ctx;
    sof_string_t name = sof_string_view(option->value, strlen(option->value));
    for (size_t i = 0; i < sizeof(encodings) / sizeof(encodings[0]); i++) {
        sof_string_t known = sof_string_view(encodings[i].name, strlen(encodings[i].name));
        if (sof_string_casecmp(&name, &known) == 0) {
            if (option == &opt_from) {
                settings->from = &encodings[i];
            } else {
                settings->to = &encodings[i];
            }
            return 0;
        }
    }
    fprintf(stderr, "%s needs utf-8, utf-16, utf-16be, utf-16le, utf-32, utf-32be or utf-32le, not \"%s\"\n", option->option, option->value);
    return BAD_ARGUMENT;
}

/// @brief Frees the units of a file's text, if they were allocated.
static void free_text(text_t *t) {
    sof_alloc_free(NULL, t->owned, t->count * t->encoding->width);
    memset(t, 0, sizeof(*t));
}

/// @brief Makes a file's text from the bytes read, in the encoding given,
/// or with detect set, the one a byte order mark gives.
/// @details UTF-8 is used where it is. UTF-16 and UTF-32 units are put
/// together from their bytes in the encoding's order, into an array in the
/// machine's.
/// @return 0, or EXIT_TROUBLE if memory ran out, after printing why.
static int load_text(const sof_string_t *in, const encoding_t *encoding, int detect, text_t *t) {
    const unsigned char *bytes = (const unsigned char *)in->data;
    size_t size = in->length;
    size_t skip;
    const encoding_t *marked = detect_bom(bytes, size, &skip);
    memset(t, 0, sizeof(*t));
    if (detect && marked != NULL) {
        encoding = marked;
    } else if (encoding->bom && encoding->width == 2 && size >= 2 && (bytes[0] == 0xfe || bytes[0] == 0xff) && bytes[0] + bytes[1] == 0xfe + 0xff) {
        encoding = &encodings[bytes[0] == 0xfe ? UTF16BE : UTF16LE];
        skip = 2;
    } else if (encoding->bom && encoding->width == 4 && marked != NULL && marked->width == 4) {
        encoding = marked;
    } else {
        // An unordered encoding without its mark is big-endian; any other
        // mark is text.
        encoding = encoding->bom ? &encodings[encoding->width == 2 ? UTF16BE : UTF32BE] : encoding;
        skip = 0;
    }
    unsigned width = encoding->width;
    t->encoding = encoding;
    t->offset = skip;
    bytes += skip;
    size -= skip;
    t->count = size / width;
    t->extra = size % width;
    if (width == 1 || t->count == 0) {
        t->units = bytes;
        return 0;
    }
    t->owned = sof_alloc_calloc(NULL, t->count, width);
    if (t->owned == NULL) {
        fprintf(stderr, "out of memory\n");
        t->count = 0;
        return EXIT_TROUBLE;
    }
    for (size_t i = 0; i < t->count; i++) {
        const unsigned char *b = bytes + i * width;
        uint32_t unit = 0;
        for (unsigned j = 0; j < width; j++) {
            unsigned shift = encoding->big_endian ? 8 * (width - 1 - j) : 8 * j;
            unit |= (uint32_t)b[j] << shift;
        }
        if (width == 2) {
            ((uint16_t *)t->owned)[i] = (uint16_t)unit;
        } else {
            ((uint32_t *)t->owned)[i] = unit;
        }
    }
    t->units = t->owned;
    return 0;
}

/// @brief Returns a file's name for messages: "standard input" for "-".
static const char *name_of(const char *path) {
    return strcmp(path, "-") == 0 ? "standard input" : path;
}

/// @brief Prints where an error is, and what it is.
static void print_error(const text_t *t, const char *name, size_t line, size_t position, size_t used, uint32_t code) {
    size_t byte = t->offset + position * t->encoding->width;
    printf("%s: line %zu, byte %zu: ", name, line, byte);
    switch (t->encoding->width) {
    case 1: {
        const unsigned char *s = (const unsigned char *)t->units + position;
        printf(code == SOF_UTF_INCOMPLETE ? "UTF-8 sequence cut off by the end:" : "invalid UTF-8:");
        for (size_t i = 0; i < used; i++) {
            printf(" %02x", s[i]);
        }
        printf("\n");
        break;
    }
    case 2: {
        uint16_t unit = ((const uint16_t *)t->units)[position];
        if (code == SOF_UTF_INCOMPLETE) {
            printf("high surrogate %04x cut off by the end\n", unit);
        } else if (unit >= 0xdc00) {
            printf("low surrogate %04x without a high one before it\n", unit);
        } else {
            printf("high surrogate %04x without a low one after it\n", unit);
        }
        break;
    }
    default:
        printf("%08lx isn't a code point\n", (unsigned long)code);
        break;
    }
}

/// @brief Reads one file, or standard input for "-", and checks or converts
/// it.
/// @return EXIT_VALID, EXIT_INVALID if the text has errors, or EXIT_TROUBLE
/// if it couldn't be read or written, or memory ran out, after printing why.
static int process_file(const char *path, const settings_t *settings, sof_string_t *in, char *buffer) {
    const char *name = name_of(path);
    int rc = read_file(path, in, buffer);
    text_t t;
    if (rc != 0 || load_text(in, settings->from, opt_detect.present, &t) != 0) {
        return EXIT_TROUBLE;
    }
    size_t errors = check_text(&t, name, opt_check.present);
    if (opt_check.present) {
        size_t codes;
        switch (t.encoding->width) {
        case 1:
            codes = sof_utf8_count(t.units, t.count);
            break;
        case 2:
            codes = sof_utf16_count(t.units, t.count);
            break;
        default:
            codes = t.count;
            break;
        }
        codes += t.extra > 0;
        printf("%s: %s%s%s, %zu code point%s", name, errors == 0 ? "valid " : "", t.encoding->label, t.offset > 0 ? " after a byte order mark" : "", codes, codes == 1 ? "" : "s");
        if (errors > 0) {
            printf(", %zu error%s", errors, errors == 1 ? "" : "s");
        }
        printf("\n");
    } else {
        rc = convert_text(&t, settings->to, stdout);
        if (rc == 0 && errors > 0) {
            fprintf(stderr, "%s: %zu error%s in the %s written as U+FFFD\n", name, errors, errors == 1 ? "" : "s", t.encoding->label);
        }
    }
    free_text(&t);
    if (rc == 0 && errors > 0) {
        rc = EXIT_INVALID;
    }
    return rc;
}

/// @brief Reads one file, or standard input for "-", into in, replacing what
/// was there, a piece at a time through buffer.
/// @return 0, or EXIT_TROUBLE if it couldn't be read or memory ran out,
/// after printing why.
static int read_file(const char *path, sof_string_t *in, char *buffer) {
    int is_stdin = strcmp(path, "-") == 0;
    FILE *f = is_stdin ? stdin : fopen(path, "rb");
    if (f == NULL) {
        fprintf(stderr, "error opening %s: %s\n", path, strerror(errno));
        return EXIT_TROUBLE;
    }
    sof_string_clear(in);
    int rc = 0;
    size_t got;
    while ((got = fread(buffer, 1, CHUNK_SIZE, f)) > 0) {
        if (sof_string_append_bytes(in, buffer, got) != SOF_STRING_OK) {
            fprintf(stderr, "out of memory reading %s\n", name_of(path));
            rc = EXIT_TROUBLE;
            break;
        }
    }
    if (rc == 0 && ferror(f)) {
        fprintf(stderr, "error reading %s\n", name_of(path));
        rc = EXIT_TROUBLE;
    }
    if (!is_stdin) {
        fclose(f);
    }
    return rc;
}

/// @brief Puts count units, of width bytes, in the machine's order, into the
/// bytes of the encoding's order, in place.
/// @details Each unit is read whole before its own bytes are written, so
/// nothing is overwritten before it is read.
static void serialize(void *units, size_t count, unsigned width, int big_endian) {
    unsigned char *bytes = units;
    if (width == 1) {
        return;
    }
    for (size_t i = 0; i < count; i++) {
        uint32_t unit = width == 2 ? ((uint16_t *)units)[i] : ((uint32_t *)units)[i];
        for (unsigned j = 0; j < width; j++) {
            unsigned shift = big_endian ? 8 * (width - 1 - j) : 8 * j;
            bytes[i * width + j] = (unsigned char)(unit >> shift);
        }
    }
}

/// @brief Prints the usage message, with the options sof_copt knows.
static void usage(FILE *stream) {
    fprintf(stream, "usage: utfconv [-h] [-c] [-d] [-b] [-f encoding] [-t encoding] [file]...\n");
    sof_copt_print(stream);
    fprintf(stream,
            "Converts the files, or standard input if there are none or a file is \"-\",\n"
            "from one encoding to another, writing U+FFFD for each error. The encodings\n"
            "are utf-8, utf-16be, utf-16le, utf-32be, utf-32le, and utf-16 and utf-32,\n"
            "which are big-endian unless the input starts with a byte order mark that\n"
            "says otherwise, and are written with one. Exits with 0 if the input was\n"
            "valid, 1 if it wasn't, and 2 on any other error.\n");
}

/// @brief Finds the first error from a position in a file's text, with the
/// encoding's validate function.
/// @return The units from position to the error, or to the end if there is
/// none.
static size_t validate_units(const text_t *t, size_t position) {
    switch (t->encoding->width) {
    case 1:
        return sof_utf8_validate((const unsigned char *)t->units + position, t->count - position);
    case 2:
        return sof_utf16_validate((const uint16_t *)t->units + position, t->count - position);
    default:
        return sof_utf32_validate((const uint32_t *)t->units + position, t->count - position);
    }
}

/// @brief Writes one code point to out in the encoding to.
/// @return 0, or EXIT_TROUBLE if it couldn't be written, after printing why.
static int write_code(uint32_t code, const encoding_t *to, FILE *out) {
    uint32_t units[SOF_UTF8_SIZE_MAX];
    size_t n;
    switch (to->width) {
    case 1:
        n = sof_utf8_encode(code, units, sizeof(units));
        break;
    case 2:
        n = sof_utf16_encode(code, (uint16_t *)units, SOF_UTF16_SIZE_MAX);
        break;
    default:
        units[0] = code;
        n = 1;
        break;
    }
    serialize(units, n, to->width, to->big_endian);
    if (fwrite(units, to->width, n, out) != n) {
        fprintf(stderr, "error writing the output: %s\n", strerror(errno));
        return EXIT_TROUBLE;
    }
    return 0;
}

int main(int ac, char **av) {
    settings_t settings = {&encodings[UTF8], &encodings[UTF8]};
    int index;
    sof_copt_t *option;
    sof_copt_error_t err = sof_copt_parse(ac, av, &settings, &index, &option);
    switch (err) {
    case SOF_COPT_OK:
        break;
    case SOF_COPT_NO_MATCH:
        fprintf(stderr, "unknown option %s\n", av[index]);
        usage(stderr);
        return EXIT_TROUBLE;
    case SOF_COPT_MISSING_ARGUMENT:
        fprintf(stderr, "%s needs an argument\n", option->option);
        usage(stderr);
        return EXIT_TROUBLE;
    case SOF_COPT_UNEXPECTED_ARGUMENT:
        fprintf(stderr, "%s takes no argument\n", option->option);
        usage(stderr);
        return EXIT_TROUBLE;
    case SOF_COPT_STOPPED:
        return EXIT_TROUBLE;
    default:
        fprintf(stderr, "can't read the options (error %d)\n", (int)err);
        return EXIT_TROUBLE;
    }
    if (opt_help.present) {
        usage(stdout);
        return EXIT_VALID;
    }
    if (opt_check.present && (opt_to.present || opt_bom.present)) {
        fprintf(stderr, "-t and -b can't be used with -c\n");
        usage(stderr);
        return EXIT_TROUBLE;
    }

    sof_string_t in;
    char *buffer = sof_alloc_malloc(NULL, CHUNK_SIZE);
    if (buffer == NULL || sof_string_init(&in, 0, NULL) != SOF_STRING_OK) {
        fprintf(stderr, "out of memory\n");
        sof_alloc_free(NULL, buffer, CHUNK_SIZE);
        return EXIT_TROUBLE;
    }
    int rc = EXIT_VALID;
    if (!opt_check.present && (opt_bom.present || settings.to->bom)) {
        rc = write_code(0xfeff, settings.to, stdout);
    }
    static char *standard_input[] = {"-"};
    char **paths = index == ac ? standard_input : av + index;
    int num_paths = index == ac ? 1 : ac - index;
    for (int i = 0; i < num_paths && rc != EXIT_TROUBLE; i++) {
        int file_rc = process_file(paths[i], &settings, &in, buffer);
        if (file_rc > rc) {
            rc = file_rc;
        }
    }
    if (rc != EXIT_TROUBLE && fflush(stdout) != 0) {
        fprintf(stderr, "error writing the output: %s\n", strerror(errno));
        rc = EXIT_TROUBLE;
    }
    sof_string_free(&in);
    sof_alloc_free(NULL, buffer, CHUNK_SIZE);
    return rc;
}
