---
title: Standard modules
shortTitle: Standard modules
intro: The modules every LumaScript mod can use.
contentType: reference
---

## About standard modules

This page lists the modules every LumaScript mod can use without declaring them.

Always available without `use`: `json` (`encode`, `decode<T>`, `Value` with `.text .int .list .map`),
`math` names (`min max abs clamp lerp sqrt sin cos atan2 dot cross length normal`), `hash32(text)`, `wrap(...)`
(integer conversion keeping the low bits). Everything else is a service or a library mod.

## Further reading

* [LumaScript reference](index.md)
