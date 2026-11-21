/**
 * @file main.c
 * @author Warren Mann (warren@nonvol.io)
 * @brief Encodes or decodes files in base16, base32 or base64, as basenc
 * does.
 * @details Encodes a file, or standard input, in one of the encodings of
 * RFC 4648, writing the text in lines of 76 characters, or with -d decodes
 * it back. Reads and writes a chunk at a time, through a sof_base_encoder_t
 * or a sof_base_decoder_t, so input of any size takes the same memory.
 *
 *     basenc [-h] [-d] [-w cols] [--base16 | --base32 | --base32hex |
 *            --base64 | --base64url] [--lower] [--no-pad] [--lenient]
 *            [--strict] [file]
 *
 * Decoding skips whitespace, such as the line breaks encoding writes,
 * unless --strict is given, and otherwise accepts only what encoding with
 * the same options writes, unless --lenient is given, which lets padding be
 * missing. Its options are read with sof_copt. It exits with 0, 1 if the
 * input couldn't be read or isn't valid, and 2 if the options are wrong.
 * @version 0.0.1
 * @date 2026-11-20
 * @copyright Copyright (c) 2026
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sof_base.h"
#include "sof_copt.h"

/// @brief The size of the chunks input is read in.
#define CHUNK_SIZE 65536

/// @brief The length of an encoded line, unless -w is given: MIME's.
#define WRAP 76

/// @brief Room for what encoding or decoding a chunk can give: base16
/// doubles it, and a stream holds a few bytes over.
#define OUT_SIZE (2 * (CHUNK_SIZE + SOF_BASE_PENDING_MAX) + 8)

static int decode(FILE *in, sof_base_encoding_t encoding, unsigned flags);
static int encode(FILE *in, sof_base_encoding_t encoding, unsigned flags, size_t wrap);
static int read_failed(FILE *in);
static int write_text(const char *text, size_t length, size_t wrap, size_t *column);
static void usage(FILE *stream);

static sof_copt_t opt_help = {
    .option = "-h",
    .description = "print this help and exit",
};

static sof_copt_t opt_decode = {
    .option = "-d",
    .description = "decode instead of encoding",
};

static sof_copt_t opt_wrap = {
    .option = "-w",
    .description = "break encoded lines after cols characters, 0 for none (default 76)",
    .type = SOF_COPT_ARG_REQUIRED,
};

static sof_copt_t opt_base16 = {
    .option = "--base16",
    .description = "hex: 0-9 and A-F",
};

static sof_copt_t opt_base32 = {
    .option = "--base32",
    .description = "base32: A-Z and 2-7",
};

static sof_copt_t opt_base32hex = {
    .option = "--base32hex",
    .description = "base32 with the extended hex alphabet, 0-9 and A-V",
};

static sof_copt_t opt_base64 = {
    .option = "--base64",
    .description = "base64: A-Z, a-z, 0-9, + and / (the default)",
};

static sof_copt_t opt_base64url = {
    .option = "--base64url",
    .description = "base64 with - and _ for + and /, safe in URLs and file names",
};

static sof_copt_t opt_lower = {
    .option = "--lower",
    .description = "encode base16 and base32 in lower case",
};

static sof_copt_t opt_no_pad = {
    .option = "--no-pad",
    .description = "encode without '=' padding; decode refusing it",
};

static sof_copt_t opt_lenient = {
    .option = "--lenient",
    .description = "decode text whose padding is missing or whose last bits aren't 0",
};

static sof_copt_t opt_strict = {
    .option = "--strict",
    .description = "decode refusing whitespace, line breaks included",
};

SOF_COPT_OPTIONS static sof_copt_t *options[] = {
    &opt_help,
    &opt_decode,
    &opt_wrap,
    &opt_base16,
    &opt_base32,
    &opt_base32hex,
    &opt_base64,
    &opt_base64url,
    &opt_lower,
    &opt_no_pad,
    &opt_lenient,
    &opt_strict,
};

/// @brief Decodes the text in, a chunk at a time, to standard output.
/// @return 0, or 1 if it couldn't be read or isn't valid, after saying
/// why.
static int decode(FILE *in, sof_base_encoding_t encoding, unsigned flags) {
    static char chunk[CHUNK_SIZE];
    static unsigned char out[OUT_SIZE];
    sof_base_decoder_t decoder;
    sof_base_decoder_init(&decoder, encoding, flags);
    size_t got, written, offset = 0;
    sof_base_error_t err = SOF_BASE_OK;
    while (err == SOF_BASE_OK && (got = fread(chunk, 1, sizeof(chunk), in)) > 0) {
        err = sof_base_decoder_update(&decoder, chunk, got, out, sizeof(out), &written, &offset);
        if (err == SOF_BASE_OK) {
            fwrite(out, 1, written, stdout);
        }
    }
    if (read_failed(in)) {
        return 1;
    }
    if (err == SOF_BASE_OK) {
        err = sof_base_decoder_final(&decoder, out, sizeof(out), &written, &offset);
        if (err == SOF_BASE_OK) {
            fwrite(out, 1, written, stdout);
        }
    }
    if (err != SOF_BASE_OK) {
        fprintf(stderr, "basenc: invalid input at character %zu\n", offset);
        return 1;
    }
    return 0;
}

/// @brief Encodes the bytes in, a chunk at a time, to standard output, in
/// lines of wrap characters, or one line if wrap is 0.
/// @return 0, or 1 if it couldn't be read or the output written, after
/// saying why.
static int encode(FILE *in, sof_base_encoding_t encoding, unsigned flags, size_t wrap) {
    static unsigned char chunk[CHUNK_SIZE];
    static char out[OUT_SIZE];
    sof_base_encoder_t encoder;
    sof_base_encoder_init(&encoder, encoding, flags);
    size_t got, written, column = 0;
    while ((got = fread(chunk, 1, sizeof(chunk), in)) > 0) {
        sof_base_encoder_update(&encoder, chunk, got, out, sizeof(out), &written);
        if (write_text(out, written, wrap, &column) != 0) {
            return 1;
        }
    }
    if (read_failed(in)) {
        return 1;
    }
    sof_base_encoder_final(&encoder, out, sizeof(out), &written);
    if (write_text(out, written, wrap, &column) != 0) {
        return 1;
    }
    if (wrap != 0 && column != 0) {
        putchar('\n');
    }
    return 0;
}

/// @brief Says if reading in failed, and why.
/// @return 1 if it failed, or 0.
static int read_failed(FILE *in) {
    if (ferror(in)) {
        fprintf(stderr, "basenc: error reading input: %s\n", strerror(errno));
        return 1;
    }
    return 0;
}

/// @brief Writes length characters of text, breaking the line after every
/// wrap characters, or never if wrap is 0; *column is the characters on the
/// current line so far.
/// @return 0, or 1 if writing failed, after saying why.
static int write_text(const char *text, size_t length, size_t wrap, size_t *column) {
    while (length > 0) {
        size_t take = length;
        if (wrap != 0 && take > wrap - *column) {
            take = wrap - *column;
        }
        fwrite(text, 1, take, stdout);
        text += take;
        length -= take;
        *column += take;
        if (wrap != 0 && *column == wrap) {
            putchar('\n');
            *column = 0;
        }
    }
    if (ferror(stdout)) {
        fprintf(stderr, "basenc: error writing output: %s\n", strerror(errno));
        return 1;
    }
    return 0;
}

/// @brief Prints the usage message, with the options sof_copt knows.
static void usage(FILE *stream) {
    fprintf(stream, "usage: basenc [-h] [-d] [-w cols] [--base16 | --base32 | --base32hex | --base64 |\n"
                    "              --base64url] [--lower] [--no-pad] [--lenient] [--strict] [file]\n");
    sof_copt_print(stream);
    fprintf(stream,
        "Encodes a file, or standard input if there is none or it is \"-\", in base64\n"
        "or another encoding of RFC 4648, or with -d decodes it. Decoding skips\n"
        "whitespace unless --strict is given, and otherwise accepts only what\n"
        "encoding with the same options writes, unless --lenient is given. Exits\n"
        "with 0, 1 if the input can't be read or isn't valid, 2 on bad options.\n");
}

int main(int ac, char **av) {
    int index;
    sof_copt_t *option;
    sof_copt_error_t err = sof_copt_parse(ac, av, NULL, &index, &option);
    switch (err) {
    case SOF_COPT_OK:
        break;
    case SOF_COPT_NO_MATCH:
        fprintf(stderr, "basenc: unknown option %s\n", av[index]);
        usage(stderr);
        return 2;
    case SOF_COPT_UNEXPECTED_ARGUMENT:
        fprintf(stderr, "basenc: %s takes no argument\n", option->option);
        usage(stderr);
        return 2;
    case SOF_COPT_MISSING_ARGUMENT:
        fprintf(stderr, "basenc: %s needs an argument\n", option->option);
        usage(stderr);
        return 2;
    default:
        fprintf(stderr, "basenc: can't read the options (error %d)\n", (int)err);
        return 2;
    }
    if (opt_help.present) {
        usage(stdout);
        return 0;
    }

    static const struct {
        sof_copt_t *option;
        sof_base_encoding_t encoding;
    } choices[] = {
        {&opt_base16, SOF_BASE_16},
        {&opt_base32, SOF_BASE_32},
        {&opt_base32hex, SOF_BASE_32HEX},
        {&opt_base64, SOF_BASE_64},
        {&opt_base64url, SOF_BASE_64URL},
    };
    sof_base_encoding_t encoding = SOF_BASE_64;
    int chosen = 0;
    for (size_t i = 0; i < sizeof(choices) / sizeof(choices[0]); i++) {
        if (choices[i].option->present) {
            encoding = choices[i].encoding;
            chosen++;
        }
    }
    const char *wrong = NULL;
    if (chosen > 1) {
        wrong = "choose one encoding";
    } else if (opt_decode.present && (opt_wrap.present || opt_lower.present)) {
        wrong = "-w and --lower are for encoding";
    } else if (!opt_decode.present && (opt_lenient.present || opt_strict.present)) {
        wrong = "--lenient and --strict are for decoding (-d)";
    } else if (ac - index > 1) {
        wrong = "one file at most";
    }
    size_t wrap = WRAP;
    if (wrong == NULL && opt_wrap.present) {
        const char *s = opt_wrap.value;
        char *end;
        errno = 0;
        unsigned long value = strtoul(s, &end, 10);
        if (*s < '0' || *s > '9' || *end != '\0' || errno != 0) {
            wrong = "-w needs a number of characters";
        }
        wrap = (size_t)value;
    }
    if (wrong != NULL) {
        fprintf(stderr, "basenc: %s\n", wrong);
        usage(stderr);
        return 2;
    }

    const char *path = index < ac ? av[index] : "-";
    int is_stdin = strcmp(path, "-") == 0;
    FILE *in = is_stdin ? stdin : fopen(path, "rb");
    if (in == NULL) {
        fprintf(stderr, "basenc: error opening %s: %s\n", path, strerror(errno));
        return 1;
    }
    unsigned flags = opt_no_pad.present ? SOF_BASE_NO_PAD : 0;
    int rc;
    if (opt_decode.present) {
        flags |= (opt_lenient.present ? SOF_BASE_LENIENT : 0) | (opt_strict.present ? 0 : SOF_BASE_SKIP_SPACE);
        rc = decode(in, encoding, flags);
    } else {
        flags |= opt_lower.present ? SOF_BASE_LOWER : 0;
        rc = encode(in, encoding, flags, wrap);
    }
    if (!is_stdin) {
        fclose(in);
    }
    if (fflush(stdout) != 0 && rc == 0) {
        fprintf(stderr, "basenc: error writing output: %s\n", strerror(errno));
        rc = 1;
    }
    return rc;
}
