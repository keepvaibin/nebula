"""Whole-program audit: where can the address of a local (stack) object outlive it?

Every function definition in the native tree is summarised by symbol (USR): which of its pointer and reference
parameters (and `this`) it keeps past its return (stores into a member, a global, through a pointer, or a reference
member), returns, or passes on, by parameter position, following pointer copies through its locals. Virtual calls
reach every override. The summaries are solved to a fixpoint, then every call or store that gives a local's address
to something that keeps it is printed with the local's scope, for review.

    python escape_audit.py <compile_commands.json> <out.txt> [jobs]     (pip install clang; LIBCLANG=path/to/libclang)

Writes <out.txt> (the sites), <out.txt>.keeps (every function and parameter it keeps) and <out.txt>.unresolved (calls
with a local's address whose callee has no definition here).
"""
import collections
import ctypes
import json
import multiprocessing as mp
import os
import shlex
import sys

import clang.cindex as ci

if os.environ.get("LIBCLANG"):
    ci.Config.set_library_file(os.environ["LIBCLANG"])
elif os.name == "nt":
    ci.Config.set_library_file(r"C:/Program Files/LLVM/bin/libclang.dll")
K = ci.CursorKind
# the native tree: this script's parent directory
TREE = os.path.dirname(os.path.dirname(os.path.abspath(__file__))).replace("\\", "/").lower() + "/"
FUNCS = {K.FUNCTION_DECL, K.CXX_METHOD, K.CONSTRUCTOR, K.DESTRUCTOR, K.CONVERSION_FUNCTION, K.FUNCTION_TEMPLATE}
SCOPES = {K.NAMESPACE, K.CLASS_DECL, K.STRUCT_DECL, K.CLASS_TEMPLATE, K.CLASS_TEMPLATE_PARTIAL_SPECIALIZATION,
          K.UNION_DECL, K.LINKAGE_SPEC, K.UNEXPOSED_DECL}
WRAPS = {K.CSTYLE_CAST_EXPR, K.CXX_STATIC_CAST_EXPR, K.CXX_REINTERPRET_CAST_EXPR, K.CXX_CONST_CAST_EXPR,
         K.CXX_FUNCTIONAL_CAST_EXPR, K.CXX_DYNAMIC_CAST_EXPR, K.UNEXPOSED_EXPR, K.PAREN_EXPR}
ASSIGN = {"=", "+=", "-="}


def in_tree(cur):
    f = cur.location.file
    return f is not None and f.name.replace("\\", "/").lower().startswith(TREE)


def where(cur):
    l = cur.location
    return "%s:%d:%d" % (l.file.name.replace("\\", "/") if l.file else "?", l.line, l.column)


_ov = None


def overridden(cur):
    global _ov
    if _ov is None:
        lib = ci.conf.lib
        _ov = (lib.clang_getOverriddenCursors, lib.clang_disposeOverriddenCursors)
        _ov[0].argtypes = [ci.Cursor, ctypes.POINTER(ctypes.POINTER(ci.Cursor)), ctypes.POINTER(ctypes.c_uint)]
        _ov[0].restype = None
        _ov[1].argtypes = [ctypes.POINTER(ci.Cursor)]
        _ov[1].restype = None
    arr = ctypes.POINTER(ci.Cursor)()
    n = ctypes.c_uint(0)
    _ov[0](cur, ctypes.byref(arr), ctypes.byref(n))
    out = [arr[i].get_usr() for i in range(n.value)]
    if n.value:
        _ov[1](arr)
    return out


def kids(c):
    return list(c.get_children())


def expr_kids(c):
    return [k for k in c.get_children() if k.kind.is_expression()]


def strip(c):
    while c is not None and c.kind in WRAPS:
        e = expr_kids(c)
        if not e:
            break
        c = e[-1]
    return c


def first_token(c):
    for t in c.get_tokens():
        return t.spelling
    return ""


def is_ptr_like(t):
    k = t.get_canonical().kind
    return k in (ci.TypeKind.POINTER, ci.TypeKind.LVALUEREFERENCE, ci.TypeKind.RVALUEREFERENCE,
                 ci.TypeKind.CONSTANTARRAY, ci.TypeKind.INCOMPLETEARRAY, ci.TypeKind.MEMBERPOINTER)


def arrow(base):
    b = strip(base)
    return b is not None and b.type.get_canonical().kind == ci.TypeKind.POINTER


def is_ref(t):
    return t.get_canonical().kind in (ci.TypeKind.LVALUEREFERENCE, ci.TypeKind.RVALUEREFERENCE)


class Fn:
    """One function's summary. Sources: ('p', i), ('this',), ('L', id), ('heap',), ('g',) and
    ('call', site) for a call's result. Facts:
      stores: (target, sources)   target is ('g',) | ('p', i) | ('this',) | ('L', id) | ('heap',)
      rets:   sources
      args:   (site, callee_usrs, position, sources, object_sources)"""

    def __init__(self, cur):
        self.usr = cur.get_usr()
        self.where = where(cur)
        self.name = cur.spelling
        self.params = {}
        self.locals = {}  # id -> (name, where, block-scoped, block extent)
        self.taint = collections.defaultdict(set)  # local var id -> sources of the pointer it holds
        self.stores = []
        self.rets = []
        self.calls = []  # (site, [usr], [arg sources], object sources, callee kind)
        self.overrides = []


def analyse(fcur):
    fn = Fn(fcur)
    if fcur.kind in (K.CXX_METHOD, K.DESTRUCTOR) and fcur.is_virtual_method():
        fn.overrides = overridden(fcur)
    idx = 0
    for k in fcur.get_children():
        if k.kind == K.PARM_DECL:
            fn.params[k.hash] = idx
            idx += 1
    fn.nparams = idx
    blocks = []
    in_init = [False]

    def var_id(v):
        return v.hash

    def lhs_root(c):
        """The storage an lvalue names: ('L', id) a local, ('p', i) / ('this',) the object a parameter or this
        points to, ('g',) a global; deref and subscript go through the pointer's sources."""
        c = strip(c)
        if c is None:
            return [("g",)]
        if c.kind == K.DECL_REF_EXPR:
            r = c.referenced
            if r is None:
                return [("g",)]
            if r.hash in fn.params:
                return [("pv", fn.params[r.hash])] if not is_ref(r.type) else [("p", fn.params[r.hash])]
            if r.hash in fn.locals:
                return [("L", r.hash)] if not is_ref(r.type) else list(fn.taint[r.hash]) or [("g",)]
            return [("g",)]
        if c.kind == K.MEMBER_REF_EXPR:
            e = expr_kids(c)
            if not e:
                return [("this",)]
            # p->m goes through p's value; obj.m is obj's storage
            if arrow(e[0]):
                return list(sources(e[0])) or [("g",)]
            return lhs_root(e[0])
        if c.kind == K.ARRAY_SUBSCRIPT_EXPR:
            e = expr_kids(c)
            if e:
                b = strip(e[0])
                if b is not None and b.type.get_canonical().kind in (ci.TypeKind.CONSTANTARRAY, ci.TypeKind.INCOMPLETEARRAY):
                    return lhs_root(e[0])
                return list(sources(e[0])) or [("g",)]
        if c.kind == K.UNARY_OPERATOR and first_token(c) == "*":
            e = expr_kids(c)
            return (list(sources(e[0])) or [("g",)]) if e else [("g",)]
        if c.kind == K.CXX_THIS_EXPR:
            return [("this",)]
        return [("g",)]

    def addr_of(c):
        """Sources of &c: the object c names."""
        roots = lhs_root(c)
        out = set()
        for r in roots:
            if r[0] == "pv":
                out.add(("Lp", r[1]))  # the parameter's own slot: this function's stack
            else:
                out.add(r)
        return out

    def sources(c):
        """Where a pointer (or the object a reference binds) may come from."""
        c = strip(c)
        if c is None:
            return set()
        k = c.kind
        if k == K.DECL_REF_EXPR:
            r = c.referenced
            if r is None:
                return set()
            if r.hash in fn.params:
                i = fn.params[r.hash]
                t = r.type.get_canonical().kind
                if t == ci.TypeKind.POINTER or is_ref(r.type):
                    return {("p", i)}
                return set()
            if r.hash in fn.locals:
                tk = r.type.get_canonical().kind
                if tk in (ci.TypeKind.CONSTANTARRAY, ci.TypeKind.INCOMPLETEARRAY):
                    return {("L", r.hash)}
                if is_ref(r.type):
                    # a reference to a pointer yields the pointer stored in its referent, which is not tracked;
                    # a reference to an object stands for that object
                    pk = r.type.get_canonical().get_pointee().get_canonical().kind
                    return set() if pk == ci.TypeKind.POINTER else {("T", r.hash)}
                if tk == ci.TypeKind.POINTER:
                    return {("T", r.hash)}
                return {("L", r.hash)} if tk in (ci.TypeKind.RECORD, ci.TypeKind.ELABORATED) else set()
            return set()
        if k == K.CXX_THIS_EXPR:
            return {("this",)}
        if k == K.UNARY_OPERATOR:
            tok = first_token(c)
            e = expr_kids(c)
            if not e:
                return set()
            if tok == "&":
                return addr_of(e[0])
            if tok == "*":
                return set()  # a value loaded through a pointer: not an address we track
            return sources(e[0])
        if k in (K.BINARY_OPERATOR, K.COMPOUND_ASSIGNMENT_OPERATOR):
            e = expr_kids(c)
            out = set()
            for x in e:
                if strip(x) is not None and is_ptr_like(strip(x).type):
                    out |= sources(x)
            return out
        if k == K.CONDITIONAL_OPERATOR:
            e = expr_kids(c)
            out = set()
            for x in e[1:]:
                out |= sources(x)
            return out
        if k == K.ARRAY_SUBSCRIPT_EXPR:
            return set()  # an element's value
        if k == K.MEMBER_REF_EXPR:
            # obj.arr (array member decays) is obj's storage; obj.ptr is a loaded value
            r = c.referenced
            if r is not None and r.type.get_canonical().kind in (ci.TypeKind.CONSTANTARRAY, ci.TypeKind.INCOMPLETEARRAY):
                return addr_of(c)
            return set()
        if k == K.CALL_EXPR:
            return {("call", c.hash)}
        if k == K.CXX_NEW_EXPR:
            return {("heap",)}
        return set()

    def visit_call(c):
        r = c.referenced
        args = list(c.get_arguments())
        obj = None
        ek = expr_kids(c)
        if r is not None:
            usrs = [r.get_usr()]
        else:
            # a dependent call in a template (resolved by name and argument count) or a call through a pointer
            callee = ek[0] if ek else None
            while callee is not None and callee.kind in (K.PAREN_EXPR, K.UNEXPOSED_EXPR) and expr_kids(callee):
                callee = expr_kids(callee)[0]
            ref = callee.referenced if callee is not None else None
            args = ek[1:]
            usrs = ["<indirect>"]
            if callee is not None and callee.kind == K.DECL_REF_EXPR and ref is not None and ref.kind == K.OVERLOADED_DECL_REF:
                usrs = ["name:%s/%d" % (ref.spelling, len(args))]
            elif callee is not None and callee.kind == K.MEMBER_REF_EXPR and ref is None:
                toks = [t.spelling for t in callee.get_tokens()]
                mk = expr_kids(callee)
                if toks:
                    usrs = ["name:%s/%d" % (toks[-1], len(args))]
                    obj = (sources(mk[0]) if arrow(mk[0]) else addr_of(mk[0])) if mk else {("this",)}
            elif c.spelling and ref is None:
                usrs = ["name:%s/%d" % (c.spelling, len(args))]
            if usrs == ["<indirect>"] and callee is not None and (ref is None or ref.kind not in (K.VAR_DECL, K.FIELD_DECL, K.PARM_DECL)):
                toks = [t.spelling for t in callee.get_tokens()]
                if "<" in toks:
                    toks = toks[: toks.index("<")]
                idents = [t for t in toks if t.isidentifier()]
                if idents:
                    usrs = ["name:%s/%d" % (idents[-1], len(args))]
        if r is not None and r.kind in (K.CXX_METHOD, K.CONVERSION_FUNCTION, K.DESTRUCTOR):
            callee = strip(ek[0]) if ek else None
            if callee is not None and callee.kind == K.MEMBER_REF_EXPR:
                mk = expr_kids(callee)
                if mk:
                    obj = sources(mk[0]) if arrow(mk[0]) else addr_of(mk[0])
                else:
                    obj = {("this",)}
            elif callee is not None and callee.kind == K.DECL_REF_EXPR and len(args) == len(list(r.get_arguments())) + 1:
                obj, args = addr_of(args[0]), args[1:]  # a member operator: the object is the first operand
            elif callee is not None and callee.kind == K.DECL_REF_EXPR:
                obj = {("this",)}
        elif r is not None and r.kind == K.CONSTRUCTOR:
            obj = {("this",)} if in_init[0] else {("ctor", c.hash)}
        fn.calls.append((c.hash, where(c), usrs, [sources(a) for a in args], obj, c))

    def walk(c):
        k = c.kind
        if k == K.COMPOUND_STMT:
            ext = c.extent
            blocks.append((ext.start.offset, ext.end.offset))
            for x in c.get_children():
                walk(x)
            blocks.pop()
            return
        if k == K.LAMBDA_EXPR:
            return  # separate body; captured locals are reviewed with the call sites that take the lambda
        if k == K.VAR_DECL:
            if c.storage_class in (ci.StorageClass.STATIC, ci.StorageClass.EXTERN):
                for x in c.get_children():
                    walk(x)
                return
            ext = blocks[-1] if blocks else (0, 0)
            fn.locals[c.hash] = (c.spelling, where(c), len(blocks) > 1, ext)
            init = [x for x in c.get_children() if x.kind.is_expression()]
            for x in init:
                walk(x)
            if init:
                e = init[-1]
                if strip(e) is not None and strip(e).kind == K.CALL_EXPR and strip(e).referenced is not None \
                        and strip(e).referenced.kind == K.CONSTRUCTOR:
                    fn.taint[("ctor", strip(e).hash)].add(("L", c.hash))
                elif is_ref(c.type):
                    fn.taint[c.hash] |= addr_of(e)
                elif is_ptr_like(c.type):
                    fn.taint[c.hash] |= sources(e)
            return
        if k == K.CXX_NEW_EXPR:
            for x in c.get_children():
                if x.kind == K.CALL_EXPR and x.referenced is not None and x.referenced.kind == K.CONSTRUCTOR:
                    fn.taint[("ctor", x.hash)].add(("heap",))
        if k in (K.BINARY_OPERATOR, K.COMPOUND_ASSIGNMENT_OPERATOR):
            op = c.binary_operator.name if k == K.BINARY_OPERATOR else ""
            e = expr_kids(c)
            if len(e) == 2 and (k == K.COMPOUND_ASSIGNMENT_OPERATOR or op == "Assign"):
                lhs, rhs = e
                ls = strip(lhs)
                if ls is not None and is_ptr_like(ls.type) or (ls is not None and ls.type.get_canonical().kind in (
                        ci.TypeKind.ULONGLONG, ci.TypeKind.LONGLONG, ci.TypeKind.ULONG, ci.TypeKind.UINT)):
                    src = sources(rhs)
                    if src:
                        for root in lhs_root(lhs):
                            if root[0] == "L" and ls.kind == K.DECL_REF_EXPR:
                                fn.taint[root[1]] |= src
                            elif root[0] == "pv":
                                fn.taint[("pv", root[1])] |= src
                            else:
                                fn.stores.append((root, src, where(c)))
        if k == K.RETURN_STMT:
            e = expr_kids(c)
            if e:
                fn.rets.append(sources(e[0]))
        if k == K.CALL_EXPR:
            visit_call(c)
        for x in c.get_children():
            walk(x)

    # constructor member initialisers: MEMBER_REF followed by its initialiser
    ch = kids(fcur)
    for i, x in enumerate(ch):
        if x.kind == K.MEMBER_REF and i + 1 < len(ch) and ch[i + 1].kind.is_expression():
            r = x.referenced
            if r is not None and is_ptr_like(r.type):
                init = ch[i + 1]
                src = addr_of(init) if is_ref(r.type) else sources(init)
                if src:
                    fn.stores.append((("this",), src, where(x)))
    for x in ch:
        if x.kind == K.COMPOUND_STMT:
            walk(x)
        elif x.kind.is_expression():
            in_init[0] = fcur.kind == K.CONSTRUCTOR
            walk(x)
            in_init[0] = False
    # resolve taint (locals holding pointers) to base sources
    def base(srcs, seen=None):
        seen = seen or set()
        out = set()
        for s in srcs:
            if s[0] == "T":
                if s[1] in seen:
                    continue
                seen.add(s[1])
                out |= base(fn.taint.get(s[1], set()), seen)
            else:
                out.add(s)
        return out

    fn.stores = [(t2, base(s), w) for t, s, w in fn.stores for t2 in (base({t}) if t[0] == "T" else {t})]
    fn.rets = [base(s) for s in fn.rets]
    calls = []
    for h, w, usrs, argsrc, obj, cc in fn.calls:
        if obj is not None and any(o[0] == "ctor" for o in obj):
            obj = base(fn.taint.get(("ctor", h), set())) or {("L", ("temp", h))}
        calls.append((h, w, usrs, [base(a) for a in argsrc], base(obj) if obj else None))
    fn.calls = calls
    fn.locals = {k: v for k, v in fn.locals.items()}
    del fn.taint
    return fn


def args_of(entry, ccdir):
    cmd = entry.get("command") or " ".join(entry["arguments"])
    parts = shlex.split(cmd.replace("\\", "/"), posix=True)
    out = []
    skip = False
    for a in parts[1:]:
        if skip:
            skip = False
            continue
        if a in ("-o", "-c", "-MF", "-MT", "-MQ"):
            skip = a != "-c"
            continue
        if a.endswith((".c", ".cpp", ".cc")) and os.path.normcase(a) == os.path.normcase(entry["file"].replace("\\", "/")):
            continue
        out.append(a)
    return out


def worker(job):
    entries, ccdir = job
    index = ci.Index.create()
    done = set()
    fns = []
    unresolved = collections.Counter()
    for e in entries:
        try:
            tu = index.parse(e["file"], args=args_of(e, ccdir), options=0)
        except ci.TranslationUnitLoadError:
            print("parse failed", e["file"], file=sys.stderr)
            continue

        def scan(c):
            for x in c.get_children():
                if x.kind in FUNCS:
                    if x.is_definition() and in_tree(x):
                        u = x.get_usr()
                        if u not in done:
                            done.add(u)
                            fns.append(analyse(x))
                    # templates' and classes' nested definitions
                    if x.kind == K.FUNCTION_TEMPLATE:
                        continue
                elif x.kind in SCOPES:
                    if x.location.file is None or in_tree(x) or x.kind in (K.LINKAGE_SPEC, K.NAMESPACE, K.UNEXPOSED_DECL):
                        scan(x)

        scan(tu.cursor)
    return [(f.usr, f.where, f.name, f.nparams, f.locals, f.stores, f.rets, f.calls, f.overrides) for f in fns]


def main():
    cc = json.load(open(sys.argv[1]))
    out = sys.argv[2]
    jobs = int(sys.argv[3]) if len(sys.argv) > 3 else 12
    cc = [e for e in cc if e["file"].replace("\\", "/").lower().startswith(TREE)]
    chunks = [(cc[i::jobs * 4], os.path.dirname(sys.argv[1])) for i in range(jobs * 4)]
    summaries = {}
    with mp.Pool(jobs) as pool:
        for res in pool.imap_unordered(worker, chunks):
            for s in res:
                summaries.setdefault(s[0], s)
    print("functions:", len(summaries), file=sys.stderr)

    over = collections.defaultdict(set)  # base usr -> overriding usrs
    for s in summaries.values():
        for b in s[8]:
            over[b].add(s[0])

    by_name = collections.defaultdict(set)
    any_count = collections.defaultdict(set)
    for s in summaries.values():
        by_name["name:%s/%d" % (s[2], s[3])].add(s[0])
        by_name["name:%s/%d" % (s[2], s[3] + 1)].add(s[0])  # a member called with its object as an operand
        any_count[s[2]].add(s[0])
    for s in summaries.values():
        for h, w, usrs, argsrc, obj in s[7]:
            for i, u in enumerate(usrs):
                if u.startswith("name:") and u not in by_name:
                    n = u[5:].rsplit("/", 1)[0]
                    if n in any_count:
                        by_name[u] = any_count[n]
                    else:
                        usrs[i] = "<indirect>"

    def targets(usr, seen=None):
        """usr and every override below it."""
        seen = seen or set()
        if usr.startswith("name:"):
            for u in by_name.get(usr, ()):
                targets(u, seen)
            return seen
        if usr in seen:
            return seen
        seen.add(usr)
        for o in over.get(usr, ()):
            targets(o, seen)
        return seen

    # fixpoint: keeps[(usr, pos)] = set of targets in the callee's terms; returns[(usr)] = set of positions
    keeps = collections.defaultdict(set)
    returns = collections.defaultdict(set)
    changed = True
    rounds = 0

    def resolve(srcs, s, call_res):
        out = set()
        for x in srcs:
            if x[0] == "call":
                out |= call_res.get(x[1], set())
            else:
                out.add(x)
        return out

    def call_results(s):
        res = {}
        for h, w, usrs, argsrc, obj in s[7]:
            r = set()
            for u in usrs:
                for t in targets(u):
                    for pos in returns.get(t, ()):
                        if pos == "this" and obj:
                            r |= obj
                        elif isinstance(pos, int) and pos < len(argsrc):
                            r |= argsrc[pos]
            res[h] = r
        # nested call results inside arguments: one more pass
        for _ in range(2):
            for h, w, usrs, argsrc, obj in s[7]:
                res[h] = resolve(res[h], s, res)
        return res

    while changed and rounds < 50:
        changed = False
        rounds += 1
        for s in summaries.values():
            usr, nparams = s[0], s[3]
            cr = call_results(s)

            def note_keep(src_set, target):
                nonlocal changed
                for x in src_set:
                    if x[0] == "p":
                        key = (usr, x[1])
                    elif x[0] == "this":
                        key = (usr, "this")
                    else:
                        continue
                    if target not in keeps[key]:
                        keeps[key].add(target)
                        changed = True

            for target, src, w in s[5]:
                src = resolve(src, s, cr)
                tgt = target if target[0] in ("g", "p", "this", "heap") else None
                if tgt is not None:
                    note_keep(src, tgt if tgt[0] != "heap" else ("g",))
            for src in s[6]:
                src = resolve(src, s, cr)
                for x in src:
                    pos = x[1] if x[0] == "p" else ("this" if x[0] == "this" else None)
                    if pos is not None and pos not in returns[usr]:
                        returns[usr].add(pos)
                        changed = True
            for h, w, usrs, argsrc, obj in s[7]:
                argsrc = [resolve(a, s, cr) for a in argsrc]
                objr = resolve(obj, s, cr) if obj else set()
                for u in usrs:
                    for t in targets(u):
                        for pos in list(range(len(argsrc))) + ["this"]:
                            for tg in list(keeps.get((t, pos), ())):
                                src = objr if pos == "this" else argsrc[pos]
                                # the callee's target in this function's terms
                                if tg[0] == "g":
                                    mapped = {("g",)}
                                elif tg[0] == "this":
                                    mapped = objr
                                elif tg[0] == "p":
                                    mapped = argsrc[tg[1]] if tg[1] < len(argsrc) else {("g",)}
                                else:
                                    mapped = {("g",)}
                                for m in mapped:
                                    if m[0] in ("g", "p", "this", "heap"):
                                        note_keep(src, m if m[0] != "heap" else ("g",))
    print("fixpoint rounds:", rounds, "keeping (fn, param):", len(keeps), file=sys.stderr)

    # report: a local's address reaching storage that outlives it
    report = []
    unresolved = collections.Counter()
    for s in summaries.values():
        usr, locs = s[0], s[4]
        cr = call_results(s)

        def lifetime_ok(local, holder):
            """holder is a local too: fine when it lives no longer than local (same or inner block)."""
            if holder not in locs or local not in locs:
                return holder not in locs and local not in locs
            lo, hi = locs[local][3]
            ho, hh = locs[holder][3]
            return lo <= ho and hh <= hi

        def check(src, target, w, how):
            for x in src:
                if x[0] not in ("L", "Lp"):
                    continue
                if target[0] == "L":
                    if x[0] == "L" and lifetime_ok(x[1], target[1]):
                        continue
                    if isinstance(target[1], tuple):
                        continue  # a temporary holder
                name = locs[x[1]][0] if x[0] == "L" and x[1] in locs else ("param #%s" % (x[1],))
                decl = locs[x[1]][1] if x[0] == "L" and x[1] in locs else s[1]
                scoped = locs[x[1]][2] if x[0] == "L" and x[1] in locs else False
                report.append((w, s[2], name, decl, "block" if scoped else "function", how, str(target)))

        for target, src, w in s[5]:
            src = resolve(src, s, cr)
            check(src, target, w, "store")
        for h, w, usrs, argsrc, obj in s[7]:
            argsrc = [resolve(a, s, cr) for a in argsrc]
            objr = resolve(obj, s, cr) if obj else set()
            if "<indirect>" in usrs:
                for pos, a in enumerate(argsrc):
                    check(a, ("g",), w, "indirect call arg %d" % pos)
                continue
            for u in usrs:
                if u not in summaries and not targets(u) and any(x[0] in ("L", "Lp") for a in argsrc for x in a):
                    unresolved[u + "  " + w] += 1
                for t in targets(u):
                    for pos in list(range(len(argsrc))) + ["this"]:
                        for tg in keeps.get((t, pos), ()):
                            src = objr if pos == "this" else argsrc[pos]
                            if tg[0] == "g":
                                mapped = {("g",)}
                            elif tg[0] == "this":
                                mapped = objr or {("g",)}
                            elif tg[0] == "p":
                                mapped = argsrc[tg[1]] if tg[1] < len(argsrc) else {("g",)}
                            else:
                                mapped = {("g",)}
                            for m in mapped:
                                check(src, m, w, "call %s arg %s -> %s" % (summaries[t][2] if t in summaries else u, pos, tg))
    report = sorted(set(report))
    with open(out, "w", encoding="utf-8") as o:
        for r in report:
            o.write("%s  in %s: %s (%s, %s scope) %s into %s\n" % (r[0], r[1], r[2], r[3], r[4], r[5], r[6]))
    print("sites:", len(report), file=sys.stderr)
    with open(out + ".keeps", "w", encoding="utf-8") as o:
        for (u, pos), tg in sorted(keeps.items(), key=lambda kv: (summaries[kv[0][0]][1] if kv[0][0] in summaries else "", str(kv[0][1]))):
            if u in summaries:
                o.write("%s %s arg %s -> %s\n" % (summaries[u][1], summaries[u][2], pos, sorted(map(str, tg))))
    with open(out + ".unresolved", "w", encoding="utf-8") as o:
        for u, n in unresolved.most_common():
            o.write("%6d %s\n" % (n, u))


if __name__ == "__main__":
    main()
