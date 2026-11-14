# An online leaderboard

State: written, not yet proven by a test.

Each star's time is sent to a leaderboard server under a name the player types once. Leaderboard, a new line of the
pause menu, shows the server's ten fastest times for the star being played and the player's place. The server is an
option: any server that answers the two requests in part 4, on the internet, on the local network or on this
computer. The whole mod is the folder `online-leaderboard/` beside this page.

## The folder

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

## 1. Start the folder

```sh
lumalink new online-leaderboard --id online-leaderboard --name "Online Leaderboard" --summary "Sends each star's time to a leaderboard server under the name you type, and shows the server's top ten from the pause menu." --author you
lumalink add need lumakit online-leaderboard --version ">=1.0.0"
lumalink add permission net.internet online-leaderboard --host "*" --why "Sends your star times to the leaderboard server you choose and reads its top ten."
lumalink add permission net.local online-leaderboard --why "Reaches a leaderboard server on this computer or your local network."
lumalink add option server online-leaderboard --kind text --default "https://example.invalid/board" --max-length 256 --name "Leaderboard server"
```

The mod asks for two permissions, and the player may refuse either one. `net.internet` with the host `*` reaches any
public address; `net.local` reaches this computer and the local network. The code imports both as optional, so a
refused one leaves the mod running without it.

The option `server` is the leaderboard's address. Its default is under `.invalid`, a name that never has an address,
so the mod sends nothing anywhere until the player sets a server.

`luma.json`'s `text` block gives the mod's name, its summary and the option's name in each of the game's languages.
`lumalink add text JpJapanese online-leaderboard --name ... --summary ...` writes the name and the summary of one; the
option's name goes into the same block by hand, as `"options": { "server": { "name": "..." } }`.

## 2. The lines it shows

`layers/base/merge/MessageData/messages.json` adds the mod's lines to the game's messages: the pause menu's line, the
board's titles, the name prompt, and what the mod says when a time is not sent. `default` is English; the other keys
are the game's languages (`fr` and `es` serve both of their regions). A line with `{place}` takes a value from the
code.

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

The code names each line as `text "Board.Menu"`, or `text "Board.TitlePlace"(place: 3)` for one with a value.

## 3. The code

Put this in `online-leaderboard/source/main.lum`:

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

What each part does:

- The time. `startStagePlayFirst` and `startStagePlayRetry` run when a stage starts and when it starts again after a
  miss. They arm the attempt in a galaxy (not the file select, not the observatory); it begins at the first
  `GameScene::exeSceneAction`, the game's play, so the opening camera is not timed. `time.now().step` counts the
  world's steps, which stop while the game is paused. The star's "got it" scene (`requestPowerStarGetDemo`) ends the
  attempt and sends its steps; a game over throws it away. The star is the stage's name and the star's number,
  as `EggStarGalaxy/1`.
- The name. The first time a star is sent with no name, `input.text_request` opens the platform's text entry (the
  keyboard or input method on a computer, the system keyboard on an Android phone) and `hud.text` shows the prompt over the game with what
  is typed so far (`input.text_peek`). Enter takes the name; Esc, Android's Back or a lost window focus takes none,
  and that star's time is not sent. The
  name is kept with `save.global`, so it is asked once, not once a game file.
- Sending. `submit` posts the time as JSON. When a time is not sent, `say` shows why with `screen.message`, which
  stays until the player presses A or B. A HUD line holds at most 64 characters, so `hud.text` keeps only the short
  name prompt.
- The board. `menu.add_pause_entry` adds Leaderboard to the pause menu. `screen.list` shows the board with
  Loading... while the server answers, then the ten times and the player's place in its title. When the request
  fails, `screen.message` says why. B closes it.
- Which service. `local_url` reads the address's host: `localhost`, `127.x.x.x`, `10.x.x.x`, `172.16.x.x` to
  `172.31.x.x`, `192.168.x.x`, `169.254.x.x`, `[::1]`, `[fc..]`, `[fd..]`, `[fe80..]` and those as `[::ffff:...]` go
  through `net.local`, any other host through `net.internet`. These are the addresses each service takes; neither
  stands in for the other, so a refused `net.local` is never tried through `net.internet`. A refused service is an
  empty slot (`local == none`), and the mod says which reach the player did not allow. This computer's own public
  address is not in that list: it goes to `net.internet`, which refuses it as a local address.

## 4. The server it talks to

Any HTTP server that answers these two requests. `{server}` is the option, without a final `/`.

| request | body sent | answer |
|---|---|---|
| `POST {server}/submit` | `{"star": "EggStarGalaxy/1", "name": "Mario", "frames": 689}`, `Content-Type: application/json` | 200 and any body |
| `GET {server}/top?star=EggStarGalaxy%2F1&name=Mario` | none | 200 and `{"top": [{"name": "Rosalina", "frames": 6}, ...], "place": 11}` |

`frames` are world steps at 60 a second. `top` is the star's best times, fastest first; the board shows the first
ten. `place` is the name's place by its best time, 0 when it has none. Any other status, or no answer within 10
seconds, is shown as the reason.

## 5. Build and check

```sh
lumalink build online-leaderboard
lumalink check online-leaderboard
```

`check` ends `Errors: 0`.

## 6. Test it

A test must not reach the internet. `tests/board/server/` is a second small mod that listens on `127.0.0.1:47613`
through `net.local` and answers as part 4 says. It holds ten times for Good Egg's first star, Rosalina's 0.10 seconds
to Spike's 1.00, and writes each request it gets as facts. The tests set the option `server` to
`http://127.0.0.1:47613/board`; the tests of the internet path use names under `.invalid`, which never have an
address, so no test connects to a host on the internet.

```sh
lumalink test online-leaderboard
```

- `tests/board`: Good Egg, star 1. The harness calls the star scene at frame 600; the prompt opens, `Marix`,
  Backspace, `o` and Enter type Mario, and the server gets the star, the name and a time of more than 600 steps as
  JSON. In the galaxy again, PLUS and A on Leaderboard show the ten times with `Your place: 11` in the title, and B
  closes it. A second star is sent under Mario with no prompt.
- `tests/no-name`: Esc on the prompt. Nothing is sent and the message says no name was typed; the next
  star asks again.
- `tests/local-declined`: `net.local` refused, `net.internet` allowed. The board and the message say the
  mod may not reach this computer, and the server gets no request.
- `tests/internet-declined`: `net.internet` refused, the server `https://board.invalid/board`. The board and the line
  over the game say the mod may not reach the internet.
- `tests/unresolvable`: both allowed and the option left at `https://example.invalid/board`. The board and the line
  say the server cannot be reached.
- `tests/server-down`: the server `http://127.0.0.1:47614/board`, where nothing listens. The board and the line say
  the server cannot be reached.

Each test runs the game hidden and silent and ends `pass` or `fail` with its run folder.

Not yet: LumaLink does not build `local == none` on an optional service or `input.text_peek`, so the mod does not
build until the compiler takes both.

## 7. Play it

```sh
lumalink pack online-leaderboard
```

Drop `online-leaderboard.luma` on the game's window, switch it on in the Mods screen, allow what it asks for and set
Leaderboard server to the board's address. Get a star and type your name. Pause and choose Leaderboard to see the
star's top ten.

## See also

`services/net.md` (`net.local`, `net.internet`, `http`), `services/input.md` (`text_request`), `services/save.md`
(`save.global`), `lumakit/menu.md`, `lumakit/screen.md`, `lumakit/hud.md`, `format/luma.md` (permissions,
options, text), `lumalink/test-harness.md`, `guides/livesplit-link.md`.
