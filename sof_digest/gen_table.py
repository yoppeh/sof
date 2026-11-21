#!/usr/bin/env python3
#
# gen_table.py
#
# Writes sof_digest_internal.h, the tables sof_digest.c computes CRC-32 and
# CRC-32C with, to standard output:
#
#     python3 gen_table.py > sof_digest_internal.h
#
# For each polynomial, in the reflected form both CRCs use:
# - the tables of the portable code, which takes SLICES bytes at a step:
#   table[0][b] is the CRC register after the byte b goes through a register
#   of 0, and table[k][b] the same followed by k zero bytes;
# - x^(8 * 2^k) mod P for k from 0 to 63, which shift a CRC past 2^k zero
#   bytes, for combining CRCs;
# - the constants of the carry-less multiplication code on arm64 and
#   x86-64, for folding 128, 64 and 16 bytes at a time, then 64 bits, and
#   Barrett's reduction, as Intel's "Fast CRC Computation for Generic
#   Polynomials Using PCLMULQDQ Instruction" (2009) defines them.
# The arithmetic is Python's exact integers throughout.
#

SLICES = 16

POLYNOMIALS = [
    # name, polynomial with its x^32 term, what it is
    ("crc32", 0x104C11DB7, "CRC-32 (IEEE 802.3, zip, gzip, PNG)"),
    ("crc32c", 0x11EDC6F41, "CRC-32C (Castagnoli: iSCSI, ext4, SSE4.2)"),
]


def reflect(v, bits):
    """v with its low bits bits in the opposite order."""
    return int(format(v, f"0{bits}b")[::-1], 2)


def pmulmod(a, b, p):
    """a * b mod p, not reflected."""
    r = 0
    while b:
        if b & 1:
            r ^= a
        b >>= 1
        a <<= 1
        if a >> 32 & 1:
            a ^= p
    return r


def xmod(e, p):
    """x^e mod p, not reflected, by squaring and multiplying."""
    r, s = 1, 2
    while e:
        if e & 1:
            r = pmulmod(r, s, p)
        s = pmulmod(s, s, p)
        e >>= 1
    return r


def pdiv(a, b):
    """The quotient of the polynomial a by b."""
    q = 0
    while a.bit_length() >= b.bit_length():
        s = a.bit_length() - b.bit_length()
        q |= 1 << s
        a ^= b << s
    return q


def byte_table(p):
    """The CRC register after each byte value goes through a register of 0."""
    r = reflect(p & 0xFFFFFFFF, 32)
    table = []
    for b in range(256):
        c = b
        for _ in range(8):
            c = (c >> 1) ^ r if c & 1 else c >> 1
        table.append(c)
    return table


def slice_tables(p):
    tables = [byte_table(p)]
    for _ in range(1, SLICES):
        prev = tables[-1]
        tables.append([(c >> 8) ^ tables[0][c & 0xFF] for c in prev])
    return tables


def x8n(p):
    # x^(8 * 2^k), reflected, in the 32-bit form multmodp() takes.
    return [reflect(xmod(8 << k, p), 32) for k in range(64)]


def fold(p):
    # The reflected 33-bit constants: each x^e mod P reflected and shifted
    # left one bit, for the 64, 16 and 8 byte folds, then P itself and mu,
    # x^64 / P, reflected, for Barrett's reduction, then the pair for the
    # 128-byte fold.
    k = [reflect(xmod(e, p), 32) << 1 for e in (4 * 128 + 32, 4 * 128 - 32, 128 + 32, 128 - 32, 64)]
    mu = reflect(pdiv(1 << 64, p), 33)
    k8 = [reflect(xmod(e, p), 32) << 1 for e in (8 * 128 + 32, 8 * 128 - 32)]
    return k + [0, reflect(p, 33), mu] + k8


def words(values, fmt, per_line, indent):
    lines = []
    for i in range(0, len(values), per_line):
        lines.append(indent + ", ".join(fmt.format(v) for v in values[i:i + per_line]) + ",")
    return "\n".join(lines)


def main():
    print(f"""/**
 * @file sof_digest_internal.h
 * @author Warren Mann (warren@nonvol.io)
 * @brief The tables sof_digest.c computes CRC-32 and CRC-32C with.
 * @details Written by gen_table.py, with exact integer arithmetic; don't
 * edit it by hand, but run
 *
 *     python3 gen_table.py > sof_digest_internal.h
 *
 * For each CRC, in the reflected form both use: the {SLICES} tables of the
 * portable code, where table[0][b] is the CRC register after the byte b goes
 * through a register of 0, and table[k][b] the same followed by k zero
 * bytes; x^(8 * 2^k) mod P for k from 0 to 63, which shift a register past
 * 2^k zero bytes; and the constants of the carry-less multiplication code
 * on arm64 and x86-64. sof_digest.c defines SOF_DIGEST_CRC_TABLES and
 * SOF_DIGEST_CRC_FOLD for those its build uses, which leaves out the rest.
 * Only sof_digest.c includes this header; it isn't installed with the
 * public ones.
 * @version 0.0.1
 * @date 2026-11-20
 * @copyright Copyright (c) 2026
 */

#ifndef SOF_DIGEST_INTERNAL_H
#define SOF_DIGEST_INTERNAL_H

#include <stdint.h>

/// @brief The bytes the portable CRC code takes at each step, one table to
/// each.
#define SOF_DIGEST_CRC_SLICES {SLICES}
""")
    for name, p, what in POLYNOMIALS:
        print(f"/// @brief {what}: the polynomial 0x{p & 0xFFFFFFFF:08X}, reflected.")
        print(f"#define SOF_DIGEST_{name.upper()}_POLY 0x{reflect(p & 0xFFFFFFFF, 32):08x}u")
        print()
        print("#if defined(SOF_DIGEST_CRC_TABLES)")
        print()
        print(f"/// @brief {what}: the slicing tables.")
        print(f"static const uint32_t sof_digest_{name}_table[SOF_DIGEST_CRC_SLICES][256] = {{")
        for t in slice_tables(p):
            print("    {")
            print(words(t, "0x{:08x}", 8, "        "))
            print("    },")
        print("};")
        print()
        print("#endif")
        print()
        print(f"/// @brief {what}: x^(8 * 2^k) mod P, reflected, for k from 0 to 63.")
        print(f"static const uint32_t sof_digest_{name}_x8n[64] = {{")
        print(words(x8n(p), "0x{:08x}", 8, "    "))
        print("};")
        print()
        print("#if defined(SOF_DIGEST_CRC_FOLD)")
        print()
        print(f"/// @brief {what}: the folding constants: the pairs for folding 64 and")
        print("/// 16 bytes, the one for 8, 0, P and mu for Barrett's reduction, and the")
        print("/// pair for folding 128 bytes.")
        print(f"static const uint64_t sof_digest_{name}_fold[10] = {{")
        print(words(fold(p), "0x{:09x}ULL", 4, "    "))
        print("};")
        print()
        print("#endif")
        print()
    print("#endif // SOF_DIGEST_INTERNAL_H")


if __name__ == "__main__":
    main()
