"""gen_ctype_tables.py <out header> [<RMGE01 asm dir>]: MSL's "C" locale tables from the C locale's definition
in MSL's bit layout (alpha 0x1, blank 0x2, cntrl 0x4, digit 0x8, graph 0x10, lower 0x20, print 0x40, punct 0x80,
space 0x100, upper 0x200, xdigit 0x400). With the split RMGE01 assembly, checks every byte against the DOL's tables
(the decompiled MSL_C/ctype.c differs from the DOL from 'z' on)."""
import re
import sys


def classes(c):
    bits = 0
    if c < 0x80:
        ch = chr(c)
        if ch.isalpha():
            bits |= 0x1
        if ch in " \t":
            bits |= 0x2
        if c < 0x20 or c == 0x7F:
            bits |= 0x4
        if ch.isdigit():
            bits |= 0x8
        if 0x21 <= c <= 0x7E:
            bits |= 0x10
        if ch.islower():
            bits |= 0x20
        if 0x20 <= c <= 0x7E:
            bits |= 0x40
        if 0x21 <= c <= 0x7E and not ch.isalnum():
            bits |= 0x80
        if ch in " \t\n\v\f\r":
            bits |= 0x100
        if ch.isupper():
            bits |= 0x200
        if ch in "0123456789abcdefABCDEF":
            bits |= 0x400
    return bits


ctype = [classes(c) for c in range(256)]
lower = [c + 32 if 65 <= c <= 90 else c for c in range(256)]
upper = [c - 32 if 97 <= c <= 122 else c for c in range(256)]

if len(sys.argv) > 2:
    asm = open(sys.argv[2] + "/MSL_C/locale.s", encoding="utf-8").read()
    comp = re.search(r"\.obj _loc_ctyp_C.*?\.4byte 0x0+\s+\.4byte (\w+)\s+\.4byte (\w+)\s+\.4byte (\w+)", asm, re.S)
    names = {"ctype": comp.group(1), "upper": comp.group(2), "lower": comp.group(3)}

    def blob(label):
        m = re.search(r"\.obj " + label + r", \w+\n(.*?)\.endobj", asm, re.S)
        words = [int(w, 16) for w in re.findall(r"\.4byte 0x([0-9A-Fa-f]+)", m.group(1))]
        return b"".join(w.to_bytes(4, "big") for w in words)

    dol_ctype = blob(names["ctype"])
    assert [int.from_bytes(dol_ctype[i:i + 2], "big") for i in range(0, 512, 2)] == ctype, "class map differs"
    assert list(blob(names["upper"])[:256]) == upper, "upper map differs"
    assert list(blob(names["lower"])[:256]) == lower, "lower map differs"
    print("matches the RMGE01 tables")


def rows(a, fmt):
    return "\n".join("    " + ", ".join(fmt % v for v in a[i:i + 16]) + "," for i in range(0, 256, 16))


open(sys.argv[1], "w", encoding="utf-8", newline="\n").write(f"""/* MSL's "C" locale tables: class bits per byte, upper- and lower-case maps (tools/gen_ctype_tables.py). */
#ifndef NEBULA_MSL_CTYPE_TABLES_H
#define NEBULA_MSL_CTYPE_TABLES_H
static const unsigned short nebula_msl_ctype_map[256] = {{
{rows(ctype, "0x%03X")}
}};
static const unsigned char nebula_msl_upper_map[256] = {{
{rows(upper, "0x%02X")}
}};
static const unsigned char nebula_msl_lower_map[256] = {{
{rows(lower, "0x%02X")}
}};
#endif
""")
