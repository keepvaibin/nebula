"""Whole-program audit: objects reached through void* and then read as bytes or numbers at the console's offsets.

A pointer to an object whose layout differs natively (a vtable, pointers) can pass through void* slots (resource
tables, getRes(), members typed void*) and be cast back to u8*, s16*, u32* (...) and indexed at the console's field
offsets. The flow of pointer values into and out of void*-typed fields, variables, parameters and function results is
traced to a fixpoint (by symbol), and every cast of such a void* to a byte or number pointer that can see an object of
a natively wider type is printed.

    python void_flow_audit.py <compile_commands.json> <out.txt> [jobs]     (pip install clang; LIBCLANG=path/to/libclang)
"""
import collections
import json
import multiprocessing as mp
import sys

import clang.cindex as ci

from escape_audit import TREE, args_of, in_tree, where
from size_audit import wider

K = ci.CursorKind
T = ci.TypeKind
FUNCS = {K.FUNCTION_DECL, K.CXX_METHOD, K.CONSTRUCTOR, K.DESTRUCTOR, K.CONVERSION_FUNCTION, K.FUNCTION_TEMPLATE}
CASTS = {K.CSTYLE_CAST_EXPR, K.CXX_STATIC_CAST_EXPR, K.CXX_REINTERPRET_CAST_EXPR}
VIEW = {T.CHAR_S, T.CHAR_U, T.UCHAR, T.SCHAR, T.SHORT, T.USHORT, T.INT, T.UINT, T.LONG, T.ULONG, T.LONGLONG,
        T.ULONGLONG, T.FLOAT, T.DOUBLE}


def is_void_ptr(t):
    t = t.get_canonical()
    return t.kind == T.POINTER and t.get_pointee().get_canonical().kind == T.VOID


def strip(c):
    while c is not None and c.kind in (K.UNEXPOSED_EXPR, K.PAREN_EXPR):
        e = [x for x in c.get_children() if x.kind.is_expression()]
        if not e:
            break
        c = e[-1]
    return c


def param_node(fn, index):
    return "param:%s#%d" % (fn.get_usr(), index)


def parm_node(p):
    """A parameter by its function and position: declarations and the definition name it alike."""
    f = p.semantic_parent
    if f is not None:
        for i, a in enumerate(f.get_arguments()):
            if a == p:
                return param_node(f, i)
    return p.get_usr() or None


def node_of(c):
    """The slot a void* value is read from: a field, variable or parameter, or a function's result."""
    c = strip(c)
    if c is None:
        return None
    if c.kind == K.ARRAY_SUBSCRIPT_EXPR:
        e = [x for x in c.get_children() if x.kind.is_expression()]
        return node_of(e[0]) if e else None
    if c.kind in (K.MEMBER_REF_EXPR, K.DECL_REF_EXPR) and c.referenced is not None:
        r = c.referenced
        if r.kind == K.PARM_DECL:
            return parm_node(r)
        if r.kind in (K.FIELD_DECL, K.VAR_DECL):
            return r.get_usr() or None
    if c.kind == K.CALL_EXPR and c.referenced is not None:
        return "ret:" + c.referenced.get_usr()
    return None


def source_type(c):
    """The record type of an object pointer converted to void* here, if it is one."""
    s = strip(c)
    while s is not None and s.kind in CASTS:
        e = [x for x in s.get_children() if x.kind.is_expression()]
        s = strip(e[-1]) if e else None
    if s is None:
        return None
    t = s.type.get_canonical()
    if t.kind == T.POINTER and t.get_pointee().get_canonical().kind == T.RECORD:
        return t.get_pointee().get_canonical().spelling, wider(t.get_pointee())
    return None


def worker(entries):
    index = ci.Index.create()
    edges, origins, casts = set(), set(), set()
    for e in entries:
        try:
            tu = index.parse(e["file"], args=args_of(e, None), options=0)
        except ci.TranslationUnitLoadError:
            continue

        def flow(dst, rhs):
            if dst is None:
                return
            st = source_type(rhs)
            if st is not None:
                origins.add((dst, st[0], st[1]))
            src = node_of(rhs)
            if src is not None and is_void_ptr(strip(rhs).type if strip(rhs) is not None else rhs.type):
                edges.add((src, dst))

        def walk(c, fn_usr):
            k = c.kind
            kids = [x for x in c.get_children() if x.kind.is_expression()]
            if k == K.BINARY_OPERATOR and c.binary_operator.name == "Assign" and len(kids) == 2 and is_void_ptr(kids[0].type):
                flow(node_of(kids[0]), kids[1])
            elif k == K.VAR_DECL and is_void_ptr(c.type) and kids:
                flow(c.get_usr(), kids[-1])
            elif k == K.RETURN_STMT and kids and fn_usr is not None:
                flow("ret:" + fn_usr, kids[0])
            elif k == K.CALL_EXPR and c.referenced is not None:
                params = list(c.referenced.get_arguments())
                for i, (p, a) in enumerate(zip(params, list(c.get_arguments()))):
                    if is_void_ptr(p.type):
                        flow(param_node(c.referenced, i), a)
            elif k == K.MEMBER_REF and False:
                pass
            if k in CASTS:
                dst = c.type.get_canonical()
                if dst.kind == T.POINTER and dst.get_pointee().get_canonical().kind in VIEW and kids:
                    inner = strip(kids[-1])
                    if inner is not None and is_void_ptr(inner.type):
                        n = node_of(inner)
                        if n is not None:
                            casts.add((n, where(c), dst.spelling))
            for x in c.get_children():
                walk(x, fn_usr)

        def scan(c):
            for x in c.get_children():
                if x.kind in FUNCS and x.is_definition() and in_tree(x):
                    # constructor initialisers into void* members
                    ch = list(x.get_children())
                    for i, m in enumerate(ch):
                        if m.kind == K.MEMBER_REF and m.referenced is not None and is_void_ptr(m.referenced.type) \
                                and i + 1 < len(ch) and ch[i + 1].kind.is_expression():
                            flow(m.referenced.get_usr(), ch[i + 1])
                    walk(x, x.get_usr())
                elif x.kind in (K.NAMESPACE, K.CLASS_DECL, K.STRUCT_DECL, K.CLASS_TEMPLATE, K.LINKAGE_SPEC, K.UNEXPOSED_DECL):
                    scan(x)

        scan(tu.cursor)
    return edges, origins, casts


def main():
    cc = [e for e in json.load(open(sys.argv[1])) if e["file"].replace("\\", "/").lower().startswith(TREE)]
    jobs = int(sys.argv[3]) if len(sys.argv) > 3 else 12
    edges, origins, casts = set(), set(), set()
    with mp.Pool(jobs) as pool:
        for ed, og, cs in pool.imap_unordered(worker, [cc[i::jobs * 4] for i in range(jobs * 4)]):
            edges |= ed
            origins |= og
            casts |= cs
    succ = collections.defaultdict(set)
    for a, b in edges:
        succ[a].add(b)
    types = collections.defaultdict(set)  # node -> {(type, wider)}
    work = []
    for n, t, w in origins:
        if (t, w) not in types[n]:
            types[n].add((t, w))
            work.append(n)
    while work:
        n = work.pop()
        for m in succ.get(n, ()):
            new = types[n] - types[m]
            if new:
                types[m] |= new
                work.append(m)
    out = []
    for n, site, dst in sorted(casts, key=lambda x: x[1]):
        seen = sorted(t for t, w in types.get(n, ()) if w)
        if seen:
            out.append("%s  %s from %s, which can hold %s" % (site, dst, n.split("@")[-1], ", ".join(seen[:6])))
    with open(sys.argv[2], "w", encoding="utf-8") as o:
        o.write("\n".join(out) + "\n")
    print("void* slots %d, flows %d, origins %d, casts %d, flagged %d" % (len(types), len(edges), len(origins), len(casts), len(out)),
          file=sys.stderr)


if __name__ == "__main__":
    main()
