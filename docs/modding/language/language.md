# LumaScript: the language

## More rules

Nothing here is built until accepted.

### 0. What it is for, and the rules it keeps

A mod written by a person who is not a C++ programmer, compiled by LumaLink to C and then by the platform's compiler
to machine code inside the mod's own library. The game contains nothing of LumaScript (rule 3).

1. Native. Every construct has a fixed translation to C that a reader of the generated file can follow. No
   interpreter, no bytecode, no reflection at run time, no code made while the game runs.
2. Safe by construction. No raw pointers, no manual freeing, no reading a game object after it is gone. Each
   mistake the compiler cannot rule out (an index outside a list, an empty handle, division by zero, an overflow) is
   a script error: reported with the mod, the file and the line, and the game goes on with its own behaviour.
3. No garbage collector. Values, owned containers freed when their owner ends, and a scratch area emptied each
   frame. Nothing pauses.
4. The game by name. Every function, field, class, enum and global of the game's self-description is in scope
   by its own name; the compiler checks each use against the installed game's description and writes the mod's
   import list from what the script uses.
5. One way to do each thing (rule 2). Where LumaKit gives a convenience, the language does not add a second.

### 1. Files and modules

- A source file is UTF-8 text with the extension `.lum`. One file is one module; its name is its path in the mod's
  `source/` folder without the extension, with `/` between parts (`source/ball/fly.lum` is `ball/fly`).
- `use` brings a module into scope:

  | Form | What |
  |---|---|
  | `use ./ball/fly` | another module of the same mod (path from `source/`) |
  | `use kit.actor` | a LumaKit module (or any library mod's module, `use <mod id>.<module>`, which must be a `needs` of the mod) |
  | `use svc.net` | a service: its functions and events |
  | `use c "helpers.h"` | C declarations from the mod's own C sources (section 11) |

  The game is always in scope (section 7); it is not `use`d. `use x as y` renames; `use x.{a, b}` takes names.
- A name declared `pub` is visible to modules that `use` this one; anything else is private to its file.
- The mod's entry is the module `main` (`source/main.lum`). Its top-level `on start`, hooks and states are the mod's
  own; other modules' top-level hooks are the mod's too (a hook is never private).

### 2. Lexical structure

- Comments: `// to the end of the line`, `/* ... */` (nesting).
- Identifiers: a letter or `_`, then letters, digits, `_` (ASCII). A game name may hold `::` between identifiers
  (`MR::getPlayerPos`, `Kuribo::exeWander`); a file-local game function is written as the self-description spells it
  in a string: `` `Game/Enemy/Kuribo.cpp#KuriboParam::KuriboParam` `` (backquotes take any name).
- Statements end at the end of a line, or at `;`. A line ending in an operator, `,` or an open bracket continues.
- Blocks are `{ ... }`. Indentation has no meaning; the formatter (4.7) writes four spaces.
- Keywords: `use pub let var const fn return if else while for in break continue match struct enum class
  override hook before after wrap replace proceed skip state states go wait until frames seconds task stop on
  option true false none self result as is and or not extern export defer test expect`.
- Literals: `12`, `0x1F`, `0b101`, `1_000_000`; `1.5`, `1e3`, `2.0f64`; `true`, `false`, `none`;
  `"text {expression} more"` (interpolation; `{{` for a brace), `r"raw \ text"`; `b"\x00\x01"` (bytes);
  `#FFD700` and `#FFD700CC` (colour); `[1, 2, 3]` (list); `{"a": 1}` (map); `vec3(0, 1, 0)`.

### 3. Types

| Type | What | In C |
|---|---|---|
| `bool` | | `uint8_t` 0/1 |
| `i8 i16 i32 i64 u8 u16 u32 u64` | integers; `int` is `i32` | the same |
| `f32 f64`; `float` is `f32` (the game's) | | the same |
| `vec2 vec3 vec4 quat` | `f32` components `x y z w`; `vec3` is laid out as the game's `TVec3f`/`Vec3f` | struct of `float` |
| `mat34` | the game's 3x4 matrix (`TPos3f`, `Mtx`) | `float[3][4]` |
| `color` | RGBA, `f32` 0..1 each; from `#RRGGBB(AA)` | struct |
| `text` | immutable UTF-8; `len` in bytes, `chars()` by code point | runtime handle |
| `bytes` | an owned byte buffer | runtime handle |
| `list<T>` `map<K, V>` `set<T>` | owned containers; `K` is an integer, `text`, `bool` or an enum without data | runtime handles |
| `T?` | `T` or `none` | tagged struct |
| `(A, B, ...)` | a tuple: values by position, `.0`, `.1`; `let (a, b) = pair` | C struct |
| `struct S { ... }` | named fields, values | C struct |
| `enum E { A, B(int), C { x: f32 } }` | a choice, each case with or without data | tagged union |
| `fn(A, B) -> R` | a function, or a closure (section 5.4) | function pointer + owned environment |
| `ref<C>` | a handle to a game object of class `C` (or a mod's own derived class): empty once the object is gone | generation-checked handle (section 6.2) |
| game classes `C` | a view of a live game object, valid during the statement or hook that has it (section 6.1) | pointer, not storable |
| game enums, game structs by value | as the self-description gives them | the game's own layout |

- Inference. `let x = 3` is `int`; `let v = vec3(1, 2, 3)`; a function's parameter and result types are always
  written, its locals never need to be. An integer literal takes the type its place needs if it fits, else is an
  error naming the type and the value.
- Conversions are written. `x as f32`, `n as u8` (an integer that does not fit is a script error; `wrap(n as
  u8)` keeps the low bits on purpose). No implicit conversion between integer and float, or between sizes, except
  that a literal adapts as above.
- Equality and order. `==` on every type but functions, views and service handles (for those, `same(a, b)`:
  the same object or the same stream); `<` on numbers and `text`.
- Operators. `+ - * / %` (integer: overflow and division by zero are script errors; float: IEEE), `color * f32`
  and `color + color` (each channel, kept in 0..1), `and or not`,
  `& | ^ << >>` on integers, `vec * float`, `vec + vec`, `dot cross length normal` as functions, `x?.y` (none when x
  is none), `x ?? y` (y when x is none), `a..b` and `a..=b` ranges.

### 4. Statements and expressions

```lumascript skip a listing from the part's contract, not a module that builds alone
let speed = 12.0                 // a constant local
var count = 0                    // a variable local
count += 1
if count > 3 { log "many" } else if count == 0 { log "none" }
while count < 10 { count += 1 }
for i in 0..10 { }               // ranges
for item in items { }            // lists, sets: the values; maps: (key, value) pairs
for (i, item) in items.indexed() { }
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

### 5.1 Functions

```lumascript skip a listing from the part's contract, not a module that builds alone
fn area(w: f32, h: f32) -> f32 { return w * h }
pub fn greet(name: text, times: int = 1) { for i in 0..times { log "hi {name}" } }
```

Parameters are values: a function receives copies (containers are shared until written, section 6.3). A parameter
marked `var` is passed so the function may change the caller's variable: `fn grow(var l: list<int>)`, called
`grow(var mine)`.

### 5.2 Structs and their functions

```lumascript skip a listing from the part's contract, not a module that builds alone
struct Ball {
    pos: vec3
    vel: vec3 = vec3(0, 0, 0)
    life: int = 120
    fn step(var self) { self.pos += self.vel; self.life -= 1 }
    fn dead(self) -> bool { return self.life <= 0 }
}
let b = Ball { pos: vec3(0, 0, 0) }
```

### 5.3 Enums

```lumascript skip a listing from the part's contract, not a module that builds alone
enum Phase { Idle, Flying(speed: f32), Done }
```

### 5.4 Closures

`fn(x: int) -> int { return x + step }` makes a closure; it copies what it uses from around it when made (it
never refers to a local that may end). A closure may be stored and called later. It may not capture a view of a
game object (section 6.1); it captures a `ref` instead.

### 5.5 Classes derived from the game's (kernel part `K.derive`)

```lumascript skip a listing from the part's contract, not a module that builds alone
class GoldBall : Kuribo {
    var age: int = 0
    override fn control() { age += 1; if age > 300 { kill() } else { base.control() } }
}
```

- An override's parameter types may be left out when its name has one virtual in the base (`override fn
  receiveMsgPlayerAttack(msg, other, mine)`); they are then the virtual's.
- The base is any game class the self-description lists as derivable. The game's factory makes it; it can be placed
  in a stage by its name (`GoldBall`) like a game class.
- Fields declared in the class are the mod's (stored beside the game's object as the kernel provides); the base's
  fields and functions are reached by name as on any game object.
- `override` replaces a virtual of the base for this class only; `base.control()` calls the base's version. Only
  virtuals the self-description lists can be overridden; anything else is a compile error naming it.

### 6.1 Views of game objects

A value of a game class type (`MarioActor`, `Kuribo`, `LiveActor`) is a view: it reaches the object's fields and
functions, and it is valid only while the code that received it runs (a hook's body, a statement holding a game
call's result). The compiler refuses to store a view in a variable that outlives that (a `var` at module level, a
struct field, a container, a closure, a task across a `wait`), naming the line. To keep an object, take a handle:
`let h = ref(self)`.

### 6.2 Handles

`ref<C>` names an object and becomes empty when the object is gone. Using it gives a view or `none`:

```lumascript skip a listing from the part's contract, not a module that builds alone
var target: ref<Kuribo>? = none
...
if let k = target?.get() { k.mVelocity = vec3(0, 10, 0) }   // nothing when it is gone
```

`get()` on a handle known not to be empty (`target!.get()`) is a script error if it is empty. The run-time support
learns that an object is gone from the destructor rows of its class (kernel: `LUMA_TELL` on `NameObj::~NameObj` and
every derived destructor); there is no list kept by the game for it, and no cost when a mod holds no handle.

### 6.3 Values and containers

Every value is copied when assigned or passed. Containers and `text` and `bytes` are shared underneath until one
copy is written (copy on write; counted, never cyclic, since a container holds values and never refers to another
container), so copying is cheap and never visible. They are freed when the last copy ends. Nothing in the language
can make a cycle, so nothing leaks and nothing waits for a collector.

### 6.4 Scratch

Text made only to hand to the game for one call (a `const char *` argument, a log line) is made in the frame's
scratch area, which the run-time support empties at the end of each frame. The script never sees this; it is how
the compiler avoids an allocation per call.

### 6.5 Module state

`var` and `let` at the top of a module live for the mod's whole life (from `on start` until the mod is stopped).
`let` at the top must be a constant expression or the result of `on start`.

### 7.1 Names

- Every function in the self-description: `MR::getPlayerPos()`, `GameDataFunction::setGameEventValue("Name", 3)`. An
  overloaded name with several signatures is written with its parameter types when the arguments do not choose:
  `MR::startAction(self, "Walk")` is chosen by its arguments; `` `MR::isNear(const LiveActor *, const Vec3f &, f32)`
  `` names one exactly. An ambiguous call is a compile error listing each signature.
- On a view: fields `self.mVelocity`, functions `self.exeWander()`, `self.getSensor("body")`. Base class members are
  reached through the view as in C++. In a `class` body or a hook on a member function, the class's own members may be
  written without `self.` (`kill()`, `makeActorAppeared()`).
- Globals: `game.sInstance` style statics by their qualified name.
- Enums of the game: `MarioStatus_Swim`, `ACTMES_SPIN_ATTACK`; constant game data in code by name:
  `Mario::kReactionDecay` (read only: the game's code folds a `const` at every use, so nothing written at run time
  reaches it; change what reads it with a hook). Game data the game reads at run time, a global that is not `const`
  (`gIsLuigi`) or a table (`set_table`), is written by name.
- `set_table(name, steps..., value) -> bool` writes one place of a game table for the mod: the table's name as
  `luma_numbers.json` lists it, then each field by its name (text) or element by its index, then the value in the
  place's type. It gives `true` when the place was written. The game's own bytes come back when the mod is taken out.
  `table<T>(name, steps...) -> T?` reads the place as it is now.
- `as_class<T>(x)` gives `x` as the game class or mod class `T`, or `none` when the object is not a `T`. An upcast
  needs no call: a `Kuribo` is a `LiveActor` where one is wanted.

```lumascript
on start {
    let mario = "Game/Player/MarioConst.cpp#gMarioConstDefault"
    if set_table(mario, "mJumpHeight", 0, 30.0) {
        if let h = table<f32>(mario, "mJumpHeight", 0) {
            log "jump height {h}"
        }
    }
}

fn is_goomba(a: LiveActor) -> bool {
    return as_class<Kuribo>(a) != none
}
```

### 7.2 Types across the boundary

| Game type | In LumaScript |
|---|---|
| `bool`, integer and float types, `s32 u16 f32` ... | the same |
| `TVec3f`, `Vec3f`, `TVec2f`, `TPos3f`, `Mtx` by value or `const &` | `vec3`, `vec2`, `mat34` (copied in and out) |
| `T &` / `T *` to one of those, written by the game | `var` argument: `MR::calcGravityVector(self, var g)` |
| A result `Vec3f *`, `const TVec3f &` (`MR::getPlayerPos()`) | a `vec3` copy, taken when the call returns |
| `const char *` | `text` |
| `s32 *`, `f32 *` out parameters | `var` argument |
| A game class pointer or reference (`LiveActor *`, `const Kuribo &`) | a view; `none` for null where the game may give null (`LiveActor?`) |
| Anything else (a raw buffer, a function pointer, a `void *`) | not callable from LumaScript; compile error naming the parameter. Reach it from C in the same mod (section 11) |

### 7.3 Checks

Each game use is bound by name when the mod loads (kernel loader: whole or not at all). The compiler checks every
use against the self-description of the game version(s) named in `luma.json`'s `nebula` range; a name missing in
any of them is a compile error naming the version, or, written `optional MR::foo`, a handle that is `none` on
versions without it.

### 8. Hooks

```lumascript skip a listing from the part's contract, not a module that builds alone
hook before MarioActor::control() {
    if frozen { skip }                    // a void function: skip it
}

hook after MR::getStarPieceNum() -> n {  // the result, named
    result = n * 2                        // changes what the caller receives
}

wrap MR::addStarPiece(num) {             // the arguments, named; may be written
    num *= 2
    proceed()                             // the next wrap, then the game's own code
}

replace Kuribo::exeWander() {            // one mod only (a second is refused at load, naming both mods)
    self.mVelocity = vec3(0, 0, 0)
}

hook before `MR::isNear(const LiveActor *, const Vec3f &, f32)`(actor, at, dist) -> bool {
    if dist < 0.0 { skip false }          // a non-void function: skip needs the result (else a compile error)
}
```

- `self` is the view of `this` for member functions.
- `before` may `skip` (with a result for a non-void function: the compiler requires it, so a skipped call always has
  one; audits/0.2-nebula.md Z-2). `after` may change `result`. `wrap` calls `proceed()` zero or one time per path;
  calling it twice is a compile error; a path that never proceeds must set `result` for a non-void function.
  `replace` never proceeds.
- A hook written in a `class` (section 5.5) applies to that class's objects only (the compiler adds the `is_a` test).
- Hooks on constructors and destructors: `hook after Kuribo::Kuribo(name)`, `hook before Kuribo::~Kuribo()`, before
  and after only (the kernel's rule).
- `hook ... every override of LiveActor::control()` hooks the virtual and each override the self-description lists
  (kernel part `K.vhook`).
- Order: hooks of one mod run in the order written; between mods, in the player's load order (kernel).
- A script error inside a hook ends that hook's run: a `before` does not skip; a `wrap` that has not proceeded
  proceeds; an `after` leaves the result as it was. The game's own behaviour goes on (section 9).

### 9.1 When building

Every compile error names the file, the line, the column, what was found and what was expected, in the language of
the person building (LumaLink's 14 languages), with a code `S<number>` and a page in the language book (10.2). The
first error does not stop the check; all are listed.

### 9.2 While the game runs

A script error (section 0.2) does this, and only this:

1. The statement stops; the hook, task or event handler it is in ends as section 8 says.
2. The mod log on screen (3.12) and the game's log get one line: the mod, `file.lum:line`, and what happened
   (`index 7 outside a list of 5`, `handle to Kuribo is empty`, `division by zero`, `i32 overflow in a + b`).
3. The same line repeated is counted, not repeated.
4. The mod keeps running. A script error cannot damage memory, so it is not a fault; the guard is for
   faults in C code (section 11), which do take the mod's hooks out.

Generated C carries `#line` markers, so a debugger, a crash report and a profile show `file.lum:line`.

### 10. Lifecycle, options, frames, content

```lumascript skip a listing from the part's contract, not a module that builds alone
option speed: f32           // declared in luma.json "options"; the compiler checks the name and type
option colour: text

on start { log "ready, speed {speed}" }         // after every import is bound
on stop { }                                     // before a reload or when the mod is turned off
on frame { }                                    // once per game frame, at the frame's start (service time)
on option speed { }                             // an option changed on the Mods screen
on load { gIsLuigi = true }                     // game data read at run time, before the stage that uses it loads
on stage "EggStarGalaxy" { }                    // a stage became ready (service time / LumaKit stage)
on draw draw.Point.HudAfter { }                 // a drawing point of the frame (service draw); 2D or 3D by point
on layers -> list<text> { return [] }           // which `when.code` layers are on; once a start, before content
```

- `on layers` names which of the mod's layers with `"when": {"code": true}` are on. It runs once a start, after every
  mod is loaded and before any `on start`: it can read the mod's options and its own assets, and no mod has started.
  A name that is not one of those layers is left out and said in the mod log. The game prepares its files with the
  answer; when the answer differs from the one the files were prepared with (the first time, or after an option
  changed it), the game starts again once, by itself, before it shows anything. If the answer changes again in that
  start, the prepared layers stay until the next start and the mod log says so. A fault in `on layers` turns the mod
  off, as one in `on start` does. It is not run again when the mod is reloaded while the game runs. A mod without
  `on layers` has none of these layers on.

- What of the game exists when each event runs:

  | Event | Runs | What exists |
  |---|---|---|
  | `on layers` | once a start, after every mod is loaded, before any `on start` | the mod's options and its own files; nothing of the game |
  | `on load` | at the mod's start, just before its `on start` | the same as `on start` |
  | `on start` | at the first line of the game's start | the mod's options and files, and every service; not the game's messages, archives, layouts, scene or objects |
  | `on frame` | every frame from the first, the boot frames too | the game's messages a few frames in, once it has read its system files; a scene, but no stage until one loads |
  | `on stage` | when a stage is ready | everything: messages, layouts, the scene, the player and the stage's objects |

  In `on start` a mod defines things (kinds, galaxies, hooks, its tables), reads its options and data and starts its
  tasks. A `text "Key"` made there reads as the game's line when it is used later. Read before the game's messages
  exist (logged, compared, built into another text, given to the game), it reads as its key there, and the mod log
  says so once. Code that needs the running game (its objects, its scene, the player) belongs in `on stage`, or in a
  hook on the game function that makes the thing it needs.

- Content of the mod is named by its path in `layers/` or its key, checked when building against the mod's own
  files and resolved when the mod loads: `model "GoldBall"`, `effect "Burst"`, `sound "Throw"`,
  `layout "Timer"`, `text "Timer.Title"`, `font "Lyrics"`, `movie "Trailer"`, `data "lyrics.json"` (a data file of
  the mod, its bytes; `json.decode` reads it). A name that is not there is a compile error naming it.
- `sound_named(name)`, `model_named(name)`, ...: the same lookup with a name made while running; `none` when the mod
  has no such content. The only content names the build cannot check, so each use is listed in the build's report.
- `log "..."` writes to the game's log as the mod; `log fact "key" value` writes a fact for the harness
 .

### 11.1 Tasks

```lumascript skip a listing from the part's contract, not a module that builds alone
task blink(h: ref<LiveActor>) {
    for i in 0..6 {
        if let a = h.get() { MR::hideModel(a) } else { stop }
        wait 5 frames
        if let a = h.get() { MR::showModel(a) } else { stop }
        wait 5 frames
    }
}
...
start blink(ref(self))            // runs from the next frame; returns a task handle
```

- A task runs on the game thread, at the frame's start (after `on frame`), until it reaches `wait`, `stop` or its
  end. `wait N frames`, `wait N seconds` (game time; paused when the game pauses), `wait until <condition>` (checked
  once per frame), `wait <task>` (until it ends), `wait event` for a service's event (section 12), and
  `wait <pending>`: an expression whose value is the result of a call that answers later (`let reply = wait
  http.get(url)`, `let name = wait input.text(...)`, `let i = wait screen.choose(...)`).
- The compiler turns a task into a struct holding its locals and a step number, and a function that runs from the
  step to the next `wait`. Nothing in the game schedules it: the mod's run-time support calls each live task from the
  mod's own `on frame` hook (service `time`'s frame event). No stack is kept between frames.
- Locals that live across a `wait` may not be views (section 6.1); the compiler names the line.
- `stop` ends the task; `t.stop()` from outside; a task whose `ref` argument empties is not stopped by itself (the
  script checks, as above).

11.1a A task with a result. `task f(params) -> T { ... }` gives a value: each `return v` is
checked against `T`, and a path that ends without one is S0101 (MISSING_RETURN), as in a function. A call `f(args)`
starts the task as `start` does and gives `pending<T>`; `start f(args)` still gives its `task`. `wait p` gives the
value when the task returns it. `p.stop()` cancels: the task is stopped (its `defer`s run, its way out as any stopped
task's), and a `wait p` on a task that ended without a value (stopped, its own `stop`, a script error, or `p` waited
already) stops the waiting task too, its way out run; it never gives `T`'s zero. A library's `pub task f(...) -> T` is
exported (its interface line `pub task`): the caller gets the handle with
the library's own operations, and `wait` copies the value into the caller's memory. A `pending` dropped without `wait`
is still the warning of D4.14.

A task without a result called gives `pending<()>`: `wait p` goes on once the task ended by
itself, and stops the waiting task as above when it was stopped or failed. Dropped, it is no warning (it loses
nothing); `start f(args)` still gives the `task`. A library's result-less `pub task` is exported the same way (its
`lumi1` result `pending<()>`).

### 11.2 State machines

```lumascript skip a listing from the part's contract, not a module that builds alone
states Phases for GoldBall {               // one machine per GoldBall object, stored with its fields
    state Fly {
        enter { MR::startAction(self, "Fly") }
        update { if MR::isBindedWall(self) { go Burst } }
    }
    state Burst {
        enter { effect "Burst"; sound "Pop" }
        wait 30 frames                        // a state's body may wait like a task
        kill()
    }
    start Fly
}
```

A machine is the same generated code as a task, with `go` jumping to a state's start and `enter`/`update`/`exit`
blocks run as their names say. On a derived class it runs from the class's `control` by default. LumaKit's `actor`
uses the same machines; the game's own nerves are reached as functions and hooks, not through `states`.

### 12. Services, events, LumaKit

- A service the game lacks (an older game, a platform without it) makes the mod refuse to load naming it, unless used
  as `optional svc.capture`, which is `none` there.
- A service result that waits on the outside world is an event: `on net.received(sock, data) { }`, or `wait`ed in a
  task: `let reply = wait http.get("https://...")`. Events arrive at the frame's start.
- Permissions a service needs are declared in `luma.json`; using a service whose permission is not declared is a
  compile error naming the permission.
- LumaKit modules are ordinary modules (`use kit.player.form`); the language knows nothing special about them.

### 13. C and C++ in the same mod

```lumascript skip a listing from the part's contract, not a module that builds alone
use c "fastmath.h"                         // declarations from the mod's source/ C or C++ files
extern c fn fast_noise(x: f32, y: f32) -> f32
export c fn ball_count() -> i32 { return balls.len() as i32 }
```

- Types across: integers, floats, `bool`, `vec*`, `mat34`, `color`, structs marked `c struct` (C layout), `text` and
  `bytes` as (pointer, length) for the call's duration, and `ref<C>` as an opaque handle with C functions to get the
  object (`lumart_ref_get`). No container crosses.
- The C side links the same `luma.h` and may hook, call and read by name itself; its faults go to the guard.

### 14. Tests in the mod

```lumascript skip a listing from the part's contract, not a module that builds alone
test "two balls at most" {
    expect count_after_throws(5) == 2
}
```

`lumalink test` compiles the mod's tests natively for the host and runs them without the game (for code that does
not touch the game), and runs tests marked `test game "..." { }` through the harness with the
game. Each failing `expect` names the file and the line and both sides.

### 15. What the compiler emits (summary)

- One C file per module, with `#line` markers; one generated `imports.c` holding the `LumaImport` list built from
  every game name used, with signatures from the self-description; the mod's `luma_mod` with `start` and `stop`.
- The run-time support library is linked statically into the mod: containers, text, bytes, scratch,
  handles, tasks, the error report. Freestanding (no C library), the same source on every platform.
- No construct produces code that runs outside the game thread unless a service says it does.

| To | What the language needs |
|---|---|
| kernel / luma.h | A way to report a script error that the on-screen mod log shows with its file and line (`LumaCore.report(mod, file, line, code, text)` or a `mods` service call); destructor rows on every class (done: `LUMA_TELL`); a mod with no imports |
| self-description | Overloads, `const`, which classes are derivable, which functions are virtual and their overrides (B3), enums and their values, game structs' layouts, constant data names (1.5), per version |
| `time` | A frame event at the frame's start, game time that stops when the game pauses, the load and pause state |
| `mods` | The mod's options and their change events |
| `K.derive` (1.4) | Fields stored beside a derived object; overrides as hooks limited to the class |
| `luma-format` | `source/` layout (`main.lum`), options declared once in `luma.json` and read by the compiler |
| validator | The same checks of option names, content names and permissions the compiler makes, when a `.luma` is installed |

### 16a. Standard modules

Always available without `use`: `json` (`encode`, `decode<T>`, `Value` with `.text() .int() .list() .map()`),
`math` names (`min max abs clamp lerp sqrt sin cos atan2 dot cross length normal`), `hash32(text)`, `wrap(...)`
(integer conversion keeping the low bits). Everything else is a service or a library mod.

### 17. Not in LumaScript 1 (stated, so nobody waits for them)

User generics; inheritance between the mod's own structs (only `class X : GameClass`); operator overloading; threads
(services that need them do the work on their own threads and deliver events); unsafe blocks (C in the same mod is the
way out); macros. Each can be added in a later version without changing the meaning of a valid LumaScript 1 program.

---

### Additions

1. Section 4, `if let`: takes the same list as `guard`: `if let r = room, not r.host, let p = r.peers.get(1) { }`
   checks each in order; the names bound are in scope in the `if` block only.
2. Section 3, container functions (each a compile error on the wrong type, never a run-time lookup):
   - `list<T>`: `len() -> int`, `push(T)`, `pop() -> T?`, `insert(int, T)`, `remove(int) -> T` (out of range: a
     script error), `get(int) -> T?`, `[int]` (out of range: a script error), `contains(T) -> bool` (T with `==`),
     `indexed() -> list<(int, T)>`, `map(fn(T) -> U) -> list<U>`, `filter(fn(T) -> bool) -> list<T>`,
     `any(fn(T) -> bool) -> bool`, `all(fn(T) -> bool) -> bool`, `sum(fn(T) -> N) -> N` (N a number type),
     `sort()` (T with `<`), `sort_by(fn(T, T) -> bool)`, `first() -> T?`, `last() -> T?`.
   - `map<K, V>`: `len()`, `get(K) -> V?`, `[K]` (read: a missing key is a script error; write: adds or replaces),
     `contains(K)`, `remove(K) -> V?`, `keys() -> list<K>`, `values() -> list<V>` (both in key order: integers and
     enums by value, `text` by bytes, so the same on every platform); `for (k, v) in m` (section 4).
   - `set<T>`: `len()`, `add(T) -> bool`, `remove(T) -> bool`, `contains(T)`, `values() -> list<T>` (in order).
   - `text`: `len()` (bytes), `chars()`, `split(text) -> list<text>`, `starts_with`, `ends_with`, `find(text) -> int?`,
     `slice(int, int)` (byte offsets on code point boundaries, else a script error), `lower()`, `upper()` (Unicode
     simple case mapping), `parse_int() -> i64?`, `parse_float() -> f64?`.
   - `bytes`: `len()`, `[int]`, `slice`, `push(u8)`, `read_u16/u32/u64/f32/f64(at, big: bool)`,
     `write_u16/...(at, value, big: bool)` (out of range: a script error).
3. Section 7.1, nerves by name: `nerve "DinoPackunVs1.BattleLv2"` is the game's `const Nerve *` for that class's
   step, usable where a game function takes `const Nerve *` (`self.setNerve(nerve "...")`) and comparable with
   `same(...)` to `getCurrentNerve()`. Checked at compile time against the self-description's list of each class's
   nerves; a name not listed is a compile error naming the class and its nerves. A nerve
   named by a run-time `text` is not possible (no run-time lookup): A.7.9's `nerve "{kind}.{want}"` becomes a
   `match` over the steps each boss has, generated by the mod's build from the same list.
4. Section 8, `wrap ... every override of`: `wrap every override of LiveActor::movement() { ... }` as `hook`
   allows (`K.vhook`); `self` is the object, of the virtual's class.
5. Section 8, a hook calling the function it hooks: inside any hook of a mod, a call to a function the same mod
   hooks runs the other mods' hooks and the game's code, not this mod's hooks on it (no recursion through one's own
   hooks); another mod's hooks run as usual. Request to the kernel (K.hook) below; proven in 1.1's run by a mod whose
   `after MR::getPlayerPos` calls `MR::getPlayerPos`.
6. Section 8, `result =` for a function returning a pointer to a vector (`Vec3f *`, `const TVec3f &`): the value
   is kept in a slot of that hook (one per hook, on the game thread) and the caller receives a pointer to the slot,
   valid until the hook next runs. A caller that keeps the pointer across a second call of the same function sees the
   newer value; the compiler warns (S-code) on every such hook, naming the function. Replaces P-20's proposal of a
   slot per actor (simpler; A.7.13 gives the same value to repeated calls inside one actor's movement, so the
   difference does not arise there).
7. Section 16, new requests: self-description (1.8): each class's nerves by name (from the game source's
   `NEW_NERVE` and `NEW_NERVE_ONEND` declarations, 3,434 and 161 of them: `NEW_NERVE(BossKameckVs1NrvBattleLv2,
   BossKameckVs1, BattleLv2)` is `nerve "BossKameckVs1.BattleLv2"`) with their address per build; kernel (K.hook): item 5's rule;
   `svc.net` (2.x): datagrams and STUN; `svc.save`: save sets; `svc.mods`: list, offer and
   use for one start, the validator run on a fetched mod.

---

Drafts 2 and 3 stay where this does not change them. The grammar is executable:

### D4.1 Words

- Reserved (never a name of the mod's own): `use pub let var fn return if else while for in break continue match
  struct enum class override hook wrap replace proceed skip state states go wait task stop on option true false none
  self result as and or not extern export defer test expect guard start log ref`. `const` and `is` are gone (D4.9).
- Contextual (a word only where the grammar places it, a name everywhere else): `before after every override of until
  frames seconds fact optional enter update exit game c draw layers stage frame load`, and the content words `model
  effect sound layout text font movie data nerve` (a content literal only when a string follows).
- After `::` or `.` any word is a name: `GameScene::start`, `GameScene::update`, `x.frames`, `p.0`.
- A `use` path's parts are words, reserved ones included (`use kit.player.state`, `use ./player/state`). The module's
  name is its last part (unless `as`), and a reserved word right before `.` in an expression names that module
  (`state.enter(...)`).
- The game's statics are written as the game names them, `GameSystem::sInstance` (7.1's `game.sInstance` is
  withdrawn), so `game` names nothing of the language and B.150's `use svc.save.game` stands.

### D4.2 Lines

A line end ends a statement, a field, a case, an entry or a match arm. It does not when: it is inside `( )` or `[ ]`
(but inside a `{ }` within them it does again); the line ends with an operator, `,`, `:`, `->`, `=>` or an open
bracket; or the next line starts with `else` (except `else =>`, a match arm). Inside `{ }` of a literal, a struct, an
enum or a match, entries are separated by `,` or a line end, either or both. After a statement that ends with a
block's `}`, the next statement may follow on the same line (`enter { ... } wait 180 frames`).

### D4.3 Hooks: re-entry, replace's error path, threads, order

- The abi's flag is therefore cleared around `core->proceed(call)` and set again after it (abi D4 below), so a
  recursive game function, or an override that calls its base under `every override of`, runs every mod's hooks each
  time, as the game would.
- A-3 A `replace` whose body ends in a script error gives the game's own result by `core->proceed(call)` (which runs
  the wraps after it and then the function), not by `invoke` (which would enter the function from the top and run
  every hook twice).
- A-2 Every script hook, task and handler runs on the game thread only. The generated code tests it first; a call on
  another thread does nothing (a `before` does not skip, a `wrap` proceeds, an `after` leaves the result) and logs
  once per hook `S0901: <function> called off the game thread: the hook is left out there`. The self-description marks
  functions known to run off the game thread (1.8); hooking one is a build warning naming it.
- L-4 Between mods: the mod higher in the list is outermost: its `before` runs first, its `after` last (its result
  stands), its `wrap` encloses the lower mods' wraps.

### D4.4 Handles across a stage change

6.2's last paragraph reads: an object goes in one of two ways, and a handle learns both: one by one, through the
destructor rows of its class (`LUMA_TELL` on `NameObj::~NameObj` and its overrides); and all at once when a stage's
arena is released (`NebulaArena::destroy` runs no destructor for most actors): the kernel tells the mod the
released address range (1.1) and `lumart` empties every handle whose object lies in it. A test (4.8): a
`ref<Kuribo>` kept in a module `var` across a stage change gives `none`. A-5: `ref<C>` takes only a class derived
from `NameObj` (or one the self-description marks with a lifetime); anything else is a compile error naming it.

### D4.5 Operators and precedence (B.37)

Loosest first: `??` (right to left); `or`; `and`; `not` (prefix); comparisons `== != < <= > >=` (one per expression:
`a == b == c` is an error); `..` `..=`; `|`; `^`; `&`; `<< >>`; `+ -`; `* / %`; prefix `-`, `wait`, `start`; `as`
(binds tighter than every binary operator: `a as u32 * b as u32` is `(a as u32) * (b as u32)`); postfix: a call
`(...)`, `[i]`, `.name`, `?.name`, and `!` (L-6: "the value, which must not be `none`", a script error when it
is). There is no prefix `!` and no `&&`, `||`, `? :`. `x == none` and `x != none` test any `T?` (T need not have
`==`).

### D4.6 Calls: named arguments

A call gives its positional arguments first, then any by name, `name: value`: `draw.tint(a, multiply: #FFFFFF, add:
c)`. A name the function lacks, a parameter given twice, or a required one missing is a compile error naming it.
Named arguments are for the mod's functions, LumaKit's and services' (whose parameters have names); a game function
takes positional ones only (the self-description may not name its parameters). An enum case with fields is
constructed as a call, `actor.Sensor.Body(radius: 70.0)`, positional or named by its fields. A `var` argument is
written `var x` (5.1).

### D4.7 Null game pointers

Every game pointer or reference result, every pointer field read from a game object, and every pointer
parameter is `T?` (and `const char *` is `text?`), unless the self-description marks it never-null. 1.8 takes
that mark only from evidence: a C++ reference type `T &`, or a list kept by hand with each row's reason. So
`MarioAccess::getPlayerActor()` is `MarioActor?` (`if let p = MarioAccess::getPlayerActor() { ... }`), and `none`
may be passed for a pointer parameter (`initModelManagerWithAnm("Crumble", none, false)`). `self` in a hook or a class
is never `none`.

### D4.8 Hook heads (B.29, A.7)

The function is named `Class::name`, by backquotes (`` `MR::isNear(const LiveActor *, f32)` ``), or as either followed
by its parameters: names only (`(actor, at, dist)`), or names with LumaScript types (`MR::isInWater(at: vec3)`), which
choose the overload whose parameters map to those types (none or two: a compile error listing the signatures). `->
name` names the result in an `after`; `-> type` gives the result's type in a `before` that may `skip` with a value.

### D4.9 Module level

A module holds only declarations: `use`, `option`, `let`, `var`, `fn`, `struct`, `enum`, `class`, `task`, hooks,
`on`, `states`, `test`, `extern`, `export`. A module `let` or `var` may be any expression; they run when the mod
starts, in module order, before every `on start` (as abi 8 already says); one that reads the game runs after every
import is bound. `const` is withdrawn: a module `let` is the one way (the compiler folds it when it is constant).
A bare statement at module level is an error (`on start { }` holds it).

### D4.10 Literals (B.65, B.19)

- `[a, b]` a list; `{}` an empty map or set, by its place; `{k: v, ...}` a map; `{a, b}` a set; `Name { field: v,
  shorthand }` a struct (a capitalized name, or a module's: `item.Item { ... }`), fields by `,` or line ends.
- A struct literal is not allowed directly in the condition of `if`, `while`, `match`, `guard` or `for ... in` (it would
  read as the block); put it in parentheses.
- `json.Value`: a literal (list, map, text, number, bool, `none`) in a place that takes a `json.Value` becomes one,
  each value by its kind, recursively; a map's keys are `text`. `json.Value` has `.text() .int() .float() .bool()
  .list() .map()` (each `T?`) and `.get(text) -> Value?`, `.at(int) -> Value?`.

### D4.11 Conversions

`wrap(...)` as a conversion is withdrawn: `wrap` is the hook form only. Keeping an integer's low bits is
`low_bits<T>(n)`. Built-ins that take a type (`json.decode<T>`, `low_bits<T>`) write it in `< >` right before the
call's `(`; that is the only place a type follows an expression (user generics stay out, section 17).
`u32.max`, `i16.min`... are each integer type's limits.

### D4.12 Options

`option <key>: <type>` declares an option of `luma.json`'s `options` by its key; the types match its `kind`: `toggle`
-> `bool`; `number` -> `int` when its `default`, `min`, `max` and `step` are whole, else `f32` (declaring `int` for a
number with a fraction is an error naming it); `text` -> `text`; `choice` -> `text` (one of its `values`, which a
`match` on it may list); `key` -> `input.Action`. An option is read by its key as a name (`balls_at_once`); there is
no `option("key")` call (A.7.14 changes).

### D4.13 Smaller words (B.65, B.118)

- One container way each: `filter` (no `retain`: `balls = balls.filter(...)`), `set.add` (no `insert`), `filter(...)
  .last()` (no `last_where`). A handle is empty when `h.get() == none` (no `empty()`).
- `map[K]` gives `V` (a missing key is a script error); `map.get(K)` gives `V?`.
- `same(a, b)` for views and service handles (no `same_stream`).
- `self` is the first parameter of a struct's function or a class member only; a free function names its own.
- `match` arms may be integer, text or bool literals, a negative number, an enum case (`E.Case(x)`, `E.Case { a }`) or
  a tuple of patterns; `else =>` for the rest (no `_ =>`). An arm's body is a statement or a block.
- A closure's parameters are typed; its result type may be left out when it gives none (`fn(i: int) { ... }`).

### D4.14 `pending<T>` and text with placeholders

- A call that answers later (`http.get`, `input.text`, `screen.choose`, `talk.ask`, `cutscene.play`) returns
  `pending<T>`. Only `wait` reads one, and `wait` is allowed only in a `task`, a state's body or `enter`, or a
  `wait`-holding handler that the compiler makes a task (`on start` and events): in a `fn` or a hook it is an error
  naming the line (A.7.3's and A.7.14's functions become tasks). A `pending` dropped without `wait` is a warning.
- `text "Key"` is the mod's text row in the player's language. Its placeholders `{name}` are filled by named arguments
  right after it: `text "Coop.Hosting"(address: outside)`. Each placeholder of the row must be given (in every
  language's row), and no other: checked when building, as content names are.
- Content literals take a plain string (no `{...}`): content is named when building (`nerve "{a}.{b}"` is an error).

### D4.E Program changes

By kind: A.7's correction table applied where A.7 still had `!x`, `&&`, `_ =>`, `? :`, `class` as a name, untyped
closures (027-037, 039, 040, 044); `const` -> `let` (A.1, A.7); `retain`, `empty()`, `insert`, `last_where`, `p["k"]?`
-> D4.13 (A.1, B.65, B.118); `same_stream` -> `same` (A.3); the player's actor read through `if let`/`guard let`/`?.`
(A.5, B.11, B.29, B.167; D4.7); `wrap(... as u16)` -> `low_bits<u16>(...)` (B.37); `fn chat(self: ...)` -> `npc`
(B.41); module-level statements into `on start` (B.89, A.7.4); `fn` with `wait` -> `task` (A.7.3, A.7.14);
`option("k")` -> declared options (A.7, A.7.14); text with a placeholder (A.7.14); `nerve "{..}"` -> the generated
match (A.7.9). The revision blocks of 5.1 (R) parse; the amendment of A.6 is parsed as statements.

### Appendix G. Grammar (EBNF; `ls_parse.py` is its executable form)

```lumascript skip a listing from the part's contract, not a module that builds alone
(* G.1 tokens *)
IDENT    = letter_or_ "_" { letter | digit | "_" } ;          (* not a reserved word, D4.1 *)
GAME     = "`" { any but "`" and line end } "`" ;
NUMBER   = "0x" hex {hex|"_"} | "0b" bin {bin|"_"} | digit {digit|"_"} ["." digit {digit|"_"}] [exp] [suffix] ;
COLOR    = "#" hex*6 [hex*2] ;
STRING   = ["r"|"b"] '"' { char | "{{" | "{" expr "}" } '"' ;   (* "{...}" interpolates; not in r or b strings *)
comments = "//" ... line end | "/*" ... "*/" (nesting) ;
END      = line end (D4.2) | ";" ;

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

### D5.1 A `replace` off the game thread

D4.3's A-2 reads: a call on another thread runs no script hook. A `before` does not skip, a `wrap` proceeds, an
`after` leaves the result, and a `replace` proceeds: the game's own function runs and its result stands. Each is
logged once per hook as S0901.

### D5.2 Match patterns

A match arm's pattern names an enum case written with its enum: `Shape.Ball(r)`, `Shape.Box { w, h }`,
`(Shape.Dot, 0)`. A bare name at the arm, or as an element of the arm's tuple, is an error. It would match every
value, so a misspelt case would quietly take everything: "an enum case written with its enum (`E.Case`), a literal
or `else`". Inside a case's fields a bare name binds the field (`Shape.Ball(r)`). `_` is not a name anywhere
(arms, `let`, parameters); the rest of a match is `else =>`. `let`, `if let` and `for` patterns take names as before.

### D5.3 Options: which values decide `int` or `f32`

A `number` option is `int` when its `default` (always present, luma-format) and those of `min`, `max` and `step`
that are present are all whole; an absent one does not count. Otherwise it is `f32`.

### D5.4 A struct literal in a condition

The rule of D4.10 is said where it bites: `if Point { x: 1 } == p` is refused at the `{` with "no struct literal in
the condition of if, while, match, guard or for: put it in parentheses (D4.10)". (A `{` after a capitalised name
whose next words are `name:` cannot open a block, since no statement starts so.) The parser likewise names the word
for `&&` (`and`), `||` (`or`) and a prefix `!` (`not`).

### D5.5 What may be `none`, by evidence, both ways

- Results and fields stay as D4.7: `T?` unless 1.8 marks them never-null.
- Parameters change. A pointer parameter takes `T`, not `T?`, unless 1.8 marks it may-be-null.
  1.8 marks it only from evidence:
  - the function tests it (`if (p)`, `p != nullptr`, `p == nullptr` before any use);
  - or it passes it only to a parameter that is itself may-be-null, followed through the calls;
  - or a hand-kept list row gives the reason.
  Passing `none`, or a `T?`, to a `T` parameter is a compile error naming the function and the parameter: "test it
  first (`if let`) or give a value". A `var x` argument is never null.
  Rows the corpus uses, with their evidence:
  - `LiveActor::initModelManagerWithAnm` 2nd: LiveActor.cpp:382 and 384 test it, and ModelManager.cpp:437.
  - `MR::calcGravityVector(const LiveActor *, ...)` 3rd: through calcGravityVectorOrZero to
    PlanetGravityManager::calcTotalGravityVector, which tests it at :21. 4th: GravityUtil.cpp:45 tests it.
  - `MR::getFirstPolyOnLineToMap` 1st and 2nd: getFirstPolyOnLineCategory tests both (MapUtil.cpp:79 and 83).
- Text with a `T?`. Interpolating a `T?` (`"{x}"`) is a compile error: "`x` may be none: write
  `{x ?? ...}` or test it with `if let`". A `T?` given where a `T` goes is the same error, for the mod's own
  functions as for the game's.
- Never-null by evidence, for 1.8's hand list: `MR::getCurrentStageName` gives
  `mCurrSceneControlInfo.mStage`, which is `""` from SceneControlInfo's constructor (GameSystemSceneController.cpp:50).
  It is written only with `MR::internName`'s result (:57, :460 from the next info, :466 `""`), and internName maps
  null to `""` (StringUtil.cpp:480). So it is `text`, and A.2, A.4, A.5, B.65 stand as written.
- D-10, new: a result that is a field's address of another result. `MR::getPlayerPos` is
  `&MarioAccess::getPlayerActor()->mPosition` (PlayerUtil.cpp:70). With no player it is not null but an address near
  0, so neither `T` nor `T?` makes it safe. 1.8 marks such a function needs the inner one:
  - when the body dereferences or offsets a may-be-null result with no test, the row says `needs
    MarioAccess::getPlayerActor`;
  - the generated call tests the needed function first and gives `none` for a `T?` result. For a function with no
    result or a value result it gives a script error naming the need, and the game's function is not called.
  - The player rows are the first evidence: getPlayerPos, getPlayerCenterPos, getPlayerRotate,
    getPlayerShadowRotate, getPlayerGravity, getPlayerUp/Front/SideVec, getPlayerGroundPos, getPlayerHitRadius,
    isPlayerInAreaObj (PlayerUtil.cpp:66 to 154).
  - So `MR::getPlayerPos()` is `vec3?` in fact, as D4.7 wrote, and the corpus tests it.

### D5.6 Nested wraps

Under `every override of`, a base's wrap runs inside its override's (section 8). A wrap that sets module state
around `proceed()` saves and restores it: `let was = current; current = self; proceed(); current = was`, never
`current = none` after. A.7.13 is written so.

### D5.E Program changes

- A.5: `getPlayerPos` through `guard let`; the raycast's `Triangle *` is `none` (a normal is not that parameter).
- B.29, B.93: `getPlayerPos` tested.
- A.7: Peer gains `alive` (lumakit's Pose has no `alive`).
- A.7.2: `r.peers.get(r.me)?.character` (a missing key is a script error, D4.13).
- A.7.7, A.7.14: `pose.at` (Pose has no `position`). G found these two while answering D-8.
- A.7.8, A.7.11, A.7.14: `forget(word)` when the event a word allowed has happened, and every word at a stage start .
- A.7.9: "9, 6, 6 and 9 exe functions (of 12, 12, 13 and 15 rows)".
- A.7.12: only placed objects are told dead (`if let key = actor.placement(self)`).
- A.7.13: written by meaning.

---

- P6-1 In an `after`, `-> IDENT` right before the block names the result (`-> n {`, `-> bool {` too: an after's
  result type is the game's). Anything else after `->` is a type.
- P6-2 `on draw <dotted>` takes any dotted point (`d.Point.Hud` after `use draw as d`). `on <word>.<...>` is always
  an event path, whatever the word (`on stage.entered`, `on frame.x`): start, stop, frame, load, stage, layers and draw
  are the event words only when no `.` follows.
- P6-3 `log fact`: `fact` is the word when a value starts right after it (a string, a number, a name, a colour, a
  game name, true, false, none, self, result). `log fact + 1` and `log fact.n` log a variable named fact.
- P6-4 No comma before the `)` or `]` of a comma list (parameters, arguments, lists, tuples, fn types, patterns,
  event names), as G.3 and G.4 write them. Brace lists (SEP) may end with `,` or a line end.
- P6-5 D4.2's "a block's `}`" is a block's only: after a struct literal's, a map's, a match's or a declaration's
  `}` the next statement needs a line end or `;`.
- P6-6 `lparams` (a hook's or an override's) is names, each with an optional type: `var` and defaults are errors.
- P6-7 `use` takes `.{names}` after a local path too, and `as` after any form (G.2 as written).
- P6-8 After type arguments, a token that starts with `>` gives up its first `>`: `list<int>= []`,
  `map<text, list<int>>= {}`.
- P6-9 One `start` line per `states`; a second is S0020 at the second.
- P6-10 Build-time codes S0001 to S0021 (crates/lumascript/src/diag.rs), worded in LumaLink's rows
  `LumaLink.Script.*` in the 14 languages; S0901 on stay lumart's (abi section 6).
- P6-11 A file may end its lines in CR LF.

- P6-12 A program nests at most 512 levels deep (`MAX_DEPTH`). Each of these is one level: a bracket, a block, a type
  inside a type, a pattern inside a pattern, `not`, a prefix `-`, `wait`'s amount, `defer`'s statement, a value in an
  interpolation, and each node an operator or a postfix wraps around what came before (`a + b + c` is two deep,
  `f()()` two, `x as A as B` two). Past it is S0022 at the place, and the parse goes on. The compiler works on a stack
  of its own (64 MB), so any program within the limit is read in any build.
- P6-13 D4.2 as built: a line end after `>` or `>>` is not dropped by the lexer, so a type ending in them ends its
  line (`items: list<int>`, `target: ref<Actor>`, `var m: map<text, list<int>>`). As operators they still go on to the
  next line (`a >` / `b` is one comparison, `a >>` / `b` one shift): the parser skips the line end after them.
- P6-14 G.1: an interpolation, and a string inside one, ends on its line; one that does not is S0004 at its `{`.
- P6-11 as built: one leading UTF-8 byte-order mark is skipped; CR LF and a lone CR each end a line.
- P6-15 Section 2 as built: a word with a letter outside ASCII (`café`) is S0023, said once for the whole word, which
  reads on as a name.
- P6-10 now: build-time codes S0001 to S0024 (S0022 the limit, S0023 names, S0024 the compiler could not start its
  thread). Of two diagnostics at one place only the first is said.
- One could, A42R-1: P6-13 could add that type arguments take their `(` on the same line (`json.decode<list<int>>` /
  `(data)` is S0013 today, which names the wrong mistake).

### A42R-1

- P6-16 G.4 with D4.2: a call's type arguments that end a line, with its `(` at the start of the next
  (`json.decode<list<int>>` / `(data)`), are S0025 at the `(`, said once; the compiler reads on as the call. Such text
  was an error already (as written it is no call, and its comparison reading is a chained comparison, S0013). A
  comparison whose `>>` ends a line still goes on as a shift (`a < b >>` / `(c)` is `a < (b >> (c))`).

### What the checker's part (c) reads (4.2)

Drafts 2 to 6 stand where this does not change them. Each item is what the checker (crates/lumascript, check/body*)
does, tested in tests/body.rs; codes S0047 to S0100 are worded in LumaLink's rows in the 14 languages.

### D7.1 Strings

- Escapes (S0093 names any other, with what follows the `\`): `\n \t \r \0 \\ \" \' \{ \}`; `\xHH` (two hex
  digits: below 80 in text, any byte in `b"..."`); `\u{H}` to `\u{HHHHHH}` (one to six hex digits, a Unicode scalar
  value, not a surrogate). A raw string has none. The lexer reads `\u{...}` whole, so its `{` opens no
  interpolation (as `\{` does not).
- Text is joined by interpolation (`"{a}{b}"`), not by `+`. A value put in text (an interpolation, `log`) is a
  number, `bool`, `text`, `json.Value`, a vector, `quat`, `color`, `mat34`, an enum of the mod or the game, or a
  list, set, map or tuple of those; a `T?` is S0067 (test it or give a value with `??`); anything else S0080.
- `t.bytes()` gives a text's UTF-8 as `bytes`.

### D7.2 Operators (section 3 and D4.5, as built)

| Operator | Takes | Gives |
|---|---|---|
| `+ - * / %` | two numbers of one type | that type |
| `+ -` | two vectors of one size | that vector |
| `* /` | a vector and `f32` (`f32 * v` too, for `*`) | the vector |
| `+` | two colours | `color` |
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

### D7.3 Built-in calls (16a, as built)

| Call | Takes | Gives |
|---|---|---|
| `min max clamp` | numbers of one type (2, 2, 3) | that type |
| `abs` | a signed integer or a float | that type |
| `sqrt sin cos` (1), `atan2` (2) | floats of one type (literals: `f32`) | that type |
| `lerp(a, b, t)` | two floats, vectors or colours of one type, and `t` (`f32`, or the floats' type) | the type of `a` |
| `dot length` | vectors of one size | `f32` |
| `cross` | two `vec3` | `vec3` |
| `normal` | a vector | that vector |
| `hash32(t)` | `text` | `u32` |
| `low_bits<T>(n)` | an integer type `T`, any integer `n` | `T` |
| `vec2 vec3 vec4 quat` | 2, 3, 4, 4 `f32` | the value |
| `color(r, g, b)`, `color(r, g, b, a)` | `f32` 0..1 each | `color` |
| `same(a, b)` | two views of related records, the mod's object and a view of its base, a nerve and the game's `const Nerve *`, two handles of one service type (each may be `T?`) | `bool` |

Built-in names take positional arguments only. `json` alone is no value (S0083).

### D7.4 json (D4.10, as built)

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
- A `json.Value`'s `.int()` is `int?` (`i32`) and `.float()` is `f32?`: `none` when the value is no number or does
  not fit (an integer past `i32`, a fraction for `.int()`). `.text() .bool() .list() .map() .get(k) .at(i)` as D4.10.

### D7.5 Calls (D4.6 and 7.1, as built)

- A named argument may be a `var` one: `name: var x`.
- A game function is chosen from its signatures so: those that take the arguments (S0058 lists them when none does);
  among several, those each argument fits as it is: an integer literal is exact for `i32` only and a float literal for
  `f32` only, as C++ picks (`pick(1)` with `pick(s32)` and `pick(u32)` is `s32`); among several still, on a const view
  the const member. More than one left is S0059, naming them, and backquotes choose (7.1).
- Members and statics (`luma_functions.json`): a member function is called on an object (`actor.appear()`), or by its
  full name on `self` inside a class or hook of its record (`LiveActor::appear()` in a class derived from LiveActor);
  elsewhere it is S0100. A static or free function takes no object. Without the facts file, S0033 is said once per
  name and calls are read as written.
- A signature none of whose forms crosses (variadic, unreadable) is S0090; one whose parameter or result does not
  cross is S0060 or S0061 with the reason.
- A pointer to a number, vector or enum as a parameter does not cross; `T &` not const is a `var` parameter, written
  `var x` (S0062, S0063).
- A game function that takes a record by pointer or reference is given the game's object (a view), never the script's
  own copy of a plain game struct (`let b = a.mBox` copies it): S0098. `MR::useBox(a.mBox)` passes the game's object.

### D7.6 `as` (D4.11, as built)

`as` converts a number to a number, a game enum to an integer and an integer to a game enum, and an enum of the mod
without data to an integer. Nothing else (S0069).

### D7.7 The game's enums (7.1, D5.2)

- A value is written `Mode::Walk` in an expression (an unscoped enum's also by its scope and value alone, `Walk` at
  global scope); a value of an unnamed enum is an `int`.
- In a `match` arm it is written dotted, `Mode.Walk` (patterns have no `::`; the checker joins the path with `::`),
  and S0068 names missing values that way. Two names of one value are one case, so an arm for each value is a
  complete `match`.
- `as` converts a game enum to an integer and back (D7.6).

### D7.8 Places (5.1, as built)

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

### Flow, what the checker's part (d) reads (4.2)

Sections 4, 6.1, 11 and 12 and D4.14 stand. Where they say less, these sentences say exactly what is checked.

### D9.1 Returns and `guard` (section 4)

- A function, a struct's or class's function, an export, a closure or a local `fn` with a result, and an `on layers`
  handler, must not reach its closing `}`. Every path ends in `return value`, or in a statement that never ends:
  `while true` that no `break` of its own leaves (a `break` of a loop inside it does not count). An `if` or `match`
  leaves only when all of its branches do, and an `if` without `else` never does. The error is said at the closing `}`
  and names the result type: S0101.
- `guard`'s `else` block must leave by `return`, `stop`, `break`, `continue`, `go` or `skip`, or by an `if` or `match`
  whose every branch leaves: S0102, said at the `else` block's `{`.
- Hooks are part (e)'s: a `wrap` that proceeds, and a `before` that skips, are other rules.

### D9.2 Views (6.1, exactly)

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

The way to keep an object is a handle: `ref(x)`, then `h.get()`, which gives `none` once the object is gone.

### D9.3 `wait` (11, D4.14, exactly)

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

### D9.4 `defer` (section 4)

What `defer` runs must end where it started. It may not contain `return`, `stop`, `go`, `skip`, `wait`, or a `break`
or `continue` of a loop outside it. A loop inside the deferred statement may `break`. Each of these is S0106, naming
the word.

### D9.6 Programs

- A.7, programs 025, 038 and 046: the actor whose movement runs is kept as a handle, under D9.2's module rule. The
  programs now read:
  - `var current: ref<LiveActor>? = none`
  - `current = if is_mine(self) { none } else { ref(self) }`
  - `let a = current?.get()`
  The wraps that save and restore it are unchanged.

D9.3's "Views across it" reads, in place of its first sentence and list:

- A local view made before a `wait` and read after it, on a path through the `wait`, is S0104, naming the local and
  the wait's line, said once per local. A path goes through one branch of an `if` or `match` (each arm runs after the
  conditions or the subject before it, not after another arm). A branch that leaves (`return`, `stop`, `go`, `skip`,
  `break`, `continue`) takes its waits with it: a `break`'s go to what follows the loop, a `continue`'s round again.
  A `guard`'s `else` runs after its conditions, so after a `wait` that is one of them. These count as read after the
  wait:
  - a read the next time round a loop, after a wait on a path back round;
  - `wait until`'s condition, which is read on each frame after the wait starts;
  - a read inside a built-in's closure argument, which runs where it is written (A42D-1);
  - the list a `for` goes on through, a local's or a temporary's, when its body waits on a path back round: said at
    the list, naming the local, or the `for`'s first name for a temporary (A42D-2);
  - the local whose view an assignment writes through (`a.x = wait p`), since the place is written after the wait.

### The `time` service in LumaScript

Sections 10, 11 and 12 read with these. The checker builds them with part (e) (hooks, tasks, states); the codes are
assigned now.

### D10.1 `use svc.time`: the table as it is

- The functions are the header's, by their names, with `mod` given by the compiler: `time.now() -> time.Now`,
  `time.monotonic_ns() -> i64`, `time.wall() -> time.Wall`, `time.set_rate(rate: u32)`, `time.hold()`,
  `time.release()`, `time.step(count: u32) -> bool`, `time.timer(clock: time.Clock, after: u64, every: u64, fn(count:
  u32)) -> time.Timer?`, `time.cancel(timer: time.Timer)`. No property or other sugar: the docs show one list, the
  header's.
- What the C results become: `set_rate`, `hold`, `release` and `cancel` return nothing (a mistake is reported by the
  service to the mod log, registry 7); `step` is false for LUMA_E_BUSY (another mod holds too, or loading: an answer,
  not a mistake) and true for LUMA_OK; `timer` is `none` when it was refused (reported). `now` and `wall` cannot fail.
- A service's struct `Luma<Service><Name>` is `<service>.<Name>` (`time.Now`, `time.Wall`), its enums likewise
  (`time.Clock.Frames`, `.Steps`, `.Nanoseconds`; the state's `time.State`); a handle is `<service>.<Name>` of the
  kind it opens (`time.Timer`). The leading `size` field and `reserved` fields are not the script's: the compiler sets
  `size` to the struct of the minor the mod imports and zeroes `reserved`. This rule is for every service.
- `time.on_frame` and `time.on_step` are not names of the script: `on frame` and `on step` are (D10.2). Writing them
  is S0108 with `Script.Svc.Handler` (`{name} is set by "on {what}", not called`).

### D10.2 `on frame` and `on step`

- `on frame { }` stays lumart's frame pass (registry 6.1): at the frame's start in list order, before the mod's tasks
  (section 11). It runs while the world is held. One per module: the mod's one frame pass calls each module's in
  module list order. `on start` and `on stop` are one per module the same way (starts in module order, stops in
  reverse), as is lumakit rule 4's `on mods.stopped`.
- `on step { }` and `on step(now: time.Now) { }` are new: one per mod, compiled onto `time.on_step`, run once per
  world step before the scene's update (services-time item 9), so 0 times in a held frame and up to 16 in a fast one.
  `now` is the handler's own: its `step` and `step_in_frame` are this step's. A second `on step` in a mod is S0109
  (`Script.On.Twice`: `a second "on {what}"; the first is at line {line}`), as is a second `on load` or `on layers` in
  a mod, and a second `on frame`, `on start`, `on stop` or `on mods.stopped` in one module.
- `on mods.stopped(gone)` with its parameter untyped or `u64` is always lumakit rule 4 (a mod that called this
  library's exports is gone); svc.mods' STOPPED event is `on mods.stopped(e: mods.StoppedEvent)`, its record type
  written.
- `on step` may not wait (D9.3's list gains it: S0105 `Script.Wait.Where`), and it is never a task. It may start one.

### D10.3 `wait N frames` and `wait N seconds`

- `wait N frames` counts frames presented: it goes on while the world is held. `wait 0 frames` resumes at the next
  frame's pass.
- `wait N seconds` counts world steps at 60 a second: ceil(N × 60) steps, N an `int` or `f32` of at least 0. It
  follows the world's rate and stops while the world is held, as section 11's "game time; paused when the game
  pauses" said. A real-time wait is `wait until time.monotonic_ns() >= end`.
- How lumart does it: in its frame pass it reads `time.now()` once; a frames wait resumes when `frame` reaches its
  end, a seconds wait when `step` does. No timer is opened for a wait, so the service's 64 timers a mod are the
  script's own (`time.timer`). A mod whose code waits imports `time` 1.0 (the compiler adds the import; it needs no
  permission).
- After an `after`/`every` of other languages, LumaScript's way is a task: `task t() { wait 2 seconds; ... }`, a
  loop for `every`.

### D10.4 `on time.timer` is refused

Registry 2's event naming makes `on time.timer(...)` a path, but a timer's events go to the function given to
`time.timer`, not to a mod-wide handler. `on <service>.<event>` for an event that has a per-request function is S0107
with `Script.On.PerRequest` (`{event}'s events go to the function given to {call}; it is not an "on" event`). `time`'s
`timer` is the first.

### D10.E Codes

S0107 `Script.On.PerRequest` (D10.4), S0108 `Script.Svc.Handler` (D10.1), S0109 `Script.On.Twice` (D10.2); S0105
as before with `on step` in its list. Rows in the 14 languages come with the checker's commit.

### Hooks, tasks and states as the checker reads them (part e); `is_state`; `keep`/`kept`

Sections 8 and 11 said what hooks, tasks and states do; this draft says how a head and a body are read, exactly, as
LumaLink g/4.2-e checks them. Nothing in sections 8 and 11 changes but what D11.3 and D11.5 add.

### D11.1 A hook's head (section 8)

- The row. `Name::function` names a game function; its row is chosen by, in order: a quoted signature
  (``MR::isNear(const LiveActor *, const Vec3f &, f32)``); the number of parameters written; their written types,
  matched as a call's arguments are. A name no row has is S0047. A count no row of that name takes is S0112.
- The parameters take the row's types, as a hook receives them: numbers, values, enums and records by value as
  for a call; a pointer to a number, value or enum is `T?`, the place it points at (`none` for null); `const T &`
  is read, never written; other pointers as views (section 6). A parameter written with a type must be that type.
- `self` is the object for a method, a constructor and a destructor; a free or static function has none.
- Which hooks: `before`, `after`, `wrap`, `replace` on a function; `before` and `after` only on a constructor,
  a destructor, or a function the game only tells of (S0113). `every override of` only on a virtual (S0114).
  A hook in a class only on a member function of the game class it derives from, directly or through a base (S0115).
- The result: `-> name` names it for the body; `-> T` must be its type. A function returning nothing has none
  (S0116). A pointer or reference result is a place and may be written.
- Texts. A `const char *` parameter is `text?`, read from the game's bytes at the hook's
  start; a text written to it reaches the call as its kept copy (abi addition 6), in a `wrap` before `proceed()` and
  in a `before` on its way out, only when it changed (a script error writes nothing). A `const char16_t *` result
  takes `text`/`text?`: its UTF-16 kept copy, made once per distinct text; a NUL inside is S0918.
- A parameter that does not cross may be bound as `_` (never read; the kernel passes it through untouched), or the
  list left out. Bound by a name it is S0060, whose message says to bind it as `_`.

### D11.2 A hook's body (section 8)

- `skip` only in a `before` hook's own body (S0117): `skip` alone when the function returns nothing (S0118), `skip v`
  with the result's value when it returns one (S0119).
- `result` in an `after`, a `wrap` or a `replace`, in the hook's own body only, never in a closure in it (S0120).
  In a `wrap` or `replace`, reading `result` before `proceed()` or `result = v` on some path is S0124.
- `proceed()` only in a `wrap`'s own body (S0121); at most once on any path, a loop's next round included (S0122).
- Every path of a `wrap` or `replace` of a function with a result gives the result, by `proceed()` or `result = v`,
  at the body's end and at each `return` (S0123).
- `self` in a constructor's `before` (the object is not built) or a destructor's `after` (it is gone) is S0128.

### D11.3 `stop`, `go` and `is_state` (section 11)

- `stop` in a task, a state machine, or a handler that may wait (D4.14) (S0125).
- `go S` in a state machine, for one of its own states; `go M.S` in a machine or in a function or hook of the
  machine's class (S0126 elsewhere). `go S` from the class is S0130, which lists the class's machines. A state gives
  `enter`, `update` and `exit` at most once each (S0129).
- New: `is_state(S)` and `is_state(M.S)`, a standard function, `bool`: true while the object's machine M is in state
  S. It is placed and read exactly as `go` (S0126, S0127, S0130 name it); its argument is a state's name, never a
  value (S0131 otherwise). It is what A.6's `isState` meant: lumascript-programs.md A.6 reads `not
  is_state(OrbKingStates.Dizzy)` (the corpus file 006 is changed).

- The rows are by code, `Script.S0109` and so on, in the 14 languages. The names D10 gave them (`Script.On.Twice`,
  `Script.Wait.Negative`) were descriptions only.
- S0109's place is `first`, the first handler's file and line (`src/main.lum:12`), since the two may be in two of the
  mod's modules: `a second "on {what}" in the mod; the first is at {first}`.
- S0110: `wait -N frames` or `wait -N seconds` with a literal N above 0. S0902 (a computed negative wait, at run time)
  is lumart's, with 4.5. S0107, S0108 and S0111 are services', with checker part (f).

### D11.5 `keep` and `kept`

- `keep(value) -> bool` in `on stop`'s own body: the value, json data (D7.4), encoded by the compiler, handed to
  LumaCore's `keep` (reload section 4); true when kept, false when over 1 MiB encoded (lumart also writes that to the
  mod log). The last call wins.
- `kept<T>() -> T?` in `on start`'s own body, before any `wait` on its path (the kept bytes last until `start`
  returns): what the previous load of the mod's code kept this session, decoded as T; `none` when nothing was kept
  or the bytes are not a T (a changed struct after a reload), the latter also in the mod log.
- Elsewhere S0132 (`Script.Keep.Stop`, `Script.Kept.Start`); T or the value not json data is S0094.
- Both are standard names (16a); a mod's own `keep` or `kept` hides them, as for every standard name.

### D11.E Codes

S0112 to S0132 as above, with rows and the phrases `Script.Result.Before`/`NotHook`, `Script.Proceed.NotWrap`/
`Replace`, `Script.Self.NotYet`/`Gone`, `Script.Keep.Stop`, `Script.Kept.Start` in the 14 languages.

### Services as the checker reads them from their headers (part f; registry 3's one form)

D10.1 wrote `time` out by hand. The registry's rule 4 says LumaLink reads every service from its header, so this
draft gives the one reading every header gets. D10.1 becomes its first case and changes in two places (D12.7).
Requests to H for the header form are R-9a to R-9d. Until a header has them, the part they cover is a note
S0047 "not known in this game's self-description", never a guess.

### D12.1 Where a face comes from

- `use svc.<name>` needs a row of the self-description's `services` (registry 9) and the header `luma_<name>.h`
  of the game's SDK that LumaLink carries for that game version (its `native/include`, embedded when LumaLink is
  built). A dotted service (`system.clipboard`) is in its first part's header.
- A header out of the one form fails LumaLink's own mirror test when LumaLink is built. A modder never sees that
  as their error.

### D12.2 Names

- The table `Luma<Name>Service` gives functions `<name>.<member>`. `base` is not a function, and `mod` is
  passed by the compiler.
- The prefix is the service's CamelCase name (`LumaSystemClipboard` for `system.clipboard`). A struct
  `<Prefix><T>` is `<name>.<T>`. A named enum `<Prefix><T>` is `<name>.<T>`, and its cases drop the
  enum's common `LUMA_..._` prefix, each `_` word in CamelCase: `LUMA_TIME_FRAMES` -> `time.Clock.Frames`,
  `LUMA_SYSTEM_D3D12` -> `system.Renderer.D3d12`.
- The event enum `<Prefix>Event` gives events `<name>.<kind>`, the kind in lower case (D9.5).
- A `#define LUMA_<NAME>_<C>` of a whole number is a constant `<name>.<C>`, upper case as written
  (`time.TIMERS_MOST`). Anonymous enums give nothing.

### D12.3 Parameters, after `mod`

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
| `LumaEventFn fn, void *user` | one closure argument (D12.5) |
| a non-const pointer to a number, enum or struct | an out: not an argument, part of the result (D12.4) |
| `char *` or `void *` or `uint8_t *`, then `uint64_t` | an out buffer: the result is `text` or `bytes` (D12.4) |

Any other parameter makes that function not callable from LumaScript (S0060, why ServiceParam). It stays a C
function of the header for C mods.

### D12.4 Results

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

### D12.5 Requests and their events

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
  `Io`, `Timeout`, `Closed`, `Memory`, `Busy`, `Format`, `TooLarge`, `Unreachable`). It is one
  language-wide type for every service.
- A table's `cancel(LumaHandle)` is `<name>.cancel(h)`, returning nothing.

### D12.6 Handlers and codes

- `on <name>.<kind>(e) { }` is for an event kind that is the table's own mod-wide events: a kind no request names
  in its comment. For a kind whose events go to the function a request was given, it is S0107
  `{event}'s events go to the function given to {call}; it is not an "on" event` (D10.4).
- A member whose comment says it is set by `on <x>` (R-9d: the phrase `set by on <x>`; time's on_frame and
  on_step) is not a name of the script. Calling or naming it is S0108 `{name} is set by "on {what}", not called`.
- S0111: a literal outside a parameter's range that its comment states as `<a> to <b>` (R-9d: the form
  `<param>: <a> to <b>`; time.step's count 1 to 16, time.set_rate's rate 1 to 16000). `{name}'s {param} is
  {found}; it takes {low} to {high}`. D10's S0111 for time.step is its first case.
- A service used without its permission (registry 9's row) is the validator's L506, said by LumaLink at check time
  with the `use`'s line.

### D12.7 D10.1 under this draft

- `time.now() -> time.Now` and `time.wall() -> time.Wall`, `time.monotonic_ns() -> i64`, `hold()`,
  `release()`, `set_rate(rate: u32)` and `step(count: u32) -> bool` are as D10.1.
- `time.timer(clock: time.Clock, after: u64, every: u64, fn(e: time.TimerEvent)) -> time.Timer?`: the closure takes
  the event record, whose `value` is the count. D10.1's `fn(count: u32)` is withdrawn. Every request reads the same
  way, so a script reads `e.value`.
- `time.Clock` and `time.State` need R-9a (the header's enums are anonymous today). `time.Timer` needs R-9b.

### D12.E Codes

S0107, S0108 and S0111 as above, with rows in the 14 languages with checker part (f).

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

### D12.9 Streams

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

### D12.10 Struct fields, constants and text

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
