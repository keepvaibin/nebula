"""The class tree part of gen_lumacore.py: every class deriving from NameObj, from the game's headers.

classes(INC) -> list of (qualified name, primary base qualified name, header relative path, base text), NameObj first,
each parent before its children. Template classes are not rows (they have no one size); a class whose base is a
template's instance takes that template's own base as its parent. Nested classes are not rows.
"""
import re

COMMENT = re.compile(r'//[^\n]*|/\*.*?\*/', re.S)
STRING = re.compile(r'"(?:\\.|[^"\\\n])*"|\'(?:\\.|[^\'\\\n])*\'')
HEAD = re.compile(r'\b(class|struct|namespace)\b')


def split_top(text, sep=','):
    out, depth, cur = [], 0, ''
    for c in text:
        if c in '<(':
            depth += 1
        elif c in '>)':
            depth -= 1
        if c == sep and depth == 0:
            out.append(cur)
            cur = ''
        else:
            cur += c
    if cur.strip():
        out.append(cur)
    return [x.strip() for x in out]


def scan(text):
    """Yield (scope path of namespaces, kind, name, bases text, is_template) for every class head at namespace scope."""
    text = COMMENT.sub(' ', text)
    text = STRING.sub('""', text)
    # A stack of scopes: ('ns', name) or ('other', None).
    stack = []
    i = 0
    n = len(text)
    pending = None  # ('ns', name) or ('class', ...) waiting for its '{'
    while i < n:
        c = text[i]
        if c == '{':
            if pending is not None:
                stack.append(pending)
                pending = None
            else:
                stack.append(('other', None))
            i += 1
            continue
        if c == '}':
            if stack:
                stack.pop()
            i += 1
            continue
        if c == ';':
            pending = None
            i += 1
            continue
        m = HEAD.match(text, i)
        if m and (i == 0 or not (text[i - 1].isalnum() or text[i - 1] == '_')):
            kind = m.group(1)
            j = m.end()
            if kind == 'namespace':
                nm = re.match(r'\s*([A-Za-z_][\w:]*)?\s*\{', text[j:])
                if nm:
                    pending = ('ns', nm.group(1) or '')
                    i = j + nm.end() - 1
                    continue
                i = j
                continue
            # class/struct head: name, optional final, optional ': bases', then '{' (a definition) or ';' (a declaration)
            hm = re.match(r'\s*(?:__attribute__\s*\(\(.*?\)\)\s*)?([A-Za-z_]\w*)\s*(final\s*)?(:\s*([^{;]*))?\{', text[j:], re.S)
            in_namespace_scope = all(s[0] == 'ns' for s in stack)
            if hm:
                before = text[max(0, i - 200):i]
                is_template = re.search(r'template\s*<[^;{}]*>\s*$', before) is not None
                if in_namespace_scope:
                    ns = '::'.join(s[1] for s in stack if s[1])
                    yield ns, kind, hm.group(1), (hm.group(4) or '').strip(), is_template
                pending = ('class', hm.group(1))
                i = j + hm.end() - 1
                continue
            i = j
            continue
        i += 1


def base_name(base):
    b = re.sub(r'\b(public|protected|private|virtual)\b', ' ', base).strip()
    return b


def strip_template(name):
    return name.split('<', 1)[0].strip()


def classes(inc):
    found = {}       # qualified name -> (bases list, header, is_template)
    dupes = set()
    found_raw = {}   # qualified name -> its bases as written
    for h in sorted(inc.rglob('*.hpp')) + sorted(inc.rglob('*.h')):
        rel = h.relative_to(inc).as_posix()
        try:
            text = h.read_text(encoding='utf-8', errors='replace')
        except OSError:
            continue
        for ns, kind, name, bases, is_template in scan(text):
            q = f'{ns}::{name}' if ns else name
            bl = [base_name(b) for b in split_top(bases)] if bases else []
            if q in found and found[q][1] != rel:
                dupes.add(q)
                continue
            found[q] = (bl, rel, is_template, kind)
            found_raw[q] = bases

    def resolve(name, ns_hint=''):
        """A base as written -> a known qualified name (template args dropped), or None."""
        plain = strip_template(name).lstrip(':')
        if plain in found:
            return plain
        if ns_hint and f'{ns_hint}::{plain}' in found:
            return f'{ns_hint}::{plain}'
        return None

    parent = {}
    for q, (bl, rel, is_template, _) in found.items():
        if not bl:
            continue
        ns = q.rsplit('::', 1)[0] if '::' in q else ''
        # Only a public base makes the class one of NameObj's for mods (a private one hides it, as from the game).
        raw = found_raw.get(q, '')
        first = split_top(raw)[0] if raw else ''
        public = 'public' in first or (found[q][3] == 'struct' and 'private' not in first and 'protected' not in first)
        if not public:
            continue
        p = resolve(bl[0], ns)
        if p is not None:
            parent[q] = p

    def descends(q):
        seen = set()
        while q in parent and q not in seen:
            seen.add(q)
            q = parent[q]
        return q == 'NameObj'

    rows = []
    for q, (bl, rel, is_template, _) in found.items():
        if q == 'NameObj' or (not is_template and descends(q)):
            rows.append(q)

    # A row's parent is its nearest ancestor that is a row (templates skipped).
    row_set = set(rows)

    def row_parent(q):
        p = parent.get(q)
        while p is not None and p not in row_set:
            p = parent.get(p)
        return p

    # Parents first.
    order, placed = [], set()

    def place(q):
        if q in placed:
            return
        p = row_parent(q)
        if p is not None:
            place(p)
        placed.add(q)
        order.append(q)

    for q in sorted(rows, key=lambda x: (x != 'NameObj', x)):
        place(q)
    out = []
    for q in order:
        bl, rel, _, kind = found[q]
        out.append((q, row_parent(q), rel, kind))
    return out, sorted(dupes)


HEADER = "// Generated by tools/lumacore/gen_lumacore.py from the game's headers. Do not edit."


def forward(q, kind):
    if '::' in q:
        ns, name = q.rsplit('::', 1)
        return f'namespace {ns} {{ {kind} {name}; }}'
    return f'{kind} {q};'


def emit_class_ids(rows):
    """lumacore_class_ids.hpp: kClassId<T> for every row (the factory's createNameObj<T> reads it)."""
    lines = [HEADER, '#pragma once', '', '#include <nebula_types.h>', '']
    lines += [forward(q, kind) for (q, _, _, kind) in rows]
    lines += ['', 'namespace lumacore {', "    // A class's id: its row in kClassRows. 0 (NameObj) for a type with no row.",
              '    template < typename T >', '    inline constexpr u16 kClassId = 0;']
    for i, (q, _, _, _) in enumerate(rows):
        lines.append(f'    template <> inline constexpr u16 kClassId< ::{q} > = {i};')
    lines += ['}  // namespace lumacore', '']
    return '\n'.join(lines)


def emit_class_rows(rows):
    """lumacore_classes.cpp: the rows (name, parent, size), each parent checked by the compiler."""
    index = {q: i for i, (q, _, _, _) in enumerate(rows)}
    headers = sorted({rel for (_, _, rel, _) in rows})
    lines = [HEADER, "// Every class deriving from NameObj: kClassRows[id]. A wrong parent fails this file's build.", '',
             '#include "Game/System/LumaCore.hpp"']
    lines += [f'#include "{h}"' for h in headers]
    lines += ['', 'namespace {']
    for i, (q, _, _, _) in enumerate(rows):
        lines.append(f'    void* lc_cast{i}(NameObj* p) {{ return static_cast< ::{q}* >(p); }}')
    lines += ['}  // namespace', '', 'namespace lumacore {', '    extern const ClassRow kClassRows[];',
              '    extern const u16 kClassRowCount;', '    const ClassRow kClassRows[] = {']
    for i, (q, parent, _, _) in enumerate(rows):
        pid = index[parent] if parent is not None else 0
        lines.append(f'        {{"{q}", {pid}, static_cast< u32 >(sizeof(::{q})), lc_cast{i}}},')
    lines += ['    };', f'    const u16 kClassRowCount = {len(rows)};', '}  // namespace lumacore', '']
    for q, parent, _, _ in rows:
        if parent is not None:
            lines.append(f'static_assert(__is_base_of(::{parent}, ::{q}), "{q} does not derive from {parent}");')
    lines.append('')
    return '\n'.join(lines)


if __name__ == '__main__':
    import pathlib
    import sys
    rows, dupes = classes(pathlib.Path(sys.argv[1]))
    print(len(rows), 'classes;', len(dupes), 'names defined in two headers')
    for r in rows[:15]:
        print(r)
    for d in dupes[:20]:
        print('dupe', d)
