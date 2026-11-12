"""unlift_tunables.py check|write ROOT: undoes the previous engine's "tunables" lift (commits 78654b4c4, f357f1bca,
b85de281f, 445f57337), which rewrote the game's source by hand-run tool: each number into a named file static and a
record in the "nebtun" section. Plan 1.5 under rule 4 (generated, not listed): the build's instrument.py names every
number in a row's code and every file constant from the game's own source, so the source goes back to it.

  NEBULA_TUNABLE_NAMED(type, name, "key", number);   the line goes; each use of `name` in the file is `number` again
  NEBULA_TUNABLE(type, name, number);                 const type name = number;
  NEBULA_TUNABLE_RECORD(type, name, {...});           const type name = {...};
  NEBULA_TUNABLE_RECORDS(type, name, size, {...});    const type name[size] = {...};
  NEBULA_TUNABLE_LIST(type, name, size, {...});       const type name[size] = {...};

and the file's #include "Game/Util/Tunable.hpp" goes once nothing in it is a tunable. A use of a named number that this
cannot put back exactly (inside a string, a name used in another file) stops the file, named. ROOT: native/petari."""
import pathlib
import re
import sys

MACROS = ('NEBULA_TUNABLE_NAMED', 'NEBULA_TUNABLE_RECORDS', 'NEBULA_TUNABLE_RECORD', 'NEBULA_TUNABLE_LIST',
          'NEBULA_TUNABLE')
CALL = re.compile(r'\b(NEBULA_TUNABLE(?:_NAMED|_RECORDS|_RECORD|_LIST)?)\s*\(')
INCLUDE = re.compile(r'^[ \t]*#include "Game/Util/Tunable\.hpp"[ \t]*\n', re.M)
# The lift's own heading lines above its blocks, each with the blank line after it.
HEADINGS = re.compile(r'^// The (?:numbers this file\'s functions give the game\'s motion and animation calls|step counts of '
                      r'this file\'s functions), as tunables: "[^"\n]*"\.\n(?:\n)?', re.M)


class Stop(Exception):
    pass


def arguments(text, at):
    """The top-level arguments of the call whose '(' is at `at`, and the offset just past its ')'."""
    args, depth, start, i = [], 0, at + 1, at
    while i < len(text):
        c = text[i]
        if c in '"\'':
            q = c
            i += 1
            while i < len(text) and text[i] != q:
                i += 2 if text[i] == '\\' else 1
        elif c in '([{':
            depth += 1
        elif c in ')]}':
            depth -= 1
            if depth == 0:
                args.append(text[start:i].strip())
                return args, i + 1
        elif c == ',' and depth == 1:
            args.append(text[start:i].strip())
            start = i + 1
        i += 1
    raise Stop('a call with no end at %d' % at)


def code_spans(text):
    """Offsets of comments and string or character literals: a name inside them is not a use."""
    spans, i = [], 0
    while i < len(text):
        if text.startswith('//', i):
            j = text.find('\n', i)
            j = len(text) if j < 0 else j
            spans.append((i, j))
            i = j
        elif text.startswith('/*', i):
            j = text.find('*/', i + 2)
            j = len(text) if j < 0 else j + 2
            spans.append((i, j))
            i = j
        elif text[i] in '"\'' and not (text[i] == '\'' and i > 0 and (text[i - 1].isalnum())):
            q, j = text[i], i + 1
            while j < len(text) and text[j] != q and text[j] != '\n':
                j += 2 if text[j] == '\\' else 1
            spans.append((i, j + 1))
            i = j + 1
        else:
            i += 1
    return spans


def inside(spans, at):
    for a, b in spans:
        if a <= at < b:
            return True
        if a > at:
            return False
    return False


def unlift(text):
    """(new text, numbers put back, constants restored, tables restored)."""
    named = {}
    out, last, constants, tables = [], 0, 0, 0
    for m in CALL.finditer(text):
        if m.start() < last:
            continue
        line_start = text.rfind('\n', 0, m.start()) + 1
        if text[line_start:m.start()].strip():
            raise Stop('line %d: the macro is not alone at its line start' % (text.count('\n', 0, m.start()) + 1))
        if text.startswith('#define', line_start) or text[line_start:m.start()].lstrip().startswith('#'):
            continue
        args, end = arguments(text, m.end() - 1)
        if end >= len(text) or text[end] != ';':
            raise Stop('line %d: %s( ... ) not followed by ;' % (text.count('\n', 0, m.start()) + 1, m.group(1)))
        end += 1
        indent = text[line_start:m.start()]
        kind = m.group(1)
        out.append(text[last:line_start])
        if kind == 'NEBULA_TUNABLE_NAMED':
            if len(args) != 4:
                raise Stop('%s with %d arguments' % (kind, len(args)))
            named[args[1]] = args[3]
            # The whole line goes, with its line end.
            if end < len(text) and text[end] == '\n':
                end += 1
            # A mark where the line was: the blank line the lift put after its block goes with the block.
            out.append('\x00')
        elif kind == 'NEBULA_TUNABLE':
            if len(args) != 3:
                raise Stop('%s with %d arguments' % (kind, len(args)))
            out.append('%sconst %s %s = %s;' % (indent, args[0], args[1], args[2]))
            constants += 1
        elif kind == 'NEBULA_TUNABLE_RECORD':
            if len(args) != 3:
                raise Stop('%s with %d arguments' % (kind, len(args)))
            out.append('%sconst %s %s = %s;' % (indent, args[0], args[1], args[2]))
            tables += 1
        else:
            if len(args) != 4:
                raise Stop('%s with %d arguments' % (kind, len(args)))
            out.append('%sconst %s %s[%s] = %s;' % (indent, args[0], args[1], args[2], args[3]))
            tables += 1
        last = end
    out.append(text[last:])
    # Where the lift's lines were (each a mark): one blank line stays if there was any around them, else none.
    text = HEADINGS.sub('\x00', ''.join(out))
    text = re.sub(r'\n(?:\x00|\n)*\x00(?:\x00|\n)*', lambda m: '\n\n' if m.group(0).count('\n') >= 2 else '\n', text)
    if '\x00' in text:
        raise Stop('a lift line at the very start of the file')
    numbers = 0
    if named:
        spans = code_spans(text)
        pattern = re.compile(r'\b(%s)\b' % '|'.join(re.escape(n) for n in sorted(named, key=len, reverse=True)))
        pieces, last = [], 0
        for m in pattern.finditer(text):
            if inside(spans, m.start()):
                raise Stop('%s is named inside a comment or string' % m.group(1))
            number = named[m.group(1)]
            before = text[m.start() - 1] if m.start() else ''
            pieces.append(text[last:m.start()])
            # `a -cNum` with a number `-1.0f` must not become `a --1.0f`.
            if number[:1] in '+-' and before in '+-':
                pieces.append(' ')
            pieces.append(number)
            last = m.end()
            numbers += 1
        pieces.append(text[last:])
        text = ''.join(pieces)
        if numbers == 0:
            raise Stop('named numbers with no use: %s' % ', '.join(list(named)[:5]))
    if not CALL.search(text):
        text = INCLUDE.sub('', text, count=1)
    return text, numbers, constants, tables


def main():
    if len(sys.argv) != 3 or sys.argv[1] not in ('check', 'write'):
        sys.exit(__doc__)
    root = pathlib.Path(sys.argv[2])
    files = sorted(p for p in (root / 'src').rglob('*.cpp') if p.name != 'Tunable.cpp')
    totals = [0, 0, 0, 0]
    stopped = 0
    names_everywhere = {}
    for p in files:
        raw = p.read_bytes().decode('utf-8')
        if 'NEBULA_TUNABLE' not in raw:
            continue
        crlf = '\r\n' in raw
        text = raw.replace('\r\n', '\n')
        try:
            new, numbers, constants, tables = unlift(text)
        except Stop as e:
            stopped += 1
            print('%s: STOPPED: %s' % (p.relative_to(root).as_posix(), e))
            continue
        for m in re.finditer(r'NEBULA_TUNABLE_NAMED\s*\(\s*[^,]+,\s*(\w+)', text):
            names_everywhere.setdefault(m.group(1), []).append(p)
        totals[0] += 1
        totals[1] += numbers
        totals[2] += constants
        totals[3] += tables
        if sys.argv[1] == 'write':
            p.write_bytes((new.replace('\n', '\r\n') if crlf else new).encode('utf-8'))
    # A named number is its file's static: no other file can use it. Say so if any header names one.
    if names_everywhere:
        anywhere = re.compile(r'\b(%s)\b' % '|'.join(re.escape(n) for n in sorted(names_everywhere, key=len, reverse=True)))
        for h in (root / 'include').rglob('*.hpp'):
            m = anywhere.search(h.read_text(encoding='utf-8', errors='replace'))
            if m:
                print('%s names %s: STOPPED' % (h.relative_to(root).as_posix(), m.group(1)))
                stopped += 1
    print('%d files, %d numbers put back, %d constants, %d tables; %d stopped' % (*totals, stopped))
    sys.exit(1 if stopped else 0)


if __name__ == '__main__':
    main()
