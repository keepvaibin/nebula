"""instrument.py SOURCE OUTPUT --commands compile_commands.json [--entry FILE] [--root DIR] [--depfile FILE] [--libclang PATH]

Writes OUTPUT: SOURCE with one LUMA_HOOK (or LUMA_TELL, for a constructor or destructor) at the start of every
function it defines. That line is the function's row in LumaCore's name table and the test that sends a hooked call
to its hooks (native/include/luma_hook.h). The build compiles OUTPUT in SOURCE's place; SOURCE is never changed, and
OUTPUT keeps SOURCE's line numbers.

Beside OUTPUT: OUTPUT.rows (name, signature, kind, line of every row), OUTPUT.left (what got no row, and why) and
OUTPUT.globals (the mangled name of every variable with external linkage SOURCE defines: gen_types.py gives a header's
global a row only when some unit defines it), OUTPUT.types (this unit's part of the self-description) and
OUTPUT.functions (each row's facts, in the rows' order: gen_types.py merges them into luma_functions.json):
  {"name", "signature"}  the row, as OUTPUT.rows has it
  "kind"                 "free", "method", "static", "constructor" or "destructor"
  "owner"                the record's name in the types file; null for a free function, or a record with no name a
                         mod can write (an unnamed namespace's)
  "virtual", "pure", "const"
  "overrides"            for a virtual, each function it overrides directly (clang_getOverriddenCursors), by the
                         name and signature its own row has or would have
  "marks"                C-6: one entry per declared parameter (a method's `this` not counted): null, or
                         {"way": "in"|"out"|"inout"|"kept", "count": index|null, "fixed": n|null, "nullable": bool},
                         from the LUMA_ marks (native/petari/include/Game/LumaMarks.hpp) any declaration of it carries

The records, aliases and globals only SOURCE can name (a nerve class, a file's own struct) get their type rows at the end
of OUTPUT (gen_types.py's unit pass), as members of the unit's LumaUnitRows. Each record SOURCE writes declares it a
friend (on the line of its opening brace), so the rows' offsetof and & are the compiler's own, private members too.

The arguments of SOURCE's own compile command are read from --commands, where the build lists OUTPUT (or --entry).
--root: the folder the names of file-local functions are relative to.
"""
import bisect
import ctypes
import json
import os
import pathlib
import re
import shlex
import sys
from ctypes import POINTER, Structure, byref, c_char_p, c_int, c_uint, c_void_p


class CXString(Structure):
    _fields_ = [('data', c_void_p), ('flags', c_uint)]


class CXCursor(Structure):
    _fields_ = [('kind', c_int), ('xdata', c_int), ('data', c_void_p * 3)]


class CXType(Structure):
    _fields_ = [('kind', c_int), ('data', c_void_p * 2)]


class CXSourceLocation(Structure):
    _fields_ = [('ptr', c_void_p * 2), ('int_data', c_uint)]


class CXSourceRange(Structure):
    _fields_ = [('ptr', c_void_p * 2), ('begin', c_uint), ('end', c_uint)]


VISITOR = ctypes.CFUNCTYPE(c_int, CXCursor, CXCursor, c_void_p)
INCLUSION = ctypes.CFUNCTYPE(None, c_void_p, POINTER(CXSourceLocation), c_uint, c_void_p)

# CXCursorKind
K_UNEXPOSED_DECL, K_STRUCT, K_UNION, K_CLASS, K_FUNCTION, K_METHOD, K_NAMESPACE, K_LINKAGE = 1, 2, 3, 4, 8, 21, 22, 23
K_CONSTRUCTOR, K_DESTRUCTOR, K_CONVERSION, K_FUNCTION_TEMPLATE, K_CLASS_TEMPLATE, K_CLASS_PARTIAL = 24, 25, 26, 30, 31, 32
K_TRANSLATION_UNIT, K_COMPOUND, K_VAR = 350, 202, 9
K_MACRO_EXPANSION, K_FRIEND = 502, 603
FUNCTIONS = (K_FUNCTION, K_METHOD, K_CONSTRUCTOR, K_DESTRUCTOR, K_CONVERSION)
SCOPES = (K_UNEXPOSED_DECL, K_STRUCT, K_UNION, K_CLASS, K_NAMESPACE, K_LINKAGE)
# CXTypeKind
T_VOID, T_LVALUE, T_RVALUE = 2, 103, 104
T_UNEXPOSED, T_POINTER, T_BLOCK_POINTER, T_RECORD, T_ENUM, T_TYPEDEF = 1, 101, 102, 105, 106, 107
T_FUNCTION_NO_PROTO, T_FUNCTION_PROTO, T_CONSTANT_ARRAY, T_INCOMPLETE_ARRAY, T_VARIABLE_ARRAY = 110, 111, 112, 114, 115
T_DEPENDENT_ARRAY, T_MEMBER_POINTER, T_ELABORATED, T_ATTRIBUTED = 116, 117, 119, 163
T_FLOAT = 21
# C-6 (audit A18M-1): the numbers LumaScript crosses with one width on every system. A count: UChar, Char16, Char32,
# UShort, UInt, ULongLong, SChar, Short, Int, LongLong. A pointee: those, Bool, Float and Double. Never plain char (a
# buffer, or text when const), long, unsigned long, wchar_t (4 bytes on Windows, 8 or 4 elsewhere), long double,
# __int128 or a half.
T_INTEGERS = (5, 6, 7, 8, 9, 11, 14, 16, 17, 19)
T_NUMBERS = T_INTEGERS + (3, 21, 22)
# C-6's marks (Game/LumaMarks.hpp); a way is one of the first four
WAYS = ('LUMA_IN', 'LUMA_OUT', 'LUMA_INOUT', 'LUMA_KEPT')
MARKS = WAYS + ('LUMA_COUNT', 'LUMA_COUNT_FIXED', 'LUMA_NULLABLE')
# CXTranslationUnit_DetailedPreprocessingRecord: the macro expansions, where C-6's marks are
PARSE_OPTIONS = 0x01
# A #define whose own text names a mark (A18M-4): the record keeps only the outermost expansion, so such a mark would
# mark nothing.
DEFINE = re.compile(rb'^[ \t]*#[ \t]*define[ \t]+(\w+)((?:\\\r?\n|[^\n])*)', re.M)
COMMENT = re.compile(rb'//[^\n]*|/\*.*?\*/', re.S)
MARK_WORD = re.compile(rb'\bLUMA_(?:OUT|IN|INOUT|KEPT|COUNT|COUNT_FIXED|NULLABLE)\b')
LINKAGE_INTERNAL, LINKAGE_EXTERNAL = 2, 4
# 1.5's walk of a row's code
K_ENUM_DECL, K_FIELD, K_PARM, K_TYPEDEF, K_NAMESPACE_ALIAS, K_USING_DIRECTIVE, K_USING_DECL, K_TYPE_ALIAS = 5, 6, 10, 20, 33, 34, 35, 36
K_TYPE_REF, K_TEMPLATE_REF, K_NAMESPACE_REF, K_MEMBER_REF, K_OVERLOADED_REF, K_VARIABLE_REF = 43, 45, 46, 47, 49, 50
K_UNEXPOSED_EXPR, K_DECL_REF_EXPR, K_INTEGER_LITERAL, K_FLOATING_LITERAL, K_PAREN = 100, 101, 106, 107, 111
K_UNARY_OPERATOR, K_ARRAY_SUBSCRIPT, K_BINARY, K_COMPOUND_ASSIGN, K_UNARY_EXPR = 112, 113, 114, 115, 136
K_CASE, K_IF, K_ASM, K_MS_ASM, K_STATIC_ASSERT = 203, 205, 215, 229, 602
# Casts whose type is written in them (C style, static, dynamic, reinterpret, const, functional): every child but the
# last (the operand) is that type's; and new.
K_CASTS = (117, 124, 125, 126, 127, 128)
K_NEW = 134
SC_STATIC = 3
T_ENUM_KIND = 106
# Whole numbers (bool and the character types too: a const one may be a constant expression)
T_WHOLE = (3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 14, 15, 16, 17, 18, 19)
T_SIGNED, T_UNSIGNED = (13, 14, 16, 17, 18, 19), (4, 5, 6, 7, 8, 9, 10, 11)
# What a number may be read as: the whole numbers but bool, wchar_t and 128 bits; float; double
T_NUMBER_KINDS = T_SIGNED + T_UNSIGNED + (21, 22)


def load(path):
    lib = ctypes.CDLL(path)

    def fn(name, result, *args):
        f = getattr(lib, name)
        f.restype, f.argtypes = result, list(args)
        return f

    api = type('Api', (), {})()
    api.createIndex = fn('clang_createIndex', c_void_p, c_int, c_int)
    api.parse = fn('clang_parseTranslationUnit2FullArgv', c_int, c_void_p, c_char_p, POINTER(c_char_p), c_int, c_void_p,
                   c_uint, c_uint, POINTER(c_void_p))
    api.numDiagnostics = fn('clang_getNumDiagnostics', c_uint, c_void_p)
    api.diagnostic = fn('clang_getDiagnostic', c_void_p, c_void_p, c_uint)
    api.severity = fn('clang_getDiagnosticSeverity', c_int, c_void_p)
    api.formatDiagnostic = fn('clang_formatDiagnostic', CXString, c_void_p, c_uint)
    api.cursor = fn('clang_getTranslationUnitCursor', CXCursor, c_void_p)
    api.visit = fn('clang_visitChildren', c_uint, CXCursor, VISITOR, c_void_p)
    api.location = fn('clang_getCursorLocation', CXSourceLocation, CXCursor)
    api.extent = fn('clang_getCursorExtent', CXSourceRange, CXCursor)
    api.rangeStart = fn('clang_getRangeStart', CXSourceLocation, CXSourceRange)
    api.rangeEnd = fn('clang_getRangeEnd', CXSourceLocation, CXSourceRange)
    api.inMain = fn('clang_Location_isFromMainFile', c_int, CXSourceLocation)
    api.expansion = fn('clang_getExpansionLocation', None, CXSourceLocation, POINTER(c_void_p), POINTER(c_uint),
                       POINTER(c_uint), POINTER(c_uint))
    api.spellingLocation = fn('clang_getSpellingLocation', None, CXSourceLocation, POINTER(c_void_p), POINTER(c_uint),
                              POINTER(c_uint), POINTER(c_uint))
    api.isDefinition = fn('clang_isCursorDefinition', c_uint, CXCursor)
    api.spelling = fn('clang_getCursorSpelling', CXString, CXCursor)
    api.cstring = fn('clang_getCString', c_char_p, CXString)
    api.dispose = fn('clang_disposeString', None, CXString)
    api.type = fn('clang_getCursorType', CXType, CXCursor)
    api.typeSpelling = fn('clang_getTypeSpelling', CXString, CXType)
    api.resultType = fn('clang_getResultType', CXType, CXType)
    api.numArgs = fn('clang_Cursor_getNumArguments', c_int, CXCursor)
    api.argument = fn('clang_Cursor_getArgument', CXCursor, CXCursor, c_uint)
    api.argType = fn('clang_getArgType', CXType, CXType, c_uint)
    api.variadic = fn('clang_isFunctionTypeVariadic', c_uint, CXType)
    api.semanticParent = fn('clang_getCursorSemanticParent', CXCursor, CXCursor)
    api.isStatic = fn('clang_CXXMethod_isStatic', c_uint, CXCursor)
    api.isConst = fn('clang_CXXMethod_isConst', c_uint, CXCursor)
    api.linkage = fn('clang_getCursorLinkage', c_int, CXCursor)
    api.mangling = fn('clang_Cursor_getMangling', CXString, CXCursor)
    api.specialized = fn('clang_getSpecializedCursorTemplate', CXCursor, CXCursor)
    api.isNull = fn('clang_Cursor_isNull', c_int, CXCursor)
    api.definition = fn('clang_getCursorDefinition', CXCursor, CXCursor)
    api.inclusions = fn('clang_getInclusions', None, c_void_p, INCLUSION, c_void_p)
    api.fileName = fn('clang_getFileName', CXString, c_void_p)
    # A type's text with every name qualified (TY-9): LLVM 21 and later.
    if not hasattr(lib, 'clang_getFullyQualifiedName'):
        sys.exit(f'instrument.py: {path} has no clang_getFullyQualifiedName (LLVM 21 or later is needed)')
    api.fullyQualified = fn('clang_getFullyQualifiedName', CXString, CXType, c_void_p, c_uint)
    api.printingPolicy = fn('clang_getCursorPrintingPolicy', c_void_p, CXCursor)
    api.pointeeType = fn('clang_getPointeeType', CXType, CXType)
    api.elementType = fn('clang_getArrayElementType', CXType, CXType)
    api.classType = fn('clang_Type_getClassType', CXType, CXType)
    api.numArgTypes = fn('clang_getNumArgTypes', c_int, CXType)
    api.namedType = fn('clang_Type_getNamedType', CXType, CXType)
    api.modifiedType = fn('clang_Type_getModifiedType', CXType, CXType)
    api.unqualified = fn('clang_getUnqualifiedType', CXType, CXType)
    api.isVirtual = fn('clang_CXXMethod_isVirtual', c_uint, CXCursor)
    api.isPure = fn('clang_CXXMethod_isPureVirtual', c_uint, CXCursor)
    api.overridden = fn('clang_getOverriddenCursors', None, CXCursor, POINTER(POINTER(CXCursor)), POINTER(c_uint))
    api.disposeOverridden = fn('clang_disposeOverriddenCursors', None, POINTER(CXCursor))
    api.referenced = fn('clang_getCursorReferenced', CXCursor, CXCursor)
    api.hashCursor = fn('clang_hashCursor', c_uint, CXCursor)
    api.equalCursors = fn('clang_equalCursors', c_uint, CXCursor, CXCursor)
    api.fileContents = fn('clang_getFileContents', c_void_p, c_void_p, c_void_p, POINTER(ctypes.c_size_t))
    api.varInitializer = fn('clang_Cursor_getVarDeclInitializer', CXCursor, CXCursor)
    api.storageClass = fn('clang_Cursor_getStorageClass', c_int, CXCursor)
    api.tlsKind = fn('clang_getCursorTLSKind', c_int, CXCursor)
    api.canonicalType = fn('clang_getCanonicalType', CXType, CXType)
    api.isConstType = fn('clang_isConstQualifiedType', c_uint, CXType)
    api.sizeOf = fn('clang_Type_getSizeOf', ctypes.c_longlong, CXType)
    api.usr = fn('clang_getCursorUSR', CXString, CXCursor)
    api.access = fn('clang_getCXXAccessSpecifier', c_int, CXCursor)
    return api


def find_libclang(named, compiler):
    if named:
        return named
    folder = pathlib.Path(compiler).resolve().parent
    for candidate in (folder / 'libclang.dll', folder.parent / 'lib' / 'libclang.so', folder.parent / 'lib' / 'libclang.dylib',
                      folder.parent / 'lib64' / 'libclang.so'):
        if candidate.exists():
            return str(candidate)
    for candidate in sorted(folder.parent.glob('lib*/libclang.so*')):
        return str(candidate)
    sys.exit(f'instrument.py: libclang was not found beside {compiler}; name it with --libclang')


def own_resource_dir(libclang, compiler):
    """libclang takes its built-in headers (stdarg.h and the like) from beside the compiler the command names. A libclang
    that is not that compiler's own (a cross build: Android's NDK has none) is given its own folder, or it looks for
    its version's headers in the compiler's tree (seen: the NDK's lib/clang/22 for LLVM 22's libclang)."""
    own = pathlib.Path(libclang).resolve().parent
    if own == pathlib.Path(compiler).resolve().parent:
        return None
    candidates = sorted((d for base in (own.parent / 'lib' / 'clang', own / 'clang') if base.is_dir() for d in base.iterdir()
                         if (d / 'include' / 'stdarg.h').exists()), key=lambda d: [int(x) for x in d.name.split('.') if x.isdigit()])
    if not candidates:
        sys.exit(f'instrument.py: no built-in headers (lib/clang/<version>/include) beside {libclang}')
    return str(candidates[-1])


class Unit:
    def __init__(self, api, tu, source, root):
        self.api, self.tu = api, tu
        self.source = pathlib.Path(source)
        self.text = self.source.read_bytes()
        try:
            self.label = self.source.resolve().relative_to(pathlib.Path(root).resolve()).as_posix()
        except ValueError:
            self.label = self.source.name
        self.edits = []
        self.rows = []
        self.facts = []  # per row, in order: (cursor, name, signature, kind)
        self.functions = []
        self.marks = {}
        self.left = []
        self.defines = set()
        self.main_file = None
        self.types_head = b''   # before the source: what the type rows need
        self.types_tail = b''   # after it: the rows
        self.types_part = {}
        # 1.5: the unit's numbers (numbers_of), each one's uses, the reasons numbers were not named, the constants
        self.numbers = []
        self.number_index = {}
        self.number_uses = []
        self.number_skips = {}
        self.constants = {}
        self.unused_marked = set()
        self.replaces = []

    def string(self, cx):
        raw = self.api.cstring(cx)
        text = raw.decode('utf-8', 'replace') if raw else ''
        self.api.dispose(cx)
        return text

    def children(self, cursor):
        found = []

        def visit(child, _parent, _data):
            found.append(CXCursor.from_buffer_copy(child))
            return 1  # continue: this cursor's children are asked for by the caller

        self.api.visit(cursor, VISITOR(visit), None)
        return found

    def offset(self, location):
        """A location's place in the main file, or None when it is elsewhere or inside a macro's expansion."""
        if not self.api.inMain(location):
            return None
        file, line, column, expanded, spelled = c_void_p(), c_uint(), c_uint(), c_uint(), c_uint()
        self.api.expansion(location, byref(file), byref(line), byref(column), byref(expanded))
        self.api.spellingLocation(location, byref(file), byref(line), byref(column), byref(spelled))
        return expanded.value if expanded.value == spelled.value else None

    def line(self, cursor):
        file, line, column, offset = c_void_p(), c_uint(), c_uint(), c_uint()
        self.api.expansion(self.api.location(cursor), byref(file), byref(line), byref(column), byref(offset))
        return line.value

    def in_main(self, cursor):
        return bool(self.api.inMain(self.api.location(cursor)))

    def expanded_in_main(self, cursor):
        """Written in the main file, or made there by a macro whose use is in it (NEW_NERVE's sInstance)."""
        file, line, column, offset = c_void_p(), c_uint(), c_uint(), c_uint()
        self.api.expansion(self.api.location(cursor), byref(file), byref(line), byref(column), byref(offset))
        if not file.value:
            return False
        if self.main_file is None:
            self.main_file = os.path.normcase(os.path.abspath(self.source))
        return os.path.normcase(os.path.abspath(self.string(self.api.fileName(file)))) == self.main_file

    def defined_here(self, cursor):
        definition = self.api.definition(cursor)
        return not self.api.isNull(definition) and self.in_main(definition)

    def scope(self, cursor):
        """The names around a declaration, outermost first: (name, kind, cursor). None when one has no name to call
        it by or is a template."""
        parts = []
        parent = self.api.semanticParent(cursor)
        while parent.kind != K_TRANSLATION_UNIT and not self.api.isNull(parent):
            if parent.kind in (K_CLASS_TEMPLATE, K_CLASS_PARTIAL, K_FUNCTION_TEMPLATE) or parent.kind in FUNCTIONS:
                return None
            name = self.string(self.api.spelling(parent))
            if parent.kind == K_NAMESPACE:
                parts.append((name, K_NAMESPACE, parent))  # '' for an unnamed namespace
            elif parent.kind in (K_STRUCT, K_UNION, K_CLASS):
                if not name or not self.api.isNull(self.api.specialized(parent)):
                    return None
                parts.append((name, K_CLASS, parent))
            elif parent.kind not in (K_LINKAGE, K_UNEXPOSED_DECL):
                return None
            parent = self.api.semanticParent(parent)
        parts.reverse()
        return parts

    def walk(self, cursor):
        for child in self.children(cursor):
            if child.kind in FUNCTIONS:
                if self.api.isDefinition(child) and self.in_main(child):
                    left = len(self.left)
                    self.function(child)
                    if len(self.left) != left:
                        self.count_numbers(child, 'its function has no row (' + self.left[-1].split('\t')[-1] + ')')
            elif child.kind in (K_FUNCTION_TEMPLATE, K_CLASS_TEMPLATE, K_CLASS_PARTIAL) and self.in_main(child):
                self.count_numbers(child, 'in a template')
            elif child.kind == K_VAR:
                if self.api.isDefinition(child) and self.api.linkage(child) == LINKAGE_EXTERNAL and self.expanded_in_main(child):
                    self.defines.add(self.string(self.api.mangling(child)))
            elif child.kind in SCOPES and self.in_main(child):
                self.walk(child)

    def leave(self, name, line, why):
        self.left.append(f'{name}\t{line}\t{why}')

    def type_text(self, cxtype):
        """A type as the rows write it (contract types TY-3, TY-9): every record, enum and alias by its qualified name,
        the one its own row has (`Mario::MovementStates &`, not `MovementStates &`), so no reader repeats C++'s lookup;
        an alias stays an alias (`u32`, `uint32_t`). None for a type no mod can write."""
        policy = self.__dict__.get('policy')
        if policy is None:
            policy = self.policy = self.api.printingPolicy(self.api.cursor(self.tu))
        text = self.string(self.api.fullyQualified(cxtype, policy, 0))
        # clang qualifies through pointers, references and pointers to members, but leaves an array's element and a
        # function type's result and parameters as written: each named part there is put in its qualified form, the
        # longest first, where it stands as a whole name (not after `::`, which is already a qualified one's tail).
        named = {}
        self.named_parts(cxtype, named, policy)
        for written in sorted(named, key=len, reverse=True):
            if named[written] != written:
                qualified = named[written]
                text = re.sub(r'(?<![\w:])' + re.escape(written) + r'(?![\w])', lambda _: qualified, text)
        text = text.replace('(anonymous namespace)::', '')
        return None if re.search(r'\(unnamed|\(anonymous|\(lambda|type-parameter-|decltype\(|__va_list|\bva_list\b', text) else text

    def named_parts(self, t, named, policy):
        """The named types a type is made of (records, enums, typedefs; not inside template arguments, which a name's
        qualified form holds): written spelling -> qualified spelling."""
        api = self.api
        k = t.kind
        if k in (T_ELABORATED,):
            self.named_parts(api.namedType(t), named, policy)
        elif k == T_ATTRIBUTED:
            self.named_parts(api.modifiedType(t), named, policy)
        elif k in (T_POINTER, T_BLOCK_POINTER, T_LVALUE, T_RVALUE):
            self.named_parts(api.pointeeType(t), named, policy)
        elif k in (T_CONSTANT_ARRAY, T_INCOMPLETE_ARRAY, T_VARIABLE_ARRAY, T_DEPENDENT_ARRAY):
            self.named_parts(api.elementType(t), named, policy)
        elif k == T_MEMBER_POINTER:
            self.named_parts(api.pointeeType(t), named, policy)
            self.named_parts(api.classType(t), named, policy)
        elif k in (T_FUNCTION_NO_PROTO, T_FUNCTION_PROTO):
            self.named_parts(api.resultType(t), named, policy)
            for i in range(max(api.numArgTypes(t), 0)):
                self.named_parts(api.argType(t, i), named, policy)
        elif k in (T_UNEXPOSED, T_RECORD, T_ENUM, T_TYPEDEF):
            u = api.unqualified(t)
            named.setdefault(self.string(api.typeSpelling(u)), self.string(api.fullyQualified(u, policy, 0)))

    def function(self, cursor):
        api = self.api
        spelling = self.string(api.spelling(cursor))
        line = self.line(cursor)
        scope = self.scope(cursor)
        if scope is None:
            return self.leave(spelling, line, 'in a template or an unnamed class')
        qualified = '::'.join([name for name, _, _ in scope if name] + [spelling])
        if not api.isNull(api.specialized(cursor)):
            return self.leave(qualified, line, 'a template specialization')
        if spelling.startswith('operator new') or spelling.startswith('operator delete'):
            return self.leave(qualified, line, 'an allocation function')
        body = [c for c in self.children(cursor) if c.kind == K_COMPOUND]
        if len(body) != 1:
            return self.leave(qualified, line, 'no plain body')
        at = self.offset(api.rangeStart(api.extent(body[0])))
        start = self.offset(api.rangeStart(api.extent(cursor)))
        if at is None or start is None or self.text[at:at + 1] != b'{':
            return self.leave(qualified, line, 'written by a macro')
        head = self.text[start:at].decode('utf-8', 'replace')
        if re.search(r'\b(constexpr|consteval|noreturn|naked)\b', head):
            return self.leave(qualified, line, 'constexpr, noreturn or naked')
        fntype = api.type(cursor)
        if api.variadic(fntype):
            return self.leave(qualified, line, 'takes a variable number of arguments')

        # The class the function belongs to, as the function's own code names it.
        owner = None
        if scope and scope[-1][1] == K_CLASS and cursor.kind != K_FUNCTION:
            owner = '::'.join(name for name, _, _ in scope if name)
            if all(name for name, _, _ in scope):
                owner = '::' + owner
        local = api.linkage(cursor) == LINKAGE_INTERNAL or any(kind == K_CLASS and self.defined_here(c) for _, kind, c in scope)
        name = f'{self.label}#{qualified}' if local else qualified

        edits = []
        params = []
        for i in range(api.numArgs(cursor)):
            parameter = api.argument(cursor, i)
            text = self.type_text(api.argType(fntype, i))
            if text is None:
                return self.leave(qualified, line, f'parameter {i + 1} has a type with no name')
            identifier = self.string(api.spelling(parameter))
            if not identifier:
                end = self.offset(api.rangeEnd(api.extent(parameter)))
                begin = self.offset(api.rangeStart(api.extent(parameter)))
                written = self.text[begin:end] if begin is not None and end is not None else b'('
                if any(c in written for c in b'([=') or not written.strip():
                    return self.leave(qualified, line, f'parameter {i + 1} has no name and cannot be given one')
                identifier = f'luma_p{i + 1}'
                edits.append((end, f' {identifier}'.encode()))
            params.append((text, identifier))

        signature_args = ', '.join(text for text, _ in params)
        pointers = ', '.join(f'(void*)&{identifier}' for _, identifier in params)
        if cursor.kind in (K_CONSTRUCTOR, K_DESTRUCTOR):
            signature = f'({signature_args})'
            first = '(void*)this'
            macro = f'LUMA_TELL({quote(name)}, {quote(signature)}, ({first}{", " + pointers if pointers else ""}))'
            kind = 'tell'
        else:
            result = self.type_text(api.resultType(fntype))
            if result is None:
                return self.leave(qualified, line, 'its result has a type with no name')
            member = owner is not None and not api.isStatic(cursor)
            const = member and bool(api.isConst(cursor))
            signature = f'{result}({signature_args})' + (' const' if const else '')
            passed = ', '.join(f'::luma::arg<{text}>(luma_a[{i + 1}])' for i, (text, _) in enumerate(params))
            taken = [f'::luma::Param<{text}> {identifier}' for text, identifier in params]
            given = [f'static_cast<::luma::Param<{text}>&&>({identifier})' for text, identifier in params]
            if member:
                self_type = f'const {owner}' if const else owner
                call = f'static_cast<{self_type}*>(luma_a[0])->{owner}::{spelling}({passed})'
                taken.insert(0, f'{self_type}* luma_self')
                given.insert(0, 'this')
                first = 'const_cast<void*>(static_cast<const void*>(luma_self))'
            else:
                call = f'{owner + "::" if owner else qualified_call(scope)}{spelling}({passed})'
                first = 'nullptr'
            alias = ''
            if ',' in result:
                alias, result = f'typedef {result} luma_result; ', 'luma_result'
            macro = (f'{alias}LUMA_HOOK({quote(name)}, {quote(signature)}, {result}, {call}, ({", ".join(taken)}), '
                     f'({", ".join(given)}), ({first}{", " + pointers if pointers else ""}))')
            kind = 'hook'
        edits.append((at + 1, f' {macro}'.encode()))
        if self.identity(cursor) != (name, signature):
            # The name and signature an override is given (identity) are the row's own, by one rule: checked on every row.
            sys.exit(f'instrument.py: {self.label}:{line}: the row {name} {signature} is named {self.identity(cursor)} '
                     'by identity()')
        self.edits += edits
        self.rows.append(f'{name}\t{signature}\t{kind}\t{line}')
        role = {K_CONSTRUCTOR: 'constructor', K_DESTRUCTOR: 'destructor'}.get(cursor.kind)
        if role is None:
            role = 'free' if owner is None else 'static' if api.isStatic(cursor) else 'method'
        self.facts.append((cursor, name, signature, role))
        self.numbers_of(cursor, name, signature)

    # 1.5: game data in code by name. Each number written in a row's code (and each use there of a file's number
    # constant) is read from one unit-local record, luma_numbers, which the copy defines before the source with the
    # numbers as written: so the build is the source's, number for number, and a mod changes one by its name.
    def numbers_of(self, cursor, name, signature):
        """Names the numbers of one row's code (its member initializers and body): `name.k`, k from 1 in source order."""
        if self.label.startswith('Game/System/Luma'):
            # The kernel's own numbers (its tables' sizes, its hashes' constants) are not the game's data.
            return self.count_numbers(cursor, "the kernel's own code")
        found = []
        # A constructor's base written as a template (`: InlineVector< T, 4 >()`): its arguments are its children here.
        for child in self.without_template_arguments(self.children(cursor)):
            if child.kind in (K_PARM, K_TYPE_REF, K_TEMPLATE_REF, K_NAMESPACE_REF, K_MEMBER_REF) or 400 <= child.kind < 500:
                continue
            if cursor.kind != K_CONSTRUCTOR and child.kind != K_COMPOUND:
                # Beside the body, a function has only its type (an array size: `f32 (*f())[4]`); a constructor's
                # member initializers are code outside its body.
                self.count_numbers(child, "a function's type")
                continue
            self.find_numbers(child, [cursor], found)
        place = 0
        for start, end, ctype, kind, written, line, constant in found:
            if constant is None:
                place += 1
                number = (name, signature, place)
            else:
                number = self.constants[constant][:3]
            self.number_uses.append((start, end, number))
            if number not in self.number_index:
                self.number_index[number] = len(self.numbers)
                self.numbers.append({'name': number[0], 'signature': number[1], 'place': number[2], 'type': ctype,
                                     'kind': kind, 'value': written, 'line': line})

    def skip_number(self, why, cursor=None):
        self.number_skips[why] = self.number_skips.get(why, 0) + 1

    def count_numbers(self, cursor, why):
        """The numbers of a tree that are not named (the cursor itself too): counted, with the reason, for the
        completeness report."""
        if cursor.kind in (K_INTEGER_LITERAL, K_FLOATING_LITERAL):
            if self.offset(self.api.rangeStart(self.api.extent(cursor))) is not None:
                self.skip_number(why)
            return
        for child in self.children(cursor):
            if child.kind in (K_INTEGER_LITERAL, K_FLOATING_LITERAL):
                if self.offset(self.api.rangeStart(self.api.extent(child))) is not None:
                    self.skip_number(why)
            else:
                self.count_numbers(child, why)

    def find_numbers(self, cursor, parents, found):
        """Every number of an expression tree that may be read at run time, into found (start, end, C type, kind,
        written, line, constant): constant is None for a number written here, or the constant's key for a use of a
        file's number constant. What the language needs as a constant, and what is not a tuning number, is counted
        in number_skips with its reason."""
        api = self.api
        k = cursor.kind
        if k in (K_INTEGER_LITERAL, K_FLOATING_LITERAL):
            return self.literal(cursor, parents, found)
        if k == K_DECL_REF_EXPR:
            # A function's template arguments are its children: what the language needs as a constant.
            self.constant_use(cursor, parents, found)
            return self.count_numbers(cursor, 'a template argument')
        if k in (K_TYPE_REF, K_TEMPLATE_REF, K_NAMESPACE_REF, K_MEMBER_REF, K_OVERLOADED_REF, K_VARIABLE_REF,
                 K_STATIC_ASSERT) or 400 <= k < 500:
            return self.count_numbers(cursor, 'a constant the language needs (a type, a template argument, an assert)')
        if k == K_UNARY_EXPR:
            return self.count_numbers(cursor, 'inside sizeof or alignof')
        if k in (K_ASM, K_MS_ASM):
            return self.count_numbers(cursor, 'inside assembly')
        children = self.children(cursor)
        children = self.without_template_arguments(children)
        if k in K_CASTS and len(children) > 1:
            for child in children[:-1]:
                self.count_numbers(child, "a cast's type (an array size)")
            children = children[-1:]
        elif k == K_NEW:
            kept = []
            for child in children:
                if self.literal_cursor(child) is not None:
                    self.count_numbers(child, "an array size in new")
                else:
                    kept.append(child)
            children = kept
        if k == K_CASE:
            for child in children[:-1]:
                self.count_numbers(child, 'a case label')
            children = children[-1:]
        elif k == K_VAR:
            initializer = api.varInitializer(cursor)
            head = self.head_text(cursor)
            vtype = api.canonicalType(api.type(cursor))
            if api.storageClass(cursor) == SC_STATIC or api.tlsKind(cursor) != 0:
                return self.count_numbers(cursor, "a local static's initializer (lifting it would make the static's start a guarded one)")
            if re.search(r'\bconstexpr\b', head):
                return self.count_numbers(cursor, 'a constexpr')
            if api.isConstType(vtype) and vtype.kind in T_WHOLE + (T_ENUM,):
                return self.count_numbers(cursor, 'a const whole number (the language may use it as a constant)')
            kept = []
            for child in children:
                if not api.isNull(initializer) and api.equalCursors(child, initializer):
                    kept.append(child)
                else:
                    self.count_numbers(child, 'an array size or a type of a local')
            children = kept
        elif k in (K_STRUCT, K_UNION, K_CLASS, K_ENUM_DECL, K_TYPEDEF, K_TYPE_ALIAS, K_FIELD, K_USING_DECL,
                   K_USING_DIRECTIVE, K_NAMESPACE_ALIAS, K_FUNCTION, K_METHOD, K_CLASS_TEMPLATE, K_FUNCTION_TEMPLATE):
            return self.count_numbers(cursor, 'a declaration inside a function (a local type)')
        elif k == K_IF and re.match(rb'if\s+constexpr\b', self.text_of(cursor) or b''):
            self.count_numbers(children[0], 'an if constexpr condition')
            children = children[1:]
        elif k == K_ARRAY_SUBSCRIPT and len(children) == 2:
            self.whole_only(children[1], parents + [cursor], found, 'an index')
            children = children[:1]
        elif k in (K_BINARY, K_COMPOUND_ASSIGN) and len(children) == 2:
            op = self.operator_between(children[0], children[1])
            if op in (b'<<', b'>>', b'&', b'|', b'^', b'<<=', b'>>=', b'&=', b'|=', b'^='):
                for child in children:
                    self.whole_only(child, parents + [cursor], found, 'a bit pattern or a shift count')
                return
        elif k == K_UNARY_OPERATOR and len(children) == 1:
            text = self.text_of(cursor) or b''
            if text.startswith(b'~'):
                return self.whole_only(children[0], parents + [cursor], found, 'a bit pattern or a shift count')
            if text.startswith(b'&'):
                # The address of a file's constant stays the constant's own.
                return self.find_numbers_no_constants(children[0], parents + [cursor], found)
        for child in children:
            self.find_numbers(child, parents + [cursor], found)

    def without_template_arguments(self, children):
        """The children less those written inside a template's argument list (`InlineVector< T, 4 >()`: the 4 is a
        child of the expression, after its TemplateRef), each such one counted."""
        refs = [c for c in children if c.kind == K_TEMPLATE_REF]
        if not refs:
            return children
        spans = []
        for ref in refs:
            end = self.offset(self.api.rangeEnd(self.api.extent(ref)))
            if end is None:
                continue
            at = end
            while at < len(self.text) and self.text[at:at + 1] in b' \t\r\n':
                at += 1
            if self.text[at:at + 1] != b'<':
                continue
            depth = 0
            close = None
            for i in range(at, min(len(self.text), at + 4096)):
                c = self.text[i:i + 1]
                if c == b'<':
                    depth += 1
                elif c == b'>':
                    depth -= 1
                    if depth == 0:
                        close = i
                        break
            if close is not None:
                spans.append((at, close))
        kept = []
        for child in children:
            start = self.offset(self.api.rangeStart(self.api.extent(child)))
            if start is not None and any(a < start < b for a, b in spans):
                self.count_numbers(child, 'a template argument')
            else:
                kept.append(child)
        return kept

    def find_numbers_no_constants(self, cursor, parents, found):
        if cursor.kind == K_DECL_REF_EXPR:
            return self.count_numbers(cursor, 'a template argument')
        if cursor.kind in (K_UNEXPOSED_EXPR, K_PAREN):
            for child in self.children(cursor):
                self.find_numbers_no_constants(child, parents + [cursor], found)
            return
        self.find_numbers(cursor, parents, found)

    def whole_only(self, cursor, parents, found, why):
        """An operand where a whole number is not a tuning number (through implicit conversions and parentheses)."""
        if cursor.kind == K_INTEGER_LITERAL:
            return self.skip_number(why) if self.offset(self.api.rangeStart(self.api.extent(cursor))) is not None else None
        if cursor.kind in (K_UNEXPOSED_EXPR, K_PAREN):
            for child in self.children(cursor):
                self.whole_only(child, parents + [cursor], found, why)
            return
        self.find_numbers(cursor, parents, found)

    def text_of(self, cursor):
        start = self.offset(self.api.rangeStart(self.api.extent(cursor)))
        end = self.offset(self.api.rangeEnd(self.api.extent(cursor)))
        return None if start is None or end is None else self.text[start:end]

    def head_text(self, cursor):
        """A declaration's text up to its name."""
        start = self.offset(self.api.rangeStart(self.api.extent(cursor)))
        at = self.offset(self.api.location(cursor))
        return '' if start is None or at is None else self.text[start:at].decode('utf-8', 'replace')

    def operator_between(self, a, b):
        end = self.offset(self.api.rangeEnd(self.api.extent(a)))
        start = self.offset(self.api.rangeStart(self.api.extent(b)))
        if end is None or start is None or start < end:
            return None
        return COMMENT.sub(b'', self.text[end:start]).strip()

    def number_type(self, cursor, parents):
        """The type the program reads the number as: the implicit conversion's around it when that is a number type
        (so a list initializer does not narrow, and an overload is chosen as before), else the number's own."""
        api = self.api
        for parent in reversed(parents):
            if parent.kind == K_UNEXPOSED_EXPR:
                t = api.canonicalType(api.type(parent))
                if t.kind in T_NUMBER_KINDS:
                    return t
                continue
            if parent.kind == K_PAREN:
                continue
            break
        return api.canonicalType(api.type(cursor))

    def literal(self, cursor, parents, found):
        api = self.api
        start = self.offset(api.rangeStart(api.extent(cursor)))
        end = self.offset(api.rangeEnd(api.extent(cursor)))
        if start is None or end is None:
            return self.skip_number('written inside a macro')
        written = self.text[start:end].decode('ascii', 'replace')
        value = number_value(written, cursor.kind == K_FLOATING_LITERAL)
        if value is None:
            return self.skip_number('a number form instrument.py does not read')
        # A minus written before the number is part of it: "-2.0f" is one number.
        outer = list(parents)
        if outer and outer[-1].kind == K_UNARY_OPERATOR and (self.text_of(outer[-1]) or b'').startswith(b'-') and \
                self.offset(api.rangeEnd(api.extent(outer[-1]))) == end:
            start = self.offset(api.rangeStart(api.extent(outer[-1])))
            written = self.text[start:end].decode('ascii', 'replace')
            value = -value
            outer = outer[:-1]
        if value in (0, 1, -1):
            return self.skip_number('0, 1 or -1 (an identity, not a tuning number)')
        t = self.number_type(cursor, outer)
        kind = number_kind(t.kind, api.sizeOf(t))
        if kind is None:
            t = api.canonicalType(api.type(cursor))
            kind = number_kind(t.kind, api.sizeOf(t))
        if kind is None:
            return self.skip_number('a number of a type a mod does not write (long double, 128 bits)')
        ctype = self.string(api.typeSpelling(t))
        found.append((start, end, ctype, kind, written, self.line(cursor), None))

    def constant_use(self, cursor, parents, found):
        """A use of a file's number constant (`const f32 cWalkSpeed = 2.0f;` at namespace scope or a class's static)
        is read from the constant's one entry, named by the constant."""
        api = self.api
        target = api.referenced(cursor)
        if api.isNull(target) or target.kind != K_VAR:
            return
        key = self.constant_key(target)
        if key is None:
            return
        start = self.offset(api.rangeStart(api.extent(cursor)))
        end = self.offset(api.rangeEnd(api.extent(cursor)))
        if start is None or end is None:
            return self.skip_number("a file's constant used inside a macro")
        entry = self.constants[key]
        found.append((start, end, entry[3], entry[4], entry[5], self.line(cursor), key))

    def constant_key(self, var):
        """The entry of a file's number constant, made at its first use: (name, '', 0) plus its type, kind and
        initializer; None when it is not one (defined in another file, not const, constexpr, not a number written
        out, or private to a class)."""
        api = self.api
        usr = self.string(api.usr(var))
        if usr in self.constants:
            return usr if self.constants[usr] is not None else None
        self.constants[usr] = None
        definition = api.definition(var)
        if api.isNull(definition) or not self.in_main(definition):
            return None
        vtype = api.canonicalType(api.type(definition))
        parent = api.semanticParent(definition)
        if parent.kind in FUNCTIONS or not api.isConstType(vtype) or re.search(r'\bconstexpr\b', self.head_text(definition)):
            return None
        if parent.kind in (K_STRUCT, K_UNION, K_CLASS) and api.access(definition) in (2, 3):
            return None
        kind = number_kind(vtype.kind, api.sizeOf(vtype))
        initializer = api.varInitializer(definition)
        if kind is None or api.isNull(initializer):
            return None
        literal = self.literal_cursor(initializer)
        text = (self.text_of(initializer) or b'').decode('ascii', 'replace').strip()
        if literal is None or not text:
            self.skip_number("a file's constant whose value is not a number written out")
            return None
        scope = self.scope(definition)
        if scope is None:
            return None
        qualified = '::'.join([n for n, _, _ in scope if n] + [self.string(api.spelling(definition))])
        local = api.linkage(definition) == LINKAGE_INTERNAL or any(kind_ == K_CLASS and self.defined_here(c) for _, kind_, c in scope)
        name = f'{self.label}#{qualified}' if local else qualified
        ctype = self.string(api.typeSpelling(api.unqualified(vtype)))
        self.constants[usr] = (name, '', 0, ctype, kind, text)
        if parent.kind not in (K_STRUCT, K_UNION, K_CLASS):
            # Its uses in rows' code read its entry: the constant itself may be read nowhere now.
            start = self.offset(api.rangeStart(api.extent(definition)))
            if start is not None and start not in self.unused_marked:
                self.unused_marked.add(start)
                self.edits.append((start, b'[[maybe_unused]] '))
        return usr

    def literal_cursor(self, cursor):
        """The number an initializer is, through implicit conversions and parentheses, a minus allowed; None when it
        is anything else."""
        while cursor.kind in (K_UNEXPOSED_EXPR, K_PAREN):
            inner = self.children(cursor)
            if len(inner) != 1:
                return None
            cursor = inner[0]
        if cursor.kind == K_UNARY_OPERATOR and (self.text_of(cursor) or b'').startswith(b'-'):
            inner = self.children(cursor)
            return self.literal_cursor(inner[0]) if len(inner) == 1 else None
        return cursor if cursor.kind in (K_INTEGER_LITERAL, K_FLOATING_LITERAL) else None

    def number_code(self):
        """The copy's numbers: the record before the source, each number's uses replaced, the table after it."""
        if not self.numbers:
            return b'', b''
        members = ' '.join(f'{n["type"]} v{i};' for i, n in enumerate(self.numbers))
        values = ', '.join(f'({n["type"]})({n["value"]})' for n in self.numbers)
        head = (f'namespace {{ struct LumaNumbers {{ {members} }} luma_numbers = {{ {values} }}; }}\n').encode()
        for start, end, number in self.number_uses:
            i = self.number_index[number]
            self.replaces.append((start, end, f'(({self.numbers[i]["type"]})luma_numbers.v{i})'.encode()))
        rows = ',\n'.join(f'    {{{quote(n["name"])}, {quote(n["signature"])}, {n["place"]}u, {n["kind"]}u, '
                          f'&luma_numbers.v{i}}}' for i, n in enumerate(self.numbers))
        tail = ('\n// The numbers of this unit\'s code by name (tools/lumacore/instrument.py, 1.5).\nnamespace {\n'
                f'const ::luma::Number luma_number_rows[] = {{\n{rows}\n}};\n'
                f'LUMA_NUMBERS_SECTION const ::luma::Numbers luma_numbers_unit = {{{quote(self.label)}, luma_number_rows, '
                f'{len(self.numbers)}u}};\n}}\n').encode()
        return head, tail

    def identity(self, cursor):
        """The row name and signature function() gives a function, whether this unit defines it or not (an overridden
        one); None when its scope or a type has no name."""
        api = self.api
        scope = self.scope(cursor)
        if scope is None:
            return None
        qualified = '::'.join([name for name, _, _ in scope if name] + [self.string(api.spelling(cursor))])
        member_of = bool(scope) and scope[-1][1] == K_CLASS and cursor.kind != K_FUNCTION
        local = api.linkage(cursor) == LINKAGE_INTERNAL or any(kind == K_CLASS and self.defined_here(c) for _, kind, c in scope)
        name = f'{self.label}#{qualified}' if local else qualified
        fntype = api.type(cursor)
        texts = [self.type_text(api.argType(fntype, i)) for i in range(api.numArgs(cursor))]
        if None in texts:
            return None
        if cursor.kind in (K_CONSTRUCTOR, K_DESTRUCTOR):
            return name, f'({", ".join(texts)})'
        result = self.type_text(api.resultType(fntype))
        if result is None:
            return None
        const = member_of and not api.isStatic(cursor) and bool(api.isConst(cursor))
        return name, f'{result}({", ".join(texts)})' + (' const' if const else '')

    def function_facts(self, types):
        """Each row's facts (OUTPUT.functions); types: gen_types.Reader, which names records as the types file does."""
        api = self.api
        for cursor, name, signature, role in self.facts:
            owner = None if role == 'free' else types.name(api.semanticParent(cursor))
            overrides = []
            found, count = POINTER(CXCursor)(), c_uint()
            api.overridden(cursor, byref(found), byref(count))
            for i in range(count.value):
                base = CXCursor.from_buffer_copy(found[i])
                known = self.identity(base)
                if known is None:
                    # A template instance's function: the instance's name in the types file, the same signature rule.
                    record = types.name(api.semanticParent(base))
                    rest = self.identity_signature(base)
                    known = (f'{record}::{self.string(api.spelling(base))}', rest) if record and rest else (None, None)
                overrides.append({'name': known[0], 'signature': known[1]})
            if count.value:
                api.disposeOverridden(found)
            member = role in ('method', 'constructor', 'destructor')
            self.functions.append({
                'name': name, 'signature': signature, 'kind': role, 'owner': owner,
                'virtual': member and bool(api.isVirtual(cursor)), 'pure': member and bool(api.isPure(cursor)),
                'const': role == 'method' and bool(api.isConst(cursor)), 'overrides': overrides,
                'marks': self.marks_of(cursor)})

    # C-6 (contracts/self-description.md): the parameter marks of Game/LumaMarks.hpp. Each expands to nothing, so the
    # declarations do not show them; the parse's detailed preprocessing record keeps each expansion with its place.
    def spelled(self, location):
        """(file, offset, line) where a location is written."""
        file, line, column, offset = c_void_p(), c_uint(), c_uint(), c_uint()
        self.api.spellingLocation(location, byref(file), byref(line), byref(column), byref(offset))
        return file.value, offset.value, line.value

    def file_name(self, file):
        names = self.__dict__.setdefault('file_names', {})
        if file not in names:
            names[file] = self.string(self.api.fileName(file)).replace('\\', '/') if file else '(no file)'
        return names[file]

    def declarations(self, cursor, found):
        """Every function declaration the unit sees, headers' included (not inside function bodies)."""
        for c in self.children(cursor):
            if c.kind in FUNCTIONS or c.kind == K_FUNCTION_TEMPLATE:
                found.append(c)
            elif c.kind in SCOPES or c.kind in (K_CLASS_TEMPLATE, K_CLASS_PARTIAL, K_FRIEND):
                self.declarations(c, found)
        return found

    def read_marks(self):
        """Each LUMA_ mark bound to the parameter it is written in: after the function's name or the previous
        parameter, up to the end of this one (`LUMA_OUT Vec3f* p`, `const LUMA_OUT Vec3f* p`); checked; merged over a
        function's declarations, an unmarked parameter taking the marked one's. self.marks: hash of a canonical
        declaration -> [(canonical, one entry per parameter)]. Stops, naming the file and line, on a mark that marks no
        parameter, a mark of the wrong form, or two declarations marking one parameter differently."""
        api = self.api
        self.marks = {}
        self.marks_in_macros()
        found = {}  # file -> [(start, end, line, name, argument)], in the order written
        texts = {}
        for c in self.children(api.cursor(self.tu)):
            if c.kind != K_MACRO_EXPANSION:
                continue
            name = self.string(api.spelling(c))
            if name not in MARKS:
                continue
            extent = api.extent(c)
            file, start, line = self.spelled(api.rangeStart(extent))
            end_file, end, _ = self.spelled(api.rangeEnd(extent))
            where = f'{self.file_name(file)}:{line}'
            definition = api.referenced(c)
            if api.isNull(definition) or not self.file_name(self.spelled(api.location(definition))[0]).endswith(
                    '/Game/LumaMarks.hpp'):
                sys.exit(f'instrument.py: {where}: {name} is not the mark native/petari/include/Game/LumaMarks.hpp '
                         'defines')
            if not file or end_file != file:
                sys.exit(f'instrument.py: {where}: {name} is written inside a macro: a mark is written in the declaration')
            argument = None
            if name in ('LUMA_COUNT', 'LUMA_COUNT_FIXED'):
                if file not in texts:
                    texts[file] = pathlib.Path(self.file_name(file)).read_bytes()
                written = texts[file][start:end].decode('utf-8', 'replace')
                match = re.fullmatch(re.escape(name) + r'\s*\(\s*([^()]*?)\s*\)', written, re.S)
                if match is None:
                    sys.exit(f'instrument.py: {where}: {name} is written "{written}": one argument in parentheses')
                argument = match.group(1)
            found.setdefault(file, []).append((start, end, line, name, argument))
        if not found:
            return
        for marks in found.values():
            marks.sort()
        starts = {file: [m[0] for m in marks] for file, marks in found.items()}
        bound = set()
        templates = []  # (file, start, end) of each function template's declaration
        for d in self.declarations(api.cursor(self.tu), []):
            if d.kind == K_FUNCTION_TEMPLATE:
                extent = api.extent(d)
                file, start, _ = self.spelled(api.rangeStart(extent))
                templates.append((file, start, self.spelled(api.rangeEnd(extent))[1]))
                continue
            file, low, line = self.spelled(api.location(d))
            if file not in found:
                continue
            count = api.numArgs(d)
            ends = []
            for i in range(max(count, 0)):
                extent = api.extent(api.argument(d, i))
                if self.spelled(api.rangeStart(extent))[0] != file or self.spelled(api.rangeEnd(extent))[0] != file:
                    break  # a parameter a macro writes: no mark can be bound to it
                ends.append(self.spelled(api.rangeEnd(extent))[1])
            if not ends or len(ends) != count:
                continue
            at = bisect.bisect_left(starts[file], low)
            if at == len(starts[file]) or starts[file][at] >= ends[-1]:
                continue  # no mark between the name and the last parameter's end
            marks = found[file]
            per = [[] for _ in range(count)]
            for i, end in enumerate(ends):
                while at < len(marks) and marks[at][0] < end:
                    start, mark_end, mark_line, name, argument = marks[at]
                    if mark_end <= end:
                        per[i].append((name, argument, mark_line))
                        bound.add((file, start))
                    at += 1
            if not any(per):
                continue
            if self.scope(d) is None:
                first = next(p for p in per if p)
                sys.exit(f'instrument.py: {self.file_name(file)}:{first[0][2]}: {first[0][0]} is on a function in a '
                         'template or an unnamed class: no row carries its marks')
            entries = [self.mark_entry(d, i, per[i], file) for i in range(count)]
            canonical = api.canonical_cursor(d)
            bucket = self.marks.setdefault(api.hashCursor(canonical), [])
            group = next((g for g in bucket if api.equalCursors(g[0], canonical)), None)
            if group is None:
                bucket.append((canonical, entries, [(self.file_name(file), line)] * count))
                continue
            _, merged, places = group
            for i, entry in enumerate(entries):
                if entry is None:
                    continue
                if merged[i] is not None and merged[i] != entry:
                    sys.exit(f'instrument.py: {places[i][0]}:{places[i][1]} and {self.file_name(file)}:{line}: '
                             f'{self.string(api.spelling(d))} parameter {i + 1} is marked {mark_text(merged[i])} in '
                             f'one declaration and {mark_text(entry)} in the other')
                merged[i], places[i] = entry, (self.file_name(file), line)
        for file, marks in found.items():
            for start, _, line, name, _ in marks:
                if (file, start) in bound:
                    continue
                where = f'{self.file_name(file)}:{line}'
                if any(f == file and s <= start < e for f, s, e in templates):
                    sys.exit(f'instrument.py: {where}: {name} is on a function template: no row carries its marks')
                sys.exit(f'instrument.py: {where}: {name} marks no parameter (a mark is written before a parameter\'s '
                         'type in a function\'s declaration)')

    def marks_in_macros(self):
        """Stops on a #define whose own text names a mark (`#define OUT_VEC LUMA_OUT Vec3f*`): the preprocessing record
        keeps only the outermost expansion, so that mark would mark nothing, with no word. A mark in a macro's argument
        (`DECLARE(LUMA_OUT Vec3f*)`) is recorded and binds as written."""
        api = self.api
        files = []
        api.inclusions(self.tu, INCLUSION(lambda file, _stack, _depth, _data: files.append(file)), None)
        for file in files:
            size = ctypes.c_size_t()
            data = api.fileContents(self.tu, file, byref(size))
            if not data:
                continue
            text = ctypes.string_at(data, size.value)
            if b'LUMA_' not in text or self.file_name(file).endswith('/Game/LumaMarks.hpp'):
                continue
            for define in DEFINE.finditer(text):
                mark = MARK_WORD.search(COMMENT.sub(b' ', define.group(2)))
                if mark is not None:
                    line = text.count(b'\n', 0, define.start()) + 1
                    sys.exit(f'instrument.py: {self.file_name(file)}:{line}: {mark.group(0).decode()} is written inside a '
                             f'macro ({define.group(1).decode()}): a mark is written in the declaration itself')

    def mark_entry(self, d, i, names, file):
        """One parameter's marks as the functions file has them (C-6): None, or {"way", "count", "fixed", "nullable"}."""
        if not names:
            return None
        api = self.api
        parameter = api.argument(d, i)

        def stop(why):
            sys.exit(f'instrument.py: {self.file_name(file)}:{names[0][2]}: {self.string(api.spelling(d))} parameter '
                     f'{i + 1} ({self.string(api.spelling(parameter)) or "unnamed"}): {why}')

        written = [name for name, _, _ in names]
        for name in MARKS:
            if written.count(name) > 1:
                stop(f'{name} twice')
        ways = [name for name in written if name in WAYS]
        if len(ways) > 1:
            stop(f'two ways ({ways[0]} and {ways[1]})')
        way = ways[0] if ways else None
        arguments = {name: argument for name, argument, _ in names}
        count, fixed = 'LUMA_COUNT' in arguments, 'LUMA_COUNT_FIXED' in arguments
        if count and fixed:
            stop('LUMA_COUNT and LUMA_COUNT_FIXED on one parameter')
        for mark in ('LUMA_COUNT', 'LUMA_COUNT_FIXED', 'LUMA_NULLABLE'):
            if mark in arguments and way is None:
                stop(f'{mark} with no way (LUMA_IN, LUMA_OUT or LUMA_INOUT)')
            if mark in arguments and way == 'LUMA_KEPT':
                stop(f'{mark} with LUMA_KEPT')
        declared = api.type(parameter)
        pointer = api.canonical(declared)
        if pointer.kind != T_POINTER or not self.markable(api.canonical(api.pointeeType(pointer))):
            stop(f'{way} on a parameter of type {self.string(api.typeSpelling(declared))}: a mark takes a pointer to a '
                 'number of one width on every system (not char, long, wchar_t or long double), a vector or an enum')
        index = None
        if count:
            target = arguments['LUMA_COUNT']
            parameters = [self.string(api.spelling(api.argument(d, j))) for j in range(api.numArgs(d))]
            if not re.fullmatch(r'[A-Za-z_]\w*', target) or target not in parameters:
                stop(f'LUMA_COUNT({target}) names no parameter of this declaration')
            index = parameters.index(target)
            if index == i:
                stop(f'LUMA_COUNT({target}) names its own parameter')
            counted = api.argument(d, index)
            if api.canonical(api.type(counted)).kind not in T_INTEGERS:
                stop(f'LUMA_COUNT({target}) names a parameter of type '
                     f'{self.string(api.typeSpelling(api.type(counted)))}, not an integer of one width on every system')
        size = None
        if fixed:
            text = arguments['LUMA_COUNT_FIXED']
            if not re.fullmatch(r'[0-9]+', text) or not 1 <= int(text) <= 4096:
                stop(f'LUMA_COUNT_FIXED({text}): the count is a number from 1 to 4096')
            size = int(text)
        return {'way': way[len('LUMA_'):].lower(), 'count': index, 'fixed': size, 'nullable': 'LUMA_NULLABLE' in arguments}

    def markable(self, t):
        """A number LumaScript crosses (T_NUMBERS), an enum, or a vector: a record of 2 to 4 floats named x, y(, z(, w)),
        its bases' first (the lumascript-abi table's vec2, vec3, vec4 and quat)."""
        if t.kind in T_NUMBERS or t.kind == T_ENUM:
            return True
        if t.kind != T_RECORD:
            return False
        api = self.api
        names = []

        def flat(record):
            for base in api.members(api.visitBases, record):
                if not flat(api.canonical(api.type(base))):
                    return False
            for field in api.members(api.visitFields, record):
                if api.canonical(api.type(field)).kind != T_FLOAT:
                    return False
                names.append(self.string(api.spelling(field)))
            return True

        return flat(t) and names in (['x', 'y'], ['x', 'y', 'z'], ['x', 'y', 'z', 'w'])

    def marks_of(self, cursor):
        """A row's "marks" (C-6): its function's merged list, or one null per parameter."""
        api = self.api
        count = max(api.numArgs(cursor), 0)
        if self.marks:
            canonical = api.canonical_cursor(cursor)
            for first, merged, _ in self.marks.get(api.hashCursor(canonical), []):
                if api.equalCursors(first, canonical):
                    return list(merged)
        return [None] * count

    def identity_signature(self, cursor):
        """identity()'s signature of a member function whose class has no scope a row can name (a template's instance)."""
        api = self.api
        fntype = api.type(cursor)
        texts = [self.type_text(api.argType(fntype, i)) for i in range(api.numArgs(cursor))]
        result = self.type_text(api.resultType(fntype))
        if None in texts or result is None:
            return None
        const = not api.isStatic(cursor) and bool(api.isConst(cursor))
        return f'{result}({", ".join(texts)})' + (' const' if const else '')

    def type_rows(self, types):
        """The unit pass's rows (types: gen_types.Reader after its unit-pass walk). A record written by a macro gets no
        friend; one that would need it (a member that is not public) stops the build, naming it."""
        api = self.api
        instances = []  # explicit instantiations: (name, file, line)
        closing = {}  # records named through a scope that is not public: name -> offset of their closing brace
        for key, cursor in types.cursors.items():
            if isinstance(key, tuple):
                # A static member that is not public is reached through its class's friend.
                parent = api.semanticParent(cursor)
                if types.api.access(cursor) in (2, 3) and parent.kind in (K_STRUCT, K_UNION, K_CLASS) and \
                        self.offset(api.rangeStart(api.extent(parent))) is None:
                    sys.exit(f'instrument.py: {self.source}: {key[1]} is not public and its class is written by a macro: '
                             'its row cannot name it')
                continue
            start = self.offset(api.rangeStart(api.extent(cursor)))
            end = self.offset(api.rangeEnd(api.extent(cursor)))
            if start is None or end is None:
                hidden = types.api.access(cursor) in (2, 3) or any(
                    f['access'] in ('private', 'protected') for f in types.records[key]['fields'])
                if hidden:
                    sys.exit(f'instrument.py: {self.source}: {key} is written by a macro and has members that are not '
                             'public: its row cannot name them')
                continue
            brace = self.text.find(b'{', start, end)
            if brace < 0 and not api.isNull(api.specialized(cursor)):
                # `template class X<T>;`: an instance of a header's template, the header pass's row
                file, line = types.where(cursor)
                instances.append((key, types.label(file), line))
                continue
            if brace < 0:
                sys.exit(f'instrument.py: {self.source}: no opening brace in the definition of {key}')
            if self.named_privately(cursor):
                # Its rows are its own static members (gen_types.py cpp_rows): declared before its closing brace.
                close = self.text.rfind(b'}', brace, end)
                if close < 0:
                    sys.exit(f'instrument.py: {self.source}: no closing brace in the definition of {key}')
                closing[key] = close
                continue
            self.edits.append((brace + 1, b' friend struct ::LumaUnitRows;'))
        for key, _, _ in instances:
            del types.records[key]
            del types.cursors[key]
        types.inside = set(closing)
        lines = types.cpp_rows(True)
        for key, close in closing.items():
            self.edits.append((close, types.inside_decls[key].encode()))
        self.types_part = types.unit_part()
        self.types_part['instances'] = [{'name': key, 'file': file, 'line': line} for key, file, line in instances]
        if types.row_count:
            self.types_head = b'#include <luma_types.h>\nnamespace { struct LumaUnitRows; }\n'
            self.types_tail = ('\n// The type rows of what only this unit names (tools/lumacore/gen_types.py, unit pass).\n'
                               '#pragma clang diagnostic push\n#pragma clang diagnostic ignored "-Winvalid-offsetof"\n' +
                               '\n'.join(lines) + '#pragma clang diagnostic pop\n').encode()

    def named_privately(self, cursor):
        """Whether a record's qualified name passes through a member of a class that is not public (A::B with B
        private in A): no code outside A's members may write it."""
        api = self.api
        while True:
            parent = api.semanticParent(cursor)
            if parent.kind not in (K_STRUCT, K_UNION, K_CLASS, K_CLASS_TEMPLATE, K_CLASS_PARTIAL):
                return False
            if api.access(cursor) in (2, 3):
                return True
            cursor = parent

    def write(self, output):
        numbers_head, numbers_tail = self.number_code()
        text = self.text
        # Insertions (at, b'...') and replacements (start, end, b'...'), the last first: none overlaps another.
        changes = [(at, at, added) for at, added in self.edits] + list(self.replaces)
        last = None
        for start, end, added in sorted(changes, key=lambda e: (e[0], e[1]), reverse=True):
            if last is not None and end > last:
                sys.exit(f'instrument.py: {self.source}: two changes overlap at offset {start}')
            text = text[:start] + added + text[end:]
            last = start
        marker = f'#include <luma_hook.h>\n#line 1 "{self.source.resolve().as_posix()}"\n'.encode()
        if (self.types_tail or numbers_tail) and not text.endswith(b'\n'):
            text += b'\n'
        write_if_changed(output, self.types_head + numbers_head + marker + text + self.types_tail + numbers_tail)
        write_if_changed(output + '.numbers', (json.dumps({'numbers': self.numbers, 'uses': len(self.number_uses),
                                                           'skipped': self.number_skips}, ensure_ascii=False) + '\n').encode('utf-8'))
        write_if_changed(output + '.types', (json.dumps(self.types_part, ensure_ascii=False) + '\n').encode('utf-8'))
        write_if_changed(output + '.rows', ('\n'.join(self.rows) + '\n' if self.rows else '').encode())
        write_if_changed(output + '.functions', (json.dumps(self.functions, ensure_ascii=False) + '\n').encode('utf-8'))
        write_if_changed(output + '.left', ('\n'.join(self.left) + '\n' if self.left else '').encode())
        write_if_changed(output + '.globals', ''.join(name + '\n' for name in sorted(self.defines) if name).encode())


def number_value(written, floating):
    """A number as C++ writes it, read: None for a form this does not read."""
    text = written.replace("'", '')
    try:
        if floating:
            text = text.rstrip('fFlL')
            return float.fromhex(text) if text.lower().startswith('0x') else float(text)
        text = text.rstrip('uUlLzZ')
        low = text.lower()
        if low.startswith('0x'):
            return int(text[2:], 16)
        if low.startswith('0b'):
            return int(text[2:], 2)
        if len(text) > 1 and text.startswith('0'):
            return int(text[1:], 8)
        return int(text, 10)
    except ValueError:
        return None


def number_kind(type_kind, size):
    """luma::Number's kind (luma_hook.h): 1 f32, 2 f64, 3 s8, 4 u8, 5 s16, 6 u16, 7 s32, 8 u32, 9 s64, 10 u64; None
    for any other type."""
    if type_kind == 21 and size == 4:
        return 1
    if type_kind == 22 and size == 8:
        return 2
    sizes = {1: 0, 2: 2, 4: 4, 8: 6}
    if size not in sizes:
        return None
    if type_kind in T_SIGNED:
        return 3 + sizes[size]
    if type_kind in T_UNSIGNED:
        return 4 + sizes[size]
    return None


def mark_text(entry):
    """A mark entry as its macros are written, for a refusal."""
    text = 'LUMA_' + entry['way'].upper()
    if entry['count'] is not None:
        text += f' LUMA_COUNT(parameter {entry["count"] + 1})'
    if entry['fixed'] is not None:
        text += f' LUMA_COUNT_FIXED({entry["fixed"]})'
    return text + (' LUMA_NULLABLE' if entry['nullable'] else '')


def qualified_call(scope):
    """What goes before a free function's name to call it from its own body. Inside an unnamed namespace: nothing,
    since a qualified name would look in the named namespace around it first."""
    names = [name for name, _, _ in scope]
    if not all(names):
        return ''
    return '::' + ''.join(name + '::' for name in names)


def quote(text):
    return '"' + text.replace('\\', '\\\\').replace('"', '\\"') + '"'


def write_if_changed(path, data):
    path = pathlib.Path(path)
    if path.exists() and path.read_bytes() == data:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)


def write_depfile(path, output, included):
    """What OUTPUT was made from, as a Makefile rule (Ninja reads it)."""

    def escape(name):
        return name.replace('\\', '/').replace(' ', '\\ ')

    names = sorted({escape(name) for name in included if name})
    write_if_changed(path, (escape(output) + ': ' + ' \\\n  '.join(names) + '\n').encode())


def command_for(commands, output):
    wanted = os.path.normcase(os.path.abspath(output))
    for entry in json.loads(pathlib.Path(commands).read_text(encoding='utf-8')):
        file = entry['file']
        if not os.path.isabs(file):
            file = os.path.join(entry['directory'], file)
        if os.path.normcase(os.path.abspath(file)) == wanted:
            return entry.get('arguments') or split_command(entry['command']), entry['directory']
    sys.exit(f'instrument.py: {commands} has no command for {output}')


def split_command(command):
    """A command line as its program would receive it."""
    if os.name != 'nt':
        return shlex.split(command)
    count = c_int()
    split = ctypes.windll.shell32.CommandLineToArgvW
    split.restype, split.argtypes = POINTER(ctypes.c_wchar_p), [ctypes.c_wchar_p, POINTER(c_int)]
    argv = split(command, byref(count))
    return [argv[i] for i in range(count.value)]


def parse_arguments(args, source):
    """The compile command's arguments for a parse of `source`: no output, no dependency file, no input of its own."""
    kept = [args[0]]
    skip = 0
    for a in args[1:]:
        if skip:
            skip -= 1
        elif a in ('-o', '-MF', '-MT', '-MQ'):
            skip = 1
        elif a in ('-c', '-MD', '-MMD', '-MP') or a.startswith('-o') and a.endswith(('.obj', '.o')):
            pass
        elif not a.startswith('-') and a.endswith(('.cpp', '.cc', '.c')):
            pass
        else:
            kept.append(a)
    return kept + ['-fsyntax-only', '-Wno-everything', str(source)]


def main():
    argv = sys.argv[1:]
    options = {}
    for flag in ('--commands', '--root', '--libclang', '--entry', '--depfile'):
        if flag in argv:
            at = argv.index(flag)
            options[flag] = argv[at + 1]
            del argv[at:at + 2]
    if len(argv) != 2 or '--commands' not in options:
        sys.exit(__doc__)
    source, output = (os.path.abspath(a) for a in argv)
    root = os.path.abspath(options.get('--root', os.path.dirname(source)))
    args, directory = command_for(os.path.abspath(options['--commands']), options.get('--entry', output))
    args = parse_arguments(args, pathlib.Path(source).resolve())
    libclang = find_libclang(options.get('--libclang'), args[0])
    resource_dir = own_resource_dir(libclang, args[0])
    if resource_dir is not None and '-resource-dir' not in args:
        args = args[:1] + ['-resource-dir', resource_dir] + args[1:]
    import gen_types  # the type rows' reader; it imports this module (see the end of this file)
    gen_types.set_target(args)
    api = gen_types.Api(libclang)
    os.chdir(directory)
    index = api.createIndex(0, 0)
    tu = c_void_p()
    encoded = (c_char_p * len(args))(*[a.encode() for a in args])
    code = api.parse(index, None, encoded, len(args), None, 0, PARSE_OPTIONS, byref(tu))
    if code != 0 or not tu:
        sys.exit(f'instrument.py: {source} could not be parsed (libclang error {code})')
    errors = []
    for i in range(api.numDiagnostics(tu)):
        diagnostic = api.diagnostic(tu, i)
        if api.severity(diagnostic) >= 3:
            cx = api.formatDiagnostic(diagnostic, 0x1 | 0x2)
            errors.append((api.cstring(cx) or b'').decode('utf-8', 'replace'))
    if errors:
        sys.exit(f'instrument.py: {source} does not compile:\n' + '\n'.join(errors[:20]))
    unit = Unit(api, tu, source, root)
    unit.walk(api.cursor(tu))
    types = gen_types.Reader(api, tu, [], unit_pass=True, unit=unit)
    types.walk(api.cursor(tu))
    unit.type_rows(types)
    unit.read_marks()
    unit.function_facts(types)
    unit.write(output)
    if '--depfile' in options:
        included = []

        def note(file, _stack, _depth, _data):
            included.append(unit.string(api.fileName(file)))

        api.inclusions(tu, INCLUSION(note), None)
        write_depfile(options['--depfile'], output, included)


if __name__ == '__main__':
    # As the module gen_types imports, so both use one set of ctypes structures.
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import instrument
    instrument.main()
