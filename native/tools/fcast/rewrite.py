"""Routes every narrow float-to-integer conversion the audit found (sites.json) through 32 bits, as the Wii converts:
fctiwz to a 32-bit integer (saturating there), then the halfword or byte store keeps the low bits (wrapping). Clang's
-fno-strict-float-cast-overflow saturates at the narrow type instead (296 degrees as an s16 binary angle: 32767,
180 degrees; the console's -11651, -64 degrees). An explicit cast to a narrow type gets (s32) after it; any other
conversion has its expression wrapped in (s32)( ). Skipped: the OSf32to* stand-ins for the quantized paired-single
stores (os.h, OSFastCast.h), which saturate on the console too, and Nebula's own SDK (sdk/).

    python rewrite.py [--apply]"""
import json
import pathlib
import re
import sys

sys.stdout.reconfigure(encoding='utf-8')
NARROW = r'(?:s16|u16|s8|u8|short|unsigned\s+short|signed\s+char|unsigned\s+char|char|int16_t|uint16_t|int8_t|uint8_t)'
SKIP = ('revolution/os.h', 'revolution/os/OSFastCast.h')
apply = '--apply' in sys.argv
sites = json.load(open(pathlib.Path(__file__).with_name('sites.json')))
by_file = {}
for f, line, col, length in sites:
    p = f.replace('\\', '/')
    if p.endswith(SKIP) or '/native/sdk/' in p:
        continue
    by_file.setdefault(f, []).append((line, col, length))
total = 0
for f, edits in sorted(by_file.items()):
    raw = pathlib.Path(f).read_bytes()
    crlf = b'\r\n' in raw
    lines = raw.decode('utf-8').replace('\r\n', '\n').split('\n')
    for line, col, length in sorted(edits, key=lambda e: (e[0], -e[1])):
        text = lines[line - 1]
        expr = text[col - 1:col - 1 + length]
        cast = re.match(r'\(\s*(?:const\s+)?' + NARROW + r'\s*\)', expr)
        if cast:
            new = expr[:cast.end()] + '(s32)' + expr[cast.end():]
        else:
            new = '(s32)(' + expr + ')'
        lines[line - 1] = text[:col - 1] + new + text[col - 1 + length:]
        total += 1
        print(f"{re.sub(r'^.*?[/]native[/]', '', f.replace(chr(92), '/'))}:{line}: {expr[:70]}  ->  {new[:80]}")
    if apply:
        out = '\n'.join(lines)
        if crlf:
            out = out.replace('\n', '\r\n')
        pathlib.Path(f).write_bytes(out.encode('utf-8'))
print(f'{total} sites in {len(by_file)} files' + (' rewritten' if apply else ' (dry run)'))
