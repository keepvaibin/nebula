"""Classifies the narrow float-to-integer conversion sites (sites.json from the clang-query audit)."""
import collections
import json
import pathlib
import re
import sys

sys.stdout.reconfigure(encoding='utf-8')
sites = json.load(open(pathlib.Path(__file__).with_name('sites.json')))
cache = {}
kinds = collections.Counter()
for f, line, col, length in sites:
    if f not in cache:
        cache[f] = pathlib.Path(f).read_text(encoding='utf-8', errors='replace').splitlines()
    text = cache[f][line - 1]
    expr = text[col - 1:col - 1 + length] if length else text[col - 1:]
    if re.match(r'\(\s*(const\s+)?[A-Za-z_][\w:<> ]*\)', expr):
        k = 'cstyle'
    elif re.match(r'static_cast\s*<', expr):
        k = 'static'
    elif re.match(r'(s16|u16|u8|s8|short)\s*\(', expr):
        k = 'functional'
    else:
        k = 'implicit'
    kinds[k] += 1
    rel = re.sub(r'^.*?[\\/]native[\\/]', '', f)
    print(f"{k:10} {rel}:{line}:{col} [{length}] {expr[:100]}")
print(dict(kinds))
