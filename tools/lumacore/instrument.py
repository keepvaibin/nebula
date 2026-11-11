"""instrument.py SOURCE OUTPUT --commands compile_commands.json [--entry FILE] [--root DIR] [--depfile FILE] [--libclang PATH]

Writes OUTPUT: SOURCE with one LUMA_HOOK (or LUMA_TELL, for a constructor or destructor) at the start of every
function it defines. That line is the function's row in LumaCore's name table and the test that sends a hooked call
to its hooks (native/include/luma_hook.h). The build compiles OUTPUT in SOURCE's place; SOURCE is never changed, and
OUTPUT keeps SOURCE's line numbers.

Beside OUTPUT: OUTPUT.rows (name, signature, kind, line of every row), OUTPUT.left (what got no row, and why) and
OUTPUT.globals (the mangled name of every variable with external linkage SOURCE defines: gen_types.py gives a header's
global a row only when some unit defines it) and OUTPUT.types (this unit's part of the self-description).

The records, aliases and globals only SOURCE can name (a nerve class, a file's own struct) get their type rows at the end
of OUTPUT (gen_types.py's unit pass), as members of the unit's LumaUnitRows. Each record SOURCE writes declares it a
friend (on the line of its opening brace), so the rows' offsetof and & are the compiler's own, private members too.

The arguments of SOURCE's own compile command are read from --commands, where the build lists OUTPUT (or --entry).
--root: the folder the names of file-local functions are relative to.
"""
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
FUNCTIONS = (K_FUNCTION, K_METHOD, K_CONSTRUCTOR, K_DESTRUCTOR, K_CONVERSION)
SCOPES = (K_UNEXPOSED_DECL, K_STRUCT, K_UNION, K_CLASS, K_NAMESPACE, K_LINKAGE)
# CXTypeKind
T_VOID, T_LVALUE, T_RVALUE = 2, 103, 104
T_UNEXPOSED, T_POINTER, T_BLOCK_POINTER, T_RECORD, T_ENUM, T_TYPEDEF = 1, 101, 102, 105, 106, 107
T_FUNCTION_NO_PROTO, T_FUNCTION_PROTO, T_CONSTANT_ARRAY, T_INCOMPLETE_ARRAY, T_VARIABLE_ARRAY = 110, 111, 112, 114, 115
T_DEPENDENT_ARRAY, T_MEMBER_POINTER, T_ELABORATED, T_ATTRIBUTED = 116, 117, 119, 163
LINKAGE_INTERNAL, LINKAGE_EXTERNAL = 2, 4


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
        self.left = []
        self.defines = set()
        self.main_file = None
        self.types_head = b''   # before the source: what the type rows need
        self.types_tail = b''   # after it: the rows
        self.types_part = {}

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
                    self.function(child)
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
        self.edits += edits
        self.rows.append(f'{name}\t{signature}\t{kind}\t{line}')

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
        text = self.text
        for at, added in sorted(self.edits, key=lambda e: e[0], reverse=True):
            text = text[:at] + added + text[at:]
        marker = f'#include <luma_hook.h>\n#line 1 "{self.source.resolve().as_posix()}"\n'.encode()
        if self.types_tail and not text.endswith(b'\n'):
            text += b'\n'
        write_if_changed(output, self.types_head + marker + text + self.types_tail)
        write_if_changed(output + '.types', (json.dumps(self.types_part, ensure_ascii=False) + '\n').encode('utf-8'))
        write_if_changed(output + '.rows', ('\n'.join(self.rows) + '\n' if self.rows else '').encode())
        write_if_changed(output + '.left', ('\n'.join(self.left) + '\n' if self.left else '').encode())
        write_if_changed(output + '.globals', ''.join(name + '\n' for name in sorted(self.defines) if name).encode())


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
    code = api.parse(index, None, encoded, len(args), None, 0, 0, byref(tu))
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
    types = gen_types.Reader(api, tu, [], unit_pass=True)
    types.walk(api.cursor(tu))
    unit.type_rows(types)
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
