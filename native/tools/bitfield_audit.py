"""Whole-program audit: bitfields whose layout is observed.

CodeWarrior allocates bitfields from the most significant bit of their unit; clang on a little-endian host from the
least significant. Code that names one field at a time cannot tell, but code that also sees the unit (a cast of the
struct to a word, a union of the struct with a word or bytes, a struct laid over file or hardware data) reads the
fields at other bits natively. Printed for review:
  CAST   a pointer to a bitfield struct cast to or from another pointee type
  UNION  a union holding a bitfield struct (or bitfields) beside other members
  COPY   a byte copy or compare to or from a struct that holds bitfields
Each must be one-field-at-a-time host data, or carry the console's layout.

    python bitfield_audit.py <compile_commands.json> <out.txt> [jobs]     (pip install clang; LIBCLANG=path/to/libclang)
"""
import json
import multiprocessing as mp
import sys

import clang.cindex as ci

from escape_audit import TREE, args_of, expr_kids, in_tree, where

K = ci.CursorKind
T = ci.TypeKind
CASTS = {K.CSTYLE_CAST_EXPR, K.CXX_STATIC_CAST_EXPR, K.CXX_REINTERPRET_CAST_EXPR}
RECORDS = {K.STRUCT_DECL, K.CLASS_DECL, K.UNION_DECL, K.CLASS_TEMPLATE}
COPIES = {"memcpy", "memmove", "memcmp", "__builtin_memcpy", "__builtin_memmove"}


def has_bitfields(decl, depth=0):
    """A record with a bitfield of its own or in a member record (named, anonymous or an array element)."""
    if decl is None or depth > 6:
        return False
    for f in decl.get_children():
        if f.kind == K.FIELD_DECL:
            if f.is_bitfield():
                return True
            t = f.type.get_canonical()
            while t.kind in (T.CONSTANTARRAY, T.INCOMPLETEARRAY):
                t = t.element_type.get_canonical()
            if t.kind == T.RECORD and has_bitfields(t.get_declaration(), depth + 1):
                return True
        if f.kind in RECORDS and f.is_anonymous() and has_bitfields(f, depth + 1):
            return True
    return False


def pointee_record(t):
    t = t.get_canonical()
    if t.kind not in (T.POINTER, T.LVALUEREFERENCE):
        return None
    p = t.get_pointee().get_canonical()
    while p.kind == T.CONSTANTARRAY:
        p = p.element_type.get_canonical()
    return p


def bitfield_record(t):
    p = pointee_record(t) if t.get_canonical().kind in (T.POINTER, T.LVALUEREFERENCE) else t.get_canonical()
    if p is None or p.kind != T.RECORD:
        return None
    d = p.get_declaration()
    return p.spelling if has_bitfields(d) else None


def worker(entries):
    index = ci.Index.create()
    out = set()
    for e in entries:
        try:
            tu = index.parse(e["file"], args=args_of(e, None), options=0)
        except ci.TranslationUnitLoadError:
            continue

        def walk(c):
            if not in_tree(c):
                # headers outside the tree, but keep descending into the unit's own declarations
                if c.kind != K.TRANSLATION_UNIT:
                    return
            if c.kind in CASTS or c.kind == K.UNEXPOSED_EXPR and e["file"].endswith(".c"):
                inner = expr_kids(c)
                if inner and c.type.get_canonical().kind == T.POINTER:
                    src_t = inner[-1].type
                    dst = bitfield_record(c.type)
                    src = bitfield_record(src_t) if src_t.get_canonical().kind == T.POINTER else None
                    if (dst or src) and dst != src:
                        toks = " ".join(t.spelling for t in c.get_tokens())[:120]
                        out.add(("CAST", where(c), "%s <- %s" % (c.type.spelling, src_t.spelling), toks))
            if c.kind == K.CALL_EXPR and c.spelling in COPIES:
                for arg in list(c.get_arguments())[:2]:
                    inner = arg
                    while inner.kind in CASTS or inner.kind == K.UNEXPOSED_EXPR:
                        k = expr_kids(inner)
                        if not k:
                            break
                        inner = k[-1]
                    rec = bitfield_record(inner.type) if inner.type.get_canonical().kind == T.POINTER else None
                    if rec:
                        toks = " ".join(t.spelling for t in c.get_tokens())[:120]
                        out.add(("COPY", where(c), rec, toks))
            if c.kind == K.UNION_DECL and c.is_definition():
                members = [f for f in c.get_children() if f.kind == K.FIELD_DECL or
                           (f.kind in RECORDS and f.is_anonymous())]
                bits = [f for f in members if (f.kind == K.FIELD_DECL and (f.is_bitfield() or bitfield_record(f.type)))
                        or (f.kind in RECORDS and has_bitfields(f))]
                if bits and len(members) > 1:
                    names = ", ".join((f.spelling or "<anon>") + ":" + (f.type.spelling if f.kind == K.FIELD_DECL else
                                                                          "anon") for f in members)[:200]
                    out.add(("UNION", where(c), c.type.spelling, names))
            for x in c.get_children():
                walk(x)

        walk(tu.cursor)
    return out


def main():
    cc = [e for e in json.load(open(sys.argv[1])) if e["file"].replace("\\", "/").lower().startswith(TREE)]
    jobs = int(sys.argv[3]) if len(sys.argv) > 3 else 12
    found = set()
    with mp.Pool(jobs) as pool:
        for res in pool.imap_unordered(worker, [cc[i::jobs * 4] for i in range(jobs * 4)]):
            found |= res
    with open(sys.argv[2], "w", encoding="utf-8") as o:
        for kind, site, typ, toks in sorted(found):
            o.write("%-5s %s  %s  %s\n" % (kind, site, typ, toks))
    print("sites: %d" % len(found), file=sys.stderr)


if __name__ == "__main__":
    main()
