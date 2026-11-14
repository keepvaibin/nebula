# The validator's rules

One row a rule, from the validator's own table (`crates/luma-validate`): the code, whether it refuses the mod (Error) or only tells (Note), and the words the modder sees. `{name}` is filled with the finding's own value.

The validator has 73 codes in seven groups: L1xx the `.luma` container, L2xx `luma.json`, L3xx layers and files,
L4xx table patches, L5xx code, L7xx data for a library, L8xx the cook, the loader and the other mods. `lumalink check`
and the game's start check say the same codes with the same words. A `{why}`, `{what}` or `{kind}` is itself a phrase
in the player's language, listed in format/validator.md. `lumalink explain L505` prints a code's words and what to do.


## L100: The container

| Code | Level | Words |
|---|---|---|
| L100 | Error | {file} is not a .luma: {found}. |
| L101 | Error | {file}: {found}. |
| L102 | Error | {file}: the entry "{entry}" has a name that is not allowed: {why}. |
| L103 | Error | {file}: "{entry}" clashes with "{other}" (the same name on some systems, or a file where a folder is). |
| L104 | Error | {file}: "{entry}" is {what}; a .luma holds only files and folders. |
| L105 | Error | {file}: {what} is {found}; the most allowed is {expected}. |
| L106 | Error | {file}: "{entry}": {why}. |
| L107 | Error | {mod}: "{entry}" is not part of a .luma (the top level holds luma.json, layers/, code/, media/, saves/, kit/ and source/). |
| L108 | Note | {file} was made with a tool that writes backslashes between folders; they are read as /. lumalink pack writes the standard form |

## L200: luma.json

| Code | Level | Words |
|---|---|---|
| L200 | Error | {file} has no luma.json. |
| L201 | Error | {mod}: luma.json line {line}: {found}. |
| L202 | Error | {mod}: luma.json line {line}: "{key}" is not a key that can be here. |
| L203 | Error | {mod}: luma.json line {line}: "{key}" is missing. |
| L204 | Error | {mod}: luma.json line {line}: "{key}" is {found}; it must be {expected}. |
| L205 | Error | {mod} was made for a newer Nebula (format {found}); this one reads format {expected} and older. |
| L206 | Error | {mod}: the id "{found}" may only use lowercase letters, digits, - and ., start with a letter, and be 3 to 64 long; its part before the first . may not be a name Windows keeps (con, prn, aux, nul, com1 to com9, lpt1 to lpt9). |
| L207 | Error | {mod} is for {found}, not Super Mario Galaxy. |
| L208 | Note | {mod} is made for {expected}; your game is {found}, so it is off. |
| L209 | Error | {mod}: {key} names "{entry}": {why}. |
| L210 | Error | {mod}: option "{key}": {why}. |
| L211 | Error | {mod}: permission "{key}": {why}. |
| L212 | Error | {mod}: the load order cannot be met: {found}. |
| L213 | Error | {mod}: takes_over "{found}": {why}. |
| L214 | Error | {mod}: luma.json line {line}: "{found}" is not a language of the game. |
| L215 | Error | {mod}: "{entry}" is in media/ but luma.json does not name it. |
| L216 | Error | {mod}: save "{entry}": {why}. |
| L217 | Error | {mod}: "{entry}" is in saves/ but luma.json does not name it. |
| L218 | Note | {mod} asks for the "{permission}" permission, but no code of it imports a service that needs it. |
| L219 | Note | {mod}: permission "{key}" is a test permission: only a test run provides it. |
| L220 | Error | {mod}: luma.json line {line}: "{key}" is the previous engine's; write {expected}. |
| L221 | Note | {mod}: luma.json line {line}: "{key}" is the previous engine's, read for now; a later game will not read it. Write it in the mod format's own keys. |

## L300: Layers and files

| Code | Level | Words |
|---|---|---|
| L300 | Error | {mod}: layer "{layer}": {why}. |
| L301 | Error | {mod}: layer "{layer}": {why}. |
| L302 | Error | {mod}: {file}: the game has no "{found}", and this kind of file cannot be new. |
| L303 | Error | {mod}: {file} replaces "{found}", which has rows: change it with a patch by key instead. |
| L304 | Error | {mod}: {file}: a {kind} cannot stand in for "{found}" ({expected}). |
| L305 | Note | {mod} and {other} both replace "{found}"; {winner}'s is used (it is higher in the list). |
| L306 | Error | {mod}: "{entry}" is not in files/, patches/, merge/ or assets/, nor a .png or .dds in textures/. |
| L307 | Error | {mod}: layer "{layer}" is chosen by the mod's code, which does not run on {platform}. |
| L308 | Error | {mod}: {file}: hides "{found}", which is no model of the game. |
| L309 | Error | {mod}: "{entry}" is in merge/, which adds only to tables (.json), messages (.json), particles (.jpc, .jpc.json) and ray tracing rows (NebulaData/Rt*.tsv). |

L304 names a source in `files/` that the game never reads where it is. For example, a `.ttf` belongs in `assets/`,
read by a `.font.json`. A `.bin` or `.jpg` is fine beside the `.gltf` or `.glb` that names it in `buffers` or `images`.

## L400: Table patches

| Code | Level | Words |
|---|---|---|
| L400 | Error | {mod}: {file} line {line}: {found}. |
| L401 | Error | {mod}: {file}: rows of "{target}" are keyed by {expected}, not {found}. |
| L402 | Error | {mod}: {file} line {line}: {why}. |
| L403 | Error | {mod}: {file} line {line}: {why}. |
| L404 | Note | {mod} and {other} both change "{target}" row {row}: {what}; {winner}'s change is used (it is higher in the list). |
| L405 | Error | {mod}: {file} changes "{target}" of {other}; add {other} to needs first. |
| L406 | Error | {mod}: {file} line {line}: {why}. |
| L407 | Note | {mod}: {file} line {line}: the selector matched no row of "{target}". |

## L500: Code

| Code | Level | Words |
|---|---|---|
| L500 | Error | {mod}: {why}. |
| L501 | Error | {mod}: code for "{found}": {why}. |
| L502 | Error | {mod}: {file} is not a {expected} library: it is {found}. |
| L503 | Error | {mod}: {file} does not export luma_mod. |
| L504 | Error | {mod}: {file} needs {library} ({found}); a mod's own library may need nothing from the system. |
| L505 | Error | {mod}: {file}: the game has no {kind}. |
| L506 | Error | {mod} uses {service} without asking for the "{permission}" permission. |
| L507 | Note | {mod} has no code for {platform}, so it is off there. |
| L508 | Error | {mod}: {file} needs LumaCore {found}; this game has {expected}. |
| L509 | Error | {mod}: {file}: {why}. |
| L510 | Note | {mod}: {file} needs {library} ({found}). |
| L511 | Error | {mod}: {file} cannot run on iOS: {why}. |
| L512 | Error | {mod}: {file}: luma_mod cannot be used: {why}. |
| L513 | Error | {mod}: {file}: {mod} is built with LumaScript runtime layout {version} and {other}, whose {name} it calls, with layout {value}. Rebuild the older one with a current LumaLink, or update it. |

L505 names what the game lacks: a function, a virtual function, a field, a global variable, a type, a class, a
service at a version, or another mod's function. When the name is there with other signatures, the words list them,
so the fix is read off the message:

```text
moon-jump: code/windows-x64/mod.dll: the game has no function "MR::isNear" with the signature
"(const LiveActor *, s32)" (its signatures: (const LiveActor *, const LiveActor *, f32), (const LiveActor *, const Vec3f &, f32)).
```

An import that names no signature binds a name with one function; a name with several is `single function "{found}":
the import names no signature and it has several ({has})`.

L509 also refuses a mod's own library that the system would run code from when it loads it, before the mod is
started, or when it closes it, after the mod's memory is freed: a PE entry point or TLS callbacks; ELF `DT_INIT`,
`DT_INIT_ARRAY`, `DT_PREINIT_ARRAY`, `DT_FINI`, `DT_FINI_ARRAY`; Mach-O init or term function sections or
`LC_ROUTINES_64`. `lumalink build` makes none of them: C++ constructors and destructors of a mod run from `begin` and
after `stop` (lumalink/loader.md).

L513: a mod that calls another mod's function needs both built with one LumaScript runtime layout. The layout is
written into each mod's `luma_mod` by LumaLink; rebuild the older mod with a current LumaLink.

L511 checks every library of an iOS mod, its own one included. Each must be built for iOS devices (not macOS or the
Simulator) and must not be encrypted.

## L700: Data for a library (kit/)

| Code | Level | Words |
|---|---|---|
| L700 | Error | {mod}: {why}. |
| L701 | Error | {mod}: {file} line {line}: {module}: {text}. |
| L702 | Error | {mod} needs {module} data version {found}; the installed {library} gives: {expected}. Update {library}, or install a version of {mod} made for it. |

A library gives a JSON schema for each module and data version at `code/<id>.kit/<module>/<version>.json`. A mod that
uses it needs the library, names `"<library id>.<module>": <version>` in `kit`, and puts its data in
`kit/<module>/<name>.json`. Each data file is checked against that schema, and each fault is named at its line.

## L800: Cook, loader and start

| Code | Level | Words |
|---|---|---|
| L800 | Error | {mod} needs {other} {expected}: {found}. |
| L801 | Error | {mod} cannot be on with {other}: {why}. |
| L802 | Error | {mod}: another installed mod, {other}, has the id "{found}". |
| L803 | Error | {mod} needs Nebula {expected}; this is {found}. |
| L804 | Error | {mod}: {file}: {text}. |
| L805 | Error | {mod}: {file} replaces "{found}" ({sig}), which {other} replaces already; only one mod may replace a function. |
| L806 | Error | {mod} needs {feature}, which this device's graphics do not have. |

L804 gives the cook's own reason for any file of `files/` or `assets/` it cannot make. `lumalink check` and the game
run the same cook on every file. As a Note, it also says what the cook leaves out of a file it does make.
