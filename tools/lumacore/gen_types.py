"""gen_types.py --commands COMPILE_COMMANDS --out DIR [--entry FILE] [--defined DIR] [--depfile FILE] [--libclang PATH]
                [--tree DIR]

The type rows of every record, field, base, enum, alias and global the game's headers declare (language/types.md).
One unit includes every game header (native/petari/include, native/math/include, native/include) and is parsed by libclang
with a game unit's own compile command (--entry, default the first game unit), so target, defines and platform headers
are the build's. Writes:
  DIR/luma_types_table.cpp  the rows the module holds (luma_types.h): sizeof, alignof, offsetof by the compiler,
                            compiled with -fno-access-control; bit-fields and bases from libclang
  DIR/luma_types.json       the self-description's `types` (each record with `reachable`), `globals` and
                            `left_out` (libclang's numbers: the build compares them with the module's --luma-describe)
  DIR/luma_types.left       what has no row, and why
  DIR/luma_vtables_unit.cpp the unit whose compile (-fdump-vtable-layouts) gives the table at the start of each record
                            the factory makes (docs/modding/language/classes.md, overrides by slot): rewritten each run
  DIR/luma_functions.json   with --defined: every function row's facts (kind, owner, virtual, pure, const, what it
                            overrides), the units' .functions merged (merge_functions); a row without facts, facts
                            without a row, an owner that is no record of the types file or an override with no name
                            stops the build, naming it
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
import subprocess
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
# Each reason's id, the words LumaLink's rows give it (LeftOut.*, types.md): the kernel's left rows carry the
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
K_CALL_EXPR = 103
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
    reader.vtables = vtable_layouts(raw, reader.records, unit)
    for name, (prefix, count) in reader.vtables.items():
        reader.records[name]["vtable"] = {"prefix": prefix, "count": count}
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
    code = api.parse(index, None, encoded, len(args), None, 0, 0, byref(tu))  # with bodies: a virtual's calls (1.8)
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

    def __init__(self, api, tu, headers, unit_pass=False, unit=None):
        self.api, self.tu, self.headers, self.unit_pass = api, tu, headers, unit_pass
        self.main = None  # the unit's own file, lower case with '/' (unit pass)
        # The unit pass reads with instrument.py's own Unit, whose file label names a .cpp's own classes' functions
        # (identity(): "file#Class::f"); the header pass has no such class, and a bare one for its texts.
        if unit is None:
            unit = I.Unit.__new__(I.Unit)
            unit.api = api
            unit.tu = tu
            unit.label = None  # no file: a local function (identity()'s "file#...") has no row name here
        self.unit = unit
        self.foreign = {}   # typedefs outside the game's trees at file scope (<stdint.h>'s): name -> cursor (TY-9)
        self.named_instances = set()  # unit pass: the instances (and their members) its types name (TY-9)
        self.trivial_memo = {}  # TY-10: canonical spelling -> trivial
        self.polymorphic_types = {}  # objects.md: canonical spelling -> declares or inherits a virtual
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
            if 500 <= k <= 503:
                continue  # the preprocessing record's cursors (instrument.py parses with it for C-6's marks)
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
                self.virtual(row, m, False)
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
            for m in methods:
                self.virtual(row, m, True)
            for m in methods:
                self.want_signature(m)
        self.fields(c, t, row, "", 0)
        # Inherited: from the compiler's own bases, not from the rows (a base may be an instance with no row yet, or
        # none at all: DeriveActorGroup<ArrowSwitchMulti>).
        row["polymorphic"] = row["polymorphic"] or any(self.inherits_virtual(self.api.type(m))
                                                       for m in self.api.members(self.api.visitBases, t))
        row["trivial_copy"], row["trivial_destroy"], can_construct, can_assign, _ = self.trivial(t)
        row["no_copy"] = not can_construct and not can_assign

    def inherits_virtual(self, t):
        """True when the record type t declares a virtual function or has a base that does, by the compiler's own
        members and bases (an instance's too); remembered by its canonical spelling."""
        t = self.api.canonical(t)
        key = self.text(self.api.typeSpelling(t))
        if key not in self.polymorphic_types:
            self.polymorphic_types[key] = False  # ends a malformed walk; a base is never its own base
            self.polymorphic_types[key] = (
                any(m.kind in (K_METHOD, I.K_DESTRUCTOR, I.K_CONVERSION) and self.api.virtualMethod(m)
                    for m in self.api.members(self.api.visitMethods, t))
                or any(self.inherits_virtual(self.api.type(b)) for b in self.api.members(self.api.visitBases, t)))
        return self.polymorphic_types[key]

    def virtual(self, row, m, instance):
        """Contract objects: a virtual function (a destructor or conversion too) makes the record polymorphic; each one
        but a destructor is a method row, its thunk called with the compiler's dispatch (method). The name and signature
        are instrument.py's identity(), the function rows' own rule (an instance's: the record's row name and
        identity_signature). A virtual whose name or types no mod can write is in the JSON with "row": false."""
        if m.kind not in (K_METHOD, I.K_DESTRUCTOR, I.K_CONVERSION) or not self.api.virtualMethod(m):
            return
        row["polymorphic"] = True
        if m.kind == I.K_DESTRUCTOR or self.api.deleted(m):
            return
        ua = self.unit.api
        spelling = self.unit.string(ua.spelling(m))
        ident = self.unit.identity(m)
        if ident is None and instance:
            signature = self.unit.identity_signature(m)
            ident = None if signature is None else ("%s::%s" % (row["name"], spelling), signature)
        fntype = ua.type(m)
        params = [self.unit.type_text(ua.argType(fntype, i)) for i in range(ua.numArgs(m))]
        result = self.unit.type_text(ua.resultType(fntype))
        v = {"name": ident[0] if ident else "%s::%s" % (row["name"], spelling), "signature": ident[1] if ident else None,
             "pure": bool(ua.isPure(m)), "row": ident is not None and result is not None and None not in params}
        if v["row"]:
            v["call"] = {"spelling": spelling, "params": params, "result": result, "const": bool(ua.isConst(m))}
            v["params"] = "(%s)%s" % (", ".join(params), " const" if v["call"]["const"] else "")
        # Self-description: the functions a body written in the class calls, in the order written, by their
        # row names (a nerve's execute names its owner's exe function), when its definition is in this parse.
        body = ua.definition(m)
        if not ua.isNull(body):
            v["calls"] = self.calls_in(body)
        row.setdefault("virtuals", []).append(v)

    def calls_in(self, body):
        """Every function or method a definition's body calls directly, by row name and signature, once each, in the
        order written; a callee with no row identity (a template's, an unnamed scope's) is left out."""
        ua = self.unit.api
        found = []

        def visit(child, _parent, _data):
            c = I.CXCursor.from_buffer_copy(child)
            if c.kind == K_CALL_EXPR:
                target = ua.referenced(c)
                if not ua.isNull(target) and target.kind in I.FUNCTIONS:
                    ident = self.unit.identity(target)
                    if ident is not None:
                        entry = {"name": ident[0], "signature": ident[1]}
                        if entry not in found:
                            found.append(entry)
            return 2  # recurse

        ua.visit(body, I.VISITOR(visit), None)
        return found

    def trivial(self, t):
        """TY-10: (trivially copyable, trivially destructible, a copy or move constructor can be called, a copy or move
        assignment can be called, the destructor is not deleted), as the compiler says; the compiled rows check the
        first four.
        - Numbers, enums, pointers and references are all four (a reference or const field is the owner's business).
        - Destructible: the destructor is not the record's own (implicit or defaulted), not virtual, not deleted, and
          every base and field is destructible.
        - Copyable (clang's rule): a destructor that is not its own and not virtual (a deleted one is not its own, so
          clang still calls it trivially copyable), no vtable, no virtual base, no copy or move of its own (defaulted
          and deleted ones are not its own), every base and field copyable. Deleted ones do not stop it.
        - Callable copies: a declared one that is not deleted; else the implicit one, which a user-declared copy or
          move deletes ([class.copy.ctor] 6, [class.copy.assign] 2), and which needs every base and field to have its
          own (an assignment also no reference and no const field). libclang also lists a special member clang
          declared implicitly (a unit used it), written where the class's name is: its deleted flag is clang's own
          answer for that member, and an implicit move deletes nothing.
        - B02-1, destructors ([class.copy.ctor] 10, [class.dtor] 7): a part's deleted destructor deletes the implicit
          copy constructor and the implicit destructor, and a record whose destructor is deleted cannot be copy
          constructed (clang's __is_constructible, which the table checks, asks for the destructor too). An
          inaccessible destructor deletes nothing in the table, built without access control, so access is not read."""
        api = self.api
        t = api.canonical(t)
        while t.kind in (T_CONSTANT_ARRAY, T_INCOMPLETE_ARRAY):
            t = api.canonical(api.element(t))
        decl = api.declaration(t)
        if decl.kind not in RECORDS:
            return True, True, True, True, True
        key = self.text(api.typeSpelling(t))
        if key in self.trivial_memo:
            return self.trivial_memo[key]
        self.trivial_memo[key] = (False, False, False, False, False)  # while worked out (a record cannot hold itself)
        if api.sizeOf(t) < 0:
            return False, False, False, False, False
        copy, destroy = True, True
        ctors, assigns, moves = [], [], 0  # declared copy and move constructors and assignments: deleted or not
        copy_ctor, copy_assign = False, False
        record_at = self.at(decl)
        destructor = None  # declared: not deleted; None: the implicit one, deleted when a part's is
        for m in api.members(api.visitMethods, t):
            if m.kind == K_METHOD and api.virtualMethod(m):
                copy = False
            if m.kind == I.K_DESTRUCTOR:
                destructor = not api.deleted(m)
                if api.virtualMethod(m) or not (api.defaulted(m) or api.deleted(m)):
                    copy, destroy = False, False  # not trivial
                destroy = destroy and destructor
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
        parts_construct, parts_assign, parts_destroy = True, True, True
        for b in api.members(api.visitBases, t):
            c, d, pc, pa, pd = self.trivial(api.type(b))
            copy = copy and c and not api.virtualBase(b)
            destroy = destroy and d
            parts_construct, parts_assign, parts_destroy = parts_construct and pc and pd, parts_assign and pa, parts_destroy and pd
        for f in api.members(api.visitFields, t):
            ft = api.type(f)
            c, d, pc, pa, pd = self.trivial(ft)
            copy, destroy = copy and c, destroy and d
            parts_construct, parts_assign, parts_destroy = parts_construct and pc and pd, parts_assign and pa, parts_destroy and pd
            ft = api.canonical(ft)
            while ft.kind in (T_CONSTANT_ARRAY, T_INCOMPLETE_ARRAY):
                ft = api.canonical(api.element(ft))
            if ft.kind in (T_LVALUE, T_RVALUE) or api.constType(ft):
                parts_assign = False
        if destructor is None:
            destructor = parts_destroy
        can_construct = destructor and (any(ctors) or (not copy_ctor and not moves and parts_construct))
        can_assign = any(assigns) or (not copy_assign and not moves and parts_assign)
        self.trivial_memo[key] = (copy, destroy, can_construct, can_assign, destructor)
        return copy, destroy, can_construct, can_assign, destructor

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
        lines += ["#include <cstddef>", '#include "luma_types.h"', "#include <luma_hook.h>", ""]
        unit_records, unit_globals = self.merge_units(DEFINED)
        self.unit_records = unit_records
        lines += self.cpp_rows(defined)
        # The units' records and globals have their rows in the units' copies; from here they are the JSON's.
        self.records.update(unit_records)
        self.globals.update(unit_globals)
        # A record a header only declares (no size there) and a .cpp defines has that unit's row: a name is a row or
        # left out, never both (the kernel and the validator would answer it differently).
        self.left = [x for x in self.left if not (x[0] == "record" and x[1] in self.records)]
        self.foreign_aliases()
        if DEFINED is not None:
            left_records = {x[1] for x in self.left if x[0] == "record"}
            functions, wrong = merge_functions(DEFINED, set(self.records), left_records)
            if wrong:
                sys.exit("gen_types.py: the function rows' facts (luma_functions.json) do not hold:\n  " +
                         "\n  ".join(wrong[:200]) + ("\n  ... %d in all" % len(wrong) if len(wrong) > 200 else ""))
            I.write_if_changed(OUT / "luma_functions.json", (json.dumps(
                {"functions": functions}, indent=1, ensure_ascii=False) + "\n").encode("utf-8"))
            numbers, wrong = merge_numbers(DEFINED, {(f["name"], f["signature"]) for f in functions})
            if wrong:
                sys.exit("gen_types.py: the numbers (luma_numbers.json) do not hold:\n  " + "\n  ".join(wrong[:200]))
            I.write_if_changed(OUT / "luma_numbers.json", (json.dumps(numbers, indent=1, ensure_ascii=False) + "\n").encode("utf-8"))
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

        # Contract classes, overrides by slot: the header pass's virtuals of the records the factory makes get the code a
        # mod class's table takes in their slot (slot_code).
        slot_records = class_bases(self.records) if not unit and getattr(self, "vtables", None) is not None else set()
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
            claim += ' static_assert(__is_polymorphic(%%s) == %s, "contract objects: the generator says %s is %spolymorphic");' % (
                "true" if r["polymorphic"] else "false", cstr(name), "" if r["polymorphic"] else "not ")
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
            calls = [(k, v) for k, v in enumerate(r.get("virtuals", ())) if v["row"]]
            if mine:
                decls = []
                for k, v in calls:
                    decls.append("static void lumaV%d_(void** luma_a, void* luma_r);" % k)
                    decls.append("static const luma::Function lumaF%d_;" % k)
                    decls.append("static const luma::MethodRow lumaM%d_;" % k)
                    after.append("void %s::lumaV%d_(void** luma_a, void* luma_r) { %s }" % (name, k, thunk(v, "::" + name)))
                    after.append('const luma::Function %s::lumaF%d_ = {"%s", "%s", %s::lumaV%d_, nullptr};' % (
                        name, k, cstr(v["name"]), cstr(v["signature"]), name, k))
                    after.append('LUMA_METHOD_SECTION const luma::MethodRow %s::lumaM%d_ = {&%s::lumaF%d_, %s};' % (
                        name, k, name, k, method_names(name, v)))
                if field_rows:
                    decls.append("static const luma::TypeField lumaFields_[];")
                    after.append("const luma::TypeField %s::lumaFields_[] = {%s};" % (name, ", ".join(field_rows)))
                if base_rows:
                    decls.append("static const luma::TypeBase lumaBases_[];")
                    after.append("const luma::TypeBase %s::lumaBases_[] = {%s};" % (name, ", ".join(base_rows)))
                decls.append("static const luma::TypeRow lumaRow_;")
                decls.append("static void lumaTrivial_() { %s }" % (claim % (("::" + name,) * claim.count("%s"))))
                after.append('LUMA_TYPE_SECTION const luma::TypeRow %s::lumaRow_ = {"%s", sizeof(::%s), alignof(::%s), %s, %du, %s, %du, %s};' % (
                    name, cstr(name), name, name, "lumaFields_" if field_rows else "nullptr", len(field_rows),
                    "lumaBases_" if base_rows else "nullptr", len(base_rows),
                    "&typeid(::%s)" % name if r["polymorphic"] else "nullptr"))
                self.inside_decls[name] = " " + " ".join(decls) + " "
                n += 1
                continue
            if field_rows:
                member("", "TypeField", "kF%d" % n, "{%s}" % ", ".join(field_rows), "[]")
            if base_rows:
                member("", "TypeBase", "kB%d" % n, "{%s}" % ", ".join(base_rows), "[]")
            # Contract objects: a polymorphic record's type_info (the RTTI data, -frtti), which the kernel finds from an
            # object's vtable. typeid of a type is a constant address, never a call into the C++ runtime.
            vtable = r.get("vtable") or {"prefix": 0, "count": 0}
            member("LUMA_TYPE_SECTION ", "TypeRow", "kT%d" % n, '{"%s", sizeof(::%s), alignof(::%s), %s, %du, %s, %du, %s, %du, %du}' % (
                cstr(name), name, name, "kF%d" % n if field_rows else "nullptr", len(field_rows),
                "kB%d" % n if base_rows else "nullptr", len(base_rows),
                "&typeid(R%d)" % n if r["polymorphic"] else "nullptr", vtable["prefix"], vtable["count"]))
            (held if unit else lines).append(claim % (("R%d" % n,) * claim.count("%s")))
            for k, v in calls:
                if unit:
                    held.append("static void kV%d_%d(void** luma_a, void* luma_r);" % (n, k))
                    lines.append("void LumaUnitRows::kV%d_%d(void** luma_a, void* luma_r) { %s }" % (n, k, thunk(v, "R%d" % n)))
                else:
                    lines.append("void kV%d_%d(void** luma_a, void* luma_r) { %s }" % (n, k, thunk(v, "R%d" % n)))
                scope = "LumaUnitRows::" if unit else ""
                member("", "Function", "kF%d_%d" % (n, k), '{"%s", "%s", %skV%d_%d, nullptr}' % (
                    cstr(v["name"]), cstr(v["signature"]), scope, n, k))
                slot = ""
                if name in slot_records and "..." not in v["signature"]:
                    lines.extend(slot_code(n, k, v))
                    slot = ", kSp%d_%d, &kSg%d_%d" % (n, k, n, k)
                member("LUMA_METHOD_SECTION ", "MethodRow", "kM%d_%d" % (n, k), "{&%skF%d_%d, %s%s}" % (
                    scope, n, k, method_names(name, v), slot))
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
                # A constant with a known value is read from the self-description and binds nothing (types.md,
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
        the game's own s32 is: the name a modder reads, with no record. One may name another (bionic's uintptr_t is
        __uintptr_t, Darwin's size_t __darwin_size_t): each alias row's text is named too, until none is new."""
        while True:
            named = set()
            for _, text in self.texts():
                named.update(re.findall(r"(?<![\w:])[A-Za-z_]\w*", text or ""))
            new = sorted(n for n in named & set(self.foreign) if n not in self.aliases and n not in self.records)
            if not new:
                return
            for name in new:
                under = self.api.underlying(self.foreign[name])
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
            sys.exit("gen_types.py: type texts that name no row, built-in or left-out name (language/types.md):\n  " +
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


def thunk(v, self_type):
    """A virtual call's body with LumaFunction.invoke's form: args[0] is the object as the declaring class, the call is
    unqualified (the compiler dispatches by the object's own class), the result goes where Result<R> puts it, and
    kAskKind asks its kind as a function row's invoke answers (luma_hook.h)."""
    c = v["call"]
    passed = ", ".join("::luma::arg<%s>(luma_a[%d])" % (p, i + 1) for i, p in enumerate(c["params"]))
    self_type = ("const " if c["const"] else "") + self_type
    return ("using luma_result = ::luma::Type<%s>; "
            "if (__builtin_expect(luma_a == ::luma::kAskKind, 0)) { "
            "*static_cast<unsigned*>(luma_r) = ::luma::Result<luma_result>::kKind; return; } "
            "::luma::Result<luma_result>::put(luma_r, [&]() -> luma_result { "
            "return static_cast<%s*>(luma_a[0])->%s(%s); });" % (c["result"], self_type, c["spelling"], passed))


def slot_code(n, k, v):
    """Contract classes, overrides by slot: for the virtual v of record row n (alias R<n>), the code a mod class's table
    takes in its slot and what its proceed calls.
      kSe<n>_<k>::call  the entry: a member function with the virtual's parameters and result (so its calling convention
                        is the slot's), handing the call to luma::slotCall with the arguments as an invoke takes them
      kSo<n>_<k>        the invoke of kSg: calls the code the slot held in the table of the object's class before (the
                        base's own), as a member function of the entry's type, so the convention is the same again
      kSg<n>_<k>        the Function the call carries: the virtual's name and signature, kSo as its invoke
      kSp<n>_<k>        writes &R<n>::name (the kernel reads its slot per ABI) and &kSe<n>_<k>::call (the entry's code)"""
    c = v["call"]
    q = " const" if c["const"] else ""
    res = "::luma::Type<%s>" % c["result"]
    params = ", ".join("::luma::Type<%s> p%d" % (p, i) for i, p in enumerate(c["params"]))
    types = ", ".join("::luma::Type<%s>" % p for p in c["params"])
    pointers = "".join(", const_cast<void*>(static_cast<const volatile void*>(&p%d))" % i for i in range(len(c["params"])))
    passed = ", ".join("::luma::arg<%s>(luma_a[%d])" % (p, i + 1) for i, p in enumerate(c["params"]))
    e, o, g, s = "kSe%d_%d" % (n, k), "kSo%d_%d" % (n, k), "kSg%d_%d" % (n, k), "kSp%d_%d" % (n, k)
    return [
        "struct %s { %s call(%s)%s; };" % (e, res, params, q),
        "void %s(void** luma_a, void* luma_r);" % o,
        'const luma::Function %s = {"%s", "%s", %s, nullptr};' % (g, cstr(v["name"]), cstr(v["signature"]), o),
        "%s %s::call(%s)%s { void* luma_a[] = {const_cast<void*>(static_cast<const volatile void*>(this))%s}; "
        "return ::luma::slotRun<%s>(&%s, luma_a); }" % (res, e, params, q, pointers, res, g),
        "void %s(void** luma_a, void* luma_r) { using luma_result = %s; "
        "if (__builtin_expect(luma_a == ::luma::kAskKind, 0)) { "
        "*static_cast<unsigned*>(luma_r) = ::luma::Result<luma_result>::kKind; return; } "
        "::luma::tBody = nullptr; "
        "const auto luma_f = ::luma::memberAt<luma_result (%s::*)(%s)%s>(::luma::slotOriginal(luma_a[0], &%s)); "
        "::luma::Result<luma_result>::put(luma_r, [&]() -> luma_result { "
        "return (static_cast<%s%s*>(luma_a[0])->*luma_f)(%s); }); }" % (o, res, e, types, q, g, "const " if q else "", e, passed),
        "void %s(::luma::MemberBytes* pV, ::luma::MemberBytes* pE) { "
        "const auto v = static_cast<%s (R%d::*)(%s)%s>(&R%d::%s); const auto e = &%s::call; "
        'static_assert(sizeof(v) <= sizeof(pV->bytes) && sizeof(e) <= sizeof(pE->bytes), "a member pointer fits"); '
        "__builtin_memcpy(pV->bytes, &v, sizeof(v)); pV->size = sizeof(v); "
        "__builtin_memcpy(pE->bytes, &e, sizeof(e)); pE->size = sizeof(e); }" % (s, res, n, types, q, n, c["spelling"], e),
    ]


def class_bases(records):
    """Contract classes: the records a mod class may take as its base, so its table copy and its overrides by slot
    stand on them: NameObj's line (the factory makes them) and PlanetGravity (LumaCore.make_gravity makes it)."""
    return descends_from_nameobj(records) | ({"PlanetGravity"} & set(records))


def descends_from_nameobj(records):
    """Contract classes: NameObj and each record whose base at offset 0 (not virtual) is one."""
    memo = {}

    def walk(name, depth):
        if name in memo:
            return memo[name]
        memo[name] = False  # a malformed walk ends here; a record is never its own base
        r = records.get(name)
        memo[name] = name == "NameObj" or (r is not None and depth < 64 and any(
            b["offset"] == 0 and not b["virtual"] and walk(b["name"], depth + 1) for b in r["bases"]))
        return memo[name]

    return {name for name in records if walk(name, 0)}


def primary_root(records, name):
    """The record that brings the table at `name`'s start: down its polymorphic bases at offset 0 (not virtual)."""
    for _ in range(64):
        r = records.get(name)
        below = next((b["name"] for b in (r["bases"] if r else ()) if b["offset"] == 0 and not b["virtual"] and
                      records.get(b["name"], {}).get("polymorphic")), None)
        if below is None:
            return name
        name = below
    return None


MS_TABLE = re.compile(r"^VFTable for '(.*)' \((\d+) entr(?:y|ies)\)\.$")
IT_TABLE = re.compile(r"^Vtable for '(.*)' \((\d+) entr(?:y|ies)\)\.$")
ENTRY = re.compile(r"^\s*(\d+) \| (.*)$")


def read_vtable_dump(dump, records, names):
    """The compiler's -fdump-vtable-layouts text, per ABI. Microsoft's: one table per vfptr, "VFTable for 'A' in ... 'X'
    (N entries)." (A brought the vfptr, X is the record), its first entry the RTTI locator: the table at X's start is the
    one A = primary_root(X) brought, prefix 1. Itanium's: one group per record, "Vtable for 'X' (N entries).", whose
    entries before "-- (X, 0) vtable address --" are offset_to_top and the RTTI (prefix 2; a vcall or vbase offset there,
    a virtual base, is no layout this takes), and whose table at the start ends at the next offset_to_top. A name the dump
    gives two layouts is none. Returns {record: (prefix, count)}."""
    microsoft = {}
    itanium = {}
    lines = dump.splitlines()
    i = 0
    while i < len(lines):
        m = MS_TABLE.match(lines[i])
        if m:
            path = m.group(1).split("' in '")
            microsoft.setdefault(path[-1], set()).add((path[0], int(m.group(2))))
            i += 1
            continue
        m = IT_TABLE.match(lines[i])
        if not m:
            i += 1
            continue
        name, total = m.group(1), int(m.group(2))
        entries, prefix = [], None
        i += 1
        while i < len(lines) and lines[i].strip():
            e = ENTRY.match(lines[i])
            if e:
                entries.append(e.group(2).strip())
            elif lines[i].strip() == "-- (%s, 0) vtable address --" % name and prefix is None:
                prefix = len(entries)
            i += 1
        ok = prefix == 2 and entries[:1] == ["offset_to_top (0)"] and len(entries) == total
        end = next((j for j in range(2, len(entries)) if entries[j].startswith("offset_to_top")), len(entries))
        layout = (2, end) if ok else None
        itanium[name] = layout if itanium.get(name, layout) == layout else None
    out = {}
    for name in names:
        if name in itanium:
            if itanium[name] is not None:
                out[name] = itanium[name]
            continue
        root = primary_root(records, name)
        tables = [count for intro, count in microsoft.get(name, ()) if intro == root]
        if len(tables) == 1:
            out[name] = (1, tables[0])
    return out


def vtable_layouts(raw, records, unit):
    """Contract classes, overrides by slot: the table at the start of each polymorphic record the factory makes, as the
    compiler lays it out. A unit of the headers calls each one's destructor through a pointer (a virtual call: the
    compiler lays out the record's tables) and is compiled with the build's own command and -fdump-vtable-layouts (the
    code is thrown away). A unit that does not compile stops the build. Returns {record: (prefix, count)}; a record not
    there has no layout the dump gives, and no mod class takes it as its base (said at define)."""
    names = sorted(n for n in class_bases(records) if records[n].get("polymorphic"))
    line = descends_from_nameobj(records)
    path = OUT / "luma_vtables_unit.cpp"

    def use(i, n):
        # NameObj's destructor is virtual: its call lays the table out. A record with no virtual destructor
        # (PlanetGravity) gets it laid out by a pointer to one of its virtuals (the slot is in the pointer).
        if n in line:
            return "void lumaVt%d(V%d* p) { p->~V%d(); }\n" % (i, i, i)
        v = next((v for v in records[n].get("virtuals", ()) if v.get("call")), None)
        if v is None:
            return ""
        c = v["call"]
        return "void lumaVt%d() { (void)static_cast<%s (V%d::*)(%s)%s>(&V%d::%s); }\n" % (
            i, c["result"], i, ", ".join(c["params"]), " const" if c["const"] else "", i, c["spelling"])

    text = unit.read_text(encoding="utf-8") + "".join(
        "using V%d = ::%s;\n%s" % (i, n, use(i, n)) for i, n in enumerate(names))
    path.write_text(text, encoding="utf-8")
    args = [a for a in I.parse_arguments(raw, path) if a != "-fsyntax-only"][:-1]
    args += ["-Xclang", "-fno-access-control", "-Xclang", "-fdump-vtable-layouts", "-c", str(path), "-o", os.devnull]
    done = subprocess.run(args, capture_output=True)
    if done.returncode != 0:
        sys.exit("gen_types.py: the vtable layout unit %s does not compile:\n%s" % (
            path, done.stderr.decode("utf-8", "replace")[:6000]))
    dump = done.stdout.decode("utf-8", "replace") + "\n" + done.stderr.decode("utf-8", "replace")
    found = read_vtable_dump(dump, records, names)
    print(json.dumps({"vtables": len(found), "of": len(names)}))
    return found


def method_names(record, v):
    """A MethodRow's record, own name and parameter list, as C strings."""
    return '"%s", "%s", "%s"' % (cstr(record), cstr(v["call"]["spelling"]), cstr(v["params"]))


def cstr(s):
    return s.replace("\\", "\\\\").replace('"', '\\"')


def merge_functions(folder, records, left_records):
    """C-1: every unit's .functions (instrument.py), in the order of the units' paths and their rows. Returns (functions,
    what is wrong). Each unit's facts are its .rows, row for row; an owner is a record row or a record left out; each
    override has a name, and says whether it is a function row (`row`)."""
    functions, wrong = [], []
    for rows_file in sorted(pathlib.Path(folder).rglob("*.rows")):
        source = rows_file.relative_to(folder).as_posix()[:-len(".rows")]
        rows = [tuple(line.split("\t")[:2]) for line in rows_file.read_text(encoding="utf-8").splitlines() if line]
        facts_file = rows_file.with_suffix(".functions")
        if not facts_file.is_file():
            wrong.append("%s: %d rows and no .functions" % (source, len(rows)))
            continue
        facts = json.loads(facts_file.read_text(encoding="utf-8"))
        have = [(f["name"], f["signature"]) for f in facts]
        if have != rows:
            at = next((i for i, (a, b) in enumerate(zip(rows, have)) if a != b), min(len(rows), len(have)))
            wrong.append("%s: row %d is %s, its facts are for %s" % (
                source, at + 1, " ".join(rows[at]) if at < len(rows) else "(none)",
                " ".join(have[at]) if at < len(have) else "(none)"))
            continue
        for f in facts:
            if not isinstance(f.get("marks"), list):
                wrong.append("%s: %s %s has no marks (C-6)" % (source, f["name"], f["signature"]))
        functions += facts
    every = {(f["name"], f["signature"]) for f in functions}
    for f in functions:
        if f["owner"] is not None and f["owner"] not in records and f["owner"] not in left_records:
            wrong.append("%s %s: its owner %s is no record of the types file" % (f["name"], f["signature"], f["owner"]))
        for o in f["overrides"]:
            if o["name"] is None or o["signature"] is None:
                wrong.append("%s %s: it overrides a function with no name a row can have" % (f["name"], f["signature"]))
            o["row"] = (o["name"], o["signature"]) in every
    return functions, wrong


def merge_numbers(folder, rows):
    """1.5: every unit's .numbers (instrument.py) as luma_numbers.json: each number by its name, signature and place,
    with its unit, line, type and the game's value as written; and the completeness report: how many numbers were
    named, how many uses read them, and each reason a number written in the game's code was not named, counted. A
    number of a function with no row, or a name given twice, is wrong."""
    numbers, wrong, skipped = [], [], {}
    tables, tables_skipped, table_names = [], {}, set()
    uses = 0
    seen = set()
    for listed in sorted(pathlib.Path(folder).rglob("*.numbers")):
        unit = listed.relative_to(folder).as_posix()[:-len(".numbers")]
        part = json.loads(listed.read_text(encoding="utf-8"))
        uses += part["uses"]
        for why, count in part["skipped"].items():
            skipped[why] = skipped.get(why, 0) + count
        for n in part["numbers"]:
            key = (n["name"], n["signature"], n["place"])
            if key in seen:
                wrong.append("%s: %s %s.%d is named twice" % (unit, n["name"], n["signature"], n["place"]))
            seen.add(key)
            if n["place"] != 0 and (n["name"], n["signature"]) not in rows:
                wrong.append("%s: %s %s has numbers and no row" % (unit, n["name"], n["signature"]))
            numbers.append(dict(n, unit=unit))
        for why, count in part.get("tables_skipped", {}).items():
            tables_skipped[why] = tables_skipped.get(why, 0) + count
        for t in part.get("tables", []):
            if t["name"] in table_names:
                wrong.append("%s: the table %s is named twice" % (unit, t["name"]))
            table_names.add(t["name"])
            tables.append(dict(t, unit=unit))
    report = {"named": len(numbers), "uses": uses, "constants": sum(1 for n in numbers if n["place"] == 0),
              "not_named": dict(sorted(skipped.items(), key=lambda kv: -kv[1])),
              "tables": len(tables), "tables_made_writable": sum(1 for t in tables if t["const"]),
              "tables_not_named": dict(sorted(tables_skipped.items(), key=lambda kv: -kv[1]))}
    return {"report": report, "numbers": numbers, "tables": tables}, wrong


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
