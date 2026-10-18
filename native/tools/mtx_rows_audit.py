"""Whole-program audit: matrices passed with fewer rows than the parameter is declared with.

Mtx (3x4) and Mtx44 (4x4) both decay to f32 (*)[4], so the compiler accepts either where the other is meant; a 3x4
matrix given to a 4x4 parameter is read or written one row (16 bytes) past its end. Every call and construction in the
native tree is checked by the parameter's type as written; parameters written as a bare row pointer (MtxP,
f32 (*)[4]) are listed by callee with the 3-row matrices they receive, for review.

    python mtx_rows_audit.py <compile_commands.json> <out.txt> [jobs]     (pip install clang; LIBCLANG=path/to/libclang)
"""
import collections
import json
import multiprocessing as mp
import os
import re
import sys

import clang.cindex as ci

from escape_audit import TREE, args_of, in_tree, where

K = ci.CursorKind
FUNCS = {K.FUNCTION_DECL, K.CXX_METHOD, K.CONSTRUCTOR, K.DESTRUCTOR, K.CONVERSION_FUNCTION, K.FUNCTION_TEMPLATE}


def rows_written(t):
    """Rows of a parameter's type as written: 3, 4, or None for a row pointer or anything else."""
    s = t.spelling
    if re.search(r"\bMtx44\b|\[4\]\s*\[4\]", s):
        return 4
    if re.search(r"\bMtx\b|\[3\]\s*\[4\]", s):
        return 3
    d = t.get_declaration()
    if d is not None and d.kind == K.TYPEDEF_DECL:
        u = d.underlying_typedef_type.get_canonical()
        if u.kind == ci.TypeKind.CONSTANTARRAY and u.element_type.get_canonical().kind == ci.TypeKind.CONSTANTARRAY:
            return u.element_count
    if t.get_canonical().kind == ci.TypeKind.POINTER and t.get_canonical().get_pointee().get_canonical().kind == ci.TypeKind.CONSTANTARRAY:
        return None
    return 0


def rows_arg(a):
    while a.kind in (K.UNEXPOSED_EXPR, K.PAREN_EXPR) and [x for x in a.get_children() if x.kind.is_expression()]:
        a = [x for x in a.get_children() if x.kind.is_expression()][0]
    t = a.type.get_canonical()
    if t.kind == ci.TypeKind.CONSTANTARRAY and t.element_type.get_canonical().kind == ci.TypeKind.CONSTANTARRAY:
        return t.element_count, a
    return None, a


def worker(entries):
    index = ci.Index.create()
    seen = set()
    bad, rowptr = [], []
    for e in entries:
        try:
            tu = index.parse(e["file"], args=args_of(e, None), options=0)
        except ci.TranslationUnitLoadError:
            continue

        def walk(c):
            if c.kind in (K.CALL_EXPR,) and c.referenced is not None:
                r = c.referenced
                params = [p.type for p in r.get_arguments()] if r.kind != K.FUNCTION_TEMPLATE else []
                args = list(c.get_arguments())
                if len(args) == len(params) + 1:
                    args = args[1:]  # a member operator's object
                for p, a in zip(params, args):
                    pr = rows_written(p)
                    ar, inner = rows_arg(a)
                    if ar is None or pr == 0:
                        continue
                    site = where(c)
                    if site in seen:
                        continue
                    if pr is None:
                        seen.add(site)
                        rowptr.append((r.spelling, where(r), ar, site))
                    elif ar < pr:
                        seen.add(site)
                        bad.append((site, r.spelling, p.spelling, inner.type.spelling))
            for x in c.get_children():
                walk(x)

        def scan(c):
            for x in c.get_children():
                if x.kind in FUNCS and x.is_definition() and in_tree(x):
                    walk(x)
                elif x.kind in (K.NAMESPACE, K.CLASS_DECL, K.STRUCT_DECL, K.CLASS_TEMPLATE, K.LINKAGE_SPEC, K.UNEXPOSED_DECL):
                    scan(x)

        scan(tu.cursor)
    return bad, rowptr


def main():
    cc = [e for e in json.load(open(sys.argv[1])) if e["file"].replace("\\", "/").lower().startswith(TREE)]
    jobs = int(sys.argv[3]) if len(sys.argv) > 3 else 12
    bad, rowptr = set(), collections.defaultdict(set)
    with mp.Pool(jobs) as pool:
        for b, rp in pool.imap_unordered(worker, [cc[i::jobs * 4] for i in range(jobs * 4)]):
            bad |= set(b)
            for name, decl, rows, site in rp:
                if rows < 4:
                    rowptr[(name, decl)].add(site)
    with open(sys.argv[2], "w", encoding="utf-8") as o:
        o.write("# 3-row matrices given to 4-row parameters\n")
        for b in sorted(bad):
            o.write("%s  %s(%s) given %s\n" % b)
        o.write("# row-pointer parameters given 3-row matrices (callee: sites)\n")
        for (name, decl), sites in sorted(rowptr.items()):
            o.write("%s %s: %d sites, e.g. %s\n" % (name, decl, len(sites), sorted(sites)[0]))
    print("4-row parameters given 3 rows: %d; row-pointer callees given 3 rows: %d" % (len(bad), len(rowptr)), file=sys.stderr)


if __name__ == "__main__":
    main()
