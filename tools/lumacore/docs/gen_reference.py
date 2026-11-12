"""gen_reference.py: the LumaCore reference, generated from the game's own description (plan 10.1; rule 4: generated,
not listed; rule 6: never silent).

Reads what the game's build writes about itself (tools/lumacore/gen_types.py and instrument.py: luma_types.json and
luma_functions.json; later the one self-description file, same fields) and writes the reference a modder reads:
every class and struct with its size, bases, fields and member functions; every enum with its values; every alias;
every global; every free function by namespace; and everything the description LEFT OUT, with the reason. Nothing is
written by hand and nothing is dropped: --check counts every input row on exactly one page and fails otherwise.

Output is plain Markdown in a fixed order with fixed file names, so two runs over the same game give the same bytes
and a change of the game shows as a diff of the pages. search.json lists every name with its page for a site's or an
editor's search.

Usage:  python -I gen_reference.py --types luma_types.json --functions luma_functions.json --out DIR [--check]
"""
import argparse
import collections
import hashlib
import json
import os
import re
import sys


def safe(name):
    """A file name for a C++ name: its plain characters, the rest as '_', and a short hash where anything was changed
    (two names that differ only in what was replaced still get two files)."""
    plain = re.sub(r'[^A-Za-z0-9_.-]', '_', name)
    plain = re.sub(r'_+', '_', plain).strip('_') or 'unnamed'
    if plain != name or len(plain) > 80:
        plain = plain[:72] + '-' + hashlib.sha1(name.encode('utf-8')).hexdigest()[:6]
    return plain


def bucket(name):
    """The folder a name's page goes in: its first letter (classes by the thousand in one folder help nobody)."""
    for ch in name:
        if ch.isalnum():
            return ch.upper()
    return '_'


def code(text):
    """Text as inline code that survives a table cell: backticks keep <, > and *; a bar would end the cell."""
    text = str(text).replace('|', '\\|')
    fence = '``' if '`' in text else '`'
    return fence + text + fence if text else ''


def scope_of(name):
    """The scope of a free function's name: what stands before its last '::' outside template brackets, or the file
    of a file-local one ('path.cpp#name')."""
    if '#' in name:
        return 'file ' + name.split('#', 1)[0]
    depth = 0
    cut = -1
    for i, ch in enumerate(name):
        if ch == '<':
            depth += 1
        elif ch == '>':
            depth -= 1
        elif ch == ':' and depth == 0 and name[i:i + 2] == '::':
            cut = i
    return name[:cut] if cut > 0 else '(global)'


def spelled(function):
    """A function as a declaration: its result, name and parameters."""
    signature = function.get('signature') or '()'
    name = function['name'].split('#', 1)[-1]
    at = signature.find('(')
    result, parameters = (signature[:at].strip(), signature[at:]) if at >= 0 else ('', signature)
    text = (result + ' ' if result else '') + name + parameters
    if function.get('const'):
        text += ' const'
    return text


def notes_of(function):
    """What the description marks on a function: virtual, pure, what it overrides, and each parameter's mark."""
    notes = []
    if function.get('virtual'):
        notes.append('pure virtual' if function.get('pure') else 'virtual')
    if function.get('overrides'):
        notes.append('overrides ' + ', '.join(o['name'] if isinstance(o, dict) else str(o) for o in function['overrides']))
    for i, mark in enumerate(function.get('marks') or []):
        if mark:
            parts = [mark.get('way') or 'marked']
            if mark.get('count') is not None:
                parts.append('count ' + str(mark['count']))
            if mark.get('fixed') is not None:
                parts.append('fixed ' + str(mark['fixed']))
            if mark.get('nullable'):
                parts.append('may be null')
            notes.append('parameter %d: %s' % (i + 1, ', '.join(parts)))
    return '; '.join(notes)


class Pages:
    def __init__(self, out):
        self.out = out
        self.files = {}
        self.search = []
        self.counted = collections.Counter()

    def add(self, path, lines):
        assert path not in self.files, 'two pages named ' + path
        self.files[path] = '\n'.join(lines).rstrip('\n') + '\n'

    def found(self, kind, name, page):
        self.counted[kind] += 1
        if name:
            self.search.append({'name': name, 'kind': kind, 'page': page})

    def write(self):
        for path, text in sorted(self.files.items()):
            full = os.path.join(self.out, path)
            os.makedirs(os.path.dirname(full), exist_ok=True)
            with open(full, 'w', encoding='utf-8', newline='\n') as f:
                f.write(text)
        self.search.sort(key=lambda e: (e['name'], e['kind'], e['page']))
        with open(os.path.join(self.out, 'search.json'), 'w', encoding='utf-8', newline='\n') as f:
            json.dump(self.search, f, ensure_ascii=False, separators=(',', ':'))
            f.write('\n')


def digest(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for block in iter(lambda: f.read(1 << 20), b''):
            h.update(block)
    return h.hexdigest()[:16]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--types', required=True)
    parser.add_argument('--functions', required=True)
    parser.add_argument('--out', required=True)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    with open(args.types, encoding='utf-8') as f:
        described = json.load(f)
    with open(args.functions, encoding='utf-8') as f:
        functions = json.load(f)['functions']
    types = described.get('types', [])
    globals_ = described.get('globals', [])
    left_out = described.get('left_out', [])
    pages = Pages(args.out)

    records = sorted((t for t in types if t['kind'] in ('class', 'struct')), key=lambda t: t['name'])
    record_page = {}
    for t in records:
        path = 'types/%s/%s.md' % (bucket(t['name']), safe(t['name']))
        # Two records of one name (a name two files give to different types) each keep a page.
        n = 2
        while path in record_page.values():
            path = 'types/%s/%s-%d.md' % (bucket(t['name']), safe(t['name']), n)
            n += 1
        record_page[id(t)] = path
    by_name = {}
    for t in records:
        by_name.setdefault(t['name'], record_page[id(t)])

    members = collections.defaultdict(list)
    free = collections.defaultdict(list)
    for fn in functions:
        if fn.get('owner'):
            members[fn['owner']].append(fn)
        else:
            free[scope_of(fn['name'])].append(fn)

    def link(name, here):
        page = by_name.get(name)
        if not page:
            return code(name)
        return '[%s](%s)' % (code(name), os.path.relpath(page, os.path.dirname(here)).replace('\\', '/'))

    # A page a class or struct.
    owned = set()
    for t in records:
        here = record_page[id(t)]
        name = t['name']
        lines = ['# ' + code(name), '']
        facts = ['%s, %s bytes, aligned to %s' % (t['kind'], t.get('size'), t.get('align'))]
        if t.get('polymorphic'):
            facts.append('has virtual functions')
        if t.get('abstract'):
            facts.append('abstract (cannot be made on its own)')
        if t.get('no_copy'):
            facts.append('not copyable')
        lines += ['; '.join(facts) + '.', '']
        if t.get('file'):
            lines += ['Declared in `%s`%s.' % (t['file'], (', line %s' % t['line']) if t.get('line') else ''), '']
        if t.get('bases'):
            lines += ['## Bases', '', '| base | at byte | virtual |', '|---|---|---|']
            lines += ['| %s | %s | %s |' % (link(b['name'], here), b.get('offset'), 'yes' if b.get('virtual') else 'no') for b in t['bases']]
            lines.append('')
        if t.get('fields'):
            lines += ['## Fields', '', '| at byte | name | type | count | bits | access |', '|---|---|---|---|---|---|']
            for f in sorted(t['fields'], key=lambda f: (f.get('offset') if f.get('offset') is not None else 1 << 30, f.get('name') or '')):
                lines.append('| %s | %s | %s | %s | %s | %s |' % (
                    f.get('offset'), code(f.get('name') or '(unnamed)'), code(f.get('type')), f.get('count'),
                    f.get('bits') if f.get('bits') is not None else '', f.get('access') or ''))
            lines.append('')
        if t.get('reachable'):
            lines += ['## Fields of its bases', '', '| at byte | name | type | from |', '|---|---|---|---|']
            lines += ['| %s | %s | %s | %s |' % (r.get('offset'), code(r.get('name')), code(r.get('type')), link(r.get('from'), here))
                      for r in sorted(t['reachable'], key=lambda r: (r.get('offset') or 0, r.get('name') or ''))]
            lines.append('')
        mine = members.get(name, []) if by_name.get(name) == here else []
        if mine:
            owned.add(name)
            lines += ['## Functions', '', '| function | kind | notes |', '|---|---|---|']
            for fn in sorted(mine, key=lambda fn: (fn['name'], fn.get('signature') or '')):
                lines.append('| %s | %s | %s |' % (code(spelled(fn)), fn['kind'], notes_of(fn).replace('|', '\\|')))
                pages.found('function', fn['name'], here)
            lines.append('')
        pages.add(here, lines)
        pages.found('record', name, here)

    # Member functions whose owner has no described type: named on a page of their own, never dropped.
    orphans = sorted(set(members) - owned)
    if orphans:
        lines = ['# Functions of types the description does not hold', '',
                 'Their owner is a template, a nested or a left-out type. They can be called and hooked by name all the same.', '']
        for owner in orphans:
            lines += ['## ' + code(owner), '', '| function | kind | notes |', '|---|---|---|']
            for fn in sorted(members[owner], key=lambda fn: (fn['name'], fn.get('signature') or '')):
                lines.append('| %s | %s | %s |' % (code(spelled(fn)), fn['kind'], notes_of(fn).replace('|', '\\|')))
                pages.found('function', fn['name'], 'functions/of-undescribed-types.md')
            lines.append('')
        pages.add('functions/of-undescribed-types.md', lines)

    # Free functions, a page a scope.
    scope_page = {}
    for scope in sorted(free):
        path = 'functions/%s.md' % safe(scope)
        scope_page[scope] = path
        lines = ['# Functions of ' + code(scope), '', '| function | notes |', '|---|---|']
        for fn in sorted(free[scope], key=lambda fn: (fn['name'], fn.get('signature') or '')):
            lines.append('| %s | %s |' % (code(spelled(fn)), notes_of(fn).replace('|', '\\|')))
            pages.found('function', fn['name'], path)
        pages.add(path, lines)

    # Enums: the named ones by letter, the unnamed by the file that declares them.
    enums = [t for t in types if t['kind'] == 'enum']
    by_letter = collections.defaultdict(list)
    for e in enums:
        by_letter[bucket(e['name']) if e.get('name') else 'unnamed'].append(e)
    for letter in sorted(by_letter):
        path = 'enums/%s.md' % letter
        lines = ['# Enums: ' + letter, '']
        for e in sorted(by_letter[letter], key=lambda e: (e.get('name') or '', e.get('file') or '', e.get('line') or 0)):
            title = code(e['name']) if e.get('name') else 'unnamed, in `%s` line %s' % (e.get('file'), e.get('line'))
            lines += ['## ' + title, '', '%s over %s%s.' % ('Scoped enum' if e.get('scoped') else 'Enum', code(e.get('underlying')),
                                                           (', declared in `%s`' % e['file']) if e.get('name') and e.get('file') else ''), '',
                      '| name | value |', '|---|---|']
            lines += ['| %s | %s |' % (code(v['name']), v['value']) for v in e.get('values', [])]
            lines.append('')
            pages.found('enum', e.get('name'), path)
        pages.add(path, lines)

    # Aliases and spellings.
    for kind, title, path in (('alias', 'Aliases', 'aliases.md'), ('spelling', 'Other spellings of a type', 'spellings.md')):
        rows = sorted((t for t in types if t['kind'] == kind), key=lambda t: t['name'])
        lines = ['# ' + title, '', '| name | is |', '|---|---|']
        for t in rows:
            lines.append('| %s | %s |' % (code(t['name']), link(t.get('of'), path) if t.get('of') in by_name else code(t.get('of'))))
            pages.found(kind, t['name'], path)
        pages.add(path, lines)

    # Globals by letter.
    by_letter = collections.defaultdict(list)
    for g in globals_:
        by_letter[bucket(g['name'])].append(g)
    for letter in sorted(by_letter):
        path = 'globals/%s.md' % letter
        lines = ['# Globals: ' + letter, '', '| name | type | constant | declared in |', '|---|---|---|---|']
        for g in sorted(by_letter[letter], key=lambda g: (g['name'], g.get('file') or '')):
            lines.append('| %s | %s | %s | `%s` |' % (code(g['name']), code(g.get('type')), 'yes' if g.get('const') else 'no', g.get('file') or ''))
            pages.found('global', g['name'], path)
        pages.add(path, lines)

    # What was left out, and why: counted by kind and reason; the whole list as a table beside it.
    reasons = collections.Counter((l.get('kind'), re.sub(r':.*', '', l.get('reason') or '')[:90]) for l in left_out)
    lines = ['# Left out of the description', '',
             'What the game has that a mod cannot name yet, with the reason the generator gave. The whole list is `left-out.tsv`.', '',
             '| kind | reason | how many |', '|---|---|---|']
    lines += ['| %s | %s | %d |' % (kind, reason.replace('|', '\\|'), n) for (kind, reason), n in sorted(reasons.items(), key=lambda kv: (-kv[1], str(kv[0])))]
    pages.add('left-out.md', lines)
    pages.files['left-out.tsv'] = 'kind\tname\tfile\tline\treason\n' + ''.join(
        '%s\t%s\t%s\t%s\t%s\n' % (l.get('kind'), l.get('name'), l.get('file'), l.get('line'), (l.get('reason') or '').replace('\t', ' ').replace('\n', ' '))
        for l in sorted(left_out, key=lambda l: (l.get('kind') or '', l.get('name') or '', l.get('file') or '')))
    pages.counted['left_out'] = len(left_out)

    # The front page.
    totals = collections.Counter(t['kind'] for t in types)
    lines = ['# LumaCore reference', '',
             'Generated from the game\'s own description of itself. Nothing here is written by hand: a page changes when the game does.', '',
             '| what | how many | where |', '|---|---|---|',
             '| classes and structs | %d | `types/<letter>/` |' % (totals['class'] + totals['struct']),
             '| enums | %d | `enums/` |' % totals['enum'],
             '| aliases | %d | [aliases.md](aliases.md) |' % totals['alias'],
             '| other spellings of a type | %d | [spellings.md](spellings.md) |' % totals['spelling'],
             '| globals | %d | `globals/` |' % len(globals_),
             '| functions | %d | on their type\'s page, or `functions/` by scope |' % len(functions),
             '| left out, with reasons | %d | [left-out.md](left-out.md) |' % len(left_out), '',
             'Every name with its page: `search.json`.', '',
             'Sources: `%s` (%s), `%s` (%s).' % (os.path.basename(args.types), digest(args.types), os.path.basename(args.functions), digest(args.functions)), '']
    pages.add('index.md', lines)

    # Never silent: every row of the inputs stands on a page, counted.
    want = {'record': totals['class'] + totals['struct'], 'enum': totals['enum'], 'alias': totals['alias'], 'spelling': totals['spelling'],
            'global': len(globals_), 'function': len(functions), 'left_out': len(left_out)}
    wrong = {k: (pages.counted[k], v) for k, v in want.items() if pages.counted[k] != v}
    pages.write()
    print('pages %d, names %d: %s' % (len(pages.files), len(pages.search), ', '.join('%s %d' % kv for kv in sorted(want.items()))))
    if wrong:
        print('NOT EVERYTHING IS ON A PAGE (written, wanted): %s' % wrong, file=sys.stderr)
        return 1
    if args.check:
        print('check: every row of the description is on exactly one page')
    return 0


if __name__ == '__main__':
    sys.exit(main())
