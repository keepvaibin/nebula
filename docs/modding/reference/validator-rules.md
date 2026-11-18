---
title: Validator rules
shortTitle: Validator rules
intro: Every code the validator reports, with its level and its words, and every reason a finding can give.
contentType: reference
---

## About validator rules

The validator checks a mod in LumaLink (`lumalink check`, `build` and `pack`) and again in the game, when it
imports the mod and at each start. Both report the same codes with the same words, in the player's language.

* An **Error** turns the mod off, or stops LumaLink.
* A **Note** only tells you something. The mod stays on.

In the words, `{mod}`, `{file}`, `{entry}` and the other names in braces are filled with the finding's own
values. A `{why}`, `{what}` or `{kind}` is itself one of the [Reasons](#reasons) below.

To print a code's words and what to do, run `lumalink explain CODE`, for example `lumalink explain L505`.

## The container (L100)

The `.luma` file as a ZIP archive.

| Code | Level | Words |
|---|---|---|
| L100 | Error | {file} is not a .luma: {found}. |
| L101 | Error | {file}: {found}. |
| L102 | Error | {file}: the entry "{entry}" has a name that is not allowed: {why}. |
| L103 | Error | {file}: "{entry}" clashes with "{other}" (the same name on some systems, or a file where a folder is). |
| L104 | Error | {file}: "{entry}" is {what}; a .luma holds only files and folders. |
| L105 | Error | {file}: {what} is {found}; the most allowed is {expected}. |
| L106 | Error | {file}: "{entry}": {why}. |
| L107 | Error | {mod}: "{entry}" is not part of a .luma (the top level holds luma.json, layers/, code/, media/, saves/, kit/, source/ and tests/). |
| L108 | Note | {file} was made with a tool that writes backslashes between folders; they are read as /. lumalink pack writes the standard form |

## luma.json (L200)

The manifest. For every key, see [luma.json](luma-json.md).

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

## Layers and files (L300)

The content in `layers/`.

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

## Table patches (L400)

Patches of the game's tables. For the keys, see [Table keys](table-keys.md).

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

## Code (L500)

The mod's built libraries in `code/`.

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

## Data for a library (L700)

A mod's data for a library mod, in `kit/`.

| Code | Level | Words |
|---|---|---|
| L700 | Error | {mod}: {why}. |
| L701 | Error | {mod}: {file} line {line}: {module}: {text}. |
| L702 | Error | {mod} needs {module} data version {found}; the installed {library} gives: {expected}. Update {library}, or install a version of {mod} made for it. |

## Preparing, loading and starting (L800)

What the game finds when it prepares, loads and starts the mods.

| Code | Level | Words |
|---|---|---|
| L800 | Error | {mod} needs {other} {expected}: {found}. |
| L801 | Error | {mod} cannot be on with {other}: {why}. |
| L802 | Error | {mod}: another installed mod, {other}, has the id "{found}". |
| L803 | Error | {mod} needs Nebula {expected}; this is {found}. |
| L804 | Error | {mod}: {file}: {text}. |
| L805 | Error | {mod}: {file} replaces "{found}" ({sig}), which {other} replaces already; only one mod may replace a function. |
| L806 | Error | {mod} needs {feature}, which this device's graphics do not have. |

## Reasons

A finding's `{why}`, `{what}` or `{kind}` is one of these phrases, listed by its key.

### The ZIP archive

| Key | Words |
|---|---|
| `Zip.BadDirectory` | its zip directory is damaged |
| `Zip.BadZip64` | its Zip64 directory is damaged |
| `Zip.Encrypted` | "{entry}" is encrypted, which a .luma may not be |
| `Zip.Method` | "{entry}" is packed with method {method}; a .luma may only be stored or deflated |
| `Zip.MultiDisk` | it spans several disks; a .luma is one file |
| `Zip.NoEnd` | it has no zip directory at its end |
| `Zip.TooShort` | it is too short to be a zip file |
| `Zip.Unreadable` | it cannot be read |

### Unpacking

| Key | Words |
|---|---|
| `Unpack.Broken` | it cannot be unpacked; its data is damaged |
| `Unpack.CrcDiffers` | its unpacked data does not match its checksum |
| `Unpack.Larger` | it unpacks to more than the {expected} bytes it states |
| `Unpack.Overlap` | its data shares bytes with "{other}" |
| `Unpack.SizeDiffers` | it unpacks to {found} bytes, not the {expected} it states |

### Limits

| Key | Words |
|---|---|
| `Limit.Entries` | the number of entries |
| `Limit.FileSize` | the size of "{entry}" in bytes |
| `Limit.TotalSize` | the unpacked size in bytes |

### Entry names

| Key | Words |
|---|---|
| `Name.Absolute` | it starts at the root of a drive or of the system |
| `Name.Backslash` | it uses a backslash; folders are separated by / |
| `Name.Class` | class "{found}" |
| `Name.Combining` | it holds a combining mark or a Hangul jamo, which systems store in different forms |
| `Name.Control` | it holds a control character |
| `Name.DotPart` | it has a part that is . or .. |
| `Name.EmptyPart` | it has an empty part (two / in a row, or / at the end) |
| `Name.Field` | field "{found}" |
| `Name.FieldSig` | field "{found}" of type "{sig}" |
| `Name.FieldUnbound` | field "{found}" a mod can bind ({why}) |
| `Name.Forbidden` | it holds a character Windows does not allow in names: < > : \| ? * or a quotation mark |
| `Name.Function` | function "{found}" |
| `Name.FunctionMany` | single function "{found}": the import names no signature and it has several ({has}) |
| `Name.FunctionSig` | function "{found}" with the signature "{sig}" |
| `Name.FunctionSigHas` | function "{found}" with the signature "{sig}" (its signatures: {has}) |
| `Name.Global` | global variable "{found}" |
| `Name.GlobalSig` | global variable "{found}" of type "{sig}" |
| `Name.GlobalUnbound` | global variable "{found}" a mod can bind ({why}) |
| `Name.LeadingAt` | a layer, or a path in files/ or assets/, may not start with @, which names another mod's files |
| `Name.LocalDiffers` | its name in the zip's directory differs from the name stored with its data |
| `Name.Method` | virtual function "{found}" with the signature "{sig}" |
| `Name.MethodHas` | virtual function "{found}" with the signature "{sig}" (its signatures: {has}) |
| `Name.ModFunction` | function "{found}" of a mod that is on |
| `Name.NotUtf8` | it is not written in UTF-8 |
| `Name.Reserved` | a part is a device name Windows reserves (CON, PRN, AUX, NUL, COM1 to COM9, LPT1 to LPT9 and the like) |
| `Name.Service` | service "{found}" at version {sig} or a later one with the same major number |
| `Name.TooLong` | it is longer than 240 bytes |
| `Name.TrailingDotOrSpace` | a part ends in a dot or a space, which Windows removes |
| `Name.Type` | type "{found}" |
| `Name.TypeUnbound` | type "{found}" a mod can bind ({why}) |

### Clashing names

| Key | Words |
|---|---|
| `Clash.Column` | the column "{column}" |
| `Clash.Row` | one adds the row and the other removes it |

### Entry kinds

| Key | Words |
|---|---|
| `Kind.Device` | a device |
| `Kind.Link` | a link |

### JSON

| Key | Words |
|---|---|
| `Json.AfterEnd` | "{detail}" comes after the end of the JSON |
| `Json.BadEscape` | a backslash is followed by something JSON does not allow |
| `Json.BadNumber` | "{detail}" is not a number JSON allows |
| `Json.Bom` | it starts with a byte order mark; save it as UTF-8 without one |
| `Json.Comment` | JSON has no comments |
| `Json.ControlInString` | a control character ({detail}) is inside a string; write it as an escape |
| `Json.DuplicateKey` | the key "{detail}" is given twice |
| `Json.Expected` | "{detail}" was expected here |
| `Json.KeyNotString` | a key must be text in quotation marks, not "{detail}" |
| `Json.LeadingZero` | a number starts with 0 |
| `Json.NotUtf8` | it is not UTF-8 text |
| `Json.TooDeep` | lists and objects are nested more than {detail} deep |
| `Json.TooLarge` | it is larger than 1 MiB |
| `Json.TrailingComma` | a comma comes before a closing bracket |
| `Json.Truncated` | the file ends before the JSON does |
| `Json.Unexpected` | "{detail}" cannot be here |
| `Json.Unreadable` | it cannot be read |

### Expected values

| Key | Words |
|---|---|
| `Expect.Array` | a list |
| `Expect.Bool` | true or false |
| `Expect.Count` | a list of {min} to {max} items |
| `Expect.Feature` | a graphics feature the game's renderer lists |
| `Expect.FeatureName` | a feature name: up to 32 lowercase letters, digits and single _, starting with a letter and not ending in _ |
| `Expect.Format` | a format version such as 1.0 |
| `Expect.Host` | a host: a name such as example.com or *.example.com, an IPv4 address, or *, with no port |
| `Expect.Https` | a link that starts with https:// |
| `Expect.Id` | an id of 3 to 64 lowercase letters, digits, - and ., starting with a letter |
| `Expect.Key` | a key of 1 to 64 lowercase letters, digits, -, . and _, starting with a letter |
| `Expect.Language` | one of the game's languages |
| `Expect.LibraryName` | a library's file name, without folders, not starting with mod. |
| `Expect.Line` | one line of {min} to {max} characters |
| `Expect.ListedPlatform` | a platform listed in code.platforms |
| `Expect.MaxLength` | a whole number from 1 to 256 |
| `Expect.ModuleId` | a LumaKit module id such as author.module |
| `Expect.NotSelf` | the id of another mod |
| `Expect.Number` | a number |
| `Expect.Object` | an object |
| `Expect.Once` | a value that appears only once |
| `Expect.OptionKind` | one of choice, number, toggle, text or key |
| `Expect.Platform` | one of windows-x64, windows-arm64, linux-x64, linux-arm64, macos-arm64, macos-x64, android-arm64, android-x64 or ios-arm64 |
| `Expect.Range` | a version range such as >=1.2.0 or ^1.0 |
| `Expect.Region` | one of RMGE01, RMGP01 or RMGJ01 |
| `Expect.SchemaVersion` | a whole number from 1 to 1000000 |
| `Expect.Spdx` | an SPDX license name such as MIT or CC-BY-4.0 |
| `Expect.String` | text |
| `Expect.True` | true |
| `Expect.Version` | a version such as 1.0.0 |

### Found values

| Key | Words |
|---|---|
| `Found.Items` | a list of {count} items |
| `Found.ThisText` | text that does not fit |

### The game

| Key | Words |
|---|---|
| `Game.Galaxy2` | Super Mario Galaxy 2 |

### Media

| Key | Words |
|---|---|
| `Media.IconSize` | the icon must be square, 64 to 1024 pixels on a side |
| `Media.Missing` | the mod has no such file |
| `Media.NotInMedia` | the file must be directly in media/ |
| `Media.NotPicture` | it is not a PNG or JPEG picture |
| `Media.NotPng` | the icon must be a PNG picture |
| `Media.PictureSize` | a picture may be at most 4096 pixels on a side |
| `Media.Unreadable` | the file cannot be read |

### Options

| Key | Words |
|---|---|
| `Option.DefaultNotAValue` | its default "{value}" is not one of its values |
| `Option.DefaultOffStep` | its default is not min plus a whole number of steps |
| `Option.DefaultOutside` | its default is outside min to max |
| `Option.DefaultTooLong` | its default is longer than max_length or is more than one line |
| `Option.KeyDefaultUnknown` | its default "{value}" is not a control the game names (a, b, one, two, plus, minus, home, c, z, dpad_up, stick_up, shake, pointer_up and the like) |
| `Option.MinNotBelowMax` | min must be less than max |
| `Option.NameOfNoValue` | value_names names "{value}", which is not one of its values |
| `Option.StepNotPositive` | step must be more than 0 |
| `Option.TextOfNoOption` | text gives words for it, but luma.json has no such option |
| `Option.Twice` | two options have this key |
| `Option.ValueTwice` | the value "{value}" is given twice |
| `Option.ValueUnnamed` | the value "{value}" has no name in value_names |

### Permissions

| Key | Words |
|---|---|
| `Perm.HostCount` | net.internet takes 1 to 32 hosts |
| `Perm.HostTwice` | a host is listed twice (upper and lower case count as the same) |
| `Perm.HostsOnlyNet` | only net.internet takes hosts |
| `Perm.NetHosts` | net.internet needs hosts, the servers it may reach |
| `Perm.NetSplit` | net is two permissions now: write net.local or net.internet |
| `Perm.NoWhy` | it needs a why that players read |
| `Perm.RootCount` | roots lists 1 to 8 files |
| `Perm.RootMissing` | the mod has no file "{entry}" |
| `Perm.RootNotPem` | the root "{entry}" holds no PEM certificate (-----BEGIN CERTIFICATE-----) |
| `Perm.RootPath` | the root "{entry}" must be a file in the mod's certs/ folder |
| `Perm.RootSize` | the root "{entry}" is larger than 64 KiB |
| `Perm.RootTwice` | the root "{entry}" is listed twice |
| `Perm.RootsOnlyNet` | only net.local and net.internet take roots |
| `Perm.Twice` | it is asked for twice |
| `Perm.Unknown` | this game has no such permission |

### takes_over

| Key | Words |
|---|---|
| `TakesOver.On` | that mod is installed and on; turn it off first |
| `TakesOver.Self` | a mod cannot take over itself |

### Saves

| Key | Words |
|---|---|
| `Save.Checksum` | its checksum does not match (the file was changed or damaged) |
| `Save.Chunk` | a part of it is damaged |
| `Save.Encrypted` | it is the encrypted copy a Wii writes to an SD card; give the save file itself |
| `Save.Length` | the length it states differs from its size |
| `Save.Missing` | the mod has no such file |
| `Save.NotInItsFolder` | it must be in its region's folder, such as saves/RMGE01/ |
| `Save.RegionNotOfMod` | its region is not in game.regions |
| `Save.Short` | it is too short to be a save |
| `Save.Table` | its table of parts is damaged |
| `Save.UnknownRegion` | its region must be RMGE01, RMGP01 or RMGJ01 |
| `Save.Unreadable` | the file cannot be read |
| `Save.Version` | it is a save of a version this game does not read |

### Needs

| Key | Words |
|---|---|
| `Need.Cycle` | each needs the next, in a circle ({chain}), so none of them can start first |
| `Need.Missing` | it is not installed |
| `Need.Off` | it is off |
| `Need.Version` | version {version} is installed |

### Conflicts

| Key | Words |
|---|---|
| `Conflict.Theirs` | {other} lists this mod in its conflicts |

### Layers

| Key | Words |
|---|---|
| `Layer.EmptyList` | a list in its when is empty, so it can never apply |
| `Layer.EmptyWhen` | its when is empty; leave when out for a layer that always applies |
| `Layer.NoFolder` | it is listed in luma.json, but there is no layers/{layer}/ folder |
| `Layer.NoSuchOption` | its when names "{value}", which is not an option of this mod |
| `Layer.NotListed` | there is a folder for it, but luma.json does not list it |
| `Layer.OptionNotChoosing` | its when names "{value}", a text or key option: the mod reads that itself, and it cannot choose a layer |
| `Layer.RegionNotOfMod` | its when names the region {value}, which is not in game.regions |
| `Layer.Twice` | it is listed twice |
| `Layer.ValueNever` | its when asks the option "{value}" for a value it can never have |

### Patches

| Key | Words |
|---|---|
| `Patch.AddNoKey` | an added row must give every key column: {detail} |
| `Patch.ArgsShape` | "args" gives the object's arguments by name: an object of names and whole numbers |
| `Patch.BytesShape` | "{column}": "bytes" must be an even number of hex digits, with nothing else beside it |
| `Patch.KeyInGroup` | the key {row} is in {where} already: the game links by it across the zone's layers; give another |
| `Patch.KeyRepeated` | the game has {count} rows with the key {row}; change them with select |
| `Patch.KeyShape` | "key" must be a list of 1 to 8 column names |
| `Patch.KeyValue` | the key "{column}" must be a number or text; "#" is a place, 0 or more |
| `Patch.NoKey` | it has no "key", the list of the columns that name a row |
| `Patch.NoPanes` | it has no "panes", the panes of the layout it changes, by name |
| `Patch.NoRows` | "{detail}" has no rows to patch; replace it as a whole file in files/ |
| `Patch.NoSuchColumn` | "{target}" has no column "{column}" |
| `Patch.NoVersion` | it has no "patch": 1 |
| `Patch.NotArray` | "{detail}" must be a list |
| `Patch.NotJson` | a patch file's name must end in .json |
| `Patch.NotObject` | a patch is one JSON object |
| `Patch.PanesShape` | "panes" must be an object: each pane's name and what changes in it |
| `Patch.PlaceNotFound` | the table has {count} rows, numbered from 0; there is no row "#" {row} |
| `Patch.RemoveByPlace` | rows of this table are named by place ("#"), and a remove would move every later row; give the whole table with "rows" |
| `Patch.RowExists` | a row with the key {row} exists already; change it with set |
| `Patch.RowNotFound` | no row has the key {row} |
| `Patch.RowShape` | each added row is an object of column names and values |
| `Patch.RowsNotByPlace` | "rows" gives a whole table, which only a table named by place ("#") takes; this one is keyed: use set, add and remove |
| `Patch.SelectShape` | each entry of "select" is an object with only "match" and "values", neither empty |
| `Patch.SetShape` | each entry of "set" is an object with only "where" and "values", "values" not empty |
| `Patch.TooLarge` | it is larger than 64 MiB |
| `Patch.UnknownKey` | "{detail}" is not a key of a patch (patch, key, set, add, remove, select, rows) |
| `Patch.ValueType` | column "{column}" holds {expected}; {found} does not fit |
| `Patch.Version` | "patch" is {detail}; this game reads patches of version 1 |
| `Patch.WhereNotKey` | it must give exactly the key columns, in order: {detail} |

### Patch rows

| Key | Words |
|---|---|
| `Where.Font` | a font goes in assets/ with a .font.json that names it |
| `Where.ModelPart` | a model's buffer or image, read only when a .gltf or .glb beside it names it |
| `Where.Movie` | a movie goes in assets/ as AV1 in WebM or IVF, with a .movie.json that names it |
| `Where.Music` | a MIDI file goes in assets/ with a .sound.json that makes it a sequence |
| `Where.Table` | files/ cooks a .json only as .bcsv.json, .bcam.json, a placement table in .arc/jmp/, .layout.json, .anim.json or .model.json; other .json sources go in assets/ |
| `Where.Texture` | a model's image, read only when a .gltf or .glb beside it names it, or a game texture in textures/ |

### Selectors

| Key | Words |
|---|---|
| `Select.GlobNotText` | a glob on "{column}" must be text |
| `Select.MatchKind` | the match on "{column}" must be one of equals, prefix or glob, with one value |
| `Select.NoSuchColumn` | this file has no column "{column}" |

### Columns

| Key | Words |
|---|---|
| `Column.Float` | numbers |
| `Column.Int` | whole numbers from {min} to {max} |
| `Column.Text` | text |

### Value types

| Key | Words |
|---|---|
| `Type.Array` | a list |
| `Type.Bool` | true or false |
| `Type.Null` | null |
| `Type.Number` | a number |
| `Type.Object` | an object |
| `Type.String` | text |

### Code

| Key | Words |
|---|---|
| `Code.ExtraMissing` | {expected}, named in code.libraries, is missing |
| `Code.ExtraWithoutNative` | extra libraries ({found}) need the native permission |
| `Code.FolderWithoutKey` | there is a code/ folder, but luma.json has no code |
| `Code.KeyWithoutFolder` | luma.json has code, but there is no code/ folder |
| `Code.Nested` | a code folder holds files only, not folders |
| `Code.NoEntryLibrary` | its library {expected} is missing |
| `Code.NotAFolder` | it must be a folder, not a file |
| `Code.NotListed` | the folder is there, but code.platforms does not list it |
| `Code.NotNamed` | this file is neither the mod's library nor named in code.libraries |
| `Code.UnknownPlatform` | this is not a platform (windows-x64, windows-arm64, linux-x64, linux-arm64, macos-arm64, macos-x64, android-arm64, android-x64, ios-arm64) |

### Libraries

| Key | Words |
|---|---|
| `Lib.AnyLibrary` | a library it does not name |
| `Lib.BadElf` | a damaged ELF file |
| `Lib.BadMachO` | a damaged Mach-O file |
| `Lib.BadPe` | a damaged PE file |
| `Lib.CallTell` | "{found}" is a constructor, a destructor or a function with a variable number of arguments, which cannot be called by name |
| `Lib.Encrypted` | it is encrypted (LC_ENCRYPTION_INFO_64), so the device cannot sign it again |
| `Lib.HookOnConstructor` | "{found}" is a constructor or destructor, which takes before and after hooks only |
| `Lib.ImportRowBad` | import {row} is not one luma.h makes |
| `Lib.ImportsUnreadable` | its list of imports points outside the library |
| `Lib.InfoTooSmall` | it is shorter than luma.h's LumaModInfo (it was not built with luma.h) |
| `Lib.InfoUnreadable` | it points outside the library |
| `Lib.NoIos` | it does not say it is built for iOS devices (no LC_BUILD_VERSION or LC_VERSION_MIN_IPHONEOS) |
| `Lib.NoNames` | loaded as a whole |
| `Lib.NotALibrary` | not a library of any system |
| `Lib.NotIos` | it is built for {found}, not for iOS devices |
| `Lib.NotLumaH` | its luma_mod says api 0 (it was not built with luma.h) |
| `Lib.NotShared` | a {kind} file that is not a shared library |
| `Lib.RunsAtClose` | the system would run its {found} when it closes it, after the mod's memory is freed: build it with luma.h's flags |
| `Lib.RunsAtLoad` | the system would run its {found} when it loads it, before the mod is started: build it with luma.h's flags |
| `Lib.StartFaulted` | its begin or start failed and the guard stopped it (see its GUARD entry) |
| `Lib.StartRefused` | its start refused to run (it returned {found}) |
| `Lib.ThirtyTwoBit` | a 32-bit library |
| `Lib.TooManyImports` | it lists more than 65536 imports |
| `Lib.TooManyMods` | more mods with code are on than the {count} this game takes |
| `Lib.Universal` | a universal Mach-O file holding several machines (give one file per platform) |
| `Lib.WholeLibrary` | loaded as a whole |

### Hooks

| Key | Words |
|---|---|
| `Hook.Constexpr` | "{found}" is constexpr: the game may run it while it is being built, so it takes no hooks |
| `Hook.NoDefaultResult` | {found} gives a result that has no empty value, so a hook may not skip it, wrap it or replace it |
| `Hook.Variadic` | "{found}" takes a variable number of arguments, so it takes before hooks only; the function it passes them to with a va_list takes every hook |

### Names the game's self-description leaves out

| Key | Words |
|---|---|
| `LeftOut.AliasTwice` | a name that .cpp files give to different types: {what} |
| `LeftOut.Ambiguous` | {what} each have it |
| `LeftOut.InstanceInCpp` | a template instance only a .cpp file names |
| `LeftOut.InternalLinkage` | internal linkage: no name a mod can bind |
| `LeftOut.KernelOwn` | LumaCore's own |
| `LeftOut.NoName` | no name a mod can write |
| `LeftOut.NoSize` | no size: incomplete or dependent |
| `LeftOut.NotDefined` | declared, but nothing in the game defines it |
| `LeftOut.TemplateMember` | a static member of a class template |
| `LeftOut.TemplateUnused` | a template the game never uses with these arguments |
| `LeftOut.ThreadLocal` | thread-local: one per thread, no single address |
| `LeftOut.TypeNoName` | its type has no name a mod can write |
| `LeftOut.VirtualBase` | reached through the virtual base {what} |
| `LeftOut.WideBitField` | a bit-field wider than 32 bits |

### Library data

| Key | Words |
|---|---|
| `Kit.AnyOf` | {key} matches none of the schema's {count} choices |
| `Kit.DeclaredNoFolder` | kit names {module}, but there is no kit/{module}/ folder |
| `Kit.Enum` | {key} is {found}; it must be one of {expected} |
| `Kit.FolderNotDeclared` | there is a kit/{module}/ folder, but kit does not name {module} |
| `Kit.Least` | {key}: {what} is {found}; the least allowed is {min} |
| `Kit.LessThan` | {key}: {what} is {found}; it must be less than {max} |
| `Kit.MoreThan` | {key}: {what} is {found}; it must be more than {min} |
| `Kit.Most` | {key}: {what} is {found}; the most allowed is {max} |
| `Kit.NoModuleData` | none |
| `Kit.NotAllowed` | {key} has "{name}", which the schema does not allow |
| `Kit.NotData` | {entry} is never read: a module reads only kit/<module>/<name>.json, the name 1 to 64 of a-z, 0-9, _, - and ., not starting with a dot |
| `Kit.NotJson` | not JSON: {found} |
| `Kit.NotNeeded` | kit names {module}, which is no module of this mod or of a mod in needs |
| `Kit.OneOf` | {key} matches {found} of the schema's {count} choices; it must match exactly one |
| `Kit.Pattern` | {key} is {found}, which does not match the pattern {expected} |
| `Kit.Repeated` | {key} has {found} more than once; its items must differ |
| `Kit.Required` | {key} has no "{name}", which the schema requires |
| `Kit.SchemaKeyword` | the data schema {entry} has "{found}" at {key}, which the kit schema form does not read |
| `Kit.SchemaPlace` | {entry}: a library's data schema is code/<id>.kit/<module>/<version>.json, the version a whole number from 1 to 1000000 |
| `Kit.SchemaUnread` | the data schema {entry} is not JSON: {found} |
| `Kit.Type` | {key} is {found}; the schema's type is {expected} |
| `Kit.What.Bytes` | the size in bytes |
| `Kit.What.Items` | the number of items |
| `Kit.What.Keys` | the number of keys |
| `Kit.What.Length` | the number of characters |
| `Kit.What.Value` | the value |

### Rules not checked

| Key | Words |
|---|---|
| `Missing.AfterStart` | a class no list of the game names may be one a mod's code makes; the game checks it once the mods have started |
| `Missing.Classes` | the game's list of classes was not given; it comes with the game's self-description |
| `Missing.Device` | what this device's graphics have is known only when the game starts the mod |
| `Missing.Fields` | the game's list of fields was not given; it comes with the game's self-description |
| `Missing.Game` | no game was given to check against |
| `Missing.Load` | how many rows a selector matches is known only when the game loads the mod |
| `Missing.ModFolder` | no mod folder was given to find the needed mods in |
| `Missing.NewScenarios` | a new galaxy's scenario table this mod brings could not be read as a table (which layers are on together) |
| `Missing.OtherModRows` | another mod's rows are known only when the game loads the mods |
| `Missing.RendererFeatures` | the game's list of graphics features (luma_renderer.json) was not given; it comes with the game's self-description |
| `Missing.Rows` | the game's rows of this file were not given |
| `Missing.Scenarios` | the galaxies' scenario tables were not given (which layers are on together) |
| `Missing.SelfDescription` | the game's self-description was not given |
| `Missing.ServiceImports` | which services a library uses cannot be read from it yet |
| `Missing.Services` | the game's list of services (their versions and permissions) was not given |
| `Missing.SigningRules` | the iOS signing rules for extra libraries are not defined yet |
| `Missing.Store` | the game's files were not given |
| `Missing.Types` | the game's types file (luma_types.json) was not given; it comes with the game's self-description |
| `Missing.Virtuals` | the game's types file (luma_types.json) does not list virtual functions; a newer one comes with the game's self-description |

### Opening a file

| Key | Words |
|---|---|
| `Open.Denied` | this program may not read {detail} |
| `Open.File` | the file cannot be read ({detail}) |
| `Open.Folder` | the folder cannot be read ({detail}) |
| `Open.NotFound` | {detail} does not exist |
| `Open.TooDeep` | its folders go more than 64 deep, at {detail} |

### Preparing files

| Key | Words |
|---|---|
| `Cook.Anim.ModelUnread` | the game's {model} was imported by an older Nebula, so its materials and textures cannot be read here: import the game again |
| `Cook.Anim.NoGame` | {file}: an animation file is made from the game's model, which is not known here (give the game: --game) |
| `Cook.Anim.NoGameMaterial` | the animation file's material {material} is no material of the game's {model} (its materials: {materials}) |
| `Cook.Anim.NoGameTexture` | the animation file swaps in the texture {texture}, which is no texture of the game's {model} (its textures: {textures}) |
| `Cook.Anim.NoName` | animation {animation} has no name; its name is the name of the clip it makes |
| `Cook.Anim.None` | {file} has no animation |
| `Cook.Anim.NotAJoint` | animation {animation} moves {node}, which is no joint of the game's model {model} |
| `Cook.Anim.NotANumber` | animation {animation} has a key that is not a number (NaN or infinite) |
| `Cook.Anim.NotInArchive` | {file}: an animation file goes in the archive of the model it moves (ObjectData/<Name>.arc/) |
| `Cook.Anim.OnlyJoints` | animation {animation} animates {path}; an animation file moves joints and materials (their color and texture transform) only |
| `Cook.Anim.ParentDiffers` | joint {joint} hangs from {found} here but from {parent} in the game's model; the game's clips move each joint from its parent |
| `Cook.Anim.ParentMissing` | joint {joint} hangs from {parent} in the game's model but from no joint here |
| `Cook.Anim.RootDiffers` | joint {joint} is the game model's root but hangs from {found} here |
| `Cook.Anim.TextureUnnamed` | {texture}: a texture an animation file swaps in is the game's texture of its image's name (give the image a name, or a file name, as {model} names its textures) |
| `Cook.Anim.TooLong` | animation {animation} lasts {seconds} seconds; a clip lasts at most {max} |
| `Cook.Anim.Twice` | two animations are named {name} |
| `Cook.Anim.WhichModel` | {archive} has more than one model ({models}) and none has the archive's name: an animation file there cannot tell which it moves |
| `Cook.Camera.NoAnimation` | no animation moves the camera's node or its yfov |
| `Cook.Camera.NoFov` | camera {camera} has no yfov |
| `Cook.Camera.None` | no node has a camera: give the camera's node a "camera" |
| `Cook.Camera.NotPerspective` | camera {camera} is not a perspective camera |
| `Cook.Camera.ParentMoves` | the camera's parent node {node} moves: animate the camera's own node |
| `Cook.Cell.Number` | column "{name}" holds a number: give it a number |
| `Cook.Cell.Text` | column "{name}" holds text: give it text |
| `Cook.Cell.Whole` | column "{name}" holds a whole number: give it a whole number |
| `Cook.Clip.Plays` | the animation {animation} plays "{found}"; it can play once, once_then_first, loop, there_and_back or loop_there_and_back |
| `Cook.Clip.Pointer` | the animation {animation} moves {pointer}; an animation can move a material's baseColorFactor and its base color texture's KHR_texture_transform |
| `Cook.Clip.SwapNoKeys` | the animation {animation}'s texture swap of the material {material} has no keys |
| `Cook.Clip.SwapNoTexture` | the animation {animation} swaps the textures of the material {material}, which has no base color texture |
| `Cook.Clip.Undrawn` | the animation {animation} changes the material {material}, which no mesh draws |
| `Cook.Collision.Attribute` | a collision part uses material {found}; the most is {max} |
| `Cook.ConsoleOnly` | {file}: only the console reads this file (its banner, HOME menu or module list); the game has no use for it |
| `Cook.Effect.Choice` | {where} is "{value}", which is not one of {expected} |
| `Cook.Effect.Color` | {where} is "{value}", not a color written as rrggbbaa |
| `Cook.Effect.Count` | the mods' particles come to {count}; the game takes at most {max} |
| `Cook.Effect.Empty` | {where} has no particle; an effect has at least one |
| `Cook.Effect.FileMissing` | {where} names the file "{uri}", which the mod does not have |
| `Cook.Effect.FilePath` | {where} names the file "{uri}"; a texture's file is a path inside the layer's assets folder, with / between folders and no .. or : |
| `Cook.Effect.Frames` | {where} comes to {count} textures; a particle takes at most 255 |
| `Cook.Effect.Is.Bool` | true or false |
| `Cook.Effect.Is.List` | a list in [ ] |
| `Cook.Effect.Is.Number` | a number |
| `Cook.Effect.Is.Object` | names with values, in braces |
| `Cook.Effect.Is.Text` | text in quotes |
| `Cook.Effect.Is.Whole` | a whole number |
| `Cook.Effect.Keys` | the keys at {where} need at least one key, each a list that starts with its frame, the frames rising |
| `Cook.Effect.Kind` | {where} must be {expected} |
| `Cook.Effect.Length` | {where} holds {found} values; it takes {count} |
| `Cook.Effect.LikeUnknown` | {where} names "{name}", which is not one of the game's particles |
| `Cook.Effect.Missing` | {where} needs "{key}" |
| `Cook.Effect.NameBad` | the effect name "{name}" at {where} may use only lowercase letters, digits and _, must start with a letter, be at most 64 long and not end in two digits |
| `Cook.Effect.NoTexture` | {where} names the texture "{name}", which is neither one of the file's textures nor one of the game's particle textures |
| `Cook.Effect.OldForm` | particles are written in effects.json in a layer's assets folder, not as Particles.jpc.json |
| `Cook.Effect.Overlap` | two mods replace the game's particle "{name}": {winner}'s is used, not {other}'s |
| `Cook.Effect.Positional` | {where} is a list; name each texture by its role: sprite, child, second or indirect |
| `Cook.Effect.Range` | {where} is {value}, outside {min} to {max} |
| `Cook.Effect.RawIndex` | {where}: a particle names its textures by role, not by number; give the texture's name as "{role}" in its textures |
| `Cook.Effect.ReplaceUnknown` | {where}: the game has no particle "{name}" to replace |
| `Cook.Effect.RoleOff` | {where} gives a texture as "{role}", but the particle does not draw that one: its record is missing or turned off |
| `Cook.Effect.TableNoFirst` | {where}: the effect name "{word}" starts the particles {word}00, {word}01 and on, and there is no {word}00 |
| `Cook.Effect.TableUnknown` | {where}: "{word}" in the effect name is not a particle of the game or of any mod that is on |
| `Cook.Effect.TextureName` | the texture name "{name}" at {where} may use only letters, digits, _, - and ., and be 1 to 64 long |
| `Cook.Effect.TooMany` | {where} has {count} particles; an effect has at most {max} |
| `Cook.Effect.UnknownPart` | {where} has no part "{key}"; its parts are {expected} |
| `Cook.Effect.UnknownValue` | {where} has no value "{key}"; its values are {expected} |
| `Cook.EmptyFile` | {file} is empty |
| `Cook.Events.ClipName` | {file}: the clip's name (the file's name before .anim.json) is 1 to {max} bytes |
| `Cook.Events.ClipUnknown` | {file}: the game's {archive} has no clip {clip}; the frames are not checked against a clip here |
| `Cook.Events.GameUnknown` | {file}: the game is not known here, so the frames are not checked against the clip (give the game: --game) |
| `Cook.Events.Name` | {where}: the event "{name}" is 1 to {max} of the letters A-Z a-z, the digits 0-9, _ and . |
| `Cook.Events.None` | {file} has no event |
| `Cook.Events.NotInArchive` | {file}: a clip's events go in the archive of the clip (ObjectData/<Name>.arc/<clip>.anim.json) |
| `Cook.Events.PastEnd` | {where}: frame {frame} is past the end of clip {clip}, whose last frame is {last} |
| `Cook.Events.TooMany` | {file} has {count} events; a clip has at most {max} |
| `Cook.Events.Twice` | {where}: event {event} at frame {frame} is given twice |
| `Cook.Failed` | the game's file cannot be made from it: {detail} |
| `Cook.Font.Beyond` | {where}: {char} is past U+FFFF, and the game's text draws characters up to U+FFFF |
| `Cook.Font.Code` | {where} is "{value}": a character is given as a number or as U+ and its hexadecimal code (U+00E9) |
| `Cook.Font.KernCut` | {uri} has {count} kerning pairs; the {max} that move most are kept |
| `Cook.Font.Missing` | {uri} has no glyph for {count} of the characters asked for ({chars}); {fallback} draws them |
| `Cook.Font.NoGlyph` | {uri} draws none of the characters asked for (Latin, and the source's chars and ranges) |
| `Cook.Font.NotAsset` | {file}: a font goes in a layer's assets folder (assets/<path>.font.json); a text names it Luma/<id>/<path> |
| `Cook.Font.RangeOrder` | {where}: the first character comes after the last |
| `Cook.Font.TooLarge` | {uri} at size {size} is more than the game's fonts hold (a line or a character up to 127 pixels, a cell up to 255); the largest size that fits is {most} |
| `Cook.Font.TooMany` | {uri} at size {size} has {count} characters, and at most {max} fit: ask for fewer characters or a smaller size |
| `Cook.Font.Unreadable` | {uri} is not a TrueType or OpenType font that can be read |
| `Cook.Image.Kind` | the image {image} is neither PNG nor JPEG |
| `Cook.Image.Side` | the image {image} is {width} by {height} pixels; images go up to {max} pixels a side |
| `Cook.Image.Unreadable` | the image {image} cannot be read |
| `Cook.Layout.Both` | {where} gives both "{key}" and "{other}"; give one of them |
| `Cook.Layout.Control` | {where} holds a control character; text takes letters, signs and line breaks (\n) |
| `Cook.Layout.Name` | the name "{name}" at {where} may use only letters, digits and _, and be 1 to 16 long |
| `Cook.Layout.NoFont` | {where} names the font "{name}", but the mod has no {uri} in any layer |
| `Cook.Layout.NoPane` | {where} names the pane "{name}", which the layout does not have |
| `Cook.Layout.NoTexture` | {where} names the texture "{name}", but the mod has no {uri} |
| `Cook.Layout.NotInArchive` | {file}: a layout goes in its own archive (LayoutData/<Name>.arc/<Name>.layout.json) |
| `Cook.Layout.NotLayout` | the game's layout file does not read as a layout (its blocks run past its end) |
| `Cook.Layout.PatchPlace` | {where}: a patch of a game layout goes in a layer's patches/ as <archive>/blyt/<Name>.brlyt.json, so other mods' patches of it combine |
| `Cook.Layout.TextLong` | {where} has {count} characters; the pane holds {max} (its length) |
| `Cook.Layout.TextTall` | {where}: {lines} lines are {height} high; the pane is {max} high |
| `Cook.Layout.TextWide` | {where}: the line "{line}" is at least {width} wide; the pane is {max} wide |
| `Cook.Layout.TooMany` | the layout has {count} panes; a layout holds at most {max} |
| `Cook.Layout.Twice` | {where}: the name "{name}" is used twice; a pane or group is found by its name |
| `Cook.LeftOut.Console` | only the console's own menus read it |
| `Cook.LeftOut.Tool` | the tools that made the game left it in the archive; no code opens it |
| `Cook.Material.Clamped` | the material {material}'s {factor} is {found}, outside what glTF allows; it was brought to the nearest allowed value |
| `Cook.Material.CoordSet` | the material {material}'s {map} reads texture coordinates {set}, which the mesh {mesh} does not have |
| `Cook.Material.MissingTexture` | the material {material}'s {map} names texture {texture}, which has no image in the file |
| `Cook.Material.Ray` | the material {material}: {key} is not a ray value a material takes, or not in its range (ior, thickness, body, shine, tint_keep, traced, sun_through, metal_kind, glow, vertex_colour) |
| `Cook.RayData` | ray tracing data: read and not used by this game (a note: shader.json `ray`, a glTF material's `extras.nebula.ray`, or a `merge/NebulaData/Rt*.tsv` table, each checked as before) |
| `Cook.Material.TextureTransform` | the material {material}'s {map} has a KHR_texture_transform; it was baked into its texture coordinates |
| `Cook.Material.TransformShared` | the material {material}'s {map} has its own KHR_texture_transform on texture coordinates {set}, which another of its textures also reads; give it a set of its own |
| `Cook.Merge.ChangeForm` | each item of "change" must have a "where" and a "set", each a set of columns |
| `Cook.Merge.ChangeNoRow` | "change" where {row} matched no row; the rest of the file was added |
| `Cook.Merge.NoBase` | the game has no {file} to add to, and the mod brings none |
| `Cook.Merge.NoColumn` | the table has no column "{name}" |
| `Cook.Merge.RemoveForm` | each item of "remove" must be a set of columns |
| `Cook.Merge.RemoveNoRow` | "remove" {row} matched no row; the rest of the file was added |
| `Cook.Model.Accessor` | accessor {accessor} has {count} elements; a model has up to {max} |
| `Cook.Model.AccessorPast` | accessor {accessor} reaches past its buffer view |
| `Cook.Model.Billboard` | the node {node}'s billboard is {value}; it can be "y" or "xy" |
| `Cook.Model.Blends` | the mesh {mesh} needs more blends of joints than a model has room for ({max}) |
| `Cook.Model.ColorSets` | the mesh {mesh} has {count} vertex color sets; a mesh has up to {max} |
| `Cook.Model.CoordSets` | the mesh {mesh} uses {count} texture coordinate sets; a mesh has up to {max} |
| `Cook.Model.Cut` | the .glb file is cut off |
| `Cook.Model.Extension` | the file requires {name}, which Nebula does not read; export it without (Nebula reads {read}) |
| `Cook.Model.FlatNormals` | the mesh {mesh} has no normals: each triangle was given its own (flat shading) |
| `Cook.Model.GameUnknown` | the game's model for {file} was not read: its joints keep the file's numbers, not the game's |
| `Cook.Model.Hide` | {file}: a model entry holds exactly "hide": true, which hides the game's model it names |
| `Cook.Model.HideNoGame` | {file}: a hidden model keeps the game model's joints, and that model is not known here (give the game: --game) |
| `Cook.Model.Invalid` | the file is not valid glTF 2.0 at {where}: it is missing or not what glTF says it is |
| `Cook.Model.JointName` | joint "{joint}" has a letter the game's models cannot hold; name it with English letters, digits and signs only |
| `Cook.Model.JointTwice` | two joints are named "{joint}"; each joint needs a name of its own |
| `Cook.Model.Joints` | the model has {count} joints; a model has up to {max} |
| `Cook.Model.Json` | the file's JSON cannot be read (line {line}, column {column}) |
| `Cook.Model.Layers` | the material {material} has {count} textures or coordinate sets; a material has up to {max} |
| `Cook.Model.Leaves` | "{uri}" points outside the model's folder: keep the model's files beside it |
| `Cook.Model.LevelAlone` | {file} is a level of detail of the model {model}, which the mod does not have; add {model} or give the file another name |
| `Cook.Model.MaterialName` | material "{material}" has a letter the game's models cannot hold; name it with English letters, digits and signs only |
| `Cook.Model.Meshes` | the model has {count} meshes; a model has up to {max} |
| `Cook.Model.MissingJoint` | the game's model {model} has the joint "{joint}", which this model lacks; the game's animations and code move joints by these names |
| `Cook.Model.NoGameModel` | {archive} has no game model of this name (its models: {models}); a new model goes in assets/ |
| `Cook.Model.NoPositions` | mesh {mesh} has a part with no POSITION attribute |
| `Cook.Model.NoTriangles` | the model has no triangles |
| `Cook.Model.NodeMissing` | the file names node {node}, which it does not have |
| `Cook.Model.NotTriangles` | the mesh {mesh} is drawn as points or lines (mode {mode}); a model is drawn in triangles |
| `Cook.Model.SkinNoWeights` | the mesh {mesh} has a skin but no JOINTS_0 and WEIGHTS_0 |
| `Cook.Model.SkinOutside` | the skin names the node {node}, which is not in the scene |
| `Cook.Model.SocketIsJoint` | the socket "{name}" has the name of a joint; give it a name of its own |
| `Cook.Model.SocketTwice` | two sockets are named "{name}" |
| `Cook.Model.TransformNoTexture` | the material {material} has a texture transform but no base color texture |
| `Cook.Model.TwoGameModels` | {archive} has both {name}.bdl and {name}.bmd: name the file {name}.bdl.glb or {name}.bmd.glb |
| `Cook.Model.TwoSkins` | the meshes {mesh} and {other} have different skins; a model has one skin |
| `Cook.Model.UnevenTransform` | the material {material}'s texture is turned with unequal scales; scale it evenly, or do not turn it |
| `Cook.Model.Unreadable` | "{uri}", a file the model names, cannot be read |
| `Cook.Model.Unweighted` | {count} vertices of the mesh {mesh} have no weight: they follow the skin's root |
| `Cook.Model.Version` | the file is glTF "{found}"; Nebula reads glTF 2.0 |
| `Cook.Model.VertexPast` | a triangle of the mesh {mesh} names vertex {vertex}; the mesh has {count} |
| `Cook.Model.ViewPast` | buffer view {view} reaches past its buffer |
| `Cook.Model.WeightsOver4` | the mesh {mesh} weighs {vertices} vertices on more than four joints; a vertex follows up to four |
| `Cook.Movie.AudioTrack` | {uri}: its own sound track ({found}) is not read; give the sound as a WAV, FLAC or Ogg Vorbis file in audio |
| `Cook.Movie.Codec` | {uri} is {found}; give the video as AV1 in WebM or IVF (the game decodes AV1 itself on every system) |
| `Cook.Movie.Colour` | {uri} has the color {found}; give BT.709 limited range or BT.601 full range |
| `Cook.Movie.File` | {uri} cannot be read as a WebM or IVF movie: {detail} |
| `Cook.Movie.FirstKey` | {uri} does not start with a key frame |
| `Cook.Movie.Form` | {uri} is AV1 {found}; the game decodes AV1 main profile, 8 bits, 4:2:0 |
| `Cook.Movie.Name` | {where}: "{name}" is not a game movie's name (its file name in MovieData without .thp) |
| `Cook.Movie.NoGameMovie` | {where} names the game movie "{name}", which the game does not have |
| `Cook.Movie.NotAsset` | {file}: a movie goes in a layer's assets folder (assets/<path>.movie.json) |
| `Cook.Movie.Rate` | {uri} plays at {value} frames a second; a movie plays at more than 0 and at most {max} |
| `Cook.Movie.Size` | {uri} is {width} x {height}; a movie is at most 1920 x 1080 (or 1080 x 1920) |
| `Cook.Movie.Subtitle` | {where}: the subtitle's time {value} is not inside the movie's {seconds} seconds, or ends before it starts |
| `Cook.Movie.TooLong` | the movie lasts {seconds} seconds; a movie lasts at most {max} |
| `Cook.NotArchive` | {file} is named as an archive (.arc) but is not one |
| `Cook.Post.Forbidden` | {file} line {line}: a screen pass may not write {word}; the scene, depth, motion, its parameters and the entry point come from the game |
| `Cook.Post.Json` | {file}: {key} is not what a screen pass's interface takes (params with name, type float to float4 and default; reads depth and motion; draw contract 15) |
| `Cook.Post.Missing` | {file} is a screen pass, but the mod has no {other} beside it |
| `Cook.Post.NoPost` | {file} defines no post(PostIn i) |
| `Cook.Post.Text` | {file} is not UTF-8 text |
| `Cook.Post.TooLarge` | {file} is {value} bytes; a screen pass has at most {max} |
| `Cook.Post.TooManyParams` | {file}: the parameters take {count} floats; a screen pass has {max}, in 4 rows of 4, and a parameter never crosses a row |
| `Cook.Post.Twice` | {file}: the name "{name}" is given twice |
| `Cook.Shader.Compile` | {file} line {line} does not compile for {target}: {detail} |
| `Cook.Shader.CompileProgram` | the material {material} with the shader {file} does not compile for {target} where the shader's code meets the material's: {detail} |
| `Cook.Shader.Compiler` | the shader compiler {file} did not run: {detail} |
| `Cook.Shader.Forbidden` | {file} line {line}: a shader may not write {word}; its parameters, textures and inputs come through SurfaceIn and Surface |
| `Cook.Shader.Json` | {file}: {key} is not what a shader's interface takes (stage material; params, textures and reads as the materials contract 3.4 says) |
| `Cook.Shader.Missing` | the material {material} names the shader "{name}", but the mod has no {file} in any layer |
| `Cook.Shader.Name` | the material {material} names the shader "{name}": a shader is named by its path under assets without its ending (letters, digits, _ and -) |
| `Cook.Shader.NoSurface` | {file} defines no surface(in SurfaceIn i, inout Surface s) |
| `Cook.Shader.NoTexture` | the material {material} gives no texture for {texture} of the shader {shader} |
| `Cook.Shader.NotCompiled` | the shader {file} of the material {material} was not compiled for {target}: no compiler was found ({name} beside LumaLink or the game) |
| `Cook.Shader.ParamRange` | the parameter {param} of the material {material} is outside {min} to {max}, what its place holds |
| `Cook.Shader.ParamType` | the material {material} gives the parameter {param} a value that is not a {expected} |
| `Cook.Shader.Text` | {file} is not UTF-8 text |
| `Cook.Shader.TooLarge` | {file} is {value} bytes; a shader has at most {max} |
| `Cook.Shader.TooManyParams` | the shader of the material {material} has {count} parameters; {max} of the material's places are free |
| `Cook.Shader.Twice` | {file}: the name "{name}" is given twice |
| `Cook.Shader.UnknownParam` | the material {material} gives the parameter {param}, which the shader {shader} does not have |
| `Cook.Shader.UnknownTexture` | the material {material} gives the texture {texture}, which the shader {shader} does not have |
| `Cook.Shader.Unlit` | the material {material} is unlit (KHR_materials_unlit), so the shader {shader} has no light to fill |
| `Cook.Sound.Broken` | {uri} is damaged at byte {value} |
| `Cook.Sound.Channels` | {uri} has {count} channels; a sound effect has at most {max} |
| `Cook.Sound.Cut` | {uri} ends before the sound it describes; it was cut short |
| `Cook.Sound.Empty` | {uri} has no sound in it |
| `Cook.Sound.InstrumentKey` | {where}: "{name}" is not a MIDI program (0 to 127) or drums |
| `Cook.Sound.LoopOutside` | the loop {value} of {uri} is not inside its {count} frames, or is empty |
| `Cook.Sound.Midi` | {uri} cannot be read as a Standard MIDI file: {detail} |
| `Cook.Sound.MidiDropped` | {uri}: left out, as the game's sequences do not play them: {detail} |
| `Cook.Sound.NoGameSequence` | {where} names the game sequence "{name}", which the game does not have |
| `Cook.Sound.NoGameSound` | {where} names the game sound "{name}", which the game does not have |
| `Cook.Sound.NoGameStream` | {where} names the game stream "{name}", which the game does not have |
| `Cook.Sound.NoInstrument` | {uri} plays the MIDI program {name}, which instruments does not map to a game instrument |
| `Cook.Sound.NotANumber` | {uri} holds a sample that is not a number |
| `Cook.Sound.NotAsset` | {file}: a sound effect goes in a layer's assets folder (assets/<path>.sound.json) |
| `Cook.Sound.Opus` | {uri} is Ogg Opus; give the sound as WAV, FLAC or Ogg Vorbis |
| `Cook.Sound.Order` | {where}: the first number must be smaller than the second |
| `Cook.Sound.Rate` | {uri} is sampled at {value} Hz; give it at {min} to {max} Hz |
| `Cook.Sound.Sequence` | {uri} made a sequence the game's sequence cook refuses: {detail} |
| `Cook.Sound.SequenceName` | {where}: "{name}" is not a path in the game's sequence archive (AudioRes/Seqs/JaiSeq.arc) |
| `Cook.Sound.StreamChannels` | {uri} has {count} channels; a stream has at most {max} |
| `Cook.Sound.StreamFile` | {uri} cannot be read as a BRSTM, BCSTM or BFSTM stream: {detail} |
| `Cook.Sound.StreamName` | {where}: "{name}" is not a game stream's name (its file name in AudioRes/Stream without .ast) |
| `Cook.Sound.StreamTooLong` | the stream lasts {seconds} seconds; a stream lasts at most {max} |
| `Cook.Sound.TooLong` | the sound lasts {seconds} seconds; a sound effect lasts at most {max} (longer music is a stream) |
| `Cook.Sound.Unreadable` | {uri} is not a WAV, FLAC or Ogg Vorbis file |
| `Cook.Sound.WavKind` | {uri} is a WAV of format {kind} with {width}-bit samples; give it as 8-, 16-, 24- or 32-bit PCM or 32- or 64-bit float |
| `Cook.Stage.ArgName` | {where}: {other} has no argument "{name}"; its arguments are {expected} |
| `Cook.Stage.ArgTwice` | {where}: "{name}" is {column}, which the row already gives |
| `Cook.Stage.NoFacts` | arguments by name need the game's placement facts ({uri}), which could not be read; reinstall the game, or give Obj_arg0 to Obj_arg7 |
| `Cook.Stage.NoObject` | {where} gives arguments by name, but the game has no object "{name}" to take them from; give Obj_arg0 to Obj_arg7 |
| `Cook.Table.CellKind` | row {row}: "{name}" must be text or a number |
| `Cook.Table.ColumnKind` | column "{name}" in "columns" must be int, float or string |
| `Cook.Table.Json` | the table's JSON cannot be read (line {line}, column {column}) |
| `Cook.Table.KindChanged` | row {row}: "{name}" is another kind of value than in the rows before |
| `Cook.Table.NoRows` | the table has no "rows" list |
| `Cook.Table.RowNotObject` | row {row} is not a set of columns |
| `Cook.Text.TagArguments` | {key}, column {column}: [{word}] is written {syntax} |
| `Cook.Text.TagNumber` | {key}, column {column}: {value} in [{word}] is not a number from 0 to {most} |
| `Cook.Text.Unclosed` | {key}, column {column}: a "[" with no "]"; write "[[" for a bracket of its own |
| `Cook.Text.UnknownTag` | {key}, column {column}: "[{word}" is no message tag; write a tag word, or "[[" for a bracket of its own |
| `Cook.Texture.DdsForm` | the image {image} is a DDS the game does not read; it reads one flat picture in BC1 to BC5, BC7, or 8-bit RGBA or BGRA |
| `Cook.ToolFile` | {file} is left out: only the tools that made the game use a file like it, and no code opens it |

## Further reading

* [luma.json](luma-json.md)
* [The .luma file format](luma-file-format.md)
* [lumalink check](lumalink-commands/check.md)
