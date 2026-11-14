# Text and service values

How a script reads a service's structs, enums and constants, shows them in text, and uses the mod's own text rows.

## Smallest example

```lumascript
use svc.input

on start {
    let pad = input.Pad.CoStar
    log "pad {pad}, name kind {input.TEXT_NAME}"
}
```

The game's log gets `pad CoStar, name kind 2`.

## A service struct with pointer fields

A field of a service struct that is a pointer in C is a value in LumaScript. The compiler writes the pointer and the
count or size beside it for the call, and the script never sees the count or size field.

| C field | LumaScript field |
|---|---|
| `const char *x` | `text` |
| `const void *x` (or `const uint8_t *`), then `uint64_t` size | `bytes`; the size field is not written |
| `const <Prefix><T> *x`, then `uint32_t` count | a list of `<name>.<T>`; the count field is not written |

A pointer field left out of the literal is null in C (a count or size of 0). Every other field must be written
(S0075). `size` is set by the compiler. A struct of the header that no service table owns is named by the table it
is grouped with: `LumaNetHeader` is `net.internet.Header`.

```lumascript error S0075
use svc.net.internet as internet

on start {
    internet.http(internet.HttpRequest { url: "https://example.com/" }, fn(e: internet.Event) {
        return
    })
}
```

`timeout_ms` and `flags` are not pointers, so they cannot be left out.

```lumascript
use svc.net.internet as internet

on start {
    let r = internet.http(internet.HttpRequest {
        url: "https://example.com/scores"
        headers: [internet.Header { name: "Accept", value: "application/json" }]
        timeout_ms: 5000
        flags: 0
    }, fn(e: internet.Event) {
        match e {
            internet.Event.Done(socket, value) => log "done: {value}"
            internet.Event.Error(socket, answer, value) => {
                if let a = answer { log "failed: {a}" }
            }
            else => return
        }
    })
    if r == none { log "refused" }
}
```

`method` and `body` are left out: the request is a `GET` with no body. A mod that makes this call needs the
`net.internet` permission naming the host (`lumalink add permission net.internet <folder> --host example.com`).

## Enums and constants of a service

- A named enum `<Prefix><T>` of the header is `<name>.<T>`. Its cases drop the enum's common prefix and are written
  in CamelCase: `LUMA_INPUT_PAD_CO_STAR` is `input.Pad.CoStar`.
- The cases of an unnamed `enum { }` are constants of the service, upper case as written, like a `#define` of a whole
  number: `LUMA_INPUT_TEXT_NAME` is `input.TEXT_NAME`, `LUMA_INPUT_BUTTON_A` is `input.BUTTON_A`. The cases of the
  event enum are events, not constants.
- `svc.Answer` is one enum for every service: `Denied`, `Absent`, `NotFound`, `Exists`, `Io`, `Timeout`, `Closed`,
  `Memory`, `Busy`, `Format`, `TooLarge`, `Unreachable`. An event's `status` is `svc.Answer?`: `none` when the
  request succeeded. As a type it is written through a service the module uses: `internet.Answer`.

A service enum, a game enum and `svc.Answer` are shown in text by the case's name. A constant is a number.

```lumascript
use svc.net.internet as internet

fn said(answer: internet.Answer?) -> text {
    if let a = answer {
        return "failed: {a}"
    }
    return "ok"
}
```

`said` of a request refused for its host gives `failed: Denied`.

## The mod's text rows

`text "Key"` is the mod's line `Key` in the player's language, as `text`. The lines are in
`layers/<layer>/merge/MessageData/messages.json`; the game adds them to its messages when the mod is imported. A line
is a string, or an object with `default` and a line per language, keyed by code (`fr`, `fr-CA`) or by the game's name
for the language (`EuGerman`). The player's language takes its own line, else its base language's, else `default`.
A key with no line at all gives the key itself. A `{name}` in a line is filled by the argument `name:` given after
the key.

```json
{
  "lines": {
    "Shop_Price": { "default": "{item} costs {price} star bits.", "fr": "{item} coûte {price} fragments d'étoile." }
  }
}
```

```lumascript
on start {
    let price = 30
    log text "Shop_Price"(item: "A mushroom", price: price)
}
```

Each placeholder of the line is given by name. Against the mod's text rows the checker refuses a missing one (S0137),
a name the line does not have (S0138) and an argument without a name (S0139).

## Logging text

`log` writes any value of type `text` to the game's log as the mod: a literal, a string with values placed in it, a
`text "Key"` row, a variable or a function's result. `fail` takes the same.

```lumascript
fn greeting(name: text) -> text {
    return "hello, {name}"
}

on start {
    let line = greeting("Luigi")
    log line
    log greeting("Mario")
}
```

## Text of the game

A game function's `const char16_t *` result is `text?`: the game's UTF-16 converted to UTF-8. The game's message
tags are written as words in brackets (every word: `language/message-tags.md`), a `[` of the text as `[[`, and a
lone surrogate as U+FFFD. A text given back to the game as `const char16_t *` has its words made into tags again.

`text "Key"` made before the game has read its messages (in `on start`) holds the key and reads as the game's line
when it is used. Read before then, it is its key there, and the mod log says so once.

## Limits

- `text "Key"` takes a key written as a plain string, without values placed in it (S0015).
- A text row's placeholders are checked only when the build is given the mod's text rows; `lumalink build` does not
  read `messages.json`, so a missing key builds and shows the key at run time.
- A struct field that is a pointer of any other form (a pointer to a number, a `char *` the service writes) makes
  the struct unusable from LumaScript; C mods still use it.

## See also

- `language/language.md` (sections 3, 7.2, 10, D7.4, D12)
- `language/message-tags.md`
- `services/net.md`, `services/input.md`
- `format/luma.md` (layer folders, `merge/`)
