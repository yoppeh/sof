/**
 * @file sof_base.h
 * @author Warren Mann (warren@nonvol.io)
 * @brief Base16, base32 and base64 encoding and decoding in C.
 * @details The encodings of RFC 4648: base16 (hex), base32 and base32hex,
 * and base64 and base64url, chosen with a sof_base_encoding_t.
 * sof_base_encode() and sof_base_decode() convert memory to text and back,
 * into the caller's buffer, which sof_base_encoded_size() and
 * sof_base_decoded_size() say how large to make. A sof_base_encoder_t or a
 * sof_base_decoder_t does the same for data that comes in pieces, such as a
 * file read a buffer at a time, giving the same result however it is split.
 *
 * The sof_base_flag_t flags choose the details: upper or lower case,
 * padding or none, and whether decoding is strict, accepting only the text
 * encoding gives (the default), or lenient, and whether it skips
 * whitespace. The same flags work for both directions, so text encoded with
 * some flags always decodes with them.
 *
 * Nothing is allocated. The library doesn't append to a sof_string_t
 * itself, which would make every user of it build sof_string and sof_alloc
 * for two lines: sof_string_reserve() of the size these functions give, an
 * encode or decode into it, and sof_string_commit() of what was written.
 * @version 0.0.1
 * @date 2026-11-20
 * @copyright Copyright (c) 2026
 */

#ifndef SOF_BASE_H
#define SOF_BASE_H

#include <stddef.h>
#include <stdint.h>

/// @brief The most characters a sof_base_decoder_t holds between updates,
/// one fewer than a base32 group; a sof_base_encoder_t holds fewer bytes.
#define SOF_BASE_PENDING_MAX 7

/**
 * @brief The library's error codes.
 */
typedef enum sof_base_error_t {
    /// @brief No error.
    SOF_BASE_OK = 0,
    /// @brief Not returned: the library allocates nothing.
    SOF_BASE_NO_MEM,
    /// @brief Not returned: the library looks nothing up.
    SOF_BASE_NO_MATCH,
    /// @brief A NULL or dead stream, an encoding that isn't a
    /// sof_base_encoding_t, or a flag that isn't a sof_base_flag_t.
    SOF_BASE_ERROR,
    /// @brief The text isn't a valid encoding, under the flags given.
    SOF_BASE_BAD_INPUT,
    /// @brief The buffer for the result is too small.
    SOF_BASE_TOO_SMALL,
} sof_base_error_t;

/**
 * @brief The encodings, as RFC 4648 defines them.
 * @details Each turns a group of bytes into a group of characters:
 * base16 one byte into 2, base32 and base32hex 5 bytes into 8, and base64
 * and base64url 3 bytes into 4. A last group of fewer bytes gives fewer
 * characters, padded to a whole group with '=' in base32, base32hex,
 * base64 and base64url unless SOF_BASE_NO_PAD is given.
 */
typedef enum sof_base_encoding_t {
    /// @brief Base16, or hex: 0-9 and A-F, or a-f with SOF_BASE_LOWER.
    SOF_BASE_16 = 1,
    /// @brief Base32: A-Z and 2-7.
    SOF_BASE_32,
    /// @brief Base32 with the extended hex alphabet, 0-9 and A-V, which
    /// sorts as the bytes do.
    SOF_BASE_32HEX,
    /// @brief Base64: A-Z, a-z, 0-9, + and /.
    SOF_BASE_64,
    /// @brief Base64 with the URL and filename safe alphabet: A-Z, a-z,
    /// 0-9, - and _.
    SOF_BASE_64URL,
} sof_base_encoding_t;

/**
 * @brief The flags that choose how text is written and read, or'ed
 * together; 0 for none.
 * @details Each direction uses those that concern it and ignores the rest,
 * so one set of flags can be given to both. With none, encoding writes the
 * RFC's alphabet in upper case, with padding, and decoding is strict: the
 * text must be exactly what encoding with the same flags writes, except
 * that letters of base16, base32 and base32hex may be in either case. That
 * is, it must have padding (or none, with SOF_BASE_NO_PAD), no whitespace,
 * no character after the padding, and 0 in the bits of its last character
 * that don't make a whole byte, so each byte string has one encoding.
 */
typedef enum sof_base_flag_t {
    /// @brief Encode base16, base32 and base32hex in lower case. Ignored by
    /// base64, and by decoding, which takes either case.
    SOF_BASE_LOWER = 0x1,
    /// @brief Encode without padding, and when decoding strictly, refuse
    /// it. Ignored by base16, which has none.
    SOF_BASE_NO_PAD = 0x2,
    /// @brief Decode leniently: padding may be missing, or any part of it
    /// given, and the bits of the last character that don't make a whole
    /// byte are ignored. Ignored by encoding.
    SOF_BASE_LENIENT = 0x4,
    /// @brief Decode skipping ASCII whitespace (space, tab, newline,
    /// vertical tab, form feed and carriage return) wherever it is, as in
    /// the line breaks of PEM and MIME. Ignored by encoding.
    SOF_BASE_SKIP_SPACE = 0x8,
} sof_base_flag_t;

/**
 * @brief The state of a decoding of text that comes in pieces.
 * @details The application provides it (on the stack or anywhere else) and
 * sets it up with sof_base_decoder_init(), gives it the text with
 * sof_base_decoder_update(), as many times as there are pieces, and ends
 * it with sof_base_decoder_final(). It allocates nothing, so there is
 * nothing to free. The fields can be read but must not be changed. It holds
 * no pointers, so it can be copied.
 */
typedef struct sof_base_decoder_t {
    /// @brief The encoding, or 0 if the stream isn't set up or has failed.
    sof_base_encoding_t encoding;
    /// @brief The sof_base_flag_t flags.
    unsigned flags;
    /// @brief The characters given so far, whitespace included.
    size_t offset;
    /// @brief The data characters held, fewer than a group.
    size_t pending;
    /// @brief Their values, the first in the highest bits.
    uint64_t bits;
    /// @brief The offset of the last data character.
    size_t last;
    /// @brief The '=' given so far, once padding has started.
    size_t padding;
    /// @brief The '=' that complete the last group, once padding has
    /// started.
    size_t padding_needed;
} sof_base_decoder_t;

/**
 * @brief The state of an encoding of data that comes in pieces.
 * @details The application provides it (on the stack or anywhere else) and
 * sets it up with sof_base_encoder_init(), gives it the data with
 * sof_base_encoder_update(), as many times as there are pieces, and ends
 * it with sof_base_encoder_final(). It allocates nothing, so there is
 * nothing to free. The fields can be read but must not be changed. It holds
 * no pointers, so it can be copied.
 */
typedef struct sof_base_encoder_t {
    /// @brief The encoding, or 0 if the stream isn't set up.
    sof_base_encoding_t encoding;
    /// @brief The sof_base_flag_t flags.
    unsigned flags;
    /// @brief The bytes held in bytes, fewer than a group.
    size_t pending;
    /// @brief The start of a group, waiting for the rest of it: room for
    /// a base32 group of 5 bytes.
    unsigned char bytes[5];
} sof_base_encoder_t;

/**
 * @brief Decodes text into bytes.
 * @details The text is checked as the flags say (see sof_base_flag_t).
 * Decoding text into a buffer of sof_base_decoded_size(encoding, length)
 * bytes never fails for lack of room. On an error, data may hold part of
 * the result.
 * @param encoding The encoding.
 * @param flags sof_base_flag_t flags, or 0: strict.
 * @param text The characters. Need not end with a NUL. NULL is taken as
 * no characters, whatever length is.
 * @param length The number of characters.
 * @param data Where the bytes go. NULL is taken as a buffer of 0 bytes,
 * whatever size is, which with written measures the result.
 * @param size The bytes data has room for.
 * @param written If not NULL, set to the bytes written; on
 * SOF_BASE_TOO_SMALL, the bytes needed; and on other errors, 0.
 * @param offset If not NULL, set on SOF_BASE_BAD_INPUT to the offset of
 * the first bad character, or to length if the text ends too soon, as in
 * the middle of a group or before padding it needs. Not changed otherwise.
 * @return SOF_BASE_OK; SOF_BASE_BAD_INPUT if the text isn't valid, which
 * comes before SOF_BASE_TOO_SMALL if data hasn't room for the result; or
 * SOF_BASE_ERROR if encoding or flags are bad.
 */
extern sof_base_error_t sof_base_decode(sof_base_encoding_t encoding, unsigned flags, const char *text, size_t length, void *data, size_t size, size_t *written, size_t *offset);

/**
 * @brief Gives the most bytes length characters can decode to.
 * @details The exact number for unpadded text without whitespace, and for
 * padded text, up to the bytes of the padding.
 * @param encoding The encoding.
 * @param length The number of characters.
 * @return The number of bytes, or 0 if encoding is bad.
 */
extern size_t sof_base_decoded_size(sof_base_encoding_t encoding, size_t length);

/**
 * @brief Ends a decoding, decoding the last group.
 * @details Checks that the text ended where it may: after a whole group,
 * after padding, or with SOF_BASE_NO_PAD or SOF_BASE_LENIENT, part way
 * through a group that has enough characters to make a byte. The stream is
 * then ready for new text with the same encoding and flags, or after an
 * error, dead until set up again.
 * @param decoder The stream.
 * @param data Where the bytes go: at most 4. NULL is taken as a buffer of
 * 0 bytes, whatever size is.
 * @param size The bytes data has room for.
 * @param written If not NULL, set to the bytes written; on
 * SOF_BASE_TOO_SMALL, the bytes needed; and on other errors, 0.
 * @param offset If not NULL, set on SOF_BASE_BAD_INPUT to the offset in
 * the whole text of the bad character, or to its length if it ended too
 * soon.
 * @return SOF_BASE_OK; SOF_BASE_BAD_INPUT if the text didn't end where it
 * may; SOF_BASE_TOO_SMALL, changing nothing, if data hasn't room; or
 * SOF_BASE_ERROR if decoder is NULL or dead.
 */
extern sof_base_error_t sof_base_decoder_final(sof_base_decoder_t *decoder, void *data, size_t size, size_t *written, size_t *offset);

/**
 * @brief Starts a decoding of text that comes in pieces.
 * @param decoder The stream to set up.
 * @param encoding The encoding.
 * @param flags sof_base_flag_t flags, or 0: strict.
 * @return SOF_BASE_OK, or SOF_BASE_ERROR if decoder is NULL, or encoding or
 * flags are bad, which leave the stream dead.
 */
extern sof_base_error_t sof_base_decoder_init(sof_base_decoder_t *decoder, sof_base_encoding_t encoding, unsigned flags);

/**
 * @brief Decodes the next piece of a text.
 * @details Pieces can be of any length, 0 included, and split the text
 * anywhere; the bytes don't depend on how. Every group the piece completes
 * is decoded, and the characters of one it doesn't are held for the next
 * piece. Bad characters are reported as soon as they are given.
 * @param decoder The stream.
 * @param text The characters. Need not end with a NUL. NULL is taken as
 * no characters, whatever length is.
 * @param length The number of characters.
 * @param data Where the bytes go. It must have room for
 * sof_base_decoded_size(encoding, decoder->pending + length) bytes, even
 * where fewer are written, and so always does with
 * sof_base_decoded_size(encoding, length + SOF_BASE_PENDING_MAX). NULL is
 * taken as a buffer of 0 bytes, whatever size is.
 * @param size The bytes data has room for.
 * @param written If not NULL, set to the bytes written; on
 * SOF_BASE_TOO_SMALL, the bytes data must have room for; and on other
 * errors, 0.
 * @param offset If not NULL, set on SOF_BASE_BAD_INPUT to the offset in
 * the whole text, counting every piece, of the first bad character.
 * @return SOF_BASE_OK; SOF_BASE_BAD_INPUT if a character is bad, which
 * leaves the stream dead; SOF_BASE_TOO_SMALL, changing nothing, if size is
 * too small; or SOF_BASE_ERROR if decoder is NULL or dead.
 */
extern sof_base_error_t sof_base_decoder_update(sof_base_decoder_t *decoder, const char *text, size_t length, void *data, size_t size, size_t *written, size_t *offset);

/**
 * @brief Encodes bytes as text.
 * @details Writes sof_base_encoded_size(encoding, flags, length)
 * characters, and no NUL.
 * @param encoding The encoding.
 * @param flags sof_base_flag_t flags, or 0: upper case, padded.
 * @param data The bytes. NULL is taken as no bytes, whatever length is.
 * @param length The number of bytes.
 * @param text Where the characters go. NULL is taken as a buffer of 0
 * characters, whatever size is, which with written measures the result.
 * @param size The characters text has room for.
 * @param written If not NULL, set to the characters written; on
 * SOF_BASE_TOO_SMALL, the characters needed; and on SOF_BASE_ERROR, 0.
 * @return SOF_BASE_OK; SOF_BASE_TOO_SMALL, writing nothing, if text hasn't
 * room; or SOF_BASE_ERROR if encoding or flags are bad.
 */
extern sof_base_error_t sof_base_encode(sof_base_encoding_t encoding, unsigned flags, const void *data, size_t length, char *text, size_t size, size_t *written);

/**
 * @brief Gives the characters length bytes encode to.
 * @param encoding The encoding.
 * @param flags sof_base_flag_t flags; only SOF_BASE_NO_PAD matters.
 * @param length The number of bytes.
 * @return The number of characters, not counting a NUL; 0 if encoding is
 * bad; or SIZE_MAX if the number is too large for a size_t.
 */
extern size_t sof_base_encoded_size(sof_base_encoding_t encoding, unsigned flags, size_t length);

/**
 * @brief Ends an encoding, encoding the last bytes and any padding.
 * @details The stream is then ready for new data with the same encoding
 * and flags.
 * @param encoder The stream.
 * @param text Where the characters go: at most 8. NULL is taken as a
 * buffer of 0 characters, whatever size is.
 * @param size The characters text has room for.
 * @param written If not NULL, set to the characters written; on
 * SOF_BASE_TOO_SMALL, the characters needed; and on SOF_BASE_ERROR, 0.
 * @return SOF_BASE_OK; SOF_BASE_TOO_SMALL, changing nothing, if text
 * hasn't room; or SOF_BASE_ERROR if encoder is NULL or dead.
 */
extern sof_base_error_t sof_base_encoder_final(sof_base_encoder_t *encoder, char *text, size_t size, size_t *written);

/**
 * @brief Starts an encoding of data that comes in pieces.
 * @param encoder The stream to set up.
 * @param encoding The encoding.
 * @param flags sof_base_flag_t flags, or 0: upper case, padded.
 * @return SOF_BASE_OK, or SOF_BASE_ERROR if encoder is NULL, or encoding or
 * flags are bad, which leave the stream dead.
 */
extern sof_base_error_t sof_base_encoder_init(sof_base_encoder_t *encoder, sof_base_encoding_t encoding, unsigned flags);

/**
 * @brief Encodes the next piece of data.
 * @details Pieces can be of any length, 0 included; the text doesn't
 * depend on how the data is split. Every group the piece completes is
 * encoded, and the bytes of one it doesn't are held for the next piece.
 * @param encoder The stream.
 * @param data The bytes. NULL is taken as no bytes, whatever length is.
 * @param length The number of bytes.
 * @param text Where the characters go: at most
 * sof_base_encoded_size(encoding, flags, length + SOF_BASE_PENDING_MAX).
 * NULL is taken as a buffer of 0 characters, whatever size is.
 * @param size The characters text has room for.
 * @param written If not NULL, set to the characters written; on
 * SOF_BASE_TOO_SMALL, the characters needed; and on SOF_BASE_ERROR, 0.
 * @return SOF_BASE_OK; SOF_BASE_TOO_SMALL, changing nothing, if text
 * hasn't room; or SOF_BASE_ERROR if encoder is NULL or dead.
 */
extern sof_base_error_t sof_base_encoder_update(sof_base_encoder_t *encoder, const void *data, size_t length, char *text, size_t size, size_t *written);

#endif // SOF_BASE_H
