"""ctor_ids.py NEBULA_ROOT [--check]: every constructor of a class deriving from NameObj names its class for mods.

The first statement of each constructor defined in native/petari/src is `lumacore::setClass(this, kClassId<T>)`: base
constructors run first, so the last one to run, the object's own class's, leaves the exact id (and records its vtable,
for objects of the classes whose constructors live in headers). Run once when classes are added; --check lists the
constructors that lack it (exit 1), for the pre-push check.
"""
import pathlib
import re
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import gen_classes  # noqa: E402

ROOT = pathlib.Path(sys.argv[1]).resolve()
CHECK = '--check' in sys.argv
INC = ROOT / 'native' / 'petari' / 'include'
SRC = ROOT / 'native' / 'petari' / 'src'
INCLUDE = '#include "Game/System/LumaCore.hpp"'
MARK = 'lumacore::setClass(this, lumacore::kClassId<'


def skip_ws(s, i):
    while i < len(s):
        if s[i] in ' \t\r\n':
            i += 1
        elif s.startswith('//', i):
            i = s.index('\n', i) if '\n' in s[i:] else len(s)
        elif s.startswith('/*', i):
            i = s.index('*/', i) + 2
        else:
            break
    return i


def skip_group(s, i):
    """s[i] is an opening bracket: the index past its match."""
    pairs = {'(': ')', '{': '}', '[': ']', '<': '>'}
    stack = [pairs[s[i]]]
    i += 1
    while i < len(s) and stack:
        c = s[i]
        if c in '"\'':
            q = c
            i += 1
            while i < len(s) and s[i] != q:
                i += 2 if s[i] == '\\' else 1
        elif c in '({[':
            stack.append(pairs[c])
        elif c in ')}]':
            if stack and c == stack[-1]:
                stack.pop()
        i += 1
    return i


def body_start(s, i):
    """i at a constructor's '(' (its parameters): the index of its body's '{', or -1 (a declaration, or a shape not
    understood)."""
    i = skip_group(s, i)
    i = skip_ws(s, i)
    while s.startswith('noexcept', i) or s.startswith('throw', i):
        i = skip_ws(s, i + 8 if s.startswith('noexcept', i) else i + 5)
        if i < len(s) and s[i] == '(':
            i = skip_ws(s, skip_group(s, i))
    if i < len(s) and s[i] == ':':
        i += 1
        while True:
            i = skip_ws(s, i)
            m = re.compile(r'[A-Za-z_:][\w:]*').match(s, i)
            if m is None:
                return -1
            i = skip_ws(s, m.end())
            if i < len(s) and s[i] == '<':
                i = skip_ws(s, skip_group(s, i))
            if i >= len(s) or s[i] not in '({':
                return -1
            i = skip_ws(s, skip_group(s, i))
            if i < len(s) and s[i] == ',':
                i += 1
                continue
            break
    return i if i < len(s) and s[i] == '{' else -1


def main():
    rows, _ = gen_classes.classes(INC)
    names = {q for (q, _, _, _) in rows if q != 'NameObj' and '::' not in q}
    missing = []
    changed = 0
    for path in sorted(SRC.rglob('*.cpp')):
        if 'generated' in path.parts:
            continue
        with open(path, encoding='utf-8', newline='') as f:  # line ends kept as they are
            s = f.read()
        if '::' not in s:
            continue
        nl = '\r\n' if '\r\n' in s else '\n'
        edits = []
        for m in re.finditer(r'^([A-Za-z_]\w*)::\1\s*\(', s, re.M):
            cls = m.group(1)
            if cls not in names:
                continue
            b = body_start(s, m.end() - 1)
            if b < 0:
                continue
            after = s[b + 1:b + 400]
            if MARK in after.split('}', 1)[0] or MARK in after[:200]:
                continue
            edits.append((b, cls))
        if not edits:
            continue
        if CHECK:
            missing += [f'{path.relative_to(ROOT).as_posix()}: {cls}' for (_, cls) in edits]
            continue
        for b, cls in sorted(edits, reverse=True):
            # The body's own indentation: the line after '{', or four spaces.
            stmt = f'{nl}    {MARK} {cls} >);'
            if s[b + 1] == '}':
                stmt += nl  # an empty body: the closing brace on its own line
            s = s[:b + 1] + stmt + s[b + 1:]
        if INCLUDE not in s:
            includes = list(re.finditer(r'^#include [^\r\n]*', s, re.M))
            at = includes[-1].end() if includes else 0
            s = s[:at] + (nl if at else '') + INCLUDE + ('' if at else nl) + s[at:]
        with open(path, 'w', encoding='utf-8', newline='') as f:
            f.write(s)
        changed += 1
    if CHECK:
        for line in missing:
            print('constructor without its class id:', line)
        raise SystemExit(1 if missing else 0)
    print(f'{changed} files')


if __name__ == '__main__':
    main()
