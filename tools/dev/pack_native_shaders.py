"""Pack build-compiled shader headers, sharing byte-identical blob storage."""
import hashlib
import pathlib
import re
import struct
import sys


def pack(template, output):
    text = template.read_text()
    unique = {}
    aliases = {}
    raw_size = 0
    includes = []
    pattern = re.compile(r'const unsigned (char|int)\s+(\w+)\[\]\s*=\s*\{(.*?)\};', re.S)
    for line in text.splitlines(keepends=True):
        include = re.fullmatch(r'#include "([^"/]+\.h)"\s*', line.strip())
        if not include:
            includes.append(line)
            continue
        header = template.parent / include[1]
        match = pattern.search(header.read_text())
        if not match:
            raise ValueError(f'{header.name}: no compiled shader array')
        kind, name, contents = match.groups()
        words = [int(n, 0) for n in re.findall(r'0[xX][0-9a-fA-F]+|\d+', contents)]
        data = bytes(words) if kind == 'char' else struct.pack(f'<{len(words)}I', *words)
        if not data:
            raise ValueError(f'{header.name}: empty compiled shader')
        digest = hashlib.sha256(data).hexdigest()
        key = (kind, digest)
        symbol = f'g_native_blob_{digest}'
        if key in unique and unique[key][1] != data:
            raise ValueError('shader bytecode digest collision')
        unique.setdefault(key, (symbol, data))
        aliases[name] = symbol
        raw_size += len(data)
    body = re.sub(r'\bg_native_\w+\b', lambda m: aliases.get(m[0], m[0]), ''.join(includes))
    declarations = []
    for (kind, _), (symbol, data) in unique.items():
        values = data if kind == 'char' else struct.unpack(f'<{len(data)//4}I', data)
        width = 2 if kind == 'char' else 8
        lines = ['    ' + ', '.join(f'0x{n:0{width}x}' for n in values[i:i+16]) + ','
                 for i in range(0, len(values), 16)]
        declarations.append(f'alignas(16) const unsigned {kind} {symbol}[] = {{\n' + '\n'.join(lines) + '\n};\n')
    packed = '// Generated: identical compiled shader blobs share storage.\n' + ''.join(declarations) + body
    if not output.exists() or output.read_text() != packed:
        output.write_text(packed)
    print(f'native shaders: {len(aliases)} blobs -> {len(unique)} unique; '
          f'{raw_size} -> {sum(len(v[1]) for v in unique.values())} bytecode bytes')


if __name__ == '__main__':
    pack(pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2]))
