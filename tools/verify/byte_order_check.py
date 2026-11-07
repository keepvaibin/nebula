#!/usr/bin/env python3
"""byte_order_check.py [REPO]: fails on code whose meaning depends on the machine's byte order.

The game's colours and flag words once read a 32-bit word through its bytes, as the big-endian console did: on a PC
yellow lights came out blue and Mario's effect tables read 0. A value is read with shifts, never through an overlay.
Reports a union of a word over smaller members, and a cast of a colour to or from a word. Each exception below is
reviewed: it says why it cannot change a value. Exit 1 on anything not listed."""
import glob, os, re, sys

REVIEWED = {
    'MarioSound.cpp': 'kind() reads the word with a shift; the bytes are unused',
    'TalkNodeCtrl.hpp': 'the cooked flow node is laid out per kind of node',
}
COLOR_CAST = re.compile(
    r'\*\s*\(\s*(?:const\s+)?u32\s*\*\s*\)\s*&\s*\w*[Cc]olo|'
    r'reinterpret_cast<\s*(?:const\s+)?u32\s*[*&]\s*>\(\s*&?\s*\w*[Cc]olo|'
    r'reinterpret_cast<\s*(?:const\s+)?(?:NebulaColor8|Color8|TColor)\s*[*&]\s*>\(\s*&?\s*\w*(?:[Ww]ord|u32|[Vv]alue)')
WORD = re.compile(r'\b(u32|s32|u64|s64|uint32_t|int32_t|uint64_t|int)\b')
SMALL = re.compile(r'\b(u8|s8|u16|s16|uint8_t|uint16_t|char)\b')

def strip(text):
    return re.sub(r'//[^\n]*|/\*.*?\*/', '', text, flags=re.S)

def main():
    root = sys.argv[1] if len(sys.argv) > 1 else '.'
    bad = []
    for pattern in ('native/petari/src/**/*.cpp', 'native/petari/include/**/*.hpp', 'native/petari/libs/**/*.hpp', 'native/host/**/*.cpp'):
        for path in glob.glob(os.path.join(root, pattern), recursive=True):
            name = os.path.basename(path)
            text = strip(open(path, encoding='utf8', errors='replace').read())
            for m in COLOR_CAST.finditer(text):
                bad.append(f'{path}:{text.count(chr(10), 0, m.start()) + 1}: a colour read as a word: {m.group(0)}')
            if name in REVIEWED:
                continue
            for m in re.finditer(r'\bunion\b[^{;()]*\{', text):
                i, depth = m.end(), 1
                while i < len(text) and depth:
                    depth += {'{': 1, '}': -1}.get(text[i], 0)
                    i += 1
                body = text[m.end():i]
                if WORD.search(body) and SMALL.search(body):
                    bad.append(f'{path}:{text.count(chr(10), 0, m.start()) + 1}: a word over smaller members: {" ".join(body.split())[:90]}')
    for line in bad:
        print(line)
    print(f'byte order: {len(bad)} problems')
    return 1 if bad else 0

if __name__ == '__main__':
    sys.exit(main())
