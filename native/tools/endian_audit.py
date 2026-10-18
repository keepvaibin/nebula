"""Whole-program audit: raw memory viewed as host-order numbers.

Resource and disc data are big-endian; natively a u16, u32, f32 (...) read through a pointer cast from void* or bytes
gets the bytes swapped. Every cast (explicit, or C's implicit one from void*) from a void or byte pointer to a pointer
to a multi-byte number, and every NEBULA_BE-free read of such a pointer, is printed for review: each must be host data
or read through NEBULA_BE.

    python endian_audit.py <compile_commands.json> <out.txt> [jobs]     (pip install clang; LIBCLANG=path/to/libclang)
"""
import json
import multiprocessing as mp
import sys

import clang.cindex as ci

from escape_audit import TREE, args_of, in_tree, where

K = ci.CursorKind
T = ci.TypeKind
FUNCS = {K.FUNCTION_DECL, K.CXX_METHOD, K.CONSTRUCTOR, K.DESTRUCTOR, K.CONVERSION_FUNCTION, K.FUNCTION_TEMPLATE}
CASTS = {K.CSTYLE_CAST_EXPR, K.CXX_STATIC_CAST_EXPR, K.CXX_REINTERPRET_CAST_EXPR}
WIDE = {T.SHORT, T.USHORT, T.INT, T.UINT, T.LONG, T.ULONG, T.LONGLONG, T.ULONGLONG, T.FLOAT, T.DOUBLE, T.WCHAR,
        T.CHAR16, T.CHAR32}
RAW = {T.VOID, T.CHAR_S, T.CHAR_U, T.UCHAR, T.SCHAR}


def pointee_kind(t):
    t = t.get_canonical()
    if t.kind != T.POINTER:
        return None
    p = t.get_pointee().get_canonical()
    while p.kind == T.CONSTANTARRAY:
        p = p.element_type.get_canonical()
    return p.kind


def worker(entries):
    index = ci.Index.create()
    out = set()
    for e in entries:
        try:
            tu = index.parse(e["file"], args=args_of(e, None), options=0)
        except ci.TranslationUnitLoadError:
            continue

        def walk(c, fn):
            if c.kind in CASTS or (c.kind == K.UNEXPOSED_EXPR and e["file"].endswith(".c")):
                dst = pointee_kind(c.type)
                inner = [x for x in c.get_children() if x.kind.is_expression()]
                if dst in WIDE and inner:
                    src = pointee_kind(inner[-1].type)
                    if src in RAW:
                        toks = " ".join(t.spelling for t in c.get_tokens())
                        out.add((where(c), fn, c.type.spelling, toks[:140]))
            for x in c.get_children():
                walk(x, fn)

        def scan(c):
            for x in c.get_children():
                if x.kind in FUNCS and x.is_definition() and in_tree(x):
                    walk(x, x.spelling)
                elif x.kind in (K.NAMESPACE, K.CLASS_DECL, K.STRUCT_DECL, K.CLASS_TEMPLATE, K.LINKAGE_SPEC, K.UNEXPOSED_DECL):
                    scan(x)

        scan(tu.cursor)
    return out


def main():
    cc = [e for e in json.load(open(sys.argv[1])) if e["file"].replace("\\", "/").lower().startswith(TREE)]
    jobs = int(sys.argv[3]) if len(sys.argv) > 3 else 12
    found = set()
    with mp.Pool(jobs) as pool:
        for res in pool.imap_unordered(worker, [cc[i::jobs * 4] for i in range(jobs * 4)]):
            found |= res
    with open(sys.argv[2], "w", encoding="utf-8") as o:
        for site, fn, typ, toks in sorted(found):
            o.write("%s  in %s: %s  %s\n" % (site, fn, typ, toks))
    print("sites: %d" % len(found), file=sys.stderr)


if __name__ == "__main__":
    main()
