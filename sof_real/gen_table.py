#!/usr/bin/env python3
#
# gen_table.py
#
# Writes sof_real_internal.h, the table of powers of ten that sof_real.c uses
# to format and parse numbers, to standard output:
#
#     python3 gen_table.py > sof_real_internal.h
#
# For each q from POWER_MIN to POWER_MAX, the entry is 10^q scaled by a
# power of two into [2^127, 2^128) and rounded down: the 128 most
# significant bits of 10^q, truncated. It is exact for q from 0 to 55, where
# 5^q fits in 128 bits, and a little low for every other q. The arithmetic
# is Python's exact integers throughout.
#

POWER_MIN = -342
POWER_MAX = 324


def entry(q):
    """The 128 leading bits of 10^q, rounded down."""
    if q >= 0:
        v = 10 ** q
        b = v.bit_length()
        return v << (128 - b) if b <= 128 else v >> (b - 128)
    v = 10 ** -q
    # 2^(b-1) <= v < 2^b, so 2^(127 + b) / v is in (2^127, 2^128), and never
    # 2^128, since v isn't a power of two.
    return (1 << (127 + v.bit_length())) // v


def main():
    print("""/**
 * @file sof_real_internal.h
 * @author Warren Mann (warren@nonvol.io)
 * @brief The powers of ten sof_real.c formats and parses with.
 * @details Written by gen_table.py, with exact integer arithmetic; don't
 * edit it by hand, but run
 *
 *     python3 gen_table.py > sof_real_internal.h
 *
 * Entry q - SOF_REAL_POWER_MIN holds 10^q, for q from SOF_REAL_POWER_MIN to
 * SOF_REAL_POWER_MAX, scaled by a power of two into [2^127, 2^128) and
 * rounded down: the 128 most significant bits of 10^q, high 64 bits first.
 * The entries for q from 0 to 55 are exact; the others are below 10^q by
 * less than one in their last place. The power of two is
 * 2^(floor(log2(10^q)) - 127). Only sof_real.c includes this header, and
 * the tests, which check it; it isn't installed with the public ones.
 * @version 0.0.1
 * @date 2026-11-20
 * @copyright Copyright (c) 2026
 */

#ifndef SOF_REAL_INTERNAL_H
#define SOF_REAL_INTERNAL_H

#include <stdint.h>
""")
    print("/// @brief The smallest power of ten in the table.")
    print(f"#define SOF_REAL_POWER_MIN ({POWER_MIN})")
    print()
    print("/// @brief The largest power of ten in the table.")
    print(f"#define SOF_REAL_POWER_MAX {POWER_MAX}")
    print()
    print("/// @brief The 128 leading bits of each power of ten, high half first.")
    print("static const uint64_t sof_real_powers[SOF_REAL_POWER_MAX - SOF_REAL_POWER_MIN + 1][2] = {")
    for q in range(POWER_MIN, POWER_MAX + 1):
        t = entry(q)
        assert (1 << 127) <= t < (1 << 128)
        print(f"    {{0x{t >> 64:016x}ULL, 0x{t & ((1 << 64) - 1):016x}ULL}}, // 1e{q}")
    print("};")
    print()
    print("#endif // SOF_REAL_INTERNAL_H")


if __name__ == "__main__":
    main()
