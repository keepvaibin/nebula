"""gen_lumacore.py NEBULA_ROOT [--check]: LumaCore's class rows, from the game's headers.

Writes:
  include/Game/System/generated/lumacore_class_ids.hpp  kClassId<T> for every class deriving from NameObj (the
                                                       factory's createNameObj<T> reads it)
  src/Game/System/generated/lumacore_classes.cpp        the class rows (name, parent, size, cast); the compiler checks
                                                       every parent
  src/Game/System/generated/lumacore_hash.txt           the hash of what was read; --check compares it, so the
                                                       generated files can't drift
and docs/modding/CLASSES.md (every row).

No build of the game runs this: the outputs are committed.
"""
import hashlib
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import gen_classes  # noqa: E402

ROOT = pathlib.Path(sys.argv[1]).resolve()
CHECK = '--check' in sys.argv
PETARI = ROOT / 'native' / 'petari'
INC = PETARI / 'include'
OUT = PETARI / 'src' / 'Game' / 'System' / 'generated'
IDS = INC / 'Game' / 'System' / 'generated'
DOCS = ROOT / 'docs' / 'modding'


def write(path, text):
    # CRLF, as the tree keeps these files.
    path.write_bytes(text.replace('\r\n', '\n').replace('\n', '\r\n').encode('utf-8'))


def main():
    here = pathlib.Path(__file__).resolve().parent
    inputs = sorted(list(INC.rglob('*.hpp')) + list(INC.rglob('*.h')))
    inputs = [h for h in inputs if IDS not in h.parents] + [here / 'gen_lumacore.py', here / 'gen_classes.py']
    digest = hashlib.sha256()
    for h in inputs:
        digest.update(h.as_posix().split('/native/')[-1].split('/tools/')[-1].encode())
        digest.update(h.read_bytes().replace(b'\r\n', b'\n'))
    digest = digest.hexdigest()
    hash_file = OUT / 'lumacore_hash.txt'
    if CHECK:
        old = hash_file.read_text().strip() if hash_file.exists() else ''
        if old != digest:
            raise SystemExit('LumaCore generated files are out of date: run tools/lumacore/gen_lumacore.py')
        print('LumaCore generated files current')
        return
    rows, dupes = gen_classes.classes(INC)
    OUT.mkdir(parents=True, exist_ok=True)
    IDS.mkdir(parents=True, exist_ok=True)
    write(IDS / 'lumacore_class_ids.hpp', gen_classes.emit_class_ids(rows))
    write(OUT / 'lumacore_classes.cpp', gen_classes.emit_class_rows(rows))
    write(hash_file, digest + '\n')
    doc = ['# Classes', '', f'{len(rows)} classes derive from NameObj. A luma.json `object_classes` row\'s `like` names '
           'one of them; a mod\'s code reads the same rows through LumaCore (class count, row, is-a).', '',
           '| Class | Parent | Header |', '|---|---|---|']
    doc += [f'| {q} | {p or ""} | {rel} |' for (q, p, rel, _) in rows]
    write(DOCS / 'CLASSES.md', '\n'.join(doc) + '\n')
    print(f'classes {len(rows)} ({len(dupes)} names in two headers: {", ".join(dupes)})')


if __name__ == '__main__':
    main()
