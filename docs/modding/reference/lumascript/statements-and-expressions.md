---
title: Statements and expressions
shortTitle: Statements and expressions
intro: Declarations, control flow and operators in LumaScript.
contentType: reference
---

## About statements and expressions

This page describes declarations, control flow and expressions in LumaScript.

```lumascript
let speed = 12.0                 // a constant local
var count = 0                    // a variable local
count += 1
if count > 3 { log "many" } else if count == 0 { log "none" }
while count < 10 { count += 1 }
for i in 0..10 { }               // ranges
for item in items { }            // lists, sets: the values; maps: (key, value) pairs
for (i, item) in items.indexed { }
match shape {
    Shape.Circle(r) => area = 3.14159 * r * r
    Shape.Box { w, h } => area = w * h
    else => area = 0.0
}
defer log "left"                 // runs when the block ends, however it ends
return value
```

- `match` must cover every case or have `else`; a missing case is a compile error naming it.
- `guard <condition or let> else { ... }`: the `else` block must leave the function, task or loop (`return`, `stop`,
  `break`, `continue`); after it, names bound by `guard let` are in scope. `guard let a = x, let b = y, a > 0 else`
  checks each in order.
- `if` and `match` are expressions when every branch gives a value: `let s = if big { 2.0 } else { 1.0 }`.

## Further reading

* [LumaScript reference](index.md)
