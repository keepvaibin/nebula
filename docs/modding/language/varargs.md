# Functions with a variable argument list

## More rules

### Every function a row

Header: `native/include/luma.h` (signatures as before; `va_list` and `...` now appear in them). Code:
`tools/lumacore/instrument.py` (rows), `Game/System/LumaHook.cpp` (a row's kinds), `tools/lumacore/luma_test.py` (call
refusals). Branch f/1.9-varargs. Reviewer: H, after it compiles and has run (R2). G and K: what LumaScript takes.

### 1. What is left out today (l22, 85 functions) and what each becomes

| reason | count | becomes |
|---|---|---|
| a template or an unnamed class's, a template specialization | 29 | left out (no one function to name) |
| an allocation function (operator new/delete) | 9 | left out (a hook would allocate inside the allocator) |
| a parameter of type `va_list` ("a type with no name") | 10 | a full row (2) |
| a variable number of arguments (`...`) | 13 | a row with before hooks; its va_list sibling the full one (3) |
| constexpr | 7 | a call row, no hooks (4) |
| "written by a macro" (a macro before the head: NEBULA_GAME_API) | 17 | a full row (5) |

After 1.9 the left-out list holds only the first two lines; anything else stops the build, naming the function.

### 2. `va_list` parameters

The type's text is `va_list` on every platform (the functions file, the self-description),
whatever the compiler's type is (`char*`, `__va_list_tag[1]`, a struct). A C mod binds and calls the function with its
own va_list. Hooks of every kind: `args[i]` points at the va_list as the function receives it (luma.h `luma_va_arg`:
on x86-64 outside Windows the array's element pointer, elsewhere the va_list); a hook reads a copy made with
`LUMA_VA_COPY(mine, args[i])`, never the original, so the function and later hooks see it whole; a wrap that proceeds
passes it on unread. The
validator's C-1 facts mark the parameter `va_list`. LumaScript cannot make one: such a function is not callable from a
script (naming it); the harness's `call` refuses it before the run.

### 3. Variadic functions `R f(P..., ...)`

A row's call goes through its invoke, which cannot pass `...`; so a mod calls and hooks a variadic function's work
through the va_list function it hands its arguments to, which has a full row (2):
- The rule, checked by instrument.py: a variadic function's body calls a function with a va_list parameter (its
  sibling) or another variadic function (MR::createCsvParser(const char *, const char *, ...) calls its other
  overload); one that calls neither stops the build, naming it. All 13 do already: formatText and addNumberFontTag call
  formatTextV, FormattedName::format and its constructor and setCurrentSceneControllerState call
  FormattedName::vformat, setTextBoxFormatRecursive calls Text16::format, isExistFileInArc, createCsvParser and
  tryCreateCsvParser call their Local helpers, ReplaceTagFunction::ReplaceArgs calls ReplaceTagProcessor::ReplaceArgs,
  luma::classes::say calls nebula_vsnprintf. No source changes.
- The row of the variadic function has the signature written with `...`
  (`bool(const ResourceHolder *, const char *, ...)`) and takes before hooks only, as a tell: `args[1..n]` the
  fixed parameters, `args[n+1]` a va_list of the rest, made for the hooks and ended before the body runs (each hook
  reads a va_copy). After, wrap, replace and a call through the row are refused (`Hook.Variadic`, `Call.Variadic`),
  naming the sibling, where a mod calls and hooks the behaviour.
- LumaScript and the harness call the sibling only where they can make a va_list (neither can now).

### 4. constexpr functions

A call row: the address and signature, so a mod calls it; no hook (the compiler evaluates some calls while compiling,
so a hook would see only some). `hook` on it is refused with `Hook.Constexpr`. noreturn and naked functions (none in
the game) stay a stop.

### 5. A macro before the head

The body is found from the head's spelling location, not its expansion; the row is as any other.

### 6. Proof

- Build: the left-out report lists only templates and allocation (counted in RUNS); the functions file has the 47.
- Tests 1901-1906 (suite f/tests/varargs, with a C test mod): a mod calls MR::formatTextV with its own va_list
  (`%d`) and reads the text; a before hook on MR::isExistFileInArc reads its va_list (the name formatted) and the function still answers; a wrap
  on FormattedName::vformat changes every formatted name, the variadic callers included; `after` on a variadic
  refused with Hook.Variadic; a call of a constexpr function (CubeGravity's encodeRegion) answers as the source does;
  `hook` on it refused with Hook.Constexpr.
- No mod: the copies' variadic functions do the same work as before (one va_start more only when a hook is on).

### 7. Known limits

- A va_list read by a hook without va_copy breaks the function's own read; the docs say so; it cannot be checked.
- A variadic function's after hook and call are not offered; its va_list sibling's are.
