"""gen_lumacore.py NEBULA_ROOT [--objects GAME_BUILD_DIR] [--check]: LumaCore's generated layers, from the game's headers.

Writes, under native/petari/src/Game/System/generated/:
  lumacore_class_ids.hpp  (in include/Game/System/generated) kClassId<T> for every class deriving from NameObj (the factory's createNameObj<T> reads it)
  lumacore_classes.cpp    the class rows (name, parent, size, cast); the compiler checks every parent
  lumacore_fields.cpp     each class's own fields (names and types from clang's record layouts, offsets from offsetof)
  lumacore_toolkit.cpp    a thunk for every MR:: utility, the listed classes' public methods and the plugin API whose
                          types map to LumaCore values, and the table of them, sorted by name
  lumacore_hash.txt       the hash of what was read; --check compares it, so the generated files can't drift
and docs/modding/TOOLKIT.md, FIELDS.md and CLASSES.md (every row, and what was left out and why).

--objects: the game module's build folder. A function the headers declare but no object defines (and no header
defines inline) is left out, so the toolkit never names a symbol the game lacks. Without it, every declared function
is offered (a missing one then fails the link, which names it).

No build of the game runs this: the outputs are committed.
"""
import hashlib
import json
import os
import pathlib
import re
import subprocess
import sys
import tempfile

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import gen_classes  # noqa: E402
import gen_fields  # noqa: E402

ROOT = pathlib.Path(sys.argv[1]).resolve()
CHECK = '--check' in sys.argv
OBJECTS = None
if '--objects' in sys.argv:
    OBJECTS = pathlib.Path(sys.argv[sys.argv.index('--objects') + 1])
NATIVE = ROOT / 'native'
PETARI = NATIVE / 'petari'
INC = PETARI / 'include'
OUT = PETARI / 'src' / 'Game' / 'System' / 'generated'
DOCS = ROOT / 'docs' / 'modding'
LLVM = pathlib.Path(os.environ.get('LUMACORE_LLVM', r'C:\Program Files\LLVM\bin'))
CLANG = str(LLVM / 'clang++.exe') if (LLVM / 'clang++.exe').exists() else str(LLVM / 'clang++')
NM = str(LLVM / 'llvm-nm.exe') if (LLVM / 'llvm-nm.exe').exists() else str(LLVM / 'llvm-nm')
HEADER = gen_classes.HEADER

# Classes whose public methods are offered beside MR:: (the ones mods meet most). Grown with the domains.
METHOD_CLASSES = ['NameObj', 'LiveActor', 'HitSensor', 'NPCActor', 'MarioActor', 'LayoutActor', 'AreaObj',
                  'MapObjActor', 'PartsModel', 'ModelObj']

# Value kinds (nebula_mod_api.h NebulaKind) and their letters in a row's signature.
K_NONE, K_BOOL, K_INT, K_FLOAT, K_VEC3, K_MATRIX, K_TEXT, K_HANDLE, K_VEC4, K_VEC2 = range(10)
KIND_NAME = ['none', 'bool', 'int', 'float', 'vec3', 'matrix', 'text', 'handle', 'vec4', 'vec2']
LETTERS = 'abcdefghij'

INTS = {'char', 'signed char', 'unsigned char', 'short', 'unsigned short', 'int', 'unsigned int', 'long',
        'unsigned long', 's8', 'u8', 's16', 'u16', 's32', 'u32', 'int8_t', 'uint8_t', 'int16_t', 'uint16_t', 'int32_t',
        'uint32_t'}
FLOATS = {'float', 'double', 'f32', 'f64'}
VEC3 = {'Vec3f', 'Vec', 'Vec3<f32>', 'Vec3<float>', 'nebula::Vec3<f32>', 'nebula::Vec3<float>'}
# Handle types besides NameObj's classes: name -> lumacore::HandleType.
HANDLE_TYPES = {'HitSensor': 'HandleType_Sensor', 'Triangle': 'HandleType_Triangle',
                'CollisionParts': 'HandleType_CollisionParts', 'TalkMessageCtrl': 'HandleType_TalkCtrl',
                'ActorCameraInfo': 'HandleType_CameraInfo', 'TableIter': 'HandleType_PlacementRow'}
VEC2 = {'Vec2f', 'Vec2<f32>', 'Vec2<float>'}
VEC4 = {'Quat4f', 'Vec4f'}
MATRICES = {'Mtx34f', 'Rot3f', 'Pos3f'}
# Types never offered as handles: what a script can't hold or make sense of.
NOT_HANDLES = {'void', 'char', 'char16_t', 'wchar_t', 'MR::FunctorBase', 'FunctorBase', 'JKRHeap', 'JKRArchive',
               'JKRExpHeap', 'JKRSolidHeap'}
OPAQUE = {}  # opaque handle type name -> its tag (filled as rows are read; written to the toolkit file)


def strip(t):
    t = re.sub(r'\b(const|volatile|struct|class|enum)\b', ' ', t)
    return ' '.join(t.split())


class Param:
    """One parameter: kind, role ('in' or 'inout'), how it is passed ('value', 'ptr', 'ref'), its C++ type."""

    def __init__(self, kind, role, passing, base, name, has_default):
        self.kind, self.role, self.passing, self.base = kind, role, passing, base
        self.name, self.has_default = name, has_default


def opaque_tag(name):
    if name not in OPAQUE:
        OPAQUE[name] = len(OPAQUE) + 1
    return OPAQUE[name]


def is_type_name(b):
    return re.match(r'^[A-Za-z_][\w:]*(<[^<>]*>)?$', b) is not None and b not in NOT_HANDLES


def classify(qual, desugared, enums, classes):
    """A parameter type -> (kind, role, passing, base) or None."""
    t = qual.strip()
    if t.endswith('&&') or t.endswith('**') or '[' in t:
        return None
    if t in ('MtxPtr', 'Mtx', 'f32 (*)[4]', 'float (*)[4]'):
        return (K_MATRIX, 'inout', 'mtxptr', 'Mtx34f')
    if t in ('const MtxPtr',):
        return (K_MATRIX, 'in', 'mtxptr', 'Mtx34f')
    if '(' in t:
        return None
    ref = t.endswith('&')
    ptr = t.endswith('*')
    const = re.search(r'\bconst\b', t.rstrip('&*')) is not None
    base = strip(t.rstrip('&*').strip())
    dbase = strip((desugared or t).rstrip('&*').strip())
    passing = 'ptr' if ptr else 'ref' if ref else 'value'
    role = 'in' if passing == 'value' or const else 'inout'
    for b in (base, dbase):
        if b == 'bool':
            return (K_BOOL, role, passing, 'bool')
        if b in ('char', 'char16_t') and ptr and const:
            return (K_TEXT, 'in', 'value', b)
        if (b in INTS or b in enums) and b != 'char':
            return (K_INT, role, passing, base)
        if b in FLOATS:
            return (K_FLOAT, role, passing, base)
        if b in VEC3:
            return (K_VEC3, role, passing, 'Vec3f')
        if b in VEC2:
            return (K_VEC2, role, passing, 'Vec2f')
        if b in VEC4:
            return (K_VEC4, role, passing, b)
        if b in MATRICES:
            return (K_MATRIX, role, passing, b)
    if ptr or ref:
        for b in (base, dbase):
            if b in classes or b in HANDLE_TYPES:
                return (K_HANDLE, 'in', passing, b)
        if is_type_name(base) and base not in INTS and base not in FLOATS:
            opaque_tag(base)
            return (K_HANDLE, 'in', passing, base)
    return None


def result_kind(qual, desugared, enums, classes):
    """-> (kind, how, base): how is 'value', 'deref' (a pointer to a value: none when null) or 'view' (a matrix of the
    game's, by address). (False, why) when it can't be offered; (None, None) for void."""
    t = qual.strip()
    if t == 'void':
        return None, None, None
    if t in ('MtxPtr', 'f32 (*)[4]', 'float (*)[4]'):
        return K_MATRIX, 'view', 'Mtx34f'
    if '(' in t or '[' in t or t.endswith('**') or t.endswith('&&'):
        return False, f'result type {qual}', None
    base = strip(t.rstrip('&*').strip())
    dbase = strip((desugared or t).rstrip('&*').strip())
    ptr = t.endswith('*')
    ref = t.endswith('&')
    for b in (base, dbase):
        if b == 'bool' and not ptr:
            return K_BOOL, 'value', 'bool'
        if (b in INTS or b in enums) and not ptr and b != 'char':
            return K_INT, 'value', base
        if b in FLOATS and not ptr:
            return K_FLOAT, 'value', base
        if b == 'char' and ptr and t.startswith('const'):
            return K_TEXT, 'value', 'char'
        if b in VEC3:
            return K_VEC3, 'deref' if ptr else 'value', 'Vec3f'
        if b in VEC2:
            return K_VEC2, 'deref' if ptr else 'value', 'Vec2f'
        if b in VEC4:
            return K_VEC4, 'deref' if ptr else 'value', b
        if b in MATRICES:
            return K_MATRIX, 'view' if (ptr or ref) else 'value', b
    if ptr or ref:
        for b in (base, dbase):
            if b in classes or b in HANDLE_TYPES:
                return K_HANDLE, 'ref' if ref else 'value', b
        if is_type_name(base) and base not in INTS and base not in FLOATS:
            opaque_tag(base)
            return K_HANDLE, 'ref' if ref else 'value', base
    return False, f'result type {qual}', None


def clang_args():
    return [CLANG, '-x', 'c++', '-std=gnu++17', '-fsyntax-only', '-fno-rtti', '-fno-exceptions', '-fno-ms-compatibility',
            '-fno-delayed-template-parsing', '-Wno-everything',
            '-DNEBULA_GAME_MODULE=1', '-DNEBULA_VERSION="0"', '-DRMGE01=1', '-DRMGJ01=0', '-DRMGK01=3', '-DRMGP01=2',
            '-DVERSION=1', '-D_HAS_EXCEPTIONS=0', '-D_ACRTIMP=', '-include', 'nebula_prelude.h',
            f'-I{NATIVE / "math" / "include"}', f'-I{INC}', f'-I{NATIVE / "include"}', f'-I{PETARI / "src"}']


def run_clang(extra, tu):
    out = subprocess.run(clang_args() + extra + [str(tu)], capture_output=True, check=False)
    if out.returncode != 0:
        sys.stderr.write(out.stderr.decode('utf-8', 'replace')[:6000])
        raise SystemExit(f'clang failed ({out.returncode}) on {tu.name}')
    return out.stdout.decode('utf-8', 'replace')


def ast_json(tu, filter_name):
    text = run_clang(['-Xclang', '-ast-dump=json', '-Xclang', f'-ast-dump-filter={filter_name}'], tu)
    # One JSON object per match, one after another (each may follow a "Dumping <name>:" line).
    text = re.sub(r'^Dumping [^\n]*:\n', '', text, flags=re.M)
    decoder = json.JSONDecoder()
    objects = []
    at = 0
    while True:
        while at < len(text) and text[at] in ' \t\r\n':
            at += 1
        if at >= len(text):
            break
        obj, at = decoder.raw_decode(text, at)
        objects.append(obj)
    return objects


def defined_symbols():
    """Mangled names every game object file defines, or None without --objects."""
    if OBJECTS is None:
        return None
    objs = [str(p) for p in OBJECTS.rglob('*.obj') if 'smg_game.dir' in p.as_posix()]
    objs += [str(p) for p in OBJECTS.rglob('*.o') if 'smg_game.dir' in p.as_posix()]
    names = set()
    for i in range(0, len(objs), 200):
        out = subprocess.run([NM, '--defined-only', '--just-symbol-name'] + objs[i:i + 200], capture_output=True,
                             check=False)
        for line in out.stdout.decode('utf-8', 'replace').splitlines():
            line = line.strip()
            if line and not line.endswith(':'):
                names.add(line)
    return names


def enum_names():
    names = set()
    for h in INC.rglob('*.hpp'):
        for m in re.finditer(r'\benum\s+(?:class\s+|struct\s+)?([A-Za-z_]\w*)', h.read_text(encoding='utf-8', errors='replace')):
            names.add(m.group(1))
    return names


def return_type(decl):
    """The result type from a function type's text: everything before the parameter list (a function pointer or an
    array type as a result is left whole, and the caller refuses it)."""
    t = decl['type']['qualType']
    depth = 0
    for i in range(len(t) - 1, -1, -1):
        c = t[i]
        if c == ')':
            depth += 1
        elif c == '(':
            depth -= 1
            if depth == 0:
                return t[:i].strip()
    return t


def has_body(decl):
    return any(c.get('kind') == 'CompoundStmt' for c in decl.get('inner', []))


def params_of(decl, enums, classes):
    ps = []
    for p in decl.get('inner', []):
        if p.get('kind') != 'ParmVarDecl':
            continue
        t = p['type']
        c = classify(t['qualType'], t.get('desugaredQualType'), enums, classes)
        if c is None:
            return None, f"parameter type {t['qualType']}"
        ps.append(Param(*c, p.get('name', ''), 'init' in p))
    return ps, None


def toolkit_rows(enums, class_names, defined):
    rows, left = [], []
    with tempfile.TemporaryDirectory() as tmp:
        tu = pathlib.Path(tmp) / 'lumacore_toolkit_tu.cpp'
        headers = sorted((INC / 'Game' / 'Util').glob('*.hpp'))
        method_headers = set()
        for q, _, rel, _ in CLASS_ROWS:
            if q in METHOD_CLASSES:
                method_headers.add(INC / rel)
        tu.write_text(''.join(f'#include "{h.relative_to(INC).as_posix()}"\n' for h in headers + sorted(method_headers)))
        funcs = ast_json(tu, 'MR::')
        records = {}
        for name in METHOD_CLASSES:
            for obj in ast_json(tu, name):
                if obj.get('kind') == 'CXXRecordDecl' and obj.get('name') == name and obj.get('completeDefinition'):
                    records[name] = obj
    seen = set()
    for f in funcs:
        if f.get('kind') != 'FunctionDecl' or f.get('isImplicit') or f.get('name') is None:
            continue
        key = f.get('mangledName') or (f['name'] + f['type']['qualType'])
        if key in seen:
            continue
        seen.add(key)
        name = 'MR::' + f['name']
        if f.get('variadic') or '...' in f['type']['qualType']:
            left.append((name, 'takes a variable number of arguments'))
            continue
        if defined is not None and not has_body(f) and f.get('mangledName') not in defined:
            left.append((name, 'declared, never defined'))
            continue
        ps, bad = params_of(f, enums, class_names)
        ret = return_type(f)
        if bad is None:
            rk, rhow, rbase = result_kind(ret, None, enums, class_names)
            if rk is False:
                bad = rhow
        if bad:
            left.append((name, bad))
            continue
        rows.append({'name': name, 'call': f'MR::{f["name"]}', 'self': None, 'params': ps, 'rk': rk, 'rhow': rhow,
                     'rbase': rbase, 'rtype_ptr': ret.endswith('*') or ret in ('MtxPtr',)})
    for cls, rec in records.items():
        access = 'private' if rec.get('tagUsed') == 'class' else 'public'
        for m in rec.get('inner', []):
            if m.get('kind') == 'AccessSpecDecl':
                access = m.get('access')
                continue
            if m.get('kind') != 'CXXMethodDecl' or access != 'public' or m.get('isImplicit'):
                continue
            mname = m.get('name', '')
            if mname.startswith('operator') or m.get('explicitlyDeleted'):
                continue
            name = f'{cls}::{mname}'
            if m.get('variadic') or '...' in m['type']['qualType']:
                left.append((name, 'takes a variable number of arguments'))
                continue
            if defined is not None and not has_body(m) and m.get('mangledName') not in defined and not m.get('virtual'):
                left.append((name, 'declared, never defined'))
                continue
            if m.get('pure'):
                pass  # called through the object: its own class defines it
            ps, bad = params_of(m, enums, class_names)
            ret = return_type(m)
            if bad is None:
                rk, rhow, rbase = result_kind(ret, None, enums, class_names)
                if rk is False:
                    bad = rhow
            if bad:
                left.append((name, bad))
                continue
            static = m.get('storageClass') == 'static'
            rows.append({'name': name, 'call': f'::{cls}::{mname}' if static else mname,
                         'self': None if static else cls, 'params': ps, 'rk': rk, 'rhow': rhow, 'rbase': rbase,
                         'rtype_ptr': ret.endswith('*') or ret in ('MtxPtr',)})
    # Overloads: each gets its parameter letters after ':'; a name with one row keeps the plain name too.
    by_name = {}
    for r in rows:
        by_name.setdefault(r['name'], []).append(r)
    final = []
    for name, group in by_name.items():
        for r in group:
            r['sig'] = ('h' if r['self'] else '') + ''.join(
                LETTERS[p.kind].upper() if p.role == 'inout' else LETTERS[p.kind] for p in r['params'])
            r['res'] = LETTERS[r['rk']] if r['rk'] else ''
            r['row'] = name if len(group) == 1 else f'{name}:{r["sig"]}'
        names = {r['row'] for r in group}
        if len(names) != len(group):
            for r in group:
                left.append((r['row'], 'two overloads with the same value kinds'))
            continue
        final.extend(group)
    final.sort(key=lambda r: r['row'].encode('utf-8'))
    return final, left


def conv_expr(p, value):
    """A NebulaValue expression -> the C++ value of the parameter's type."""
    if p.kind == K_BOOL:
        return f'lumacore::asBool({value})'
    if p.kind == K_INT:
        return f'static_cast< {p.base} >(lumacore::asInt({value}))'
    if p.kind == K_FLOAT:
        return f'static_cast< {p.base} >(lumacore::asFloat({value}))'
    if p.kind == K_VEC3:
        return f'lumacore::asVec3({value})'
    if p.kind == K_VEC2:
        return f'lumacore::asVec2({value})'
    if p.kind == K_VEC4:
        return f'lumacore::asVec4< {p.base} >({value})'
    if p.kind == K_MATRIX:
        return f'lumacore::asMatrix< {p.base} >({value})'
    if p.kind == K_TEXT:
        return f'lumacore::asText({value})' if p.base == 'char' else f'lumacore::asText16({value})'
    raise ValueError(p.kind)


def out_expr(kind, base, value):
    if kind == K_BOOL:
        return f'lumacore::fromBool({value})'
    if kind == K_INT:
        return f'lumacore::fromInt(static_cast< s64 >({value}))'
    if kind == K_FLOAT:
        return f'lumacore::fromFloat(static_cast< f64 >({value}))'
    if kind == K_VEC3:
        return f'lumacore::fromVec3({value})'
    if kind == K_VEC2:
        return f'lumacore::fromVec2({value})'
    if kind == K_VEC4:
        return f'lumacore::fromVec4({value})'
    if kind == K_MATRIX:
        return f'lumacore::fromMatrix(&({value}))'
    if kind == K_TEXT:
        return f'lumacore::fromText({value})'
    if kind == K_HANDLE:
        if base in HANDLE_TYPES:
            return f'lumacore::handleOf(const_cast< void* >(static_cast< const void* >({value})), lumacore::{HANDLE_TYPES[base]})'
        if base in OPAQUE:
            return f'lumacore::opaqueHandle(static_cast< const void* >({value}), {OPAQUE[base]})'
        return f'lumacore::handle(const_cast< NameObj* >(static_cast< const NameObj* >({value})))'
    raise ValueError(kind)


def handle_expr(base, a):
    if base in HANDLE_TYPES:
        return f'static_cast< ::{base}* >(lumacore::typed({a}, lumacore::{HANDLE_TYPES[base]}))'
    if base in OPAQUE:
        return f'static_cast< {base}* >(lumacore::opaque({a}, {OPAQUE[base]}))'
    return f'lumacore::as< ::{base} >({a})'


def emit_toolkit(rows):
    headers = sorted(h.relative_to(INC).as_posix() for h in (INC / 'Game' / 'Util').glob('*.hpp'))
    # Every class a row takes or gives as a handle is complete here (its casts need it).
    used = set(METHOD_CLASSES)
    for r in rows:
        used.update(p.base for p in r['params'] if p.kind == K_HANDLE)
        if r['rk'] == K_HANDLE:
            used.add(r['rbase'])
    extra = sorted({rel for (q, _, rel, _) in CLASS_ROWS if q in used} - set(headers))
    for name in HANDLE_TYPES:
        if name in used:
            for h in sorted(INC.rglob('*.hpp')):
                if re.search(rf'^\s*(class|struct)\s+{name}\b[^;]*$', h.read_text(encoding='utf-8', errors='replace'), re.M):
                    rel = h.relative_to(INC).as_posix()
                    if rel not in extra and rel not in headers:
                        extra.append(rel)
                    break
    lines = [HEADER, "// Every MR:: utility and the listed classes' public methods: one thunk shape for scripts and plugins.",
             '', '#include "Game/System/LumaCore.hpp"', '#include "Game/LiveActor/HitSensor.hpp"']
    lines += [f'#include "{h}"' for h in headers + extra]
    lines += ['', '#pragma clang diagnostic ignored "-Wunused-variable"', '',
              "// Inside MR: a type the headers spell as MR's own code does (FormattedName) resolves the same.",
              'namespace MR {', 'namespace {']
    for i, r in enumerate(rows):
        ps = r['params']
        offset = 1 if r['self'] else 0
        required = offset + sum(1 for p in ps if not p.has_default and p.role != 'inout')
        lines.append(f'    // {r["row"]}')
        lines.append(f'    void lc{i}(const NebulaValue* a, uint32_t n, NebulaValue* o) {{')
        lines.append(f'        if (n < {required}u) {{ lumacore::refuse({i}, "too few arguments"); return; }}')
        if r['self']:
            lines.append(f'        ::{r["self"]}* const self = {handle_expr(r["self"], "a[0]")};')
            lines.append(f'        if (self == nullptr) {{ lumacore::refuse({i}, "not a {r["self"]}"); return; }}')
        args = []
        outs = []
        for j, p in enumerate(ps):
            k = j + offset
            given = f'(n > {k}u ? a[{k}] : lumacore::kNone)'
            if p.kind == K_HANDLE:
                star = f'{p.base}*' if p.base in OPAQUE else f'::{p.base}*'
                lines.append(f'        {star} const p{j} = n > {k}u ? {handle_expr(p.base, f"a[{k}]")} : nullptr;')
                if not p.has_default:
                    lines.append(f'        if (p{j} == nullptr) {{ lumacore::refuse({i}, "argument {k + 1}: no {p.base}"); return; }}')
                args.append(f'*p{j}' if p.passing == 'ref' else f'p{j}')
            elif p.kind == K_TEXT:
                lines.append(f'        const {p.base}* const p{j} = {conv_expr(p, given)};')
                args.append(f'p{j}')
            else:
                lines.append(f'        {p.base} p{j} = {conv_expr(p, given)};')
                args.append(f'&p{j}' if p.passing == 'ptr' else f'p{j}.mMtx' if p.passing == 'mtxptr' else f'p{j}')
                if p.role == 'inout':
                    outs.append((p.kind, p.base, f'p{j}'))
        firsts = [j for j, p in enumerate(ps) if p.has_default]
        counts = sorted(set([len(ps)] + firsts))
        target = f'self->{r["call"]}' if r['self'] else r['call']

        def store(count):
            call = f'{target}({", ".join(args[:count])})'
            rk, how, base = r['rk'], r['rhow'], r['rbase']
            if not rk:
                return f'{call};'
            if how == 'deref':
                return f'{{ const auto* const r = {call}; o[0] = r != nullptr ? {out_expr(rk, base, "*r")} : lumacore::kNone; }}'
            if how == 'view':
                return f'o[0] = lumacore::matrixView({call});' if r['rtype_ptr'] else f'o[0] = lumacore::matrixView(&({call}));'
            if how == 'ref':
                return f'o[0] = {out_expr(rk, base, f"&({call})")};'
            if rk == K_MATRIX:
                return f'{{ const {base} r = {call}; o[0] = lumacore::fromMatrix(&r); }}'
            return f'o[0] = {out_expr(rk, base, call)};'

        if len(counts) == 1:
            lines.append(f'        {store(len(ps))}')
        else:
            ordered = list(reversed(counts))
            for idx, count in enumerate(ordered):
                if idx == 0:
                    lines.append(f'        if (n >= {count + offset}u) {{')
                elif idx == len(ordered) - 1:
                    lines.append('        } else {')
                else:
                    lines.append(f'        }} else if (n >= {count + offset}u) {{')
                lines.append(f'            {store(count)}')
            lines.append('        }')
        for k, (kind, base, var) in enumerate(outs):
            lines.append(f'        o[{k + (1 if r["rk"] else 0)}] = {out_expr(kind, base, var)};')
        lines.append('    }')
    lines += ['}  // namespace', '}  // namespace MR', '', 'namespace lumacore {', '    extern const ToolkitRow kToolkitRows[];',
              '    extern const u32 kToolkitRowCount;', '    extern const char* const kOpaqueNames[];',
              '    extern const u32 kOpaqueCount;', '    const ToolkitRow kToolkitRows[] = {']
    for i, r in enumerate(rows):
        lines.append(f'        {{"{r["row"]}", "{r["sig"]}", "{r["res"]}", MR::lc{i}}},')
    lines += ['    };', f'    const u32 kToolkitRowCount = {len(rows)};',
              '    // The opaque handles\' types by tag (tag 0 is none).', '    const char* const kOpaqueNames[] = {', '        "",']
    for name, tag in sorted(OPAQUE.items(), key=lambda x: x[1]):
        lines.append(f'        "{name}",')
    lines += ['    };', f'    const u32 kOpaqueCount = {len(OPAQUE) + 1};', '}  // namespace lumacore', '']
    return '\n'.join(lines)


def emit_docs(rows, left, class_rows, field_count, layouts):
    DOCS.mkdir(parents=True, exist_ok=True)
    out = ['# Toolkit', '', "Every game function a script or plugin calls by name, generated from the game's headers "
           '(tools/lumacore). Letters: a none, b bool, c int, d float, e vec3, g text, h handle (an object, a sensor...); '
           'an upper-case letter is a value that comes back after the result (pass one in, or none for zero).', '',
           f'{len(rows)} functions.', '', '| Name | Takes | Gives |', '|---|---|---|']
    for r in rows:
        takes = ', '.join(([f'h self ({r["self"]})'] if r['self'] else []) + [
            f"{LETTERS[p.kind].upper() if p.role == 'inout' else LETTERS[p.kind]} {p.name}{'?' if p.has_default else ''}"
            for p in r['params']])
        out.append(f'| `{r["row"]}` | {takes} | {r["res"]} |')
    out += ['', '## Left out', '', 'What the toolkit cannot offer yet, and why.', '', '| Name | Why |', '|---|---|']
    out += [f'| `{n}` | {w} |' for n, w in sorted(left)]
    (DOCS / 'TOOLKIT.md').write_text('\n'.join(out) + '\n', encoding='utf-8')
    names = {q for (q, _, _, _) in class_rows}
    cls = ['# Classes', '', f'{len(class_rows)} classes derive from NameObj. `like` names one of them; scripts '
           'check an object with `is(object, "Kuribo")`.', '', '| Class | Parent | Header |', '|---|---|---|']
    cls += [f'| {q} | {p or ""} | {rel} |' for (q, p, rel, _) in class_rows]
    (DOCS / 'CLASSES.md').write_text('\n'.join(cls) + '\n', encoding='utf-8')
    fl = ['# Fields', '', f'{field_count} fields a script reads (`object.mVelocity`) or writes. Handles, text and '
          'pointers are read-only; a class has its bases\' fields too.', '']
    for q, _, _, _ in class_rows:
        own = sorted({f for f in layouts.get(q, []) if gen_fields.field_wanted(f[1], names)}, key=lambda f: f[0])
        if own:
            fl.append(f'## {q}')
            fl.append('')
            fl += [f'- `{n}`: {t}{" (array)" if arr else ""}' for (n, t, arr) in own]
            fl.append('')
    (DOCS / 'FIELDS.md').write_text('\n'.join(fl) + '\n', encoding='utf-8')


CLASS_ROWS = []


def main():
    global CLASS_ROWS
    inputs = sorted(list(INC.rglob('*.hpp')) + list(INC.rglob('*.h'))) + [NATIVE / 'include' / 'nebula_mod_api.h',
                                                                           pathlib.Path(__file__)]
    digest = hashlib.sha256()
    for h in inputs:
        digest.update(h.as_posix().split('/native/')[-1].encode())
        digest.update(h.read_bytes().replace(b'\r\n', b'\n'))
    digest = digest.hexdigest()
    hash_file = OUT / 'lumacore_hash.txt'
    if CHECK:
        old = hash_file.read_text().strip() if hash_file.exists() else ''
        if old != digest:
            raise SystemExit('LumaCore generated files are out of date: run tools/lumacore/gen_lumacore.py')
        print('LumaCore generated files current')
        return
    CLASS_ROWS, dupes = gen_classes.classes(INC)
    class_names = {q for (q, _, _, _) in CLASS_ROWS}
    OUT.mkdir(parents=True, exist_ok=True)
    ids = INC / 'Game' / 'System' / 'generated'
    ids.mkdir(parents=True, exist_ok=True)
    (ids / 'lumacore_class_ids.hpp').write_text(gen_classes.emit_class_ids(CLASS_ROWS), encoding='utf-8')
    classes_cpp = gen_classes.emit_class_rows(CLASS_ROWS)
    (OUT / 'lumacore_classes.cpp').write_text(classes_cpp, encoding='utf-8')
    # The record layouts (field names and types): from the classes file, whose sizeof rows lay out every class.
    layout_text = run_clang(['-Xclang', '-fdump-record-layouts-complete', '-Xclang', '-fdump-record-layouts'],
                            OUT / 'lumacore_classes.cpp')
    layouts = gen_fields.parse_layouts(layout_text)
    fields_cpp, field_count = gen_fields.emit_field_rows(CLASS_ROWS, layouts)
    (OUT / 'lumacore_fields.cpp').write_text(fields_cpp, encoding='utf-8')
    rows, left = toolkit_rows(enum_names(), class_names, defined_symbols())
    (OUT / 'lumacore_toolkit.cpp').write_text(emit_toolkit(rows), encoding='utf-8')
    hash_file.write_text(digest + '\n')
    emit_docs(rows, left, CLASS_ROWS, field_count, layouts)
    print(f'classes {len(CLASS_ROWS)} ({len(dupes)} names in two headers: {", ".join(dupes)}); fields {field_count}; '
          f'toolkit {len(rows)} rows, {len(left)} left out')


if __name__ == '__main__':
    main()
