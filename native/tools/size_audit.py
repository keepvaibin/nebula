"""Whole-program audit: allocations, copies and clears sized by a constant for a type that is larger natively.

A type with a pointer, a reference or a vtable (directly, in a base or in a member) has a different size natively;
a size written as a number (the console's size, or a count times 4 for pointers) then allocates, copies or clears too
little. Every allocation (a callee named *alloc*, new[] of bytes) whose result is cast to such a type, and every copy,
clear or fill whose destination or source is such a type, is printed when its size expression has no sizeof.

    python size_audit.py <compile_commands.json> <out.txt> [jobs]     (pip install clang; LIBCLANG=path/to/libclang)
"""
import json
import multiprocessing as mp
import re
import sys

import clang.cindex as ci

from escape_audit import TREE, args_of, in_tree, where

K = ci.CursorKind
T = ci.TypeKind
FUNCS = {K.FUNCTION_DECL, K.CXX_METHOD, K.CONSTRUCTOR, K.DESTRUCTOR, K.CONVERSION_FUNCTION, K.FUNCTION_TEMPLATE}
CASTS = {K.CSTYLE_CAST_EXPR, K.CXX_STATIC_CAST_EXPR, K.CXX_REINTERPRET_CAST_EXPR}
COPY = re.compile(r"cpy|copy|zero|fill|move|memset|memcmp|clear", re.I)
ALLOC = re.compile(r"alloc", re.I)
SIZE_PARAM = re.compile(r"size|byte|len|count|num", re.I)
_wide = {}


def wider(t):
    """True when the type's size differs natively: it holds a pointer, a reference or a vtable."""
    t = t.get_canonical()
    k = t.kind
    if k in (T.POINTER, T.LVALUEREFERENCE, T.RVALUEREFERENCE, T.MEMBERPOINTER, T.BLOCKPOINTER):
        return True
    if k == T.CONSTANTARRAY:
        return wider(t.element_type)
    if k == T.RECORD:
        key = t.spelling
        if key in _wide:
            return _wide[key]
        d = t.get_declaration()
        if d.get_definition() is None:
            return True  # only declared here: counted as wider, for review
        d = d.get_definition()
        _wide[key] = False
        out = False
        for c in d.get_children():
            if c.kind == K.FIELD_DECL and wider(c.type):
                out = True
            elif c.kind == K.CXX_BASE_SPECIFIER and wider(c.type):
                out = True
            elif c.kind in (K.CXX_METHOD, K.DESTRUCTOR) and c.is_virtual_method():
                out = True
            if out:
                break
        _wide[key] = out
        return out
    return False


BYTES = (T.VOID, T.CHAR_S, T.UCHAR, T.SCHAR, T.CHAR_U)
INTS = (T.INT, T.UINT, T.LONG, T.ULONG, T.ULONGLONG, T.LONGLONG)


def is_memory_fn(f):
    """A copy, clear or fill over raw memory: a void* or byte pointer parameter and an integer size."""
    ps = [p.type.get_canonical() for p in f.get_arguments()]
    raw = any(p.kind == T.POINTER and p.get_pointee().get_canonical().kind in BYTES for p in ps)
    return raw and any(p.kind in INTS for p in ps)


def strip_parens(c):
    while c is not None and c.kind in (K.UNEXPOSED_EXPR, K.PAREN_EXPR):
        e = [x for x in c.get_children() if x.kind.is_expression()]
        if not e:
            break
        c = e[-1]
    return c


def strip(c):
    while c is not None and c.kind in (K.UNEXPOSED_EXPR, K.PAREN_EXPR, K.CSTYLE_CAST_EXPR, K.CXX_STATIC_CAST_EXPR,
                                       K.CXX_REINTERPRET_CAST_EXPR, K.CXX_CONST_CAST_EXPR):
        e = [x for x in c.get_children() if x.kind.is_expression()]
        if not e:
            break
        c = e[-1]
    return c


def text(c):
    return " ".join(t.spelling for t in c.get_tokens())


def size_arg(call, callee):
    args = list(call.get_arguments())
    params = list(callee.get_arguments()) if callee is not None else []
    for i, p in enumerate(params):
        if i < len(args) and SIZE_PARAM.search(p.spelling or ""):
            return args[i]
    ints = [a for a in args if a.type.get_canonical().kind in (T.INT, T.UINT, T.LONG, T.ULONG, T.LONGLONG, T.ULONGLONG)]
    return ints[0] if ints else None


def worker(entries):
    index = ci.Index.create()
    seen = set()
    out = []
    for e in entries:
        try:
            tu = index.parse(e["file"], args=args_of(e, None), options=0)
        except ci.TranslationUnitLoadError:
            continue

        def flag(site, kind, typ, size):
            if site not in seen:
                seen.add(site)
                out.append((site, kind, typ, size))

        def walk(c, cast_to):
            k = c.kind
            if k in CASTS and c.type.get_canonical().kind == T.POINTER:
                cast_to = c.type.get_canonical().get_pointee()
                # a wider type reached through a byte pointer plus a constant offset (the console's layout)
                inner = [x for x in c.get_children() if x.kind.is_expression()]
                inner = strip_parens(inner[-1]) if inner else None
                if inner is not None and inner.kind == K.BINARY_OPERATOR and inner.binary_operator.name in ("Add", "Sub") \
                        and cast_to.get_canonical().kind != T.VOID and wider(cast_to):
                    lhs, rhs = [x for x in inner.get_children() if x.kind.is_expression()][:2]
                    lt = lhs.type.get_canonical()
                    if lt.kind == T.POINTER and lt.get_pointee().get_canonical().kind in BYTES \
                            and strip_parens(rhs).kind == K.INTEGER_LITERAL:
                        flag(where(c), "offset", cast_to.spelling, text(inner))
            elif (k == K.UNEXPOSED_EXPR and c.type.get_canonical().kind == T.POINTER
                  and c.type.get_canonical().get_pointee().get_canonical().kind != T.VOID):
                cast_to = c.type.get_canonical().get_pointee()  # an implicit conversion from void* (C)
            elif k not in (K.UNEXPOSED_EXPR, K.PAREN_EXPR, K.CALL_EXPR, K.CXX_NEW_EXPR):
                cast_to = None
            if k == K.CALL_EXPR and c.referenced is not None:
                name = c.referenced.spelling
                if ALLOC.search(name) and cast_to is not None and wider(cast_to):
                    sz = size_arg(c, c.referenced)
                    if sz is not None and "sizeof" not in text(sz):
                        flag(where(c), "alloc " + name, cast_to.spelling, text(sz))
                elif COPY.search(name) and is_memory_fn(c.referenced):
                    args = list(c.get_arguments())
                    sz = size_arg(c, c.referenced)
                    for a in args[:2]:
                        inner = strip(a)
                        if inner is None:
                            continue
                        t = inner.type.get_canonical()
                        pointee = t.get_pointee() if t.kind == T.POINTER else (t.element_type if t.kind == T.CONSTANTARRAY else None)
                        if pointee is not None and pointee.get_canonical().kind not in (T.VOID, T.CHAR_S, T.UCHAR, T.SCHAR, T.CHAR_U) and wider(pointee):
                            if sz is not None and "sizeof" not in text(sz):
                                flag(where(c), "copy " + name, pointee.spelling, text(sz))
                            break
            if k == K.CXX_NEW_EXPR and cast_to is not None and wider(cast_to):
                toks = text(c)
                if re.search(r"\b(u8|char|s8|unsigned char)\s*\[", toks) and "sizeof" not in toks:
                    flag(where(c), "new bytes", cast_to.spelling, toks)
            for x in c.get_children():
                walk(x, cast_to)

        def scan(c):
            for x in c.get_children():
                if x.kind in FUNCS and x.is_definition() and in_tree(x):
                    walk(x, None)
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
            found |= set(res)
    with open(sys.argv[2], "w", encoding="utf-8") as o:
        for site, kind, typ, size in sorted(found):
            o.write("%s  %s  type %s  size %s\n" % (site, kind, typ, size))
    print("sites: %d" % len(found), file=sys.stderr)


if __name__ == "__main__":
    main()
