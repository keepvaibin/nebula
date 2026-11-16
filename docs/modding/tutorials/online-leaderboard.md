---
title: Adding an online leaderboard
shortTitle: Online leaderboard
intro: Send each star's time to a leaderboard server, and show the fastest times from the pause menu.
contentType: tutorials
example: online-leaderboard
---

## Introduction

In this tutorial, you make a mod that sends each star's time to a leaderboard server, under a name the player types once. A new **Leaderboard** entry in the pause menu shows the server's ten fastest times for the current star and the player's place.

The server address is an option. It can be any server that answers the two requests described in [Setting up a leaderboard server](#setting-up-a-leaderboard-server), on the internet, on the local network or on the same computer.

You learn how to:

* Ask for optional permissions, and keep the mod working when the player refuses one.
* Choose between `net.local` and `net.internet` from the server address.
* Ask the player for text once, and keep it with the `save` service.
* Show a list that loads while a request is in progress.

## Prerequisites

* Nebula, installed with Nebula Setup.
* LumaLink. For more information, see [Installing LumaLink](../get-started/installing-lumalink.md).
* We recommend that you complete [Connecting to LiveSplit](livesplit-link.md) first.

## Mod folder layout

When you finish, the mod folder contains these files:

```text
online-leaderboard/
  luma.json
  source/main.lum
  layers/base/merge/MessageData/messages.json   the lines it shows, in the game's 14 languages
  tests/board/test.json                         a name typed, two times sent, the board read
  tests/board/server/                           the test's leaderboard server, a mod of its own
  tests/no-name/test.json                       the name prompt closed with Esc
  tests/local-declined/test.json                net.local declined
  tests/internet-declined/test.json             net.internet declined
  tests/unresolvable/test.json                  a server name with no address
  tests/server-down/test.json                   nothing listening at the server's address
```

## Creating the mod

Create the mod folder, and add the mod's needs, permissions and option.

```sh
lumalink new online-leaderboard --id online-leaderboard --name "Online Leaderboard" --summary "Sends each star's time to a leaderboard server under the name you type, and shows the server's top ten from the pause menu." --author YOUR-NAME
lumalink add need lumakit online-leaderboard --version ">=1.0.0"
lumalink add permission net.internet online-leaderboard --host "*" --why "Sends your star times to the leaderboard server you choose and reads its top ten."
lumalink add permission net.local online-leaderboard --why "Reaches a leaderboard server on this computer or your local network."
lumalink add option server online-leaderboard --kind text --default "https://example.invalid/board" --max-length 256 --name "Leaderboard server"
```

The mod asks for two permissions, and the player can refuse either one:

* `net.internet` with the host `*` reaches any public address.
* `net.local` reaches this computer and the local network.

The code imports both services as optional, so if the player refuses one, the mod keeps running without it.

The `server` option is the leaderboard's address. Its default address ends in `.invalid`, a domain that never has an address, so the mod sends nothing until the player sets a server.

To add the mod's name and summary in another language, run `lumalink add text LANGUAGE online-leaderboard --name NAME --summary SUMMARY`. Add the option's name to the same `text` block by hand, as `"options": { "server": { "name": "..." } }`.

## Adding the mod's text

Create `layers/base/merge/MessageData/messages.json`. It adds the mod's lines to the game's text: the pause menu entry, the titles of the leaderboard, the name prompt, and the messages that explain why a time was not sent. `default` is English, and the other keys are the game's languages. `fr` and `es` are used for both regions of each language. A line that contains `{place}` takes a value from the code.

```json
{
  "lines": {
    "Board.Menu": { "default": "Leaderboard", "fr": "Classement", "ja": "ランキング", "...": "..." },
    "Board.TitlePlace": { "default": "Top ten for this star. Your place: {place}", "...": "..." },
    "Board.AskName": { "default": "Type your name for the leaderboard: {name}", "...": "..." },
    "Board.LocalNotAllowed": { "default": "This mod is not allowed to reach this computer or your local network. Allow it on the mod's page in Mods.", "...": "..." }
  }
}
```

The code refers to each line by name, for example `text "Board.Menu"`, or `text "Board.TitlePlace"(place: 3)` for a line with a value.

## Writing the code

Create `online-leaderboard/source/main.lum` with this code:

```lumascript
use optional svc.net.internet as internet
use optional svc.net.local as local
use svc.input
use svc.save.global as global
use svc.time
use kit.screen
use kit.menu
use kit.hud

// The leaderboard's address: http:// or https://, a host, an optional port and a path ("https://example.org/board").
option server: text

struct Entry { name: text, frames: u32 }
struct Top { top: list<Entry>, place: u32 = 0 }
struct Score { star: text, name: text, frames: u32 }

// The name the player typed once, kept across game files ("" until then).
var player: text = ""
// The world step the attempt's play began at; armed from a stage's start until its play begins.
var started: u64 = 0
var armed: bool = false
var playing: bool = false
var board = screen.ListScreen { id: 0 }

on start {
    if let saved = global.get("name") {
        player = saved.text() ?? ""
    }
    menu.add_pause_entry(text "Board.Menu", show_board)
}

fn step() -> u64 {
    return time.now().step
}

// The star being played: the stage's name and the star's number in it.
fn star() -> text {
    return "{MR::getCurrentStageName() ?? ""}/{MR::getCurrentScenarioNo()}"
}

// A star's time counts the world's steps from the stage's play to the star. A stage starts, or starts again after a
// miss: the attempt begins when its play does (after the opening camera and the star's start), so a skipped opening
// changes nothing. The pause menu holds the world, so its time is not counted.
hook after GameScene::startStagePlayFirst() {
    arm()
}

hook after GameScene::startStagePlayRetry() {
    arm()
}

// A galaxy with a player in it: not the file select, not the observatory.
fn arm() {
    playing = false
    armed = not MR::isStageFileSelect() and not MR::isStageAstroLocation() and MarioAccess::getPlayerActor() != none
}

hook before GameScene::exeSceneAction() {
    if armed {
        armed = false
        started = step()
        playing = true
    }
}

hook before GameScene::requestStartGameOverDemo() {
    playing = false
}

hook before GameSceneFunction::requestPowerStarGetDemo() {
    if not playing {
        log "a star got outside a timed attempt: no time to send"
        return
    }
    playing = false
    start submit(star(), (step() - started) as u32)
}

// ---- The player's name: typed once, on the platform's keyboard, over the game.

var typing: bool = false
var typed: text? = none

task ask_name() {
    typing = true
    typed = none
    let prompt = hud.text(hud.Corner.Top, text "Board.AskName"(name: ""))
    let request = input.text_request(input.TextRequest { kind: input.TEXT_NAME, max_length: 16, initial: "" },
        fn(e: input.TextEvent) {
            if e.status == none {
                typed = e.data
            }
            typing = false
        })
    guard let r = request else {
        hud.remove_text(prompt)
        typing = false
        return
    }
    while typing {
        let (so_far, caret) = input.text_peek(r)
        hud.set_text(prompt, text "Board.AskName"(name: so_far))
        wait 1 frames
    }
    hud.remove_text(prompt)
    if let t = typed {
        if t.len() > 0 {
            player = t
            global.set("name", t.bytes())
        }
    }
}

// ---- Sending a time.

task submit(on_star: text, frames: u32) {
    if player == "" {
        wait ask_name()
    }
    if player == "" {
        say(text "Board.NoName")
        return
    }
    let score = Score { star: on_star, name: player, frames: frames }
    let got = wait send("POST", "{base()}/submit", json.encode(score).bytes())
    if got.status != 200 {
        say(text "Board.NotSent"(why: why(got)))
    }
}

// Why a time was not sent, on a message screen until the player closes it (A or B), and in the mod log. A HUD line
// holds 64 characters; the message's pane holds the whole sentence.
fn say(line: text) {
    log line
    log fact "said" line
    screen.message(line)
}

// ---- The board, from the pause menu.

fn show_board() {
    if screen.alive(board) {
        return
    }
    board = screen.list(screen.List { title: text "Board.Title", rows: [text "Board.Loading"] })
    start fill_board(star())
}

task fill_board(on_star: text) {
    let got = wait send("GET", "{base()}/top?star={escape(on_star)}&name={escape(player)}", b"")
    if got.status != 200 {
        fail(why(got))
        return
    }
    guard let t = got.body.text(), let answer = json.decode<Top>(t) else {
        fail(text "Board.NotBoard"(server: server))
        return
    }
    var rows: list<text> = []
    for e in answer.top {
        if rows.len() < 10 {
            rows.push("{rows.len() + 1}. {e.name}  {time_text(e.frames)}")
        }
    }
    if rows.len() == 0 {
        rows.push(text "Board.Empty")
    }
    var title = text "Board.TitleNoPlace"
    if answer.place > 0 {
        title = text "Board.TitlePlace"(place: answer.place)
    }
    show(title, rows)
}

// The board's screen with its title and rows, unless the player closed it while the server answered.
fn show(title: text, rows: list<text>) {
    var shown = "{title}\n"
    for r in rows {
        shown = "{shown}{r}\n"
    }
    log fact "board" shown
    if screen.alive(board) {
        board = screen.list(screen.List { title: title, rows: rows })
    }
}

// What went wrong, in place of the board, until the player closes it.
fn fail(line: text) {
    log fact "board" line
    if screen.alive(board) {
        screen.message(line)
    }
}

// m:ss.cc from world steps at 60 a second.
fn time_text(frames: u32) -> text {
    let cs = frames * 100 / 60
    return "{cs / 6000}:{pad2(cs / 100 % 60)}.{pad2(cs % 100)}"
}

fn pad2(n: u32) -> text {
    if n < 10 {
        return "0{n}"
    }
    return "{n}"
}

// ---- HTTP: one request at a time, in the order asked. A server on this computer or the local network is reached
// through net.local, any other through net.internet; each needs the player's permission, and neither stands in for
// the other.

struct Reply { status: u32, body: bytes, error: text }

var busy: bool = false
var done: bool = false
var reply = Reply { status: 0, body: b"", error: "" }

// The server option without a final "/".
fn base() -> text {
    if server.ends_with("/") {
        return server.slice(0, server.len() - 1)
    }
    return server
}

task send(method: text, url: text, body: bytes) -> Reply {
    while busy {
        wait 1 frames
    }
    busy = true
    done = false
    reply = Reply { status: 0, body: b"", error: "" }
    let near = local_url(url)
    var asked = false
    if near == none {
        reply.error = "url"
    } else if near! {
        if local != none {
            let request = local.HttpRequest { method: method, url: url, headers: local_headers(body), flags: 0,
                body: body, timeout_ms: 10000 }
            asked = local.http(request, on_local) != none
        } else {
            reply.error = "local"
        }
    } else {
        if internet != none {
            let request = internet.HttpRequest { method: method, url: url, headers: internet_headers(body),
                flags: 0, body: body, timeout_ms: 10000 }
            asked = internet.http(request, on_internet) != none
        } else {
            reply.error = "internet"
        }
    }
    if asked {
        while not done {
            wait 1 frames
        }
    } else if reply.error == "" {
        reply.error = "refused"
    }
    busy = false
    return reply
}

fn local_headers(body: bytes) -> list<local.Header> {
    if body.len() == 0 {
        return []
    }
    return [local.Header { name: "Content-Type", value: "application/json" }]
}

fn internet_headers(body: bytes) -> list<internet.Header> {
    if body.len() == 0 {
        return []
    }
    return [internet.Header { name: "Content-Type", value: "application/json" }]
}

fn add_body(data: bytes) {
    for i in 0..data.len() {
        reply.body.push(data[i])
    }
}

fn on_local(e: local.Event) {
    match e {
        local.Event.Headers(socket, status, data) => { reply.status = status as u32 }
        local.Event.Data(socket, data) => add_body(data)
        local.Event.Done(socket, value) => { done = true }
        local.Event.Error(socket, status, value) => {
            reply.status = 0
            reply.error = "unreachable"
            done = true
        }
        else => return
    }
}

fn on_internet(e: internet.Event) {
    match e {
        internet.Event.Headers(socket, status, data) => { reply.status = status as u32 }
        internet.Event.Data(socket, data) => add_body(data)
        internet.Event.Done(socket, value) => { done = true }
        internet.Event.Error(socket, status, value) => {
            reply.status = 0
            reply.error = "unreachable"
            done = true
        }
        else => return
    }
}

// What went wrong, in the player's words.
fn why(r: Reply) -> text {
    if r.error == "url" {
        return text "Board.BadServer"(server: server)
    }
    if r.error == "local" {
        return text "Board.LocalNotAllowed"
    }
    if r.error == "internet" {
        return text "Board.InternetNotAllowed"
    }
    if r.error == "unreachable" or r.error == "refused" {
        return text "Board.Unreachable"(server: server)
    }
    return text "Board.Answered"(server: server, status: r.status)
}

// ---- Which reach a url needs: true for this computer or the local network (localhost, 127/8, 10/8, 172.16/12,
// 192.168/16, 169.254/16, ::1, fc00::/7, fe80::/10, and those as ::ffff: addresses), false for any other host, none
// when it is no http:// or https:// url.

fn local_url(url: text) -> bool? {
    var rest = ""
    if url.starts_with("http://") {
        rest = url.slice(7, url.len())
    } else if url.starts_with("https://") {
        rest = url.slice(8, url.len())
    } else {
        return none
    }
    var end = rest.len()
    for c in ["/", "?", "#"] {
        if let at = rest.find(c) {
            if at < end {
                end = at
            }
        }
    }
    let authority = rest.slice(0, end)
    if authority.len() == 0 or authority.find("@") != none {
        return none
    }
    if authority.starts_with("[") {
        guard let close = authority.find("]") else {
            return none
        }
        return local_v6(authority.slice(1, close).lower())
    }
    var host = authority
    if let colon = authority.find(":") {
        host = authority.slice(0, colon)
    }
    if host == "localhost" {
        return true
    }
    if let a = v4(host) {
        return local_v4(a)
    }
    return false
}

// The four numbers of a dotted IPv4 address, none for anything else.
fn v4(host: text) -> list<u32>? {
    let parts = host.split(".")
    if parts.len() != 4 {
        return none
    }
    var out: list<u32> = []
    for p in parts {
        if p.len() == 0 or p.len() > 3 {
            return none
        }
        var n: u32 = 0
        let digits = p.bytes()
        for i in 0..digits.len() {
            let b = digits[i]
            if b < 48 or b > 57 {
                return none
            }
            n = n * 10 + (b - 48) as u32
        }
        if n > 255 {
            return none
        }
        out.push(n)
    }
    return out
}

fn local_v4(a: list<u32>) -> bool {
    return a[0] == 127 or a[0] == 10 or (a[0] == 172 and a[1] >= 16 and a[1] <= 31) or (a[0] == 192 and a[1] == 168) or
        (a[0] == 169 and a[1] == 254)
}

fn local_v6(h: text) -> bool {
    if h == "::1" or h == "0:0:0:0:0:0:0:1" {
        return true
    }
    if h.starts_with("::ffff:") {
        if let a = v4(h.slice(7, h.len())) {
            return local_v4(a)
        }
        return false
    }
    guard let colon = h.find(":") else {
        return false
    }
    let first = h.slice(0, colon)
    if first.len() == 0 or first.len() > 4 {
        return false
    }
    var n: u32 = 0
    let digits = first.bytes()
    for i in 0..digits.len() {
        let b = digits[i]
        var d: u32 = 0
        if b >= 48 and b <= 57 {
            d = (b - 48) as u32
        } else if b >= 97 and b <= 102 {
            d = (b - 87) as u32
        } else {
            return false
        }
        n = n * 16 + d
    }
    return (n & 0xFE00) == 0xFC00 or (n & 0xFFC0) == 0xFE80
}

// A URL query's value: letters, digits and - . _ ~ as they are, every other byte %XX (RFC 3986).
fn escape(t: text) -> text {
    let hex = "0123456789ABCDEF".bytes()
    var out: bytes = b""
    let b = t.bytes()
    for i in 0..b.len() {
        let c = b[i]
        if unreserved(c) {
            out.push(c)
        } else {
            out.push(37)
            out.push(hex[(c >> 4) as int])
            out.push(hex[(c & 15) as int])
        }
    }
    return out.text() ?? ""
}

fn unreserved(c: u8) -> bool {
    if (c >= 48 and c <= 57) or (c >= 65 and c <= 90) or (c >= 97 and c <= 122) {
        return true
    }
    return c == 45 or c == 46 or c == 95 or c == 126
}
```

### Timing a star

`startStagePlayFirst` and `startStagePlayRetry` run when a stage starts, and when it starts again after Mario loses a life. In a galaxy, they prepare the attempt. The file select screen and the observatory are not timed. The attempt begins at the first call to `GameScene::exeSceneAction`, when play begins, so the opening camera is not timed.

`time.now().step` counts the world's steps, which stop while the game is paused. The star cutscene (`requestPowerStarGetDemo`) ends the attempt and sends its step count. A game over discards it. The star is identified by the stage's name and the star's number, for example `EggStarGalaxy/1`.

### Asking for a name

The first time the mod sends a time without a name, `input.text_request` opens the platform's text entry: the keyboard or input method on a computer, or the system keyboard on an Android device. `hud.text` shows the prompt over the game with the text typed so far (`input.text_peek`).

<kbd>Enter</kbd> accepts the name. <kbd>Esc</kbd>, the Back button on Android, or a lost window focus cancels the prompt, and that star's time is not sent. The name is stored with `save.global`, so the mod asks once, not once for each save file.

### Sending a time

`submit` sends the time as JSON. When a time is not sent, `say` uses `screen.message` to explain why, and the message stays until the player presses <kbd>A</kbd> or <kbd>B</kbd>. A HUD line holds at most 64 characters, so `hud.text` is used only for the short name prompt.

### Showing the leaderboard

`menu.add_pause_entry` adds **Leaderboard** to the pause menu. `screen.list` shows the leaderboard with **Loading...** while the server answers, then the ten fastest times, with the player's place in the title. When the request fails, `screen.message` explains why. <kbd>B</kbd> closes the leaderboard.

### Choosing a network service

`local_url` reads the host of the server address. These hosts use `net.local`: `localhost`, `127.x.x.x`, `10.x.x.x`, `172.16.x.x` to `172.31.x.x`, `192.168.x.x`, `169.254.x.x`, `[::1]`, `[fc..]`, `[fd..]` and `[fe80..]`, and any of those written as `[::ffff:...]`. Any other host uses `net.internet`.

These are exactly the addresses each service accepts, and neither service replaces the other. If the player refuses `net.local`, the mod never tries the same address through `net.internet`. A refused service is empty (`local == none`), and the mod tells the player which kind of access is not allowed. This computer's own public address is not in the list above: it goes to `net.internet`, which refuses it as a local address.

## Setting up a leaderboard server

The mod works with any HTTP server that answers these two requests. `{server}` is the value of the `server` option, without a trailing `/`.

| Request | Body | Answer |
|---|---|---|
| `POST {server}/submit` | `{"star": "EggStarGalaxy/1", "name": "Mario", "frames": 689}`, with `Content-Type: application/json` | Status 200 and any body. |
| `GET {server}/top?star=EggStarGalaxy%2F1&name=Mario` | None | Status 200 and `{"top": [{"name": "Rosalina", "frames": 6}, ...], "place": 11}`. |

* `frames` is a time in world steps, at 60 per second.
* `top` lists the star's best times, fastest first. The leaderboard shows the first ten.
* `place` is the name's place by its best time, or `0` when the name has no time.

Any other status, or no answer within 10 seconds, is shown to the player as the reason the request failed.

## Building and checking the mod

```sh
lumalink build online-leaderboard
lumalink check online-leaderboard
```

The last line of the `check` output is `Errors: 0`.

> [!NOTE]
> In the current version, LumaLink cannot yet build a comparison of an optional service with `none` (`local == none`) or a call to `input.text_peek`, so this mod does not build.

## Testing the mod

A test must not reach the internet. The finished mod's `tests/board/server/` folder contains a second small mod that listens on `127.0.0.1:47613` through `net.local` and answers as described in [Setting up a leaderboard server](#setting-up-a-leaderboard-server). It holds ten times for the first star of Good Egg Galaxy, from Rosalina's 0.10 seconds to Spike's 1.00 seconds, and writes each request it receives as facts.

The tests set the `server` option to `http://127.0.0.1:47613/board`. The tests for the internet path use domains ending in `.invalid`, which never have an address, so no test connects to a host on the internet.

```sh
lumalink test online-leaderboard
```

* `tests/board` plays Good Egg Galaxy, star 1. The test calls the star cutscene at frame 600. The prompt opens, and the test types `Marix`, <kbd>Backspace</kbd>, `o` and <kbd>Enter</kbd> to enter the name Mario. The server receives the star, the name and a time of more than 600 steps as JSON. In the galaxy again, <kbd>+</kbd> and <kbd>A</kbd> on **Leaderboard** show the ten times with **Your place: 11** in the title, and <kbd>B</kbd> closes it. A second star is sent under the name Mario without the prompt.
* `tests/no-name` presses <kbd>Esc</kbd> at the prompt. Nothing is sent, the message says that no name was entered, and the next star asks again.
* `tests/local-declined` refuses `net.local` and allows `net.internet`. The leaderboard and the message say that the mod cannot reach this computer, and the server receives no request.
* `tests/internet-declined` refuses `net.internet` and uses the server `https://board.invalid/board`. The leaderboard and the message say that the mod cannot reach the internet.
* `tests/unresolvable` allows both services and leaves the option at `https://example.invalid/board`. The leaderboard and the message say that the server cannot be reached.
* `tests/server-down` uses the server `http://127.0.0.1:47614/board`, where nothing is listening. The leaderboard and the message say that the server cannot be reached.

To run the tests, copy the `tests` folder from the finished mod into `online-leaderboard`, then run the command above. Each test runs the game hidden and silent, and ends with `pass` or `fail` and the path of its run folder.

## Playing the mod

1. Pack the mod.

   ```sh
   lumalink pack online-leaderboard
   ```

1. Drag the `.luma` file onto the game's window, and turn the mod on in the **Mods** page.
1. Allow the permissions the mod asks for, and set **Leaderboard server** to the leaderboard's address.
1. Collect a star and type your name.
1. To see the star's ten fastest times, pause the game and choose **Leaderboard**.

## Next steps

* [About permissions](../concepts/permissions.md)
* [Network service](../reference/services/net.md)
* [Save service](../reference/services/save.md)
