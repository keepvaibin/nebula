"""Whole-program audit: memory the GPU or a DMA engine reads, by the type the CPU wrote it as.

The GPU reads textures, palettes, vertex arrays and display lists in the console's byte order. Natively a u16, u32,
f32 or a struct of them written by the CPU lands in host order, so the GPU sees it byte-swapped (a u32 Z24X8 texture
reads as another depth). Every pointer handed to the GPU (GX/GD/J3DGD calls taking memory, physical-address
conversions) or made coherent for it (DC flush/store) is printed with the declaration it points into and that
declaration's element type: BYTE sites are safe; every other one must be file data, data converted to the console's
order, or memory the GPU only writes.

    python gpu_data_audit.py <compile_commands.json> <out.txt> [jobs]     (pip install clang; LIBCLANG=path/to/libclang)
"""
import json
import multiprocessing as mp
import sys

import clang.cindex as ci

from escape_audit import TREE, args_of, expr_kids, in_tree, strip, where

K = ci.CursorKind
T = ci.TypeKind
FUNCS = {K.FUNCTION_DECL, K.CXX_METHOD, K.CONSTRUCTOR, K.DESTRUCTOR, K.CONVERSION_FUNCTION, K.FUNCTION_TEMPLATE}
SCOPES = {K.NAMESPACE, K.CLASS_DECL, K.STRUCT_DECL, K.CLASS_TEMPLATE, K.CLASS_TEMPLATE_PARTIAL_SPECIALIZATION,
          K.LINKAGE_SPEC, K.UNEXPOSED_DECL}
# callee -> index of the memory argument
SINKS = {
    "GXSetArray": 1, "GXCallDisplayList": 0, "GXBeginDisplayList": 0, "GXCopyDisp": 0, "GXCopyTex": 0,
    "GXInitTlutObj": 1, "GXInitTexObj": 1, "GXInitTexObjCI": 1, "GDInitGDLObj": 1, "GDSetArray": 1,
    "GDSetTexImgPtr": 1, "J3DGDSetTexImgPtr": 1, "J3DGDLoadTlut": 0, "OSCachedToPhysical": 0,
    "OSUncachedToPhysical": 0, "__NebulaToPhysical": 0, "DCFlushRange": 0, "DCStoreRange": 0,
    "DCStoreRangeNoSync": 0, "DCFlushRangeNoSync": 0, "DCInvalidateRange": 0, "DCZeroRange": 0,
    "ARStartDMA": 1, "AIInitDMA": 0,
}
BYTE = {T.VOID, T.CHAR_S, T.CHAR_U, T.UCHAR, T.SCHAR, T.BOOL}


def element(t):
    """The innermost element type of a pointer, array or reference type."""
    t = t.get_canonical()
    while t.kind in (T.POINTER, T.LVALUEREFERENCE, T.RVALUEREFERENCE, T.CONSTANTARRAY, T.INCOMPLETEARRAY):
        t = (t.get_pointee() if t.kind in (T.POINTER, T.LVALUEREFERENCE, T.RVALUEREFERENCE) else t.element_type)
        t = t.get_canonical()
    return t


def classify(t):
    e = element(t)
    if e.kind in BYTE:
        return "BYTE", e.spelling
    if e.kind == T.RECORD:
        return "STRUCT", e.spelling
    return "WIDE", e.spelling


def origin(arg):
    """The expression the pointer is computed from, past casts, &, array indexing and pointer arithmetic."""
    c = strip(arg)
    for _ in range(16):
        if c is None:
            break
        if c.kind == K.UNARY_OPERATOR:
            toks = [t.spelling for t in c.get_tokens()]
            e = expr_kids(c)
            if toks and toks[0] in ("&", "*") and e:
                c = strip(e[-1])
                continue
        if c.kind == K.ARRAY_SUBSCRIPT_EXPR:
            e = expr_kids(c)
            if e:
                c = strip(e[0])
                continue
        if c.kind == K.BINARY_OPERATOR:
            e = expr_kids(c)
            ptr = [x for x in e if x.type.get_canonical().kind in (T.POINTER, T.CONSTANTARRAY, T.INCOMPLETEARRAY)]
            if ptr:
                c = strip(ptr[0])
                continue
        break
    return c


def describe(c):
    """(kind of origin, its declaration's type spelling, whether it is static data with an initializer)."""
    if c is None:
        return "?", "", False
    if c.kind in (K.DECL_REF_EXPR, K.MEMBER_REF_EXPR):
        d = c.referenced
        if d is not None:
            init = d.kind == K.VAR_DECL and any(k.kind == K.INIT_LIST_EXPR or k.kind.is_expression() for k in d.get_children()
                                               if k.kind != K.TYPE_REF)
            static = d.kind == K.VAR_DECL and (d.semantic_parent is None or d.semantic_parent.kind != K.FUNCTION_DECL or
                                               d.storage_class == ci.StorageClass.STATIC)
            kind = {K.VAR_DECL: "var", K.PARM_DECL: "param", K.FIELD_DECL: "field"}.get(d.kind, d.kind.name.lower())
            return kind + ":" + d.spelling, d.type.spelling, bool(init and static and d.kind == K.VAR_DECL)
    if c.kind == K.CALL_EXPR:
        return "call:" + c.spelling, c.type.spelling, False
    if c.kind == K.CXX_THIS_EXPR:
        return "this", c.type.spelling, False
    return c.kind.name.lower(), c.type.spelling, False


def worker(entries):
    index = ci.Index.create()
    out = set()
    for e in entries:
        try:
            tu = index.parse(e["file"], args=args_of(e, None), options=0)
        except ci.TranslationUnitLoadError:
            continue

        def walk(c, fn):
            if c.kind == K.CALL_EXPR and c.spelling in SINKS and in_tree(c):
                args = list(c.get_arguments())
                i = SINKS[c.spelling]
                if i < len(args):
                    a = args[i]
                    o = origin(a)
                    cls, el = classify(o.type if o is not None else a.type)
                    kind, typ, static_init = describe(o)
                    toks = " ".join(t.spelling for t in a.get_tokens())[:100]
                    out.add((where(c), fn, c.spelling, cls, el, kind, typ, "STATIC-INIT" if static_init else "", toks))
            for x in c.get_children():
                walk(x, fn)

        def scan(c):
            for x in c.get_children():
                if x.kind in FUNCS and x.is_definition() and in_tree(x):
                    walk(x, x.spelling)
                elif x.kind in SCOPES:
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
    rows = sorted(found, key=lambda r: (r[3] == "BYTE", r[0]))
    with open(sys.argv[2], "w", encoding="utf-8") as o:
        for site, fn, callee, cls, el, kind, typ, si, toks in rows:
            o.write("%-6s %s  in %s: %s(%s)  -> %s [%s] elem=%s %s\n" % (cls, site, fn, callee, toks, kind, typ, el, si))
    counts = {}
    for r in rows:
        counts[r[3]] = counts.get(r[3], 0) + 1
    print("sites: %d %s" % (len(rows), counts), file=sys.stderr)


if __name__ == "__main__":
    main()
