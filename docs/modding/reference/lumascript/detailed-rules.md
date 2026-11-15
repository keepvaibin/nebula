---
title: Detailed rules
shortTitle: Detailed rules
intro: The detailed rules of LumaScript that complete the other reference pages.
contentType: reference
---

## About the detailed rules

This page holds the detailed rules of LumaScript that complete the other reference pages: the grammar, operators, conversions, `json`, flow checks, `wait`, `defer`, and how the compiler reads services from their headers.

## More types and calls

1. Section 4, `if let`: takes the same list as `guard`: `if let r = room, not r.host, let p = r.peers.get { }`
   checks each in order; the names bound are in scope in the `if` block only.
2. Section 3, container functions (each a compile error on the wrong type, never a run-time lookup):
   - `list<T>`: `len -> int`, `push(T)`, `pop -> T?`, `insert(int, T)`, `remove(int) -> T` (out of range: a
     script error), `get(int) -> T?`, `[int]` (out of range: a script error), `contains(T) -> bool` (T with `==`),
     `indexed -> list<(int, T)>`, `map(fn(T) -> U) -> list<U>`, `filter(fn(T) -> bool) -> list<T>`,
     `any(fn(T) -> bool) -> bool`, `all(fn(T) -> bool) -> bool`, `sum(fn(T) -> N) -> N` (N a number type),
     `sort` (T with `<`), `sort_by(fn(T, T) -> bool)`, `first -> T?`, `last -> T?`.
   - `map<K, V>`: `len`, `get(K) -> V?`, `[K]` (read: a missing key is a script error; write: adds or replaces),
     `contains(K)`, `remove(K) -> V?`, `keys -> list<K>`, `values -> list<V>` (both in key order: integers and
     enums by value, `text` by bytes, so the same on every platform); `for (k, v) in m`.
   - `set<T>`: `len`, `add(T) -> bool`, `remove(T) -> bool`, `contains(T)`, `values -> list<T>` (in order).
   - `text`: `len` (bytes), `chars`, `split(text) -> list<text>`, `starts_with`, `ends_with`, `find(text) -> int?`,
     `slice(int, int)` (byte offsets on code point boundaries, else a script error), `lower`, `upper` (Unicode
     simple case mapping), `parse_int -> i64?`, `parse_float -> f64?`.
   - `bytes`: `len`, `[int]`, `slice`, `push(u8)`, `read_u16/u32/u64/f32/f64(at, big: bool)`,
     `write_u16/...(at, value, big: bool)` (out of range: a script error).
3. Section 7.1, nerves by name: `nerve "DinoPackunVs1.BattleLv2"` is the game's `const Nerve *` for that class's
   step, usable where a game function takes `const Nerve *` (`self.setNerve(nerve "...")`) and comparable with
   `same(...)` to `getCurrentNerve`. Checked at compile time against the self-description's list of each class's
   nerves; a name not listed is a compile error naming the class and its nerves. A nerve
   named by a run-time `text` is not possible (no run-time lookup): a nerve chosen while running is a `match`
   over the class's steps, which the mod's build can generate from the same list.
4. Section 8, `wrap ... every override of`: `wrap every override of LiveActor::movement { ... }` as `hook`
   allows (`K.vhook`); `self` is the object, of the virtual's class.
5. Section 8, a hook calling the function it hooks: inside any hook of a mod, a call to a function the same mod
   hooks runs the other mods' hooks and the game's code, not this mod's hooks on it (no recursion through one's own
   hooks); another mod's hooks run as usual (an `after MR::getPlayerPos` that calls `MR::getPlayerPos` gets the value
   without its own hook).
6. Section 8, `result =` for a function returning a pointer to a vector (`Vec3f *`, `const TVec3f &`): the value
   is kept in a slot of that hook (one per hook, on the game thread) and the caller receives a pointer to the slot,
   valid until the hook next runs. A caller that keeps the pointer across a second call of the same function sees the
   newer value; the compiler warns (S-code) on every such hook, naming the function.
7. The self-description's nerves come from the game source's `NEW_NERVE` and `NEW_NERVE_ONEND` declarations
   (3,434 and 161 of them): `NEW_NERVE(BossKameckVs1NrvBattleLv2, BossKameckVs1, BattleLv2)` is
   `nerve "BossKameckVs1.BattleLv2"`, with its address per build.

---

The rules below refine the sections above; the grammar (Appendix G) is executable.

## Words

- Reserved (never a name of the mod's own): `use pub let var fn return if else while for in break continue match
  struct enum class override hook wrap replace proceed skip state states go wait task stop on option true false none
  self result as and or not extern export defer test expect guard start log ref`. `const` and `is` are gone.
- Contextual (a word only where the grammar places it, a name everywhere else): `before after every override of until
  frames seconds fact optional enter update exit game c draw layers stage frame load`, and the content words `model
  effect sound layout text font movie data nerve` (a content literal only when a string follows).
- After `::` or `.` any word is a name: `GameScene::start`, `GameScene::update`, `x.frames`, `p.0`.
- A `use` path's parts are words, reserved ones included (`use kit.player.state`, `use ./player/state`). The module's
  name is its last part (unless `as`), and a reserved word right before `.` in an expression names that module
  (`state.enter(...)`).
- The game's statics are written as the game names them, `GameSystem::sInstance` so `game` names nothing of the
  language and `use svc.save.game` names the service.

## Lines

A line end ends a statement, a field, a case, an entry or a match arm. It does not when: it is inside `` or `[ ]`
(but inside a `{ }` within them it does again); the line ends with an operator, `,`, `:`, `->`, `=>` or an open
bracket; or the next line starts with `else` (except `else =>`, a match arm). Inside `{ }` of a literal, a struct, an
enum or a match, entries are separated by `,` or a line end, either or both. After a statement that ends with a
block's `}`, the next statement may follow on the same line (`enter { ... } wait 180 frames`).

## Hooks: re-entry, replace's error path, threads, order

- The abi's flag is therefore cleared around `core->proceed(call)` and set again after it (abi D4 below), so a
  recursive game function, or an override that calls its base under `every override of`, runs every mod's hooks each
  time, as the game would.
- A `replace` whose body ends in a script error gives the game's own result by `core->proceed(call)` (which runs
  the wraps after it and then the function), not by `invoke` (which would enter the function from the top and run
  every hook twice).
- Every script hook, task and handler runs on the game thread only. The generated code tests it first; a call on
  another thread does nothing (a `before` does not skip, a `wrap` proceeds, an `after` leaves the result) and logs
  once per hook `S0901: <function> called off the game thread: the hook is left out there`. The self-description marks
  functions known to run off the game thread (the self-description); hooking one is a build warning naming it.
- Between mods: the mod higher in the list is outermost: its `before` runs first, its `after` last (its result
  stands), its `wrap` encloses the lower mods' wraps.

## Handles across a stage change

6.2's last paragraph reads: an object goes in one of two ways, and a handle learns both: one by one, through the
destructor rows of its class (`LUMA_TELL` on `NameObj::~NameObj` and its overrides); and all at once when a stage's
arena is released (`NebulaArena::destroy` runs no destructor for most actors): the kernel tells the mod the
released address range and `lumart` empties every handle whose object lies in it. A test: a
`ref<Kuribo>` kept in a module `var` across a stage change gives `none`. `ref<C>` takes only a class derived
from `NameObj` (or one the self-description marks with a lifetime); anything else is a compile error naming it.

## Operators and precedence

Loosest first: `??` (right to left); `or`; `and`; `not` (prefix); comparisons `== != < <= > >=` (one per expression:
`a == b == c` is an error); `..` `..=`; `|`; `^`; `&`; `<< >>`; `+ -`; `* / %`; prefix `-`, `wait`, `start`; `as`
(binds tighter than every binary operator: `a as u32 * b as u32` is `(a as u32) * (b as u32)`); postfix: a call
`(...)`, `[i]`, `.name`, `?.name`, and `!` ("the value, which must not be `none`", a script error when it
is). There is no prefix `!` and no `&&`, `||`, `? :`. `x == none` and `x != none` test any `T?` (T need not have
`==`).

## Calls: named arguments

A call gives its positional arguments first, then any by name, `name: value`: `draw.tint(a, multiply: #FFFFFF, add:
c)`. A name the function lacks, a parameter given twice, or a required one missing is a compile error naming it.
Named arguments are for the mod's functions, LumaKit's and services' (whose parameters have names); a game function
takes positional ones only (the self-description may not name its parameters). An enum case with fields is
constructed as a call, `actor.Sensor.Body(radius: 70.0)`, positional or named by its fields. A `var` argument is
written `var x`.

## Null game pointers

Every game pointer or reference result, every pointer field read from a game object, and every pointer
parameter is `T?` (and `const char *` is `text?`), unless the self-description marks it never-null. The self-description takes
that mark only from evidence: a C++ reference type `T &`, or a list kept by hand with each row's reason. So
`MarioAccess::getPlayerActor` is `MarioActor?` (`if let p = MarioAccess::getPlayerActor { ... }`), and `none`
may be passed for a pointer parameter (`initModelManagerWithAnm("Crumble", none, false)`). `self` in a hook or a class
is never `none`.

## Hook heads

The function is named `Class::name`, by backquotes (`` `MR::isNear(const LiveActor *, f32)` ``), or as either followed
by its parameters: names only (`(actor, at, dist)`), or names with LumaScript types (`MR::isInWater(at: vec3)`), which
choose the overload whose parameters map to those types (none or two: a compile error listing the signatures). `->
name` names the result in an `after`; `-> type` gives the result's type in a `before` that may `skip` with a value.

## Module level

A module holds only declarations: `use`, `option`, `let`, `var`, `fn`, `struct`, `enum`, `class`, `task`, hooks,
`on`, `states`, `test`, `extern`, `export`. A module `let` or `var` may be any expression; they run when the mod
starts, in module order, before every `on start` (as abi 8 already says); one that reads the game runs after every
import is bound. `const` is withdrawn: a module `let` is the one way (the compiler folds it when it is constant).
A bare statement at module level is an error (`on start { }` holds it).

## Literals

- `[a, b]` a list; `{}` an empty map or set, by its place; `{k: v, ...}` a map; `{a, b}` a set; `Name { field: v,
  shorthand }` a struct (a capitalized name, or a module's: `item.Item { ... }`), fields by `,` or line ends.
- A struct literal is not allowed directly in the condition of `if`, `while`, `match`, `guard` or `for ... in` (it would
  read as the block); put it in parentheses.
- `json.Value`: a literal (list, map, text, number, bool, `none`) in a place that takes a `json.Value` becomes one,
  each value by its kind, recursively; a map's keys are `text`. `json.Value` has `.text .int .float .bool
  .list .map` (each `T?`) and `.get(text) -> Value?`, `.at(int) -> Value?`.

## Conversions

`wrap(...)` as a conversion is withdrawn: `wrap` is the hook form only. Keeping an integer's low bits is
`low_bits<T>(n)`. Built-ins that take a type (`json.decode<T>`, `low_bits<T>`) write it in `< >` right before the
call's `(`; that is the only place a type follows an expression (user generics stay out; see [Not in LumaScript](not-in-lumascript.md)).
`u32.max`, `i16.min`... are each integer type's limits.

## Options

`option <key>: <type>` declares an option of `luma.json`'s `options` by its key; the types match its `kind`: `toggle`
-> `bool`; `number` -> `int` when its `default`, `min`, `max` and `step` are whole, else `f32` (declaring `int` for a
number with a fraction is an error naming it); `text` -> `text`; `choice` -> `text` (one of its `values`, which a
`match` on it may list); `key` -> `input.Action`. An option is read by its key as a name (`balls_at_once`); there is
no `option("key")` call.

## Smaller words

- One container way each: `filter` (no `retain`: `balls = balls.filter(...)`), `set.add` (no `insert`), `filter(...)
  .last` (no `last_where`). A handle is empty when `h.get == none` (no `empty`).
- `map[K]` gives `V` (a missing key is a script error); `map.get(K)` gives `V?`.
- `same(a, b)` for views and service handles (no `same_stream`).
- `self` is the first parameter of a struct's function or a class member only; a free function names its own.
- `match` arms may be integer, text or bool literals, a negative number, an enum case (`E.Case(x)`, `E.Case { a }`) or
  a tuple of patterns; `else =>` for the rest (no `_ =>`). An arm's body is a statement or a block.
- A closure's parameters are typed; its result type may be left out when it gives none (`fn(i: int) { ... }`).

## `pending<T>` and text with placeholders

- A call that answers later (a service request such as `fs.read`, or LumaKit's `screen.choose`, `talk.ask` and `cutscene.play`) returns
  `pending<T>`. Only `wait` reads one, and `wait` is allowed only in a `task`, a state's body or `enter`, or a
  `wait`-holding handler that the compiler makes a task (`on start` and events): in a `fn` or a hook it is an error
  naming the line (a function that waits is a `task`). A `pending` dropped without `wait` is a warning.
- `text "Key"` is the mod's text row in the player's language. Its placeholders `{name}` are filled by named arguments
  right after it: `text "Coop.Hosting"(address: outside)`. Each placeholder of the row must be given (in every
  language's row), and no other: checked when building, as content names are.
- Content literals take a plain string (no `{...}`): content is named when building (`nerve "{a}.{b}"` is an error).

## Grammar

```lumascript
(* G.1 tokens *)
IDENT    = letter_or_ "_" { letter | digit | "_" } ;          (* not a reserved word, D4.1 *)
GAME     = "`" { any but "`" and line end } "`" ;
NUMBER   = "0x" hex {hex|"_"} | "0b" bin {bin|"_"} | digit {digit|"_"} ["." digit {digit|"_"}] [exp] [suffix] ;
COLOR    = "#" hex*6 [hex*2] ;
STRING   = ["r"|"b"] '"' { char | "{{" | "{" expr "}" } '"' ;   (* "{...}" interpolates; not in r or b strings *)
comments = "//" ... line end | "/*" ... "*/" (nesting) ;
END      = line end | ";" ;

(* G.2 module *)
module   = { item END } ;
item     = use | option | ["pub"] ( binding | fn | struct | enum | class | task ) | hook | on | states | test
         | extern | export ;
use      = "use" ( "c" STRING | ["optional"] ( "." "/" WORD { "/" WORD } | WORD { "." WORD } )
           [ "." "{" IDENT { "," IDENT } "}" ] [ "as" IDENT ] ) ;
option   = "option" IDENT ":" type ;
binding  = "let" pattern [":" type] "=" expr | "var" IDENT ( ":" type ["=" expr] | "=" expr ) ;
fn       = "fn" IDENT params ["->" type] block ;
params   = "(" [ param { "," param } ] ")" ;
param    = ["var"] ( "self" | IDENT ":" type ["=" expr] ) ;   (* self: first, in a struct or class only *)
struct   = ["c"] "struct" IDENT "{" { ( IDENT ":" type ["=" expr] | fn ) SEP } "}" ;
enum     = "enum" IDENT "{" { IDENT [ ("(" | "{") { IDENT ":" type SEP } (")" | "}") ] SEP } "}" ;
class    = "class" IDENT ":" gname "{" { ( binding | "override" "fn" IDENT lparams ["->" type] block | fn | hook ) END } "}" ;
task     = "task" IDENT params block ;
hook     = ( "hook" ("before" | "after") | "wrap" | "replace" ) ["every" "override" "of"] gname [lparams]
           [ "->" ( IDENT | type ) ] block ;                    (* -> IDENT names an after's result *)
lparams  = "(" [ IDENT [":" type] { "," IDENT [":" type] } ] ")" ;
gname    = GAME | IDENT { "::" ["~"] WORD } ;                   (* WORD: any word, reserved or not *)
on       = "on" ( "start" | "stop" | "frame" | "load" | "option" IDENT | "stage" STRING | "draw" dotted
           | "layers" "->" type | dotted [ "(" [ IDENT { "," IDENT } ] ")" ] ) block ;
states   = "states" IDENT "for" IDENT "{" { ( "state" IDENT "{" { ( ("enter"|"update"|"exit") block | stmt ) END } "}"
           | "start" IDENT ) END } "}" ;                        (* exactly one start *)
test     = "test" ["game"] STRING block ;
extern   = "extern" "c" "fn" IDENT params ["->" type] ;
export   = "export" "c" "fn" IDENT params ["->" type] block ;
SEP      = { "," | END } ;                                      (* at least one between entries *)

(* G.3 types *)
type     = ( "(" type { "," type } ")" | "fn" "(" [ type { "," type } ] ")" ["->" type] | "ref" targs
           | ( GAME | IDENT { ("." | "::") IDENT } ) [targs] ) { "?" } ;
targs    = "<" type { "," type } ">" ;

(* G.4 expressions, loosest first *)
expr     = coalesce ;
coalesce = or_ [ "??" coalesce ] ;
or_      = and_ { "or" and_ } ;
and_     = not_ { "and" not_ } ;
not_     = "not" not_ | compare ;
compare  = range [ ("==" | "!=" | "<" | "<=" | ">" | ">=") range ] ;
range    = bor { (".." | "..=") bor } ;
bor      = bxor { "|" bxor } ;  bxor = band { "^" band } ;  band = shift { "&" shift } ;
shift    = sum { ("<<" | ">>") sum } ;  sum = product { ("+" | "-") product } ;
product  = unary { ("*" | "/" | "%") unary } ;
unary    = "-" unary | "wait" ( "until" expr | unary [ "frames" | "seconds" ] ) | "start" postfix
         | postfix { "as" type } ;
postfix  = primary { args | "[" expr "]" | ("." | "?.") WORD [targs] | "!" } ;   (* targs only before "(" *)
args     = "(" [ arg { "," arg } ] ")" ;  arg = ["var"] expr | IDENT ":" expr ;   (* positional first *)
primary  = NUMBER | COLOR | STRING | "true" | "false" | "none" | "self" | "result" | "proceed" "(" ")"
         | "ref" args | if | match | "fn" params ["->" type] block | "(" expr { "," expr } ")"
         | "[" [ expr { "," expr } ] "]" | "{" { expr [":" expr] SEP } "}"
         | CONTENT STRING [args]                                (* plain STRING; args for "text" only *)
         | GAME | IDENT { "::" ["~"] WORD } | IDENT [targs] | dotted "{" { IDENT [":" expr] SEP } "}" ;
                                                                (* struct literal: capitalized last name; not in a condition *)
dotted   = IDENT { "." IDENT } ;
CONTENT  = "model" | "effect" | "sound" | "layout" | "text" | "font" | "movie" | "data" | "nerve" ;

(* G.5 statements *)
block    = "{" { stmt END } "}" ;          (* after a "}" the next stmt may follow on the line *)
stmt     = binding | fn | "return" [expr] | "break" | "continue" | "stop" | "skip" [expr] | "go" dotted
         | "defer" stmt | "log" ["fact" expr] expr | "expect" expr | "guard" conds "else" block
         | "while" cond_expr block | "for" pattern "in" cond_expr block | expr [ assignop expr ] ;
if       = "if" conds block { "else" "if" conds block } [ "else" block ] ;
conds    = cond { "," cond } ;  cond = ["let" pattern "="] cond_expr ;   (* cond_expr: no struct literal *)
match    = "match" cond_expr "{" { ( "else" | pattern ) "=>" ( block | stmt ) SEP } "}" ;
pattern  = "(" pattern { "," pattern } ")" | NUMBER | "-" NUMBER | STRING | COLOR | "true" | "false" | "none"
         | dotted [ "(" [ pattern { "," pattern } ] ")" | "{" [ IDENT { "," IDENT } ] "}" ] ;
assignop = "=" | "+=" | "-=" | "*=" | "/=" | "%=" | "&=" | "|=" | "^=" | "<<=" | ">>=" ;
```

---

## A `replace` off the game thread

D4.3's rule reads: a call on another thread runs no script hook. A `before` does not skip, a `wrap` proceeds, an
`after` leaves the result, and a `replace` proceeds: the game's own function runs and its result stands. Each is
logged once per hook as S0901.

## Match patterns

A match arm's pattern names an enum case written with its enum: `Shape.Ball(r)`, `Shape.Box { w, h }`,
`(Shape.Dot, 0)`. A bare name at the arm, or as an element of the arm's tuple, is an error. It would match every
value, so a misspelt case would quietly take everything: "an enum case written with its enum (`E.Case`), a literal
or `else`". Inside a case's fields a bare name binds the field (`Shape.Ball(r)`). `_` is not a name anywhere
(arms, `let`, parameters); the rest of a match is `else =>`. `let`, `if let` and `for` patterns take names as before.

## Options: which values decide `int` or `f32`

A `number` option is `int` when its `default` (always present, luma.json) and those of `min`, `max` and `step`
that are present are all whole; an absent one does not count. Otherwise it is `f32`.

## A struct literal in a condition

The rule of D4.10 is said where it bites: `if Point { x: 1 } == p` is refused at the `{` with "no struct literal in
the condition of if, while, match, guard or for: put it in parentheses". (A `{` after a capitalised name
whose next words are `name:` cannot open a block, since no statement starts so.) The parser likewise names the word
for `&&` (`and`), `||` (`or`) and a prefix `!` (`not`).

## What may be `none`, by evidence, both ways

- Results and fields stay as D4.7: `T?` unless the self-description marks them never-null.
- Parameters change. A pointer parameter takes `T`, not `T?`, unless the self-description marks it may-be-null.
  It marks it only from evidence:
  - the function tests it (`if (p)`, `p != nullptr`, `p == nullptr` before any use);
  - or it passes it only to a parameter that is itself may-be-null, followed through the calls;
  - or a hand-kept list row gives the reason.
  Passing `none`, or a `T?`, to a `T` parameter is a compile error naming the function and the parameter: "test it
  first (`if let`) or give a value". A `var x` argument is never null.
  Rows with their evidence:
  - `LiveActor::initModelManagerWithAnm` 2nd: LiveActor.cpp:382 and 384 test it, and ModelManager.cpp:437.
  - `MR::calcGravityVector(const LiveActor *, ...)` 3rd: through calcGravityVectorOrZero to
    PlanetGravityManager::calcTotalGravityVector, which tests it at :21. 4th: GravityUtil.cpp:45 tests it.
  - `MR::getFirstPolyOnLineToMap` 1st and 2nd: getFirstPolyOnLineCategory tests both (MapUtil.cpp:79 and 83).
- Text with a `T?`. Interpolating a `T?` (`"{x}"`) is a compile error: "`x` may be none: write
  `{x ?? ...}` or test it with `if let`". A `T?` given where a `T` goes is the same error, for the mod's own
  functions as for the game's.
- Never-null by evidence, for the self-description's hand list: `MR::getCurrentStageName` gives
  `mCurrSceneControlInfo.mStage`, which is `""` from SceneControlInfo's constructor (GameSystemSceneController.cpp:50).
  It is written only with `MR::internName`'s result (:57, :460 from the next info, :466 `""`), and internName maps
  null to `""` (StringUtil.cpp:480). So it is `text`.
- A result that is a field's address of another result. `MR::getPlayerPos` is
  `&MarioAccess::getPlayerActor->mPosition` (PlayerUtil.cpp:70). With no player it is not null but an address near
  0, so neither `T` nor `T?` makes it safe. The self-description marks such a function needs the inner one:
  - when the body dereferences or offsets a may-be-null result with no test, the row says `needs
    MarioAccess::getPlayerActor`;
  - the generated call tests the needed function first and gives `none` for a `T?` result. For a function with no
    result or a value result it gives a script error naming the need, and the game's function is not called.
  - The player rows are the first evidence: getPlayerPos, getPlayerCenterPos, getPlayerRotate,
    getPlayerShadowRotate, getPlayerGravity, getPlayerUp/Front/SideVec, getPlayerGroundPos, getPlayerHitRadius,
    isPlayerInAreaObj (PlayerUtil.cpp:66 to 154).
  - So `MR::getPlayerPos` is `vec3?` in fact, as D4.7 wrote, and the corpus tests it.

## Nested wraps

Under `every override of`, a base's wrap runs inside its override's. A wrap that sets module state
around `proceed` saves and restores it: `let was = current; current = self; proceed; current = was`, never
`current = none` after.

- In an `after`, `-> IDENT` right before the block names the result (`-> n {`, `-> bool {` too: an after's
  result type is the game's). Anything else after `->` is a type.
- `on draw <dotted>` takes any dotted point (`d.Point.Hud` after `use draw as d`). `on <word>.<...>` is always
  an event path, whatever the word (`on stage.entered`, `on frame.x`): start, stop, frame, load, stage, layers and draw
  are the event words only when no `.` follows.
- `log fact`: `fact` is the word when a value starts right after it (a string, a number, a name, a color, a
  game name, true, false, none, self, result). `log fact + 1` and `log fact.n` log a variable named fact.
- No comma before the `)` or `]` of a comma list (parameters, arguments, lists, tuples, fn types, patterns,
  event names), as G.3 and G.4 write them. Brace lists (SEP) may end with `,` or a line end.
- D4.2's "a block's `}`" is a block's only: after a struct literal's, a map's, a match's or a declaration's
  `}` the next statement needs a line end or `;`.
- `lparams` (a hook's or an override's) is names, each with an optional type: `var` and defaults are errors.
- `use` takes `.{names}` after a local path too, and `as` after any form (G.2 as written).
- After type arguments, a token that starts with `>` gives up its first `>`: `list<int>= []`,
  `map<text, list<int>>= {}`.
- One `start` line per `states`; a second is S0020 at the second.

- A program nests at most 512 levels deep (`MAX_DEPTH`). Each of these is one level: a bracket, a block, a type
  inside a type, a pattern inside a pattern, `not`, a prefix `-`, `wait`'s amount, `defer`'s statement, a value in an
  interpolation, and each node an operator or a postfix wraps around what came before (`a + b + c` is two deep,
  `f` two, `x as A as B` two). Past it is S0022 at the place, and the parse goes on. The compiler works on a stack
  of its own (64 MB), so any program within the limit is read in any build.
- D4.2: a line end after `>` or `>>` is not dropped by the lexer, so a type ending in them ends its
  line (`items: list<int>`, `target: ref<Actor>`, `var m: map<text, list<int>>`). As operators they still go on to the
  next line (`a >` / `b` is one comparison, `a >>` / `b` one shift): the parser skips the line end after them.
- G.1: an interpolation, and a string inside one, ends on its line; one that does not is S0004 at its `{`.
- One leading UTF-8 byte-order mark is skipped; CR LF and a lone CR each end a line.
- Section 2: a word with a letter outside ASCII (`café`) is S0023, said once for the whole word, which
  reads on as a name.
- Build-time codes S0001 to S0024 (S0022 the limit, S0023 names, S0024 the compiler could not start its thread)
  are worded in LumaLink's rows `LumaLink.Script.*` in the 14 languages; codes from S0901 are the run time's. Of two diagnostics at one place only the first is said.
- Type arguments take their `(` on the same line: `json.decode<list<int>>` / `(data)` is S0025 (below).

## Type arguments at a line end

- G.4 with D4.2: a call's type arguments that end a line, with its `(` at the start of the next
  (`json.decode<list<int>>` / `(data)`), are S0025 at the `(`, said once; the compiler reads on as the call. Such text
  was an error already (as written it is no call, and its comparison reading is a chained comparison, S0013). A
  comparison whose `>>` ends a line still goes on as a shift (`a < b >>` / `(c)` is `a < (b >> (c))`).

## What the checker reads

Each item is what the checker (crates/lumascript, check/body*)
does, tested in tests/body.rs; codes S0047 to S0100 are worded in LumaLink's rows in the 14 languages.

## Strings

- Escapes (S0093 names any other, with what follows the `\`): `\n \t \r \0 \\ \" \' \{ \}`; `\xHH` (two hex
  digits: below 80 in text, any byte in `b"..."`); `\u{H}` to `\u{HHHHHH}` (one to six hex digits, a Unicode scalar
  value, not a surrogate). A raw string has none. The lexer reads `\u{...}` whole, so its `{` opens no
  interpolation (as `\{` does not).
- Text is joined by interpolation (`"{a}{b}"`), not by `+`. A value put in text (an interpolation, `log`) is a
  number, `bool`, `text`, `json.Value`, a vector, `quat`, `color`, `mat34`, an enum of the mod or the game, or a
  list, set, map or tuple of those; a `T?` is S0067 (test it or give a value with `??`); anything else S0080.
- `t.bytes` gives a text's UTF-8 as `bytes`.

## Operators

| Operator | Takes | Gives |
|---|---|---|
| `+ - * / %` | two numbers of one type | that type |
| `+ -` | two vectors of one size | that vector |
| `* /` | a vector and `f32` (`f32 * v` too, for `*`) | the vector |
| `+` | two colors | `color` |
| `*` | `color` and `f32` | `color` |
| `& \| ^` | two integers of one type | that type |
| `<< >>` | an integer, shifted by any integer | the left side's type |
| `== !=` | two of one type that compares (below) | `bool` |
| `< <= > >=` | two numbers of one type, or two texts | `bool` |
| `..` `..=` | two integers of one type | a range |
| `and or not` | `bool` | `bool` |
| `??` | a `T?` and a `T` (or a `T?`) | `T` (or `T?`) |

- A literal side takes the other side's type (`x + 1` with `x: u8` is `u8`; `1.5 * v` is `f32 * vec3`); two literals
  are `int`, or `f32` when either is a float. A mismatch is S0065, a wrong single side S0066, said once.
- What compares (`==`, `!=`, a list's `contains`): every type but functions, views, game arrays, service
  handles, classes, tasks, pending results and content names; a list, set, optional, map or tuple when what it holds
  compares; a struct or enum of the mod when each field it holds compares (one that holds itself through a list
  compares by its other fields). Views and service handles: `same(a, b)` (S0073 says so).
- `x == none` and `x != none` take any `T?`; on a value that is never none, S0086.

## Built-in calls

| Call | Takes | Gives |
|---|---|---|
| `min max clamp` | numbers of one type | that type |
| `abs` | a signed integer or a float | that type |
| `sqrt sin cos`, `atan2` | floats of one type (literals: `f32`) | that type |
| `lerp(a, b, t)` | two floats, vectors or colors of one type, and `t` (`f32`, or the floats' type) | the type of `a` |
| `dot length` | vectors of one size | `f32` |
| `cross` | two `vec3` | `vec3` |
| `normal` | a vector | that vector |
| `hash32(t)` | `text` | `u32` |
| `low_bits<T>(n)` | an integer type `T`, any integer `n` | `T` |
| `vec2 vec3 vec4 quat` | 2, 3, 4, 4 `f32` | the value |
| `color(r, g, b)`, `color(r, g, b, a)` | `f32` 0..1 each | `color` |
| `same(a, b)` | two views of related records, the mod's object and a view of its base, a nerve and the game's `const Nerve *`, two handles of one service type (each may be `T?`) | `bool` |

Built-in names take positional arguments only. `json` alone is no value (S0083).

## json

- json data is: `bool`, a number, `text`, `json.Value`, a vector, `quat`, `color`, `mat34`, a game enum; a list,
  set, optional or tuple of json data; a map whose keys are `text`, an integer, `bool` or an enum and whose values
  are json data; a struct or enum of the mod whose fields are json data. A game struct by value is not (its fields
  may be pointers: copy what is wanted into a struct of the mod). Views, `ref`s, functions, `bytes`, tasks, pending
  results, classes and service handles are not either (S0094).
- How it is written (4.3 and lumart follow this; `json.decode` reads the same back):

  | Value | json |
  |---|---|
  | `bool`, a number, `text` | `true`/`false`, a number (integers exactly, past 2^53 too), a string |
  | `none` | `null` |
  | `vec2 vec3 vec4 quat` | `[x, y]`, `[x, y, z]`, `[x, y, z, w]`, `[x, y, z, w]` |
  | `color` | `[r, g, b, a]`, each 0..1 |
  | `mat34` | `[[...], [...], [...]]`, its three rows of four |
  | a list, set or tuple | an array |
  | a map | an object; an integer key in decimal, `bool` as `"true"`/`"false"`, an enum's by its name |
  | a struct of the mod | an object of its fields by name |
  | an enum's case without fields | its name, a string (`"Walk"`) |
  | an enum's case with fields | `{"Case": {fields by name}}` |
  | a game enum | its value's name (the first declared, for values with two) |

  A float that is NaN or infinite is a script error when written (json has none). Reading, an object's extra keys
  are skipped, and a missing field takes its default, else the whole decode is `none`.
- A literal in a place that takes a `json.Value` becomes one; inside it, a value that is no literal must be json
  data, as above.
- `json.encode(x)` takes json data (a written list or map literal is read as a `json.Value`) and gives `text`.
- `json.decode<T>(data)` takes `text` or `bytes` and gives `T?` (`none` when the data does not read as a `T`); `T` is
  json data.
- A `json.Value`'s `.int` is `int?` (`i32`) and `.float` is `f32?`: `none` when the value is no number or does
  not fit (an integer past `i32`, a fraction for `.int`). `.text .bool .list .map .get(k) .at(i)` as D4.10.

## Calls

- A named argument may be a `var` one: `name: var x`.
- A game function is chosen from its signatures so: those that take the arguments (S0058 lists them when none does);
  among several, those each argument fits as it is: an integer literal is exact for `i32` only and a float literal for
  `f32` only, as C++ picks (`pick` with `pick(s32)` and `pick(u32)` is `s32`); among several still, on a const view
  the const member. More than one left is S0059, naming them, and backquotes choose.
- Members and statics (`luma_functions.json`): a member function is called on an object (`actor.appear`), or by its
  full name on `self` inside a class or hook of its record (`LiveActor::appear` in a class derived from LiveActor);
  elsewhere it is S0100. A static or free function takes no object. Without the facts file, S0033 is said once per
  name and calls are read as written.
- A signature none of whose forms crosses (variadic, unreadable) is S0090; one whose parameter or result does not
  cross is S0060 or S0061 with the reason.
- A pointer to a number, vector or enum as a parameter does not cross; `T &` not const is a `var` parameter, written
  `var x` (S0062, S0063).
- A game function that takes a record by pointer or reference is given the game's object (a view), never the script's
  own copy of a plain game struct (`let b = a.mBox` copies it): S0098. `MR::useBox(a.mBox)` passes the game's object.

## `as`

`as` converts a number to a number, a game enum to an integer and an integer to a game enum, and an enum of the mod
without data to an integer. Nothing else (S0069).

## The game's enums

- A value is written `Mode::Walk` in an expression (an unscoped enum's also by its scope and value alone, `Walk` at
  global scope); a value of an unnamed enum is an `int`.
- In a `match` arm it is written dotted, `Mode.Walk` (patterns have no `::`; the checker joins the path with `::`),
  and S0068 names missing values that way. Two names of one value are one case, so an arm for each value is a
  complete `match`.
- `as` converts a game enum to an integer and back.

## Places

A write (`=`, `+=`, a `var` argument) goes to: a `var` local; a field, component (`v.x`), tuple part or element
(list, map, `bytes`) of a place; a field of the mod's class object; in a struct's function, a part of `self` when
it is `var self`, and in a class or hook a member of `self`; a game field, global or array element that is not
const, reached through a view that is not const; an after hook's `result`. S0064 says why anything else is not: a
`let`, a parameter that is not `var`, an option, a value (a call's result, a literal, `self` itself), const in the
game, a const view, or a name a function value captured.

- D7.2, literals (A42C-2), replaces "two literals are `int`, or `f32` when either is a float": an expression of
  literals only (`1 + 2`, `1 << 4`, `(1 << 4) - 17`) takes its place's number type, an integer one also a float
  place; with no number place it is `int`, or `f32` when a literal in it is a float. An integer expression's value is
  worked out and checked to fit once, for the whole (`let x: u8 = 200 + 100` is S0050 naming 300). A shift's literal
  left side (A42C-1) takes the place's integer type, else `int`, never the count's: `1 << n` with `n: u8` is an
  `int`.
- D7.5, const (A42C-3), replaces "among several still, on a const view the const member": among several still,
  the member whose const matches the view's: the const one on a const view, the other on a view that is not const.
- D7.5, float literals (A42C-4), replaces "a float literal for `f32` only, as C++ picks": a float literal is exact
  for `f32` only. This is the language's rule (a float literal is an `f32`, D4.10), which fits the game, whose float
  functions take `f32`; C++ would pick `double`.

## Flow, as the checker reads it

Sections 4, 6.1, 11 and 12 and D4.14 stand. Where they say less, these sentences say exactly what is checked.

## Returns and `guard`

- A function, a struct's or class's function, an export, a closure or a local `fn` with a result, and an `on layers`
  handler, must not reach its closing `}`. Every path ends in `return value`, or in a statement that never ends:
  `while true` that no `break` of its own leaves (a `break` of a loop inside it does not count). An `if` or `match`
  leaves only when all of its branches do, and an `if` without `else` never does. The error is said at the closing `}`
  and names the result type: S0101.
- `guard`'s `else` block must leave by `return`, `stop`, `break`, `continue`, `go` or `skip`, or by an `if` or `match`
  whose every branch leaves: S0102, said at the `else` block's `{`.
- Hooks follow D11: a `wrap` that proceeds, and a `before` that skips, are its rules.

## Views

A view, which is a value of a game class, a game array or place, a class's object or a field group, is kept past its
statement in these places, and only these. Each is S0103, naming the name and where:
- a module value (`let` or `var` at module level): `Script.Keep.Module`;
- a field of a struct, of an enum's case or of a class: `Script.Keep.Field`;
- a task's parameter, because a task runs on later frames than the statement that starts it: `Script.Keep.Task`;
- a closure that may run later, when it reads a local view of the body around it, `self`, or a member through
  `self`: `Script.Keep.Closure`. A closure written directly as a built-in's closure argument (`filter`, `map`, `any`,
  `all`, `sum`, `sort_by`) runs at once, inside its statement, and may read them. A closure inside such a closure
  that may itself run later may not.
- a local of a state's body read in its `update` or `exit`, which run on later frames: `Script.Keep.State`.

The rule follows the type through `?`, tuples, lists, sets and maps. A local list of views lives as long as a local
view, so it is allowed. It is refused wherever the view itself would be. This replaces 6.1's "a container", which
read as any container.

A plain game struct (no vtable, crossing by value) read through a view is the script's copy, and also through `?.`
(`let b = a?.mBox` is `Box?`, a copy). It is no view.

The way to keep an object is a handle: `ref(x)`, then `h.get`, which gives `none` once the object is gone.

## `wait`

- Where. `wait` may be in a `task`; in a state's body or its `enter`; in `on start`, `on stage`, `on option` or
  `on <service>.<event>`, which the compiler makes tasks when they wait; and in a `test game`. It may not be in a
  `fn`, a hook, a closure, a state's `update` or `exit`, `on stop`, `on frame`, `on load`, `on draw`, `on layers`, or
  a plain `test`. That is S0105 with `Script.Wait.Where`.
- Where in a statement. A `wait` is the first thing its statement reads, so nothing of the statement is held
  across it. It can be the whole of an expression statement, of a `let`'s or `var`'s value, of an assignment's value
  (`=`), or of a condition of `if` or `guard`. It can also be what those read first: `wait p ?? ""`,
  `(wait p).name`, `wait p as int`, `not wait p`, `(wait p).f(x)`. Anywhere else (`1 + wait p`, `f(wait p)`,
  `x += wait p`) it is S0105 with `Script.Wait.Inside`. The fix is a `let` of its own.
- Views across it. A local view made before a `wait` and read after it is S0104, naming the local and the wait's
  line, said once per local. These count as read after the wait:
  - a read the next time round a loop that waits;
  - `wait until`'s condition, which is read on each frame after the wait starts;
  - the local a `for` goes on through when its body waits;
  - the local whose view an assignment writes through (`a.x = wait p`), since the place is written after the wait.
  A task's own `self` does not exist. A state's `self` is its object, which the machine lives with, so it may be read
  after a wait.

## `defer`

What `defer` runs must end where it started. It may not contain `return`, `stop`, `go`, `skip`, `wait`, or a `break`
or `continue` of a loop outside it. A loop inside the deferred statement may `break`. Each of these is S0106, naming
the word.

## The `time` service in LumaScript

Sections 10, 11 and 12 read with these.

## `use svc.time`: the table as it is

- The functions are the header's, by their names, with `mod` given by the compiler: `time.now -> time.Now`,
  `time.monotonic_ns -> i64`, `time.wall -> time.Wall`, `time.set_rate(rate: u32)`, `time.hold`,
  `time.release`, `time.step(count: u32) -> bool`, `time.timer(clock: time.Clock, after: u64, every: u64, fn(count:
  u32)) -> time.Timer?`, `time.cancel(timer: time.Timer)`. No property or other sugar: the docs show one list, the
  header's.
- What the C results become: `set_rate`, `hold`, `release` and `cancel` return nothing (a mistake is reported by the
  service to the mod log, registry 7); `step` is false for LUMA_E_BUSY (another mod holds too, or loading: an answer,
  not a mistake) and true for LUMA_OK; `timer` is `none` when it was refused (reported). `now` and `wall` cannot fail.
- A service's struct `Luma<Service><Name>` is `<service>.<Name>` (`time.Now`, `time.Wall`), its enums likewise
  (`time.Clock.Frames`, `.Steps`, `.Nanoseconds`; the state's `time.State`); a handle is `<service>.<Name>` of the
  kind it opens (`time.Timer`). The leading `size` field and `reserved` fields are not the script's: the compiler sets
  `size` to the struct of the minor the mod imports and zeroes `reserved`. This rule is for every service.
- `time.on_frame` and `time.on_step` are not names of the script: `on frame` and `on step` are. Writing them
  is S0108 with `Script.Svc.Handler` (`{name} is set by "on {what}", not called`).

## `on frame` and `on step`

- `on frame { }` stays lumart's frame pass: at the frame's start in list order, before the mod's tasks
 . It runs while the world is held. One per module: the mod's one frame pass calls each module's in
  module list order. `on start` and `on stop` are one per module the same way (starts in module order, stops in
  reverse), as is LumaKit's `on mods.stopped`.
- `on step { }` and `on step(now: time.Now) { }` are new: one per mod, compiled onto `time.on_step`, run once per
  world step before the scene's update, so 0 times in a held frame and up to 16 in a fast one.
  `now` is the handler's own: its `step` and `step_in_frame` are this step's. A second `on step` in a mod is S0109
  (`Script.On.Twice`: `a second "on {what}"; the first is at line {line}`), as is a second `on load` or `on layers` in
  a mod, and a second `on frame`, `on start`, `on stop` or `on mods.stopped` in one module.
- `on mods.stopped(gone)` with its parameter untyped or `u64` is always the LumaKit rule for stopped mods (a mod that called this
  library's exports is gone); svc.mods' STOPPED event is `on mods.stopped(e: mods.StoppedEvent)`, its record type
  written.
- `on step` may not wait (D9.3's list gains it: S0105 `Script.Wait.Where`), and it is never a task. It may start one.

## `wait N frames` and `wait N seconds`

- `wait N frames` counts frames presented: it goes on while the world is held. `wait 0 frames` resumes at the next
  frame's pass.
- `wait N seconds` counts world steps at 60 a second: ceil(N × 60) steps, N an `int` or `f32` of at least 0. It
  follows the world's rate and stops while the world is held, as "game time; paused when the game
  pauses" said. A real-time wait is `wait until time.monotonic_ns >= end`.
- How lumart does it: in its frame pass it reads `time.now` once; a frames wait resumes when `frame` reaches its
  end, a seconds wait when `step` does. No timer is opened for a wait, so the service's 64 timers a mod are the
  script's own (`time.timer`). A mod whose code waits imports `time` 1.0 (the compiler adds the import; it needs no
  permission).
- After an `after`/`every` of other languages, LumaScript's way is a task: `task t { wait 2 seconds; ... }`, a
  loop for `every`.

## `on time.timer` is refused

Registry 2's event naming makes `on time.timer(...)` a path, but a timer's events go to the function given to
`time.timer`, not to a mod-wide handler. `on <service>.<event>` for an event that has a per-request function is S0107
with `Script.On.PerRequest` (`{event}'s events go to the function given to {call}; it is not an "on" event`). `time`'s
`timer` is the first.

## Codes

S0107 `Script.On.PerRequest`, S0108 `Script.Svc.Handler`, S0109 `Script.On.Twice`; S0105
as before with `on step` in its list. Rows in the 14 languages come with the checker's commit.

## Hooks, tasks and states as the checker reads them; `is_state`; `keep`/`kept`

Sections 8 and 11 said what hooks, tasks and states do; this part says how a head and a body are read, exactly, as
LumaLink checks them. Nothing in sections 8 and 11 changes but what D11.3 and D11.5 add.

## A hook's head

- The row. `Name::function` names a game function; its row is chosen by, in order: a quoted signature
  (``MR::isNear(const LiveActor *, const Vec3f &, f32)``); the number of parameters written; their written types,
  matched as a call's arguments are. A name no row has is S0047. A count no row of that name takes is S0112.
- The parameters take the row's types, as a hook receives them: numbers, values, enums and records by value as
  for a call; a pointer to a number, value or enum is `T?`, the place it points at (`none` for null); `const T &`
  is read, never written; other pointers as views. A parameter written with a type must be that type.
- `self` is the object for a method, a constructor and a destructor; a free or static function has none.
- Which hooks: `before`, `after`, `wrap`, `replace` on a function; `before` and `after` only on a constructor,
  a destructor, or a function the game only tells of (S0113). `every override of` only on a virtual (S0114).
  A hook in a class only on a member function of the game class it derives from, directly or through a base (S0115).
- The result: `-> name` names it for the body; `-> T` must be its type. A function returning nothing has none
  (S0116). A pointer or reference result is a place and may be written.
- Texts. A `const char *` parameter is `text?`, read from the game's bytes at the hook's
  start; a text written to it reaches the call as its kept copy (abi addition 6), in a `wrap` before `proceed` and
  in a `before` on its way out, only when it changed (a script error writes nothing). A `const char16_t *` result
  takes `text`/`text?`: its UTF-16 kept copy, made once per distinct text; a NUL inside is S0918.
- A parameter that does not cross may be bound as `_` (never read; the kernel passes it through untouched), or the
  list left out. Bound by a name it is S0060, whose message says to bind it as `_`.

## A hook's body

- `skip` only in a `before` hook's own body (S0117): `skip` alone when the function returns nothing (S0118), `skip v`
  with the result's value when it returns one (S0119).
- `result` in an `after`, a `wrap` or a `replace`, in the hook's own body only, never in a closure in it (S0120).
  In a `wrap` or `replace`, reading `result` before `proceed` or `result = v` on some path is S0124.
- `proceed` only in a `wrap`'s own body (S0121); at most once on any path, a loop's next round included (S0122).
- Every path of a `wrap` or `replace` of a function with a result gives the result, by `proceed` or `result = v`,
  at the body's end and at each `return` (S0123).
- `self` in a constructor's `before` (the object is not built) or a destructor's `after` (it is gone) is S0128.

## `stop`, `go` and `is_state`

- `stop` in a task, a state machine, or a handler that may wait (S0125).
- `go S` in a state machine, for one of its own states; `go M.S` in a machine or in a function or hook of the
  machine's class (S0126 elsewhere). `go S` from the class is S0130, which lists the class's machines. A state gives
  `enter`, `update` and `exit` at most once each (S0129).
- New: `is_state(S)` and `is_state(M.S)`, a standard function, `bool`: true while the object's machine M is in state
  S. It is placed and read exactly as `go` (S0126, S0127, S0130 name it); its argument is a state's name, never a
  value (S0131 otherwise). For example: `not
  is_state(OrbKingStates.Dizzy)`.

- The rows are by code, `Script.S0109` and so on, in the 14 languages.
- S0109's place is `first`, the first handler's file and line (`src/main.lum:12`), since the two may be in two of the
  mod's modules: `a second "on {what}" in the mod; the first is at {first}`.
- S0110: `wait -N frames` or `wait -N seconds` with a literal N above 0. S0902 (a computed negative wait, at run time)
  is the run time's. S0107, S0108 and S0111 are the services'.

## `keep` and `kept`

- `keep(value) -> bool` in `on stop`'s own body: the value, json data, encoded by the compiler, handed to
  LumaCore's `keep`; true when kept, false when over 1 MiB encoded (lumart also writes that to the
  mod log). The last call wins.
- `kept<T> -> T?` in `on start`'s own body, before any `wait` on its path (the kept bytes last until `start`
  returns): what the previous load of the mod's code kept this session, decoded as T; `none` when nothing was kept
  or the bytes are not a T (a changed struct after a reload), the latter also in the mod log.
- Elsewhere S0132 (`Script.Keep.Stop`, `Script.Kept.Start`); T or the value not json data is S0094.
- Both are standard names; a mod's own `keep` or `kept` hides them, as for every standard name.

## Codes

S0112 to S0132 as above, with rows and the phrases `Script.Result.Before`/`NotHook`, `Script.Proceed.NotWrap`/
`Replace`, `Script.Self.NotYet`/`Gone`, `Script.Keep.Stop`, `Script.Kept.Start` in the 14 languages.

## Services as the checker reads them from their headers

LumaLink reads every service from its header (services/registry.md), each the one way below; `time` is
its first case. What a header does not state yet is a note
S0047 "not known in this game's self-description", never a guess.

## Where a face comes from

- `use svc.<name>` needs a row of the self-description's `services` and the header `luma_<name>.h`
  of the game's SDK that LumaLink carries for that game version (its `native/include`, embedded when LumaLink is
  built). A dotted service (`system.clipboard`) is in its first part's header.
- A header out of the one form fails LumaLink's own mirror test when LumaLink is built. A modder never sees that
  as their error.

## Names

- The table `Luma<Name>Service` gives functions `<name>.<member>`. `base` is not a function, and `mod` is
  passed by the compiler.
- The prefix is the service's CamelCase name (`LumaSystemClipboard` for `system.clipboard`). A struct
  `<Prefix><T>` is `<name>.<T>`. A named enum `<Prefix><T>` is `<name>.<T>`, and its cases drop the
  enum's common `LUMA_..._` prefix, each `_` word in CamelCase: `LUMA_TIME_FRAMES` -> `time.Clock.Frames`,
  `LUMA_SYSTEM_D3D12` -> `system.Renderer.D3d12`.
- The event enum `<Prefix>Event` gives events `<name>.<kind>`, the kind in lower case.
- A `#define LUMA_<NAME>_<C>` of a whole number is a constant `<name>.<C>`, upper case as written
  (`time.TIMERS_MOST`). The cases of an unnamed enum are constants too.

## Parameters, after `mod`

| C | LumaScript |
|---|---|
| a number, `bool` | the same type |
| a named service enum | that enum |
| `const char *x` with no size after it | `text` (a NUL inside: abi 6's run-time error) |
| `const char *` or `const void *` or `const uint8_t *`, then `uint64_t` | one argument: `text` for `char`, else `bytes`; the compiler passes its length |
| `const <Prefix><T> *` | the struct by value; the compiler sets `size` and zeroes `reserved` fields; `T?` when the comment says `` `x` (null: ``, or `null:` among the parenthesis's other words |
| `<Prefix><T>` by value, `LumaColor` | the struct; `color` (passed as 0xRRGGBBAA) |
| `X fn, void *user`, `typedef void (*X)(void *user, args...)` in the header | one `fn(args)` argument the service calls back: kept until the mod ends, called through the compiler's trampoline (draw.at) |
| a handle kind `<Prefix><K>` (a typedef of LumaHandle) | `<name>.<K>` |
| a bare `LumaHandle` | `<name>.Request` |
| `LumaEventFn fn, void *user` | one closure argument |
| a non-const pointer to a number, enum or struct | an out: not an argument, part of the result |
| `char *` or `void *` or `uint8_t *`, then `uint64_t` | an out buffer: the result is `text` or `bytes` |

Any other parameter makes that function not callable from LumaScript (S0060, why ServiceParam). It stays a C
function of the header for C mods.

## Results

- Statuses. Each member's comment names every status it returns. The mod's mistakes (ARGUMENT, THREAD,
  HANDLE, LIMIT) are reported by the service, and CANCELLED never reaches running code. Neither is a value a script
  sees. The function's answers are its other `LUMA_E_*`.
- Outs, in order: one is `T`, more are a tuple. An out buffer is `text` or `bytes`. The compiler asks the
  whole length first (a null buffer with size 0), then fills.
- Without LUMA_PENDING:
  - an `int32_t` status with no answers: the outs, or nothing. A mistake gives zeros or nothing; it was reported.
  - an `int32_t` status with answers: `T?` of the outs (`none` for any answer or mistake), or `bool` with no
    outs (true for LUMA_OK).
  - an `int64_t` or `uint32_t` or `uint64_t` that is not a status (no `LUMA_E_` in its comment beyond the
    mistakes) is the number.
  - an `int64_t` length with answers (save.get) is folded into its out buffer's `T?`.
- With LUMA_PENDING, see D12.5.

## Requests and their events

- A request takes `LumaEventFn fn, void *user` and gives its handle through `LumaHandle *out`. In LumaScript it
  is called in one of two ways:
  - with a closure `f(args, fn(e: <name>.<Kind>Event) { })`: it returns the handle `<name>.<K>?` (`none`
    when refused), and the closure runs for each of its events, at a frame's start, under D9.2's view rules. The
    event record has `status` (`svc.Answer?`: `none` for LUMA_OK, else the answer), `value` (u64, as the
    event kind's comment says) and `flags`.
  - waited, without a closure: `f(args) -> pending<R?>`, only for a request whose comment says it closes after
    its event. `R` is the event's value (u64), or the take's result
    (below). It is `none` for any answer, including refusal.
- Take functions. A member whose comment says `inside the <KIND> event's call` is that event's take. It is not
  callable from a script: the closure's event record has the taken `text` or `bytes` as `data`, which the compiler
  takes in the call lumart makes. A waited request's `R` is that data.
- `svc.Answer` is the enum of the answers the registry defines (`Denied`, `Absent`, `NotFound`, `Exists`,
  `Io`, `Timeout`, `Closed`, `Memory`, `Busy`, `Format`, `TooLarge`, `Unreachable`, `State`): every `LUMA_E_*` status
  except the mistakes (`ARGUMENT`, `THREAD`, `HANDLE`, `LIMIT`) and `CANCELLED`. It is one
  language-wide type for every service.
- A table's `cancel(LumaHandle)` is `<name>.cancel(h)`, returning nothing.

## Handlers and codes

- `on <name>.<kind>(e) { }` is for an event kind that is the table's own mod-wide events: a kind no request names
  in its comment. For a kind whose events go to the function a request was given, it is S0107
  `{event}'s events go to the function given to {call}; it is not an "on" event`.
- A member whose comment says it is set by `on <x>` (the phrase `set by on <x>`; time's on_frame and
  on_step) is not a name of the script. Calling or naming it is S0108 `{name} is set by "on {what}", not called`.
- S0111: a literal outside a parameter's range that its comment states as `<a> to <b>` (the form
  `<param>: <a> to <b>`; time.step's count 1 to 16, time.set_rate's rate 1 to 16000). `{name}'s {param} is
  {found}; it takes {low} to {high}`. D10's S0111 for time.step is its first case.
- A service used without its permission is the validator's L506, said by LumaLink at check time
  with the `use`'s line.

## `time` read from its header

- `time.now -> time.Now` and `time.wall -> time.Wall`, `time.monotonic_ns -> i64`, `hold`,
  `release`, `set_rate(rate: u32)` and `step(count: u32) -> bool` are as D10.1.
- `time.timer(clock: time.Clock, after: u64, every: u64, fn(e: time.TimerEvent)) -> time.Timer?`: the closure takes
  the event record, whose `value` is the count. Every request reads the same
  way, so a script reads `e.value`.
- `time.Clock` and `time.State` are the header's named enums `LumaTimeClock` and `LumaTimeState`, and `time.Timer`
  is its timer handle.

## Codes

S0107, S0108 and S0111 as above, with rows in the 14 languages.

- A handle a call opens at once (no LUMA_PENDING, no closure: content.fx's `start` with `LumaContentFxEffect *out`)
  is an out like any other: `content.fx.start(...) -> content.fx.Effect`. A mistake gives the zero handle; it was
  reported, and the next call on it is LUMA_E_HANDLE, reported.
- A request's event kind is the `a <KIND> event follows` (or `an`) of its comment. The closure is
  `fn(e: <name>.<Kind>Event)`. A request whose comment names no kind, or a kind its table's event enum lacks, is not
  callable (S0060, why ServiceParam). A request has no out but its handle.
- `LumaColor` is `color`: the compiler packs 0xRRGGBBAA, each channel `round(c * 255)` of the 0..1 value.
- A header with several tables (luma_mods.h: mods, mods.ids, mods.install) names them in its
  `#define LUMA_<X>_NAME "<a.b>"` texts; a face takes none of the names of a dotted service under it.
- The shared statuses (registry 4: ARGUMENT for a null or unknown mod, CANCELLED, THREAD) are not in each member's
  comment; none is an answer, so no result changes.

## Streams

- A member that takes `LumaEventFn fn, void *user` and whose comment has a line `events: KIND(fields), KIND(fields)`
  is a stream: one handle (or a listener and the sockets it accepts) and many events of several kinds. It may return
  LUMA_OK with its handle at once (net's udp_open, tcp_listen) or LUMA_PENDING; D12.5's one-kind request form is
  unchanged for members without the line.
- In LumaScript it takes `fn(e: <name>.Event)`. `<name>.Event` is one enum for the service: a case per KIND any of
  its lines lists, CamelCase (`DATAGRAM` -> `Datagram`), carrying the listed fields in order, read by `match`. A KIND
  has the same fields on every line of its service (a header that differs is LumaLink's mirror-test failure):
  - `socket`: the event's handle (`LumaEvent.handle`) as the member's handle kind; ACCEPTED's is the new socket
    (`LumaEvent.value` as a handle), and an accepted socket's own events (DATA, SENT, CLOSED) reach the listener's
    function with their own `socket`.
  - `data`: `bytes` (`text` where the kind's comment says UTF-8 text): the kind's take (`take`, `take_datagram`),
    taken whole by the compiler in the call, else `LumaEvent.data`.
  - `from`: the take's other out (take_datagram's `LumaNetPeer` -> `net.Peer { host: text, port: u16 }`).
  - `status`: `svc.Answer?` (`none` for LUMA_OK). `value`: `u64`. `flags`: `u32`.
- The closure runs once per event, at a frame's start, as D12.5's. The handle the call returns is `<name>.<K>?` as
  D12.5's. C mods and the C ABI are unchanged; the line is a comment.
- First headers: luma_net.h (both tables, 10 members). The other streams (audio stream_open
  and tap_open, capture open, input watch_device and open, mods watch_options and watch_mods, a repeating time.timer)
  take the line next.

## Struct fields, constants and text

- A service struct's pointer field is a value: `const char *` is `text`, `const void *` (or `const uint8_t *`) with
  a `uint64_t` size after it is `bytes`, and `const <Prefix><T> *` with a `uint32_t` count after it is a list of
  `<name>.<T>`. The compiler writes the pointer and the count or size for the call; the script never names the count
  or size field, nor `size`. A pointer field left out of the literal is null in C; every other field must be written
  (S0075). A pointer of any other form makes the struct unusable from LumaScript.
- The cases of an unnamed `enum { }` of a header are constants of the service, upper case as written:
  `LUMA_INPUT_TEXT_NAME` is `input.TEXT_NAME`, a whole number. The event enum's cases are events, not constants.
- A service enum, a game enum and `svc.Answer` are shown in text by the case's name; a constant by its number.
- `text "Key"` is the mod's line `Key` in the player's language, the key when the game has none. Made before the game
  has read its messages (`on start`), it holds the key and reads as the line when it is used. Read before then, it
  is its key there, and the mod log says so once. The message tags in a line are bracket words
  (`language/message-tags.md`).
- `log` takes any `text`: a literal, a string with values placed in it, a `text "Key"` row, a variable or a result.

```lumascript
use svc.input
use svc.net.internet as internet

fn said(answer: internet.Answer?) -> text {
    if let a = answer {
        return "failed: {a}"
    }
    return "ok"
}

on start {
    let pad = input.Pad.CoStar
    log "pad {pad}, name kind {input.TEXT_NAME}, {said(none)}"
    let title = text "Title"
    log title
}
```

The game's log gets `pad CoStar, name kind 2, ok`, then `Title`: the row is read in `on start`, before the game's
messages exist, so it is its key, and the mod log says so. Kept in a variable and logged in `on stage`, it is the
mod's line. `language/text-and-services.md` has each rule with its own example.

## Further reading

* [LumaScript reference](index.md)
