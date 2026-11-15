---
title: Lexical structure
shortTitle: Lexical structure
intro: Comments, identifiers, keywords and literals in LumaScript.
contentType: reference
---

## About lexical structure

This page describes how LumaScript source text is read: comments, names, statements, blocks, keywords and literals.

- Comments: `// to the end of the line`, `/* ... */` (nesting).
- Identifiers: a letter or `_`, then letters, digits, `_` (ASCII). A game name may hold `::` between identifiers
  (`MR::getPlayerPos`, `Kuribo::exeWander`); a file-local game function is written as the self-description spells it
  in a string: `` `Game/Enemy/Kuribo.cpp#KuriboParam::KuriboParam` `` (backquotes take any name).
- Statements end at the end of a line, or at `;`. A line ending in an operator, `,` or an open bracket continues.
- Blocks are `{ ... }`. Indentation has no meaning; the formatter writes four spaces.
- Keywords: `use pub let var const fn return if else while for in break continue match struct enum class
  override hook before after wrap replace proceed skip state states go wait until frames seconds task stop on
  option true false none self result as is and or not extern export defer test expect`.
- Literals: `12`, `0x1F`, `0b101`, `1_000_000`; `1.5`, `1e3`, `2.0f64`; `true`, `false`, `none`;
  `"text {expression} more"` (interpolation; `{{` for a brace), `r"raw \ text"`; `b"\x00\x01"` (bytes);
  `#FFD700` and `#FFD700CC` (color); `[1, 2, 3]` (list); `{"a": 1}` (map); `vec3`.

## Further reading

* [LumaScript reference](index.md)
