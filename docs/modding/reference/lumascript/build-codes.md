---
title: Build codes
shortTitle: Build codes
intro: Every code LumaLink gives when it builds a mod's LumaScript, with its message.
contentType: reference
---

## About build codes

When LumaLink builds a mod, each mistake it finds in the scripts is a line with a code, the message and the place:

```
error S0012: `&&` is written `and` (source/main.lum:2:10)
```

An error stops the build: nothing is built until every error is put right. A note does not stop it. LumaLink lists every error and note of a build, not only the first. It writes the message in the language it speaks (`--language`, one of the game's 14).

In the messages below, a word in braces is filled in for each mistake: `{found}` is what the script has, `{expected}` what the rule wants, `{what}` the kind of thing, `{name}` a name.

Codes S0901 and above are script errors while the game runs, not build codes. For those, see [Errors](errors.md).

## Codes

| Code | Kind | Message |
|---|---|---|
| S0001 | error | found {found}, expected {expected} |
| S0002 | error | a name opened with ` is not closed on its line |
| S0003 | error | a string is not closed on its line |
| S0004 | error | a value placed in this string has no closing brace |
| S0005 | error | {found} is not part of the language |
| S0006 | error | a /* comment is not closed before the end of the file |
| S0010 | error | `_` is not a name |
| S0011 | error | a `!` before a value is written `not` |
| S0012 | error | {found} is written {expected} |
| S0013 | error | a comparison cannot be compared again; join the two with `and` |
| S0014 | error | a struct value in a condition goes in parentheses |
| S0015 | error | {found} takes a plain string, without values placed in it |
| S0016 | error | a plain name in a match arm would match every value: name an enum case, or write `else =>` |
| S0017 | error | {found} belongs at the top of the file, not inside a block |
| S0018 | error | a value without a name cannot follow a named one |
| S0019 | error | `self` comes only first, in a function of a struct or a class |
| S0020 | error | `states` needs exactly one `start` line |
| S0021 | error | `_` does not match every value here: write `else =>` |
| S0022 | error | nested more than {limit} levels deep; split it up with a `let` or a function |
| S0023 | error | {found}: a name uses only the letters a to z and A to Z, the digits 0 to 9 and _ |
| S0024 | error | the compiler could not start its thread ({found}); free some memory and build again |
| S0025 | error | a call's type arguments and its `(` go on one line |
| S0026 | error | `{name}` is declared twice here; the first is on line {line} |
| S0027 | error | `use {found}`: there is no such module |
| S0028 | error | `use {found}`: add `{need}` to the needs in luma.json (lumalink add need {need}) |
| S0029 | error | these modules use each other in a cycle: {chain} |
| S0030 | error | `{name}` is not in the module `{module}` |
| S0031 | error | `{name}` in the module `{module}` is not `pub` |
| S0032 | error | `use c {found}`: the mod has no such C header |
| S0033 | note | `{found}` needs {what}, which this game's self-description does not give yet; not checked |
| S0034 | error | `{found}` is not a type |
| S0035 | error | `{found}` is not a type; nearest: {near} |
| S0036 | error | `{found}` takes {expected} type arguments |
| S0037 | error | the game's `{found}` cannot be used in LumaScript: {why} |
| S0038 | error | a map or set key is an integer, `text`, `bool` or an enum without data, not `{found}` |
| S0039 | error | a class derives from a game class; `{found}` is not one |
| S0040 | error | `option {found}`: luma.json has no option with this key |
| S0041 | error | `option {found}`: luma.json's option needs the type `{expected}` |
| S0042 | error | `states ... for {found}`: the mod has no class named `{found}` |
| S0043 | error | `start {found}`: this machine has no state named `{found}` |
| S0044 | error | `ref<{found}>`: a ref holds a game class or a class of the mod |
| S0045 | error | these hold each other by value, so they have no size: {chain} (hold one in a list, or as a ref) |
| S0046 | note | `{found}` here is LumaScript's or the mod's own; the game's `{found}` is written in backquotes: `` `{found}` `` |
| S0047 | error | `{found}` names nothing here: no local, no name of the module or its uses, no standard name, nothing of the game |
| S0048 | error | `{found}` names nothing here; nearest: {near} |
| S0049 | error | a `{found}` where a `{expected}` goes |
| S0050 | error | the number `{found}` does not fit `{expected}` |
| S0051 | error | `{found}` has no field, function or case `{name}` |
| S0052 | error | a `{found}` is called, which is no function |
| S0053 | error | `{name}` takes {expected} arguments; {found} are given |
| S0054 | error | `{callee}` has no parameter `{name}` |
| S0055 | error | the argument `{name}` is given twice |
| S0056 | error | `{callee}` needs the argument `{name}` |
| S0057 | error | a game function takes its arguments by position, not as `{name}:` |
| S0058 | error | no signature of the game's `{found}` takes these arguments; its signatures: {signatures} |
| S0059 | error | more than one signature of `{found}` takes these arguments ({signatures}): write the one meant in backquotes, with its parameter types |
| S0060 | error | the game's `{found}` cannot be called from LumaScript; its parameter `{param}`: {why} (a hook reads it as `_`) |
| S0061 | error | the game's `{found}` cannot be called from LumaScript; its result: {why} |
| S0062 | error | `{found}` writes its parameter `{param}`: write the argument as `var x` |
| S0063 | error | `{found}` does not write its parameter `{param}`: the argument takes no `var` |
| S0064 | error | this cannot be written: {why} |
| S0065 | error | `{op}` does not take `{left}` and `{right}` |
| S0066 | error | `{op}` does not take `{found}` |
| S0067 | error | a `{found}?` where a `{found}` goes: take its value first with `if let`, `guard let` or `??` |
| S0068 | error | this match does not cover {cases} and has no `else` |
| S0069 | error | `as` converts numbers, and the game's enums to and from integers; not `{found}` to `{expected}` |
| S0070 | error | `{found}` outside a loop |
| S0071 | error | `return` without a value where the function gives `{expected}` |
| S0072 | error | `return` with a value where the function, task or handler gives none |
| S0073 | error | `==` and `!=` do not compare `{found}` (views and service handles compare with `same`) |
| S0074 | error | `<` `<=` `>` `>=` compare numbers and text, not `{found}` |
| S0075 | error | this `{found}` lacks its field `{name}`, which has no default |
| S0076 | error | a struct literal of `{found}`, which is no struct (an enum's case is made with `( )`) |
| S0077 | error | `[ ]` on a `{found}` |
| S0078 | error | `{found}` where nothing says what type it is |
| S0079 | error | `self` outside a function of a struct or class |
| S0080 | error | a `{found}` cannot be shown in text |
| S0081 | error | this gives no value, and a value goes here |
| S0082 | error | an `if` that gives a value needs an `else` |
| S0083 | error | `{found}` is {what}, not a value |
| S0084 | error | `{found}` is a task: it runs with `start` |
| S0085 | error | `start` on `{found}`, which is no task |
| S0086 | error | `!` on a `{found}`, which is never none |
| S0087 | error | this `let` or `for` pattern may not match (an enum case, a literal) |
| S0088 | error | `ref` of `{found}`, which the game does not destroy through NameObj |
| S0089 | error | nothing here says the type of `{found}`: write it |
| S0090 | error | no signature of the game's `{found}` can be called from LumaScript ({signatures}) |
| S0091 | error | `{found}` changes its object, which is read-only here |
| S0092 | error | a literal in braces with some entries `key: value` and some alone |
| S0093 | error | `{found}` is no escape in a string |
| S0094 | error | a `{found}` is not data json reads or writes |
| S0095 | error | `for` over a `{found}`: a list, set, map, range or game array goes there |
| S0096 | error | a match on values that cannot all be listed (numbers, text) needs `else` |
| S0097 | note | a `pending` result is dropped without `wait` |
| S0098 | error | a game function is called on the script's own copy of a `{found}`; it takes the game's object (a view) |
| S0099 | error | `wait` takes N frames, N seconds, `until` a condition, a task or a pending result; not a `{found}` |
| S0100 | error | the game's `{found}` is a member function: call it on an object (`x.name(...)`), or on `self` inside a class or hook of its record |
| S0101 | error | this function can reach its end without returning a `{expected}` |
| S0102 | error | this `guard`'s `else` can end without leaving: end it with `return`, `stop`, `break` or `continue` |
| S0103 | error | `{found}` is a view of a game object and would be kept {why}; keep a handle (`ref`) instead |
| S0104 | error | `{found}` is a view of a game object made before the `wait` on line {line} and used after it, when the object may be gone; keep a handle (`ref`) and get it again after the wait |
| S0105 | error | `wait` cannot be here: {why} |
| S0106 | error | `{found}` cannot be inside `defer`: what `defer` runs must not leave or wait |
| S0107 | error | `on {event}` is no event of its own: its events go to the function given to {call} |
| S0108 | error | `{name}` is set by `on {what}`, not called |
| S0109 | error | a second `on {what}` where one is allowed; the first is at {first}: `on frame`, `on start`, `on stop` and `on mods.stopped` are one a module, the other `on`s one a mod, so put their bodies together |
| S0110 | error | `wait {found}` is below 0; a wait counts 0 or more |
| S0111 | error | `{name}`'s `{param}` is {found}; it takes {low} to {high} |
| S0112 | error | this hook names {found} parameters, but `{name}` takes {expected} |
| S0113 | error | `{found}` can be hooked only with `before` or `after`: it is a constructor, a destructor or a function the game only tells of |
| S0114 | error | `every override of {found}`: `{found}` is not virtual, so nothing overrides it; hook it without `every override of` |
| S0115 | error | this hook is in class `{class}`, but `{found}` is no member function of the game class `{class}` derives from |
| S0116 | error | `{found}` returns nothing: there is no result to name, type, set or skip with |
| S0117 | error | `skip` is only in a `before` hook's own body |
| S0118 | error | `skip` has a value, but `{found}` returns nothing: write `skip` alone |
| S0119 | error | `skip` needs the result `{found}` gives: write `skip` and a `{expected}` |
| S0120 | error | `result` cannot be here: {why} |
| S0121 | error | `proceed()` cannot be here: {why} |
| S0122 | error | `proceed()` may run a second time on one path (the first is on line {line}), and the game function would run twice |
| S0123 | error | a path of this hook ends without giving `{found}`'s result (a `{expected}`): call `proceed()` or set `result = ...` on it |
| S0124 | error | `result` is read before `proceed()` or `result = ...` on some path to it |
| S0125 | error | `stop` is only in a task, a state machine, or a handler that can wait |
| S0126 | error | `{what}` is only in a state machine and in the functions of its class |
| S0127 | error | `{found}` is no state of the machine `{machine}` |
| S0128 | error | `self` cannot be used here: {why} |
| S0129 | error | a second `{found}` in this state; the first is on line {line} |
| S0130 | error | `{what}` from a function of the class must name the machine of `{found}`: write `M.{found}` with M one of {machine} |
| S0131 | error | `is_state` takes one state, written `S` in its own machine or `M.S` |
| S0132 | error | `{what}` cannot be here: {why} |
| S0133 | error | `{found}`'s parameter {param} is an array of exactly {expected}; this list has {count} |
| S0134 | error | the service's `{found}` cannot be called from LumaScript; its parameter `{param}`: {why} |
| S0135 | error | the service's `{found}` cannot be called from LumaScript: {why} |
| S0136 | error | `{kind} "{found}"`: neither the mod, the mods it needs nor the game has this name |
| S0137 | error | `text "{found}"` uses the placeholder `{name}`; give it as `{name}: value` |
| S0138 | error | `text "{found}"` has no placeholder `{name}` in any language |
| S0139 | error | `text "{found}"` takes its placeholders by name: write `name: value` |
| S0140 | error | `override fn {found}`: the game class `{class}` and its bases have no virtual `{found}`; write it as a `fn` of the class, or hook it |
| S0141 | error | a mod's class cannot derive from `{found}`: the game makes one only from a class with virtual functions that starts with NameObj |
| S0142 | error | `override fn {name}` names {found} parameters, but the virtual it replaces takes {expected} |
| S0143 | error | `base.{found}`: in `override fn {name}`, `base.` calls only the base's own `{name}` |
| S0144 | error | `{found}` names no object of the game's factory |
| S0145 | error | class `{found}` cannot replace itself: name the game's object it takes the place of |
| S0146 | error | class `{found}`: the game's factory has no object `{base}`: say which object makes it with `made by <name>` |
| S0147 | error | `override fn {found}`: `{class}` has several virtuals `{found}` and the parameter count does not choose one: {signatures} |
| S0148 | error | `made by` comes before `replaces`: `class X : Base made by <name> replaces <name>` |
| S0149 | error | `{found}` in the game's factory makes `{made}`, not `{base}`: the game would keep its own object |
| S0150 | error | `{found}` names no table of the game's data |
| S0151 | error | set_table and table cannot reach or write `{found}`: {why} |
| S0152 | error | index {found} is outside the array of {count} |
| S0153 | error | `as_class<{expected}>` takes a view of a class that `{expected}` derives from or that derives from it; not `{found}` |
| S0154 | error | `{found}` needs {needs}, which nothing proves here: {how} |
| S0155 | error | `can` takes one game function's name, as `can(MR::isPlayerDead)` |
| S0156 | error | `holds` takes the name of a set the game proves: {sets}, as `holds("player")` |
| S0157 | error | `svc.{found}` may not be given (`use optional`): test it first with `if {alias} != none` or `guard {alias} != none` |
| S0158 | error | this match is too large to check that it covers every value: add `else` |
| S0160 | error | {need} was built with another LumaLink (runtime layout {found}, this one {layout}); rebuild {need} with a current LumaLink |
| S0161 | error | task `{found}` takes no `var` parameter: it runs on after the call, when the caller's value may be gone; give the value back as the task's result |
| S0162 | error | `optional` goes before a call of a game function, such as `optional MR::f(x)`: only a game function can be missing from a game |
| S0800 | error | LumaLink cannot build {what} yet |

## What S0800 names

S0800 is a construct that LumaScript has and LumaLink cannot build yet. These are the constructs it names:

* `!`
* ! on this type
* `( )`
* `-`
* \- on this type
* == on {type}
* `?.`
* ?. on a call of this kind
* ?. on this type
* `[ ]`
* \[ ] on this game place
* \[ ] on {type}
* a bit-field of a game enum whose storage does not read
* a bit-field of this type
* a bit-field wider than 32 bits
* a bool
* a C argument of this type
* a C call with named or missing arguments
* a C parameter of type {type}
* a C result of type {type}
* a c struct field of type {type}
* a call
* a call leaving out a parameter of another mod whose default is not a literal
* a call missing an argument
* a call of another mod's function giving {type}
* a call of another mod's function with a `var` parameter
* a call of another mod's function with a parameter of type {type}
* a call of this item
* a call of this item of another mod
* a call of this kind
* a call of this task
* a call of this value
* a class field bound by a pattern
* a class field of this type
* a class field outside its class's overrides
* a class's own fn
* a closure
* a closure of this type
* a colour literal
* a component of this value
* a content name
* a content name made at run time (write `<kind>_named(..)`)
* a copy of a game struct
* a copy of this game struct after ?.
* a field of self outside a hook
* a field of this game place
* a field of this service struct
* a field of this value
* a field the types file does not give
* a field whose type does not read
* a function with a parameter or result of this type
* a game argument of type {type}
* a game argument past its signature
* a game array as a whole
* a game call whose signature does not cross
* a game call whose signature does not read
* a game call with a counted list
* a game call with another number of arguments than its parameters
* a game constant of this type
* a game constant whose value does not read
* a game enum stored in other than 32 bits
* a game name in backquotes
* a game place of type {type}
* a game record argument of this kind
* a game result of type {type}
* a global the types file does not give
* a global whose type does not read
* a hook on a function returning {type}
* a hook parameter of type {type}
* a hook whose signature does not cross
* a list
* a list literal of this type
* a literal of this service struct
* a literal pattern on {type}
* a local of type {type}
* a map key of an enum with values in JSON
* a map or set
* a member
* a member function called on self outside a hook
* a member place of this kind
* a module value of type {type}
* a module-level pattern
* a name
* a number
* a number of this type
* a pattern without a value
* a place of this kind
* a request whose event's take its value does not size
* a service function giving two out buffers
* a service function taking bytes
* a service function taking or giving {type}
* a service its header gives no version
* a set in JSON
* a stream whose events' fields or takes are not written yet
* a string
* a string as {type}
* a struct function called without its value
* a struct holding a value of this kind
* a struct literal
* a task giving a value of this type
* a task with a parameter of this type
* a text argument without its placeholder's name
* a text key made at run time
* a text struct passed this way
* a text that is no string literal
* a tuple
* a tuple pattern on this value
* a value missing its field
* a value of type {type}
* a waited request of this value
* a whole game array written
* a whole game object written
* abs of this type
* an array of a game enum whose storage does not read
* an array of this element type
* an assignment
* an enum holding a value of this kind
* an exported C function's parameter of type {type}
* an exported C function's var parameter
* an expression
* an extern C function's var parameter
* an operator
* an option of type {type}
* an option without the mods (or input) service's header
* an override of a function returning {type}
* an override parameter of type {type}
* an override whose signature does not cross
* an override's result that may be none for a pointer to a number
* `as`
* base.f(...) given another object for a reference parameter
* base.f(...) given another value for a pointer parameter to a number or vector
* braces of this type
* `break`
* bytes as text
* bytes in JSON
* `continue`
* `defer`
* dot of this type
* `expect`
* expect outside a test
* `fail`
* `for`
* for over {type}
* {from} where {to} goes
* get() on a handle of this type
* `go`
* go from here
* go to this state
* `guard`
* hook ... every override of, on {on}, whose object or arguments are not those of {target}
* if as a value
* if let on {type}
* if or match giving {type}
* is_state from here
* is_state of this
* json.decode of a map keyed by {type}
* json.decode of this type
* json.decode of {type}
* length of this type
* lerp of this type
* let or var
* `log`
* low_bits of this type
* `match`
* match on {type}
* `{name}()`
* `none`
* none of this type
* `not`
* on draw at this point
* on draw without the draw service's header
* `on layers`
* on option without the mods service's header
* on stage with a name made while running
* on step without the time service's header
* `on {event}`
* `optional`
* {op} on {left} and {right}
* `proceed()`
* proceed() outside a wrap
* ref of this value
* `ref(...)`
* `result`
* result of a function with none
* `return`
* return with a value here
* same() of these values
* `self`
* self as a place
* self here
* `skip`
* skip with a value that may be none for a pointer to a number
* `start`
* start of another mod's task (call it: `wait f(args)` waits on its pending)
* start of this kind
* states for a class whose base has no control() to run them from
* `stop`
* this case
* this conversion
* this field of a service struct
* this function as a value
* this game place as a whole
* this literal
* this member
* this member after ?.
* this name as a value
* this number
* this number in JSON
* this pattern
* this pattern in for
* this place
* this shorthand field
* this struct function
* this struct function of another mod
* this tuple
* this tuple index
* this value in JSON
* {type} as text
* `{type}.{name}()`
* `wait`
* wait here
* wait on a pending of this type
* wait on a pending result
* `while`

## Further reading

* [Errors](errors.md)
* [Detailed rules](detailed-rules.md)
* [Not in LumaScript](not-in-lumascript.md)
