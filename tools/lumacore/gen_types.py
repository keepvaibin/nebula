"""gen_types.py --commands COMPILE_COMMANDS --out DIR [--entry FILE] [--defined DIR] [--depfile FILE] [--libclang PATH]
                [--tree DIR]

Plan 1.2 (contract types): the type rows of every record, field, base, enum, alias and global the game's headers declare.
One unit includes every game header (native/petari/include, native/math/include, native/include) and is parsed by libclang
with a game unit's own compile command (--entry, default the first game unit), so target, defines and platform headers
are the build's. Writes:
  DIR/luma_types_table.cpp  the rows the module holds (luma_types.h): sizeof, alignof, offsetof by the compiler,
                            compiled with -fno-access-control; bit-fields and bases from libclang
  DIR/luma_types.json       the self-description's `types` (each record with `reachable`), `globals` and
                            `left_out` (libclang's numbers: the build compares them with the module's --luma-describe)
  DIR/luma_types.left       what has no row, and why
Each is rewritten only when it changes.
--defined: the folder of instrument.py's copies. Each copy's .globals lists the variables its unit defines (mangled
names); a header's global gets a row only when one of them, or the header itself (an inline variable), defines it.
--depfile: the files the headers' unit read, as a Makefile rule (Ninja reads it).
What a .cpp defines (nerve classes, file-level structs) comes from instrument.py's per-unit pass (Reader, unit_pass).
The tree is the one this script is in (tools/lumacore). --tree: one folder's headers in place of the game's (the
fixture of tools/lumacore/types_fixture_test.py).
"""
import ctypes
import json
import os
import pathlib
import re
import sys
from ctypes import byref, c_char_p, c_int, c_longlong, c_uint, c_void_p

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import instrument as I  # noqa: E402

ARGS = sys.argv[1:]


def option(flag, default=None):
    if flag in ARGS:
        return ARGS[ARGS.index(flag) + 1]
    return default


ROOT = pathlib.Path(__file__).resolve().parents[2]
GAME_INCLUDES = [ROOT / "native" / "petari" / "include", ROOT / "native" / "math" / "include", ROOT / "native" / "include"]
GAME_TREES = [ROOT / "native" / "petari", ROOT / "native" / "math", ROOT / "native" / "include"]
# TY-5: the kernel's own headers declare nothing a mod binds through these rows.
KERNEL_FILES = re.compile(r"^native/(include/luma[a-z_]*\.h|petari/include/Game/System/Luma[A-Za-z]*\.hpp)$")

# The left_out reasons (TY-5): a fixed list, so the run's report counts by them and G's rows translate them.
R_KERNEL = "the kernel's own"
R_INTERNAL = "internal linkage: no name a mod can bind"
R_THREAD = "thread-local: one per thread, no one address"
R_NO_NAME = "no name a mod can write"
R_NO_SIZE = "no size (incomplete or dependent)"
R_AMBIGUOUS = "ambiguous: "
R_VIRTUAL = "through the virtual base "
R_NO_TYPE_NAME = "a type with no name a mod can write"
R_NOT_USED = "a template not used with these arguments"
R_WIDE_BITS = "a bit-field wider than 32 bits"
R_UNDEFINED = "declared, defined nowhere in the game"
R_TEMPLATE_MEMBER = "a static member of a class template"
R_UNIT_INSTANCE = "a template instance only a .cpp names"
R_ALIAS_TWICE = "a name .cpp files give to different types: "
# Each reason's id, the words LumaLink's rows give it (LeftOut.*, contract types TY-7): the kernel's left rows carry the
# id, so a mod refused at load reads the validator's L505. A reason with a prefix carries its names as `what`.
REASON_IDS = {R_KERNEL: "LeftOut.KernelOwn", R_INTERNAL: "LeftOut.InternalLinkage", R_THREAD: "LeftOut.ThreadLocal",
              R_NO_NAME: "LeftOut.NoName", R_NO_SIZE: "LeftOut.NoSize", R_NO_TYPE_NAME: "LeftOut.TypeNoName",
              R_NOT_USED: "LeftOut.TemplateUnused", R_WIDE_BITS: "LeftOut.WideBitField", R_UNDEFINED: "LeftOut.NotDefined",
              R_TEMPLATE_MEMBER: "LeftOut.TemplateMember", R_UNIT_INSTANCE: "LeftOut.InstanceInCpp"}
PREFIX_IDS = ((R_AMBIGUOUS, "LeftOut.Ambiguous"), (R_VIRTUAL, "LeftOut.VirtualBase"), (R_ALIAS_TWICE, "LeftOut.AliasTwice"))
# A left row's group, as the kernel looks it up (luma_types.h kLeft*): a kind 2 name among the types, then the
# templates by the name before '<'; kind 1 among the fields; kind 7 among the globals.
LEFT_GROUPS = {"record": 0, "alias": 0, "enum": 0, "field": 1, "global": 2, "template": 3}
LEFT_FIRST = {"record": 0, "alias": 1, "enum": 2}  # one name of two kinds in a group: the validator's order


def reason_id(reason):
    """(id, what) of a left_out reason; a reason without an id stops the build (it would reach no player's words)."""
    if reason in REASON_IDS:
        return REASON_IDS[reason], None
    for prefix, rid in PREFIX_IDS:
        if reason.startswith(prefix):
            return rid, reason[len(prefix):]
    sys.exit("gen_types.py: the left_out reason %r has no id (REASON_IDS)" % reason)


def left_rows(left):
    """The kernel's left rows from left_out entries (kind, name, file, line, reason): one per group and name, the first
    by LEFT_FIRST, as [(group, name, id, what)] sorted."""
    rows = {}
    for kind, name, _, _, reason in sorted(left, key=lambda x: (LEFT_FIRST.get(x[0], 0), str(x))):
        if kind not in LEFT_GROUPS:
            sys.exit("gen_types.py: a left_out kind %r has no group (LEFT_GROUPS)" % kind)
        rows.setdefault((LEFT_GROUPS[kind], name), reason_id(reason))
    return sorted((g, n, rid, what) for (g, n), (rid, what) in rows.items())
K_ENUM, K_FIELD, K_ENUM_CONST, K_VAR, K_METHOD, K_TYPEDEF, K_ALIAS, K_BASE = 5, 6, 7, 9, 21, 20, 36, 44
K_ACCESS = 39
RECORDS = (I.K_STRUCT, I.K_UNION, I.K_CLASS)
KIND_TEXT = {I.K_STRUCT: "struct", I.K_UNION: "union", I.K_CLASS: "class"}
T_CONSTANT_ARRAY, T_RECORD, T_ELABORATED = 112, 105, 119
T_POINTER, T_LVALUE, T_RVALUE, T_INCOMPLETE_ARRAY, T_MEMBER_POINTER = 101, 103, 104, 114, 117
T_TYPEDEF = 107
LINKAGE_EXTERNAL = 4
ACCESS = {1: "public", 2: "protected", 3: "private"}
LONG_BITS = 32  # set by main() from the unit's target: 32 on Windows (LLP64), 64 elsewhere (LP64)
DEFINED = None  # --defined, resolved by main(): the instrumented copies' folder
DEPFILE = None
BUILTIN = re.compile(r"\b(unsigned long long|long long|unsigned long|long double|long|unsigned int|unsigned short|"
                     r"unsigned char|signed char|short|int|unsigned|float|double)\b")


def fixed_width(text):
    """A template instance's argument text with builtin arithmetic types as the game's fixed-width names (TY-2)."""
    names = {"unsigned long long": "u64", "long long": "s64", "unsigned int": "u32", "unsigned": "u32",
             "unsigned short": "u16", "unsigned char": "u8", "signed char": "s8", "short": "s16", "int": "s32",
             "float": "f32", "double": "f64", "long double": "long double",
             "unsigned long": "u%d" % LONG_BITS, "long": "s%d" % LONG_BITS}
    return BUILTIN.sub(lambda m: names[m.group(1)], text)


MEMBER_VISITOR = ctypes.CFUNCTYPE(c_int, I.CXCursor, c_void_p)  # enum CXVisitorResult (*)(CXCursor, CXClientData)


class Api:
    def __init__(self, path):
        self.base = I.load(path)
        lib = ctypes.CDLL(path)

        def fn(name, result, *args):
            f = getattr(lib, name)
            f.restype, f.argtypes = result, list(args)
            return f

        self.sizeOf = fn("clang_Type_getSizeOf", c_longlong, I.CXType)
        self.alignOf = fn("clang_Type_getAlignOf", c_longlong, I.CXType)
        self.typeOffsetOf = fn("clang_Type_getOffsetOf", c_longlong, I.CXType, c_char_p)
        self.isBitField = fn("clang_Cursor_isBitField", c_uint, I.CXCursor)
        self.bitWidth = fn("clang_getFieldDeclBitWidth", c_int, I.CXCursor)
        self.usr = fn("clang_getCursorUSR", I.CXString, I.CXCursor)
        self.enumValue = fn("clang_getEnumConstantDeclValue", c_longlong, I.CXCursor)
        self.enumType = fn("clang_getEnumDeclIntegerType", I.CXType, I.CXCursor)
        self.scoped = fn("clang_EnumDecl_isScoped", c_uint, I.CXCursor)
        self.underlying = fn("clang_getTypedefDeclUnderlyingType", I.CXType, I.CXCursor)
        self.access = fn("clang_getCXXAccessSpecifier", c_int, I.CXCursor)
        self.anonymous = fn("clang_Cursor_isAnonymous", c_uint, I.CXCursor)
        self.anonymousRecord = fn("clang_Cursor_isAnonymousRecordDecl", c_uint, I.CXCursor)
        self.arraySize = fn("clang_getArraySize", c_longlong, I.CXType)
        self.element = fn("clang_getArrayElementType", I.CXType, I.CXType)
        self.declaration = fn("clang_getTypeDeclaration", I.CXCursor, I.CXType)
        self.canonical = fn("clang_getCanonicalType", I.CXType, I.CXType)
        self.baseOffset = fn("clang_getOffsetOfBase", c_longlong, I.CXCursor, I.CXCursor)
        self.virtualBase = fn("clang_isVirtualBase", c_uint, I.CXCursor)
        self.abstract = fn("clang_CXXRecord_isAbstract", c_uint, I.CXCursor)
        self.virtualMethod = fn("clang_CXXMethod_isVirtual", c_uint, I.CXCursor)
        self.expansionLocation = fn("clang_getExpansionLocation", None, I.CXSourceLocation, ctypes.POINTER(c_void_p),
                                    ctypes.POINTER(c_uint), ctypes.POINTER(c_uint), ctypes.POINTER(c_uint))
        self.tuSpelling = fn("clang_getTranslationUnitSpelling", I.CXString, c_void_p)
        self.constType = fn("clang_isConstQualifiedType", c_uint, I.CXType)
        self.mangling = fn("clang_Cursor_getMangling", I.CXString, I.CXCursor)
        self.tls = fn("clang_getCursorTLSKind", c_int, I.CXCursor)
        self.canonical_cursor = fn("clang_getCanonicalCursor", I.CXCursor, I.CXCursor)
        self.constexprVar = None
        self.evaluate = fn("clang_Cursor_Evaluate", c_void_p, I.CXCursor)
        self.evalKind = fn("clang_EvalResult_getKind", c_int, c_void_p)
        self.evalInt = fn("clang_EvalResult_getAsLongLong", c_longlong, c_void_p)
        self.evalDouble = fn("clang_EvalResult_getAsDouble", ctypes.c_double, c_void_p)
        self.evalDispose = fn("clang_EvalResult_dispose", None, c_void_p)
        self.pointee = fn("clang_getPointeeType", I.CXType, I.CXType)
        self.classType = fn("clang_Type_getClassType", I.CXType, I.CXType)
        self.namedType = fn("clang_Type_getNamedType", I.CXType, I.CXType)
        self.copyConstructor = fn("clang_CXXConstructor_isCopyConstructor", c_uint, I.CXCursor)
        self.moveConstructor = fn("clang_CXXConstructor_isMoveConstructor", c_uint, I.CXCursor)
        self.copyAssignment = fn("clang_CXXMethod_isCopyAssignmentOperator", c_uint, I.CXCursor)
        self.moveAssignment = fn("clang_CXXMethod_isMoveAssignmentOperator", c_uint, I.CXCursor)
        self.defaulted = fn("clang_CXXMethod_isDefaulted", c_uint, I.CXCursor)
        self.deleted = fn("clang_CXXMethod_isDeleted", c_uint, I.CXCursor)
        # The members of a type's own record, an implicit instantiation's included (its cursor has no children).
        self.visitFields = fn("clang_Type_visitFields", c_uint, I.CXType, MEMBER_VISITOR, c_void_p)
        self.visitBases = fn("clang_visitCXXBaseClasses", c_uint, I.CXType, MEMBER_VISITOR, c_void_p)
        self.visitMethods = fn("clang_visitCXXMethods", c_uint, I.CXType, MEMBER_VISITOR, c_void_p)

    def members(self, visit, t):
        """The cursors a type visitor gives, in order (copied: the callback's cursor is only valid during the call)."""
        out = []

        def take(cursor, _):
            out.append(I.CXCursor.from_buffer_copy(cursor))
            return 1  # CXVisit_Continue

        visit(t, MEMBER_VISITOR(take), None)
        return out

    def __getattr__(self, name):
        return getattr(self.base, name)


def main():
    global OUT, DEFINED, DEPFILE
    if option("--commands") is None or option("--out") is None:
        sys.exit(__doc__)
    OUT = pathlib.Path(option("--out")).resolve()
    # Paths before the chdir to the unit's folder below. A --defined folder that is not there is an error: without it
    # no global has a row and no .cpp's records are described, which must never pass unseen.
    DEFINED = pathlib.Path(option("--defined")).resolve() if option("--defined") else None
    if DEFINED is not None and not DEFINED.is_dir():
        sys.exit("gen_types.py: --defined %s is no folder" % DEFINED)
    DEPFILE = pathlib.Path(option("--depfile")).resolve() if option("--depfile") else None
    if option("--tree"):
        global GAME_INCLUDES, GAME_TREES
        GAME_INCLUDES = GAME_TREES = [pathlib.Path(option("--tree")).resolve()]
    commands = json.loads(pathlib.Path(option("--commands")).read_text(encoding="utf-8"))
    wanted = option("--entry")

    def is_game(entry):
        f = entry["file"].replace("\\", "/")
        return f.endswith(wanted.replace("\\", "/")) if wanted else f.endswith(".cpp") and "/Game/" in f

    entry = next((e for e in commands if is_game(e)), None) or sys.exit("gen_types.py: no game unit in the commands")
    raw = entry.get("arguments") or I.split_command(entry["command"])
    set_target(raw)
    OUT.mkdir(parents=True, exist_ok=True)
    headers = sorted(p for d in GAME_INCLUDES for p in d.rglob("*") if p.suffix in (".h", ".hpp"))
    unit = OUT / "luma_types_unit.cpp"  # the parse's own input, rewritten each run (the table is what the build compiles)
    unit.write_text("".join('#include "%s"\n' % h.as_posix() for h in headers), encoding="utf-8")
    args = I.parse_arguments(raw, unit)
    libclang = I.find_libclang(option("--libclang"), args[0])
    resource_dir = I.own_resource_dir(libclang, args[0])
    if resource_dir is not None and "-resource-dir" not in args:
        args = args[:1] + ["-resource-dir", resource_dir] + args[1:]
    api = Api(libclang)
    os.chdir(entry["directory"])
    index = api.createIndex(0, 0)
    tu = parse(api, index, args)
    reader = Reader(api, tu, headers)
    reader.collect()
    if reader.uninstantiated:
        # Instances named only through an alias: one sizeof each instantiates the class (not its functions), and the
        # module's table, which includes the same headers and asks sizeof, instantiates them the same way.
        forced = "".join("static_assert(sizeof(%s) > 0);\n" % t for t in sorted(reader.uninstantiated))
        unit.write_text(unit.read_text(encoding="utf-8") + forced, encoding="utf-8")
        tu = parse(api, index, args)
        reader = Reader(api, tu, headers)
        reader.collect()
    reader.write()
    if DEPFILE is not None:
        included = []

        def note(file, _stack, _depth, _data):
            included.append(reader.text(api.fileName(file)))

        api.inclusions(tu, I.INCLUSION(note), None)
        I.write_depfile(str(DEPFILE), str(OUT / "luma_types_table.cpp"), included)


def set_target(raw):
    """`long`'s width (TY-2) from a compile command's target: 32 on Windows (LLP64), 64 elsewhere (LP64)."""
    global LONG_BITS
    triple = next((a.split("=", 1)[1] for a in raw if a.startswith("--target=")), None)
    if triple is None and "-target" in raw:
        triple = raw[raw.index("-target") + 1]
    LONG_BITS = 32 if (triple is None and os.name == "nt") or (triple is not None and ("windows" in triple or "msvc" in triple)) else 64


def parse(api, index, args):
    tu = c_void_p()
    encoded = (c_char_p * len(args))(*[a.encode() for a in args])
    code = api.parse(index, None, encoded, len(args), None, 0, 0x40, byref(tu))  # function bodies skipped
    if code != 0 or not tu:
        sys.exit(f"gen_types.py: the headers could not be parsed (libclang error {code})")
    errors = []
    for i in range(api.numDiagnostics(tu)):
        d = api.diagnostic(tu, i)
        if api.severity(d) >= 3:
            text = api.formatDiagnostic(d, 0x1 | 0x2)
            errors.append(api.cstring(text).decode("utf-8", "replace"))
            api.dispose(text)
    if errors:
        sys.exit("gen_types.py: the game's headers do not parse as one unit:\n  " + "\n  ".join(errors[:20]))
    return tu


class Reader:
    """unit_pass: only what this unit's own file defines (instrument.py); else what the game's headers declare."""

    def __init__(self, api, tu, headers, unit_pass=False):
        self.api, self.tu, self.headers, self.unit_pass = api, tu, headers, unit_pass
        self.main = None  # the unit's own file, lower case with '/' (unit pass)
        self.unit = I.Unit.__new__(I.Unit)
        self.unit.api = api
        self.unit.tu = tu
        self.foreign = {}   # typedefs outside the game's trees at file scope (<stdint.h>'s): name -> cursor (TY-9)
        self.named_instances = set()  # unit pass: the instances (and their members) its types name (TY-9)
        self.trivial_memo = {}  # TY-10: canonical spelling -> trivial
        self.seen = set()
        self.records = {}   # name -> row
        self.enums = {}
        self.aliases = {}
        self.globals = {}
        self.left = []
        self.pending = []   # specializations met as a field's, base's or global's type
        self.cursors = {}   # unit pass: each record row's cursor, and each global row's ("global", name)
        self.uninstantiated = set()  # canonical spellings of instances with no size (only an alias names them)

    # ---- small readers
    def text(self, cx):
        return self.unit.string(cx)

    def at(self, cursor):
        """Where a cursor is spelled: (file, offset); a special member clang declared implicitly is at its class's
        name."""
        f, line, col, off = c_void_p(), c_uint(), c_uint(), c_uint()
        self.api.spellingLocation(self.api.location(cursor), byref(f), byref(line), byref(col), byref(off))
        return f.value, off.value

    def where(self, cursor):
        """Where a declaration is written; in the unit pass, where the unit writes it (a macro's use, not the macro: a
        pasted name is spelled in no file)."""
        f, line, col, off = c_void_p(), c_uint(), c_uint(), c_uint()
        locate = self.api.expansionLocation if self.unit_pass else self.api.spellingLocation
        locate(self.api.location(cursor), byref(f), byref(line), byref(col), byref(off))
        name = self.text(self.api.fileName(f)) if f.value else ""
        return name.replace("\\", "/"), line.value

    def label(self, file):
        """A file of the game's trees by its path from the tree's root ("native/petari/include/..."); else None."""
        for tree in GAME_TREES:
            root = str(tree).replace("\\", "/").lower() + "/"
            if file.lower().startswith(root):
                return tree.relative_to(ROOT).as_posix() + "/" + file[len(root):]
        return None

    def expanded_in(self, cursor):
        """The file a cursor's text is expanded in (a macro's use, not its definition)."""
        f, line, col, off = c_void_p(), c_uint(), c_uint(), c_uint()
        self.api.expansionLocation(self.api.location(cursor), byref(f), byref(line), byref(col), byref(off))
        return (self.text(self.api.fileName(f)) if f.value else "").replace("\\", "/").lower()

    def kernel(self, file):
        label = self.label(file)
        return label is not None and KERNEL_FILES.match(label) is not None

    def keep(self, cursor, file):
        if self.unit_pass:
            if self.main is None:
                self.main = self.text(self.api.tuSpelling(self.tu)).replace("\\", "/").lower()
            return self.expanded_in(cursor) == self.main
        return self.label(file) is not None

    def name(self, cursor):
        """The qualified name a mod writes, or None when a scope has none (an unnamed namespace, an anonymous record)."""
        parts = []
        c = cursor
        while not self.api.isNull(c) and c.kind != I.K_TRANSLATION_UNIT:
            if c.kind in RECORDS and not self.api.isNull(self.api.specialized(c)):
                parts.append(self.instance_name(self.api.type(c)))  # holds its own scope
                break
            if c.kind in (I.K_LINKAGE, I.K_UNEXPOSED_DECL):
                c = self.api.semanticParent(c)
                continue
            spelled = self.text(self.api.spelling(c))
            # Any anonymous tag or namespace: libclang spells an unnamed enum "(unnamed enum at <path>:<line>:<col>)",
            # which is no name a mod can write (and would put the build's own path in the self-description).
            if not spelled or self.api.anonymous(c):
                return None
            parts.append(spelled)
            c = self.api.semanticParent(c)
        return "::".join(reversed(parts)) if parts else None

    def type_text(self, t):
        """TY-3: instrument.py's text, the one 1.1's function rows use; None for a type no mod can write."""
        return self.unit.type_text(t)

    def instance_name(self, t):
        """TY-2: a template instance's one name: its canonical spelling, arguments in fixed-width names."""
        text = self.text(self.api.typeSpelling(self.api.canonical(t)))
        at = text.find("<")
        return text if at < 0 else text[:at] + fixed_width(text[at:])

    def spelling_alias(self, t):
        """Another spelling of a template instance written in the game's types (TY-2): an alias row to its name. The
        header pass's only: two units could write the same spelling."""
        if self.unit_pass:
            return
        decl = self.api.declaration(self.api.canonical(t))
        if decl.kind not in RECORDS or self.api.isNull(self.api.specialized(decl)):
            return
        written = self.type_text(t)
        if written is None:
            return
        written = re.sub(r"^(const |volatile )+", "", written)
        name = self.instance_name(t)
        if written != name and "<" in written and written not in self.aliases:
            self.aliases[written] = {"name": written, "kind": "spelling", "of": name, "record": name,
                                     "file": None, "line": None}

    def extents(self, t):
        out = []
        while t.kind == T_CONSTANT_ARRAY:
            out.append(self.api.arraySize(t))
            t = self.api.element(t)
        return out, t

    # ---- the walk
    def run(self):
        self.collect()
        self.write()

    def collect(self):
        self.walk(self.api.cursor(self.tu))
        while self.pending:
            self.record(self.pending.pop())

    def walk(self, cursor):
        for c in self.unit.children(cursor):
            k = c.kind
            if k in (I.K_NAMESPACE, I.K_LINKAGE, I.K_UNEXPOSED_DECL):
                self.walk(c)
                continue
            file, line = self.where(c)
            if k in (K_TYPEDEF, K_ALIAS) and not self.unit_pass and self.label(file) is None \
                    and self.api.semanticParent(c).kind in (I.K_TRANSLATION_UNIT, I.K_LINKAGE):
                # <stdint.h>, <stddef.h>: an alias row when a text names it (foreign_aliases)
                self.foreign.setdefault(self.text(self.api.spelling(c)), c)
            if not self.keep(c, file):
                continue
            if self.kernel(file):
                if k in RECORDS + (K_ENUM, K_TYPEDEF, K_ALIAS, K_VAR) and self.once(c):
                    word = "record" if k in RECORDS else "enum" if k == K_ENUM else "alias" if k != K_VAR else "global"
                    self.left.append((word, self.name(c) or "(anonymous)", self.label(file), line, R_KERNEL))
                continue
            if k in RECORDS and self.api.isDefinition(c):
                self.record(c)
            elif k == K_ENUM and self.api.isDefinition(c):
                self.enum(c)
            elif k in (K_TYPEDEF, K_ALIAS):
                self.alias(c)
            elif k == K_VAR:
                self.global_(c)
            elif k in I.FUNCTIONS:
                self.want_signature(c)
            elif k == I.K_CLASS_TEMPLATE and self.api.isDefinition(c) and not self.unit_pass and self.once(c):
                # Its instances the game uses are rows; with any other arguments it has none.
                self.left.append(("template", self.name(c) or "(anonymous)", self.label(file), line, R_NOT_USED))

    def once(self, cursor):
        u = self.text(self.api.usr(cursor))
        if u in self.seen:
            return False
        self.seen.add(u)
        return True

    def record(self, c):
        if not self.once(c):
            return
        file, line = self.where(c)
        name = self.name(c)
        if self.kernel(file):
            self.left.append(("record", name or "(anonymous)", self.label(file), line, R_KERNEL))
            return
        if name is None:
            if not self.api.anonymousRecord(c):  # an anonymous member is its owner's fields
                spelled = self.text(self.api.spelling(c))
                if not spelled or self.api.anonymous(c):
                    spelled = "(anonymous)"  # libclang's "(unnamed struct at <path>...)" holds the build's path
                self.left.append(("record", spelled, self.label(file), line, R_NO_NAME))
            return
        t = self.api.type(c)
        size = self.api.sizeOf(t)
        if size < 0:
            # An instance, or a record nested in one (clang gives a member class of an instance its pattern as
            # "specialized" too): forced by a sizeof outside the class, so only when its name is public all the way.
            nested = self.in_instance(c)
            if not self.unit_pass and (nested == "public" or nested is None and not self.api.isNull(self.api.specialized(c))):
                self.uninstantiated.add(self.text(self.api.typeSpelling(self.api.canonical(t))))
            self.left.append(("record", name, self.label(file), line, R_NO_SIZE))
            return
        row = {"name": name, "kind": KIND_TEXT[c.kind], "size": size, "align": self.api.alignOf(t),
               "polymorphic": False, "abstract": bool(self.api.abstract(c)), "bases": [], "fields": [],
               "file": self.label(file), "line": line}
        self.records[name] = row
        if self.unit_pass:
            self.cursors[name] = c
        for m in self.unit.children(c):
            if m.kind == K_BASE:
                decl = self.api.declaration(self.api.type(m))
                base = self.name(decl) or self.type_text(self.api.type(m))
                row["bases"].append({"name": base, "offset": self.api.baseOffset(c, m) // 8,
                                     "virtual": bool(self.api.virtualBase(m))})
                self.want(self.api.type(m))
            elif m.kind in I.FUNCTIONS:
                row["polymorphic"] = row["polymorphic"] or (m.kind == K_METHOD and bool(self.api.virtualMethod(m)))
                self.want_signature(m)
            elif m.kind in RECORDS or m.kind == K_ENUM:
                if self.api.isDefinition(m):
                    (self.record if m.kind in RECORDS else self.enum)(m)
            elif m.kind == K_VAR:
                self.global_(m)
            elif m.kind in (K_TYPEDEF, K_ALIAS):
                self.alias(m)
        if not self.api.isNull(self.api.specialized(c)) and not row["bases"]:
            # An implicit instantiation: the cursor walk above saw nothing of it.
            for m in self.api.members(self.api.visitBases, t):
                decl = self.api.declaration(self.api.type(m))
                base = self.name(decl) or self.type_text(self.api.type(m))
                row["bases"].append({"name": base, "offset": self.api.baseOffset(c, m) // 8,
                                     "virtual": bool(self.api.virtualBase(m))})
                self.want(self.api.type(m))
            methods = self.api.members(self.api.visitMethods, t)
            row["polymorphic"] = row["polymorphic"] or any(self.api.virtualMethod(m) for m in methods)
            for m in methods:
                self.want_signature(m)
        self.fields(c, t, row, "", 0)
        row["polymorphic"] = row["polymorphic"] or any(self.records.get(b["name"], {}).get("polymorphic") for b in row["bases"])
        row["trivial_copy"], row["trivial_destroy"], can_construct, can_assign = self.trivial(t)
        row["no_copy"] = not can_construct and not can_assign

    def trivial(self, t):
        """TY-10: (trivially copyable, trivially destructible, a copy or move constructor can be called, a copy or move
        assignment can be called), as the compiler says; the compiled rows check them.
        - Numbers, enums, pointers and references are all four (a reference or const field is the owner's business).
        - Destructible: the destructor is not the record's own (implicit or defaulted), not virtual, not deleted, and
          every base and field is destructible.
        - Copyable (clang's rule): destructible, no vtable, no virtual base, no copy or move of its own (defaulted
          and deleted ones are not its own), every base and field copyable. Deleted ones do not stop it.
        - Callable copies: a declared one that is not deleted; else the implicit one, which a user-declared copy or
          move deletes ([class.copy.ctor] 6, [class.copy.assign] 2), and which needs every base and field to have its
          own (an assignment also no reference and no const field). libclang also lists a special member clang
          declared implicitly (a unit used it), written where the class's name is: its deleted flag is clang's own
          answer for that member, and an implicit move deletes nothing."""
        api = self.api
        t = api.canonical(t)
        while t.kind in (T_CONSTANT_ARRAY, T_INCOMPLETE_ARRAY):
            t = api.canonical(api.element(t))
        decl = api.declaration(t)
        if decl.kind not in RECORDS:
            return True, True, True, True
        key = self.text(api.typeSpelling(t))
        if key in self.trivial_memo:
            return self.trivial_memo[key]
        self.trivial_memo[key] = (False, False, False, False)  # while worked out (a record cannot hold itself by value)
        if api.sizeOf(t) < 0:
            return False, False, False, False
        copy, destroy = True, True
        ctors, assigns, moves = [], [], 0  # declared copy and move constructors and assignments: deleted or not
        copy_ctor, copy_assign = False, False
        record_at = self.at(decl)
        for m in api.members(api.visitMethods, t):
            if m.kind == K_METHOD and api.virtualMethod(m):
                copy = False
            if m.kind == I.K_DESTRUCTOR and (api.deleted(m) or api.virtualMethod(m) or not api.defaulted(m)):
                destroy = False
            is_ctor = m.kind == I.K_CONSTRUCTOR and (api.copyConstructor(m) or api.moveConstructor(m))
            is_assign = m.kind == K_METHOD and (api.copyAssignment(m) or api.moveAssignment(m))
            if is_ctor or is_assign:
                (ctors if is_ctor else assigns).append(not api.deleted(m))
                if not api.defaulted(m) and not api.deleted(m):
                    copy = False
                implicit = self.at(m) == record_at
                if not implicit and ((is_ctor and api.moveConstructor(m)) or (is_assign and api.moveAssignment(m))):
                    moves += 1
                copy_ctor = copy_ctor or (is_ctor and bool(api.copyConstructor(m)))
                copy_assign = copy_assign or (is_assign and bool(api.copyAssignment(m)))
        parts_construct, parts_assign = True, True
        for b in api.members(api.visitBases, t):
            c, d, pc, pa = self.trivial(api.type(b))
            copy = copy and c and not api.virtualBase(b)
            destroy = destroy and d
            parts_construct, parts_assign = parts_construct and pc, parts_assign and pa
        for f in api.members(api.visitFields, t):
            ft = api.type(f)
            c, d, pc, pa = self.trivial(ft)
            copy, destroy = copy and c, destroy and d
            parts_construct, parts_assign = parts_construct and pc, parts_assign and pa
            ft = api.canonical(ft)
            while ft.kind in (T_CONSTANT_ARRAY, T_INCOMPLETE_ARRAY):
                ft = api.canonical(api.element(ft))
            if ft.kind in (T_LVALUE, T_RVALUE) or api.constType(ft):
                parts_assign = False
        copy = copy and destroy
        can_construct = any(ctors) or (not copy_ctor and not moves and parts_construct)
        can_assign = any(assigns) or (not copy_assign and not moves and parts_assign)
        self.trivial_memo[key] = (copy, destroy, can_construct, can_assign)
        return copy, destroy, can_construct, can_assign

    def fields(self, owner, owner_type, row, prefix, base_bits):
        """Fields of `owner`, named prefix + name; base_bits: where owner starts in the row's record. Read through the
        type (clang_Type_visitFields), so an implicit instantiation's are there too."""
        for m in self.api.members(self.api.visitFields, self.api.type(owner)):
            spelled = self.text(self.api.spelling(m))
            member = self.api.declaration(self.api.canonical(self.api.type(m)))
            if not spelled or (member.kind in RECORDS and self.api.anonymousRecord(member)):
                # an anonymous struct or union: its members are the owner's own (C++ injects them), found in
                # owner_type by their own names
                if member.kind in RECORDS and self.api.anonymousRecord(member):
                    self.fields(member, owner_type, row, prefix, base_bits)
                continue
            ft = self.api.type(m)
            extents, element = self.extents(ft)
            count = 1
            for n in extents:
                count *= n
            bits = self.api.bitWidth(m) if self.api.isBitField(m) else None
            text = self.type_text(element if extents else ft)  # an array: its element's text (TY-3)
            decl = self.api.declaration(self.api.canonical(element))
            nested = decl.kind in RECORDS and self.api.anonymous(decl) and not extents
            if text is None:
                # the member has no type a mod can write; a named member of an anonymous type still has its fields,
                # as "outer.inner"
                self.left.append(("field", row["name"] + "." + prefix + spelled, row["file"], row["line"], R_NO_TYPE_NAME))
                if nested and bits is None:
                    start = self.api.typeOffsetOf(owner_type, spelled.encode())
                    self.fields(decl, self.api.type(decl), row, prefix + spelled + ".", base_bits + max(start, 0))
                continue
            # clang_Type_getOffsetOf finds a field injected from an anonymous member too, in bits from owner_type's start
            at_bits = base_bits + self.api.typeOffsetOf(owner_type, spelled.encode())
            field = {"name": prefix + spelled, "type": text, "count": count,
                     "extents": extents if len(extents) > 1 else None, "access": ACCESS.get(self.api.access(m)),
                     "offset": at_bits // 8, "bits": None}
            if bits is not None:
                if bits > 32:
                    self.left.append(("field", row["name"] + "." + prefix + spelled, row["file"], row["line"], R_WIDE_BITS))
                    continue
                field["type"] = "%s : %d" % (text, bits)
                field["bits"] = {"width": bits, "offset": at_bits}
            row["fields"].append(field)
            self.spelling_alias(element)
            self.want(element)
            if nested:
                # a named member of an anonymous type: its fields as "outer.inner"
                start = self.api.typeOffsetOf(owner_type, spelled.encode())
                self.fields(decl, self.api.type(decl), row, prefix + spelled + ".", base_bits + max(start, 0))

    def want(self, t):
        """A template instance, or a record nested in one, that a type names (by value, pointer, reference or array,
        as a function pointer's result or parameter, as a member pointer's class: TY-9) is a row of its own; else it
        would be named by texts and described nowhere."""
        if self.unit_pass:
            # A template's instance is the header pass's row; one only a .cpp names is left out there by its name.
            self.named_instances.update(n for n in type_names(self.type_text(t) or "") if "<" in n)
            return
        self.member_alias(t)
        t = self.api.canonical(t)
        while t.kind in (T_POINTER, T_LVALUE, T_RVALUE, T_MEMBER_POINTER, T_CONSTANT_ARRAY, T_INCOMPLETE_ARRAY):
            if t.kind == T_MEMBER_POINTER:
                self.want(self.api.classType(t))
            t = self.api.canonical(self.api.element(t) if t.kind in (T_CONSTANT_ARRAY, T_INCOMPLETE_ARRAY) else self.api.pointee(t))
        if t.kind in (I.T_FUNCTION_PROTO, I.T_FUNCTION_NO_PROTO):
            self.want(self.api.resultType(t))
            for i in range(max(self.api.numArgTypes(t), 0)):
                self.want(self.api.argType(t, i))
            return
        decl = self.api.declaration(t)
        if decl.kind in RECORDS and (not self.api.isNull(self.api.specialized(decl)) or self.in_instance(decl)):
            self.pending.append(decl)
        elif decl.kind == K_ENUM and self.in_instance(decl) is not None and self.name(decl) not in self.enums:
            # An enum of a template instance (`Ui::TagProcessorBase<char16_t>::Operation`): an instance's cursor has no
            # children for record() to walk. Its own row, by its instance's name (TY-9).
            self.enum(decl, again=True)
        elif decl.kind in RECORDS and self.api.sizeOf(t) < 0:
            # Declared, defined nowhere (an opaque handle, `typedef struct NebulaActor NebulaActor`): record() leaves it
            # out with its reason, so a text naming it is never "not known".
            self.pending.append(decl)

    def want_signature(self, c):
        """The result and parameter types of a function or method (header pass): what its signature names is described
        as a field's type is (TY-9)."""
        if self.unit_pass:
            return
        t = self.api.type(c)
        if t.kind not in (I.T_FUNCTION_PROTO, I.T_FUNCTION_NO_PROTO):
            return
        self.want(self.api.resultType(t))
        for i in range(max(self.api.numArgTypes(t), 0)):
            self.want(self.api.argType(t, i))

    def member_alias(self, t):
        """A typedef or using of a template instance that a type names (`NebulaList<AudMe>::Link`): an alias row, as a
        class's own are (an instance's cursor has no children for record() to walk). TY-9."""
        api = self.api
        while True:
            if t.kind == T_ELABORATED:
                t = api.namedType(t)
            elif t.kind in (T_POINTER, T_LVALUE, T_RVALUE, T_MEMBER_POINTER):
                t = api.pointee(t)
            elif t.kind in (T_CONSTANT_ARRAY, T_INCOMPLETE_ARRAY):
                t = api.element(t)
            else:
                break
        c = api.declaration(t)  # through any sugar: `typename A<X>::Item` names A<X>'s typedef
        if c.kind not in (K_TYPEDEF, K_ALIAS):
            return
        parent = api.semanticParent(c)
        if parent.kind not in RECORDS or (api.isNull(api.specialized(parent)) and not self.in_instance(parent)):
            return
        name = self.name(c)
        if name is None or name in self.aliases:
            return
        file, line = self.where(c)
        under = api.underlying(c)
        decl = api.declaration(api.canonical(under))
        self.aliases[name] = {"name": name, "kind": "alias", "of": self.type_text(under),
                              "record": self.name(decl) if decl.kind in RECORDS else None,
                              "file": self.label(file), "line": line}
        self.spelling_alias(under)
        self.want(under)

    def in_instance(self, c):
        """For a record nested in a template instance: "public" when every scope from the instance down names it
        publicly (a sizeof outside can force it), else "private"; None for any other record."""
        access = "public"
        while True:
            parent = self.api.semanticParent(c)
            if parent.kind not in RECORDS:
                return None
            # An instantiated member says no access of its own: its pattern's (the template's member) is the one.
            said = self.api.access(c)
            if said == 0 and not self.api.isNull(self.api.specialized(c)):
                said = self.api.access(self.api.specialized(c))
            if said != 1:
                access = "private"
            if not self.api.isNull(self.api.specialized(parent)):
                return access
            c = parent

    def enum(self, c, again=False):
        """again: an instance's enum, whose USR may be its pattern's (the name keeps one row each)."""
        if not again and not self.once(c):
            return
        file, line = self.where(c)
        name = self.name(c)
        values = [{"name": self.text(self.api.spelling(v)), "value": self.api.enumValue(v)}
                  for v in self.unit.children(c) if v.kind == K_ENUM_CONST]
        self.enums[name or "(anonymous %s:%d)" % (self.label(file), line)] = {
            "name": name, "kind": "enum", "underlying": self.type_text(self.api.enumType(c)),
            "scoped": bool(self.api.scoped(c)), "values": values, "file": self.label(file), "line": line}

    def alias(self, c):
        if self.unit_pass and not self.keep(self.api.canonical_cursor(c), None):
            return  # a header's typedef written again in the unit: the header pass's
        if not self.once(c):
            return
        name = self.name(c)
        if name is None:
            return
        file, line = self.where(c)
        under = self.api.underlying(c)
        decl = self.api.declaration(self.api.canonical(under))
        record = self.name(decl) if decl.kind in RECORDS else None
        self.aliases[name] = {"name": name, "kind": "alias", "of": self.type_text(under), "record": record,
                              "file": self.label(file), "line": line}
        self.spelling_alias(under)
        self.want(under)  # an instance named only by an alias (Vec2s, Box2f) is a record a mod may size (TY-2)

    def global_(self, c):
        # The unit pass first: a static member's declaration in its class is not the definition, and must not take
        # the USR before the definition outside the class is seen.
        if self.unit_pass and not (self.api.isDefinition(c) and self.keep(self.api.canonical_cursor(c), None)):
            return  # declared in a header: the header pass's row
        if not self.once(c):
            return
        file, line = self.where(c)
        if self.api.semanticParent(c).kind in (I.K_CLASS_TEMPLATE, I.K_CLASS_PARTIAL):
            parent = self.api.semanticParent(c)
            self.left.append(("global", "%s::%s" % (self.name(parent) or "(anonymous)", self.text(self.api.spelling(c))),
                              self.label(file), line, R_TEMPLATE_MEMBER))
            return
        name = self.name(c)
        t = self.api.type(c)
        if name is None or self.api.linkage(c) != LINKAGE_EXTERNAL:
            self.left.append(("global", name or self.text(self.api.spelling(c)), self.label(file), line, R_INTERNAL))
            return
        if self.api.tls(c) != 0:
            self.left.append(("global", name, self.label(file), line, R_THREAD))
            return
        if self.type_text(t) is None:
            self.left.append(("global", name, self.label(file), line, R_NO_TYPE_NAME))
            return
        row = {"name": name, "type": self.type_text(t), "const": bool(self.api.constType(t)),
               "mangled": self.text(self.api.mangling(c)), "file": self.label(file), "line": line, "value": None,
               "inline": not self.api.isNull(self.api.definition(c))}  # defined in a header: every unit that uses it has it
        result = self.api.evaluate(c)
        if result:
            kind = self.api.evalKind(result)
            if kind == 1:
                row["value"] = self.api.evalInt(result)
            elif kind == 2:
                row["value"] = self.api.evalDouble(result)
            self.api.evalDispose(result)
        self.want(t)
        self.globals[name] = row
        if self.unit_pass:
            self.cursors[("global", name)] = c

    # ---- outputs
    def write(self):
        defined = defined_symbols(DEFINED)
        lines = ["// Generated by tools/lumacore/gen_types.py from the game's headers (contract types). Do not edit.",
                 "// Built with -fno-access-control: the game's own private fields are what mods change.",
                 "// offsetof on a class that is not standard layout is clang's to define; it does for every class without a",
                 "// virtual base (those fields have no row: luma_types.left).",
                 "#pragma clang diagnostic ignored \"-Winvalid-offsetof\"", ""]
        lines += ['#include "%s"' % h.as_posix() for h in self.headers]
        lines += ["#include <cstddef>", '#include "luma_types.h"', ""]
        unit_records, unit_globals = self.merge_units(DEFINED)
        self.unit_records = unit_records
        lines += self.cpp_rows(defined)
        # The units' records and globals have their rows in the units' copies; from here they are the JSON's.
        self.records.update(unit_records)
        self.globals.update(unit_globals)
        self.foreign_aliases()
        self.outputs(lines)

    def cpp_rows(self, defined):
        """The rows as C++. defined: the module's defined symbols, or None (no global rows), or True (the unit pass: a
        global the unit defines is defined). The header pass's are in an unnamed namespace (the table is compiled with
        no access check). The unit pass's (instrument.py appends them to the copy) are static members of the unit's
        LumaUnitRows, which each record the unit writes names as a friend: a member's initializer is in the class's
        scope, so offsetof and & reach private members with the access check on."""
        unit = defined is True
        held = []  # unit pass: the members' declarations
        lines = []
        # Unit pass: the records named through a scope that is not public (instrument.py sets `inside`). Their rows
        # are their own static members: the declarations go in their bodies (inside_decls, inserted by instrument.py),
        # the definitions after the source (`after`), in the record's scope. (Their declarator is the name with no leading
        # "::": `luma::TypeRow ::A::x` would read as `luma::TypeRow::A::x`.)
        inside = getattr(self, "inside", set()) if unit else set()
        self.inside_decls = {}
        after = []

        def member(section, kind, name, text, array=""):
            if unit:
                held.append("static const luma::%s %s%s;" % (kind, name, array))
                lines.append("%sconst luma::%s LumaUnitRows::%s%s = %s;" % (section, kind, name, array, text))
            else:
                lines.append("%sconst luma::%s %s%s = %s;" % (section, kind, name, array, text))

        n = 0
        for name in sorted(self.records):
            r = self.records[name]
            field_rows = []
            mine = name in inside
            if not mine:
                # offsetof is a macro: a template's arguments hold commas, so the record is named by an alias
                (held if unit else lines).append("using R%d = ::%s;" % (n, name))
            for f in r["fields"]:
                q = '"%s", "%s"' % (cstr(f["name"]), cstr(f["type"]))
                if f["bits"]:
                    bo = f["bits"]["offset"]
                    field_rows.append("{%s, %du, %du, %du, %du}" % (q, bo // 8, f["count"], bo % 8, f["bits"]["width"]))
                elif any(b["virtual"] for b in r["bases"]):
                    # No offsetof on a record with a virtual base: libclang's offset, as the bases' rows have (the
                    # type test checks both). Leaving the row out would let a base's field of the same name, which this
                    # one hides, be found in its place.
                    field_rows.append("{%s, %du, %du, 0, 0}" % (q, f["offset"], f["count"]))
                elif mine:
                    # The builtin is no macro: the qualified name may hold a template's commas.
                    field_rows.append("{%s, __builtin_offsetof(::%s, %s), %du, 0, 0}" % (q, name, f["name"], f["count"]))
                else:
                    field_rows.append("{%s, offsetof(R%d, %s), %du, 0, 0}" % (q, n, f["name"], f["count"]))
            base_rows = ['{"%s", %du, %du}' % (cstr(b["name"]), b["offset"], int(b["virtual"])) for b in r["bases"]]
            # TY-10: the generator's claim, checked by the compiler.
            claim = ('static_assert(__is_trivially_copyable(%%s) == %s && __is_trivially_destructible(%%s) == %s, '
                     '"TY-10: the generator says %s is %strivially copyable and %strivially destructible");') % (
                "true" if r.get("trivial_copy") else "false", "true" if r.get("trivial_destroy") else "false", cstr(name),
                "" if r.get("trivial_copy") else "not ", "" if r.get("trivial_destroy") else "not ")
            # no_copy: whether any copy or move can be called. The table is built without access control, so its traits
            # see every member and check both ways; a unit's rows and an in-class claim see public members only, so
            # they check that a no_copy record has none.
            callable_ = ("(__is_constructible(%s, const %s &) || __is_constructible(%s, %s &&) || "
                         "__is_assignable(%s &, const %s &) || __is_assignable(%s &, %s &&))")
            if r.get("no_copy"):
                claim += ' static_assert(!%s, "TY-10: the generator says %s has no copy or move to call");' % (
                    callable_, cstr(name))
            elif not unit and not mine:
                claim += ' static_assert(%s, "TY-10: the generator says %s has a copy or move to call");' % (
                    callable_, cstr(name))
            if mine:
                decls = []
                if field_rows:
                    decls.append("static const luma::TypeField lumaFields_[];")
                    after.append("const luma::TypeField %s::lumaFields_[] = {%s};" % (name, ", ".join(field_rows)))
                if base_rows:
                    decls.append("static const luma::TypeBase lumaBases_[];")
                    after.append("const luma::TypeBase %s::lumaBases_[] = {%s};" % (name, ", ".join(base_rows)))
                decls.append("static const luma::TypeRow lumaRow_;")
                decls.append("static void lumaTrivial_() { %s }" % (claim % (("::" + name,) * claim.count("%s"))))
                after.append('LUMA_TYPE_SECTION const luma::TypeRow %s::lumaRow_ = {"%s", sizeof(::%s), alignof(::%s), %s, %du, %s, %du};' % (
                    name, cstr(name), name, name, "lumaFields_" if field_rows else "nullptr", len(field_rows),
                    "lumaBases_" if base_rows else "nullptr", len(base_rows)))
                self.inside_decls[name] = " " + " ".join(decls) + " "
                n += 1
                continue
            if field_rows:
                member("", "TypeField", "kF%d" % n, "{%s}" % ", ".join(field_rows), "[]")
            if base_rows:
                member("", "TypeBase", "kB%d" % n, "{%s}" % ", ".join(base_rows), "[]")
            member("LUMA_TYPE_SECTION ", "TypeRow", "kT%d" % n, '{"%s", sizeof(::%s), alignof(::%s), %s, %du, %s, %du}' % (
                cstr(name), name, name, "kF%d" % n if field_rows else "nullptr", len(field_rows),
                "kB%d" % n if base_rows else "nullptr", len(base_rows)))
            (held if unit else lines).append(claim % (("R%d" % n,) * claim.count("%s")))
            n += 1
        # Aliases and other spellings of a record (TY-2): kind 1 and kind 2 find the record through them. The header
        # pass's alone, the units' merged (merge_units): a row names its record by name, wherever its row is.
        a = 0
        records = {} if unit else set(self.records) | set(getattr(self, "unit_records", {}))
        for name in sorted(self.aliases):
            row = self.aliases[name]
            row["row"] = not unit and row.get("record") in records and row["record"] != name and name not in records
            if row["row"]:
                member("LUMA_ALIAS_SECTION ", "AliasRow", "kA%d" % a, '{"%s", "%s"}' % (cstr(name), cstr(row["record"])))
                a += 1
        self.alias_rows = a
        g = 0
        for name in sorted(self.globals):
            row = self.globals[name]
            inline = row.pop("inline", False)
            row["defined"] = defined is True or inline or defined is not None and row["mangled"] in defined
            if not row["defined"]:
                # A constant with a known value is read from the self-description and binds nothing (contract types,
                # section 2); any other is left out.
                if row["value"] is None and defined is not None:
                    self.left.append(("global", name, row["file"], row["line"], R_UNDEFINED))
                continue
            cast = "const_cast<void*>(static_cast<const void*>(&::%s))" % name
            member("LUMA_GLOBAL_SECTION ", "GlobalRow", "kG%d" % g, '{"%s", "%s", %s, %du}' % (
                cstr(name), cstr(row["type"]), cast, int(row["const"])))
            g += 1
        self.global_rows = g
        self.row_count = n + a + g
        if unit:
            return ["namespace {", "struct LumaUnitRows {"] + ["    " + x for x in held] + ["};"] + lines + ["}", ""] + after + [""]
        return ["namespace {"] + lines + ["}", ""]

    def unit_part(self):
        """The unit pass's part of the self-description (instrument.py writes it as <copy>.types; the header pass
        merges every unit's)."""
        return {"records": list(self.records.values()), "enums": list(self.enums.values()),
                "aliases": list(self.aliases.values()), "globals": list(self.globals.values()),
                "left": [list(x) for x in self.left], "named": sorted(self.named_instances)}

    def merge_units(self, folder):
        """Each unit's part, before this pass's rows. Returns the units' records and globals (their rows are in the
        units' copies; the caller adds them to this pass's for `reachable` and the JSON). A record or global two parts
        (or a part and a header) define is the build's fault, and stops it. The units' aliases join this pass's, whose
        rows they are: a name given to one record is one row; a name given to different records is left out."""
        records, globals_ = {}, {}
        if folder is None:
            return records, globals_
        twice = {}  # alias name -> {record: file}
        named_once = {}  # an instance only .cpp files name -> the first such file
        clashes = []  # a record or global defined twice: all of them are said, then the build stops
        for part in sorted(pathlib.Path(folder).rglob("*.types")):
            data = json.loads(part.read_text(encoding="utf-8"))
            for key, table, mine in (("records", records, self.records), ("globals", globals_, self.globals)):
                for row in data[key]:
                    other = table.get(row["name"]) or mine.get(row["name"])
                    if other is not None:
                        clashes.append("%s is defined twice (%s and %s)" % (row["name"], part, other["file"]))
                        continue
                    table[row["name"]] = row
            for row in data["aliases"]:
                name = row["name"]
                if name in twice:
                    twice[name].setdefault(row["record"], row["file"])
                    continue
                had = self.aliases.get(name)
                if had is None:
                    self.aliases[name] = row
                elif had["record"] != row["record"]:
                    twice[name] = {had["record"]: had["file"], row["record"]: row["file"]}
                    del self.aliases[name]
            for row in data["enums"]:
                key = row["name"] or "(anonymous %s:%d)" % (row["file"], row["line"])
                if key in self.enums:
                    sys.exit("gen_types.py: the enum %s is named twice (%s)" % (key, part))
                self.enums[key] = row
            self.left += [tuple(x) for x in data["left"]]
            for x in data.get("instances", []):
                # An explicit instantiation in a .cpp: this pass's row when a header names the instance.
                if x["name"] not in self.records:
                    self.left.append(("record", x["name"], x["file"], x["line"], R_UNIT_INSTANCE))
            source = part.relative_to(folder).as_posix()[:-len(".types")]
            for name in data.get("named", []):
                # An instance (or its member) only .cpp files name: left out by that name, once.
                if name not in self.records and name not in self.aliases and name not in named_once:
                    named_once[name] = source
        for name, source in sorted(named_once.items()):
            self.left.append(("record", name, source, 0, R_UNIT_INSTANCE))
        if clashes:
            sys.exit("gen_types.py: one name, two definitions (C++'s one-definition rule):\n  " + "\n  ".join(clashes))
        for name, given in sorted(twice.items()):
            file = sorted(given.values())[0]
            self.left.append(("alias", name, file, 0, R_ALIAS_TWICE + ", ".join(
                "%s (%s)" % (record, f) for record, f in sorted(given.items(), key=lambda x: str(x)))))
        return records, globals_

    def reach(self):
        """TY-1: each record's inherited names by C++'s lookup. A name the record declares hides its bases'; otherwise
        it must be found in exactly one base subobject, not through a virtual base. Writes row["reachable"] (the
        inherited rows; own fields are the record's `fields`) and the left-out names."""
        memo = {}

        def members(name, stack):
            # name -> [(declaring record, offset, field row, virtual base or None)], one entry per subobject
            if name in memo:
                return memo[name]
            r = self.records.get(name)
            if r is None or name in stack:
                return {}
            own = {f["name"]: [(name, f["offset"], f, None)] for f in r["fields"]}
            found = {}
            for b in r["bases"]:
                for n, entries in members(b["name"], stack | {name}).items():
                    if n in own:
                        continue
                    found.setdefault(n, []).extend(
                        (d, b["offset"] + off, f, via or (b["name"] if b["virtual"] else None)) for d, off, f, via in entries)
            found.update(own)
            memo[name] = found
            return found

        rows = 0
        for name, r in self.records.items():
            r["reachable"] = []
            own = {f["name"] for f in r["fields"]}
            for n, entries in sorted(members(name, frozenset()).items()):
                if n in own:
                    continue
                virtual = next((via for _, _, _, via in entries if via), None)
                if virtual is not None:
                    self.left.append(("field", name + "." + n, r["file"], r["line"], R_VIRTUAL + virtual))
                elif len(entries) > 1:
                    self.left.append(("field", name + "." + n, r["file"], r["line"],
                                      R_AMBIGUOUS + ", ".join(sorted("%s::%s" % (d, n) for d, _, _, _ in entries))))
                else:
                    d, off, f, _ = entries[0]
                    r["reachable"].append({"name": n, "from": d, "offset": off, "type": f["type"]})
                    rows += 1
        return rows

    def texts(self):
        """Every type text the self-description gives: (where, text)."""
        for r in self.records.values():
            for f in r["fields"]:
                yield r["name"] + "." + f["name"], f["type"]
        for g in self.globals.values():
            yield g["name"], g["type"]
        for a in self.aliases.values():
            yield a["name"], a["of"]
        for e in self.enums.values():
            yield e["name"] or "(anonymous enum)", e["underlying"]

    def foreign_aliases(self):
        """TY-9: a typedef from outside the game's trees (<stdint.h>'s uint32_t) that a text names is an alias row, as
        the game's own s32 is: the name a modder reads, with no record."""
        named = set()
        for _, text in self.texts():
            named.update(re.findall(r"(?<![\w:])[A-Za-z_]\w*", text or ""))
        for name in sorted(named & set(self.foreign)):
            if name in self.aliases or name in self.records:
                continue
            c = self.foreign[name]
            under = self.api.underlying(c)
            self.aliases[name] = {"name": name, "kind": "alias", "of": self.type_text(under), "record": None,
                                  "file": None, "line": None}

    def unnamed(self):
        """TY-9's check: every name a text holds outside template arguments (past pointers, references, arrays, cv
        and a bit-field's width, in a function pointer's result and parameters, a member pointer's class) is a row, a
        built-in word or left out. An instance is one name with its arguments. Returns the texts that are not, as
        (where, text, name)."""
        known = set(self.records) | {e["name"] for e in self.enums.values() if e["name"]} | set(self.aliases)
        known |= {x[1] for x in self.left}
        out = []
        for where, text in self.texts():
            for name in type_names(text or ""):
                if name not in known:
                    out.append((where, text, name))
        return out

    def outputs(self, lines):
        inherited = self.reach()
        missing = self.unnamed()
        if missing:
            sys.exit("gen_types.py: type texts that name no row, built-in or left-out name (contract types TY-9):\n  " +
                     "\n  ".join("%s: %s (%s)" % m for m in sorted(missing)[:200]) +
                     ("\n  ... %d in all" % len(missing) if len(missing) > 200 else ""))
        g = self.global_rows
        # The left rows, after `reach` (ambiguous and virtual-base names) and every unit's part.
        lines.append("namespace {")
        for i, (group, name, rid, what) in enumerate(left_rows(self.left)):
            lines.append('LUMA_LEFT_SECTION const luma::LeftRow kL%d = {"%s", %du, "%s", %s};' % (
                i, cstr(name), group, rid, "nullptr" if what is None else '"%s"' % cstr(what)))
        lines += ["}", ""]
        I.write_if_changed(OUT / "luma_types_table.cpp", ("\n".join(lines) + "\n").encode())
        types = list(self.records.values()) + list(self.enums.values()) + list(self.aliases.values())
        left_out = [{"kind": k, "name": n, "file": f, "line": ln, "reason": why}
                    for k, n, f, ln, why in sorted(self.left, key=str)]
        I.write_if_changed(OUT / "luma_types.json", (json.dumps(
            {"types": types, "globals": list(self.globals.values()), "left_out": left_out},
            indent=1, ensure_ascii=False) + "\n").encode("utf-8"))
        I.write_if_changed(OUT / "luma_types.left", "".join(
            "%s\t%s\t%s\t%s\t%s\n" % x for x in sorted(self.left, key=str)).encode("utf-8"))
        reasons = {}
        for x in self.left:
            reasons[x[4]] = reasons.get(x[4], 0) + 1
        print(json.dumps({"records": len(self.records), "fields": sum(len(r["fields"]) for r in self.records.values()),
                          "inherited": inherited,
                          "enums": len(self.enums), "aliases": len(self.aliases), "globals": len(self.globals),
                          "global_rows": g, "alias_rows": self.alias_rows, "left": reasons}))


BUILTIN_NAMES = {"void", "bool", "char", "signed char", "unsigned char", "short", "unsigned short", "int", "unsigned int",
                 "long", "unsigned long", "long long", "unsigned long long", "float", "double", "long double", "wchar_t",
                 "char8_t", "char16_t", "char32_t", "std::nullptr_t", "__int128", "unsigned __int128"}


def outer_name(text):
    """The type a text names past pointers, references, arrays, cv-qualifiers and a bit-field's width; None for a
    function type (or a pointer to one)."""
    if text is None:
        return None
    text = re.sub(r"\s*:\s*\d+$", "", text)
    if "(" in text or "::*" in text:
        return None
    # Outside template arguments only: `NebulaList<const Foo *>` keeps its argument whole.
    out, depth, words = "", 0, []
    for ch in text + " ":
        depth += (ch == "<") - (ch == ">")
        if depth == 0 and (ch.isspace() or ch in "*&[]"):
            words.append(out)
            out = ""
        else:
            out += ch
    words = [w for w in words if w and not w.isdigit() and w not in ("const", "volatile", "struct", "class", "union", "enum", "typename")]
    return " ".join(words) or None


TYPE_WORDS = {"const", "volatile", "struct", "class", "union", "enum", "typename", "signed", "unsigned", "char", "short",
              "int", "long", "float", "double", "bool", "void", "wchar_t", "char8_t", "char16_t", "char32_t", "__int128",
              "std::nullptr_t"}
NAME_START = re.compile(r"[A-Za-z_][\w:]*")


def type_names(text):
    """Every name a type text holds outside template arguments, built-in words left out; an instance is one name
    with its arguments (`FxMid &(*)(FxBase *, FxBox<u16> &)` holds FxMid, FxBase and FxBox<u16>; `s32 Foo::*` holds
    s32 and Foo)."""
    out, i = [], 0
    while i < len(text):
        m = NAME_START.match(text, i)
        if not m:
            i += 1
            continue
        name, i = m.group(0), m.end()
        # template arguments, and a member of an instance after them (`NebulaList<Foo>::Link`, `A<x>::B<y>::C`)
        while i < len(text) and text[i] == "<":
            depth, j = 0, i
            while j < len(text):
                depth += {"<": 1, ">": -1}.get(text[j], 0)
                j += 1
                if depth == 0:
                    break
            name, i = name + text[i:j], j
            m = NAME_START.match(text, i + 2) if text.startswith("::", i) else None
            if m:
                name, i = name + "::" + m.group(0), m.end()
        name = name.rstrip(":")
        if name and name not in TYPE_WORDS:
            out.append(name)
    return out


def cstr(s):
    return s.replace("\\", "\\\\").replace('"', '\\"')


def defined_symbols(folder):
    """The variables the module's units define (mangled), from the .globals each instrumented copy has beside it; None
    with no folder (no global rows: the JSON lists them with "defined": null)."""
    if folder is None:
        return None
    out = set()
    for listed in pathlib.Path(folder).rglob("*.globals"):
        out.update(line for line in listed.read_text(encoding="utf-8").splitlines() if line)
    return out


if __name__ == "__main__":
    main()
