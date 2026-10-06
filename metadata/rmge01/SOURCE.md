# RMGE01 Function Boundaries

`functions.csv` contains only guest addresses and byte sizes. It contains no
symbol names, source code, assembly, or machine-code bytes.

The initial map was mechanically reduced from the CC0 Petari RMGE01 symbol
configuration and checked against the supported DOL. Nebula treats it as
untrusted metadata: every range must be aligned, sorted, non-overlapping,
contained in a DOL text section, and fully decodable before installation may
continue.

Petari: https://github.com/SMGCommunity/Petari

