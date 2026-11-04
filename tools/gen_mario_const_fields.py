#!/usr/bin/env python3
# Rewrites native/petari/src/Game/Player/MarioConstFields.inc from MarioConst.hpp. Run when that struct changes.
import os, re
root = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'native', 'petari')
hdr = open(os.path.join(root, 'include/Game/Player/MarioConst.hpp'), encoding='utf8', errors='replace').read()
body = hdr[hdr.index('struct MarioConstTable {'):hdr.index('};', hdr.index('struct MarioConstTable {'))]
kinds = {'f32': 'f', 'u16': 'H', 's16': 'h', 'u8': 'B', 's32': 'i'}
out = ["// Written by tools/gen_mario_const_fields.py from MarioConst.hpp: every number of the player's table by its name.\n"]
for line in body.splitlines()[1:]:
    m = re.match(r'\s*(\w+)\s+(\w+)(?:\[(\w+)\])?;', line)
    if m:
        out.append('    {"%s", offsetof(MarioConstTable, %s), \'%s\', %s},\n' % (m.group(2), m.group(2), kinds[m.group(1)], m.group(3) or '1'))
open(os.path.join(root, 'src/Game/Player/MarioConstFields.inc'), 'w', encoding='utf8', newline='\n').write(''.join(out))
