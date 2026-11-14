# Balloon World

A game mode with two halves. Hide: the clock starts, the player runs somewhere in the stage and puts a balloon on
the ground under Mario, types a hint, and the balloon is sent to a balloon server with its stage, star and
position. Find: the server's balloons for the stage and star the player is in are listed by their hints; the one
chosen stands in the stage, and the player races the clock to touch it and pop it; the time is sent to the server.
The whole mod is the folder `balloon-world/` beside this page.

## 1. Make the folder

```sh
lumalink new balloon-world --id balloon-world --name "Balloon World" --summary "Hide a balloon in a stage for others to find, or find one another player hid, against the clock." --author you
lumalink add need lumakit balloon-world
lumalink add permission net.local balloon-world --why "Talks to a balloon server on this computer or your local network."
lumalink add permission net.internet balloon-world --host "*" --why "Sends the balloons you hide and your times to the balloon server you set, and gets the balloons others hid."
lumalink add option server balloon-world --kind text --max-length 200 --default "" --name "Balloon server"
lumalink add option hide_seconds balloon-world --kind number --default 30 --min 10 --max 120 --step 5 --name "Seconds to hide"
lumalink add option find_seconds balloon-world --kind number --default 60 --min 10 --max 300 --step 10 --name "Seconds to find"
lumalink add option place balloon-world --kind key --default dpad_down --name "Place the balloon"
```

- `net.local` reaches this computer and the local network, `net.internet` any other host: the server is an option,
  so its host is `*`. The player is asked for both.
- `server` is the server's address with no `/` at its end, for example `http://192.168.1.20:18741` or
  `https://balloons.example.org`. The two numbers are the clocks. `place` is a button the player binds in Controls;
  until bound it is the D-pad's down.
- `lumalink add text <language> balloon-world --name ... --summary ...` gives the name and summary in each of the
  game's 14 languages, and the option names go under `text.<language>.options` in `luma.json`.

## 2. The balloon

`tools/make_balloon.py` writes the model from shapes in code:

```sh
python tools/make_balloon.py
```

It writes `layers/base/assets/Balloon.glb`: a red balloon 80 units across on a string, 130 units tall, its origin at
the string's end, with a two-joint skin and a looping clip `Float` that bobs it. The code names it `model "Balloon"`
and starts the clip as its idle action.

## 3. The lines

Every line the mod shows is a row of `layers/base/merge/MessageData/messages.json`, in English and the game's other
languages (`fr`, `es`, `de`, `it`, `nl`, `pt`, `ja`, `ko`, `zh-Hans`, `zh-Hant`):

| row | shown |
|---|---|
| `Balloon_Hide`, `Balloon_Find` | the two pause menu entries |
| `Balloon_HideHow`, `Balloon_AskHint`, `Balloon_NoGround` | the line at the top while hiding, and under it |
| `Balloon_Pick`, `Balloon_NoHint`, `Balloon_Hint` | the list of hints, and the line at the top while finding |
| `Balloon_Sent`, `Balloon_NotSent`, `Balloon_HideTimeUp` | the result screens of a hide |
| `Balloon_Found`, `Balloon_FindTimeUp`, `Balloon_NoneHere`, `Balloon_NoAnswer` | the result screens of a find |
| `Balloon_NoServer` | either entry chosen with no server set |

## 4. The code

Put this in `balloon-world/source/main.lum`:

```lumascript
use svc.net.local as local
use svc.net.internet as internet
use svc.input
use svc.time
use kit.actor
use kit.hud
use kit.menu
use kit.screen

option server: text
option hide_seconds: int
option find_seconds: int

// A hidden balloon as the server keeps it: its star, where it stands and the hider's hint. The server gives the id.
struct Hidden { id: text = "", stage: text, scenario: int, at: vec3, hint: text }

// A found balloon's time, in world steps (60 a second).
struct Found { id: text, steps: u32 }

enum Mode { Idle, Hiding, Hinting, Finding, Busy }

var mode = Mode.Idle
var kind: actor.KindId? = none
var place: input.Action? = none
var left: u32 = 0                          // world steps left on the clock
var given: u32 = 0                         // the clock's steps at its start
var timer: hud.Timer? = none
var line: hud.Text? = none
var balloon: ref<LiveActor>? = none
var hidden_at = vec3(0.0, 0.0, 0.0)
var hunted: Hidden? = none
var asking: input.Request? = none

on start {
    kind = actor.define(actor.Kind {
        name: "Balloon"
        look: model "Balloon"
        idle_action: "Float"
        sensors: [actor.Sensor.Body(60.0)]
        on_touch_player: touched
    })
    place = input.action("place")
    menu.add_pause_entry(text "Balloon_Hide", hide)
    menu.add_pause_entry(text "Balloon_Find", find)
}

fn two(n: u32) -> text {
    if n < 10 {
        return "0{n}"
    }
    return "{n}"
}

// World steps as minutes, seconds and hundredths.
fn clock(n: u32) -> text {
    let seconds = n / 60
    return "{seconds / 60}:{two(seconds % 60)}.{two((n % 60) * 100 / 60)}"
}

fn star() -> text {
    return MR::getCurrentStageName() ?? ""
}

// The clock and a line at the top of the screen; the clock counts down from `seconds`.
fn show(seconds: int, s: text) {
    clear()
    given = (seconds * 60) as u32
    left = given
    let t = hud.timer(hud.Corner.TopRight, left as u64)
    hud.run_timer(t, false)
    timer = t
    line = hud.text(hud.Corner.Top, s)
}

fn clear() {
    if let t = timer {
        hud.remove_timer(t)
    }
    timer = none
    if let l = line {
        hud.remove_text(l)
    }
    line = none
}

fn remove_balloon() {
    if let r = balloon, let b = r.get() {
        b.kill()
    }
    balloon = none
    count()
}

// The mod's balloons standing in the stage now.
fn count() {
    if let k = kind {
        log fact "balloons" actor.all(k).len()
    }
}

// A result screen; the mode ends once the player closes it.
task outcome(s: text) {
    mode = Mode.Busy
    log fact "result" s
    wait screen.message(s)
    mode = Mode.Idle
}

// ---- hiding: the clock runs; Place puts the balloon on the ground under the player, then a hint is asked and the
// balloon is sent with its star and position

fn hide() {
    if mode != Mode.Idle {
        return
    }
    if server == "" {
        start outcome(text "Balloon_NoServer")
        return
    }
    menu.close_pause()
    mode = Mode.Hiding
    show(hide_seconds, text "Balloon_HideHow")
    log fact "hiding" hide_seconds
}

fn put_balloon() {
    guard let feet = MR::getPlayerPos(), let down = MR::getPlayerGravity() else {
        return
    }
    var ground = vec3(0.0, 0.0, 0.0)
    if not MR::getFirstPolyOnLineToMap(var ground, none, feet - down * 50.0, down * 1000.0) {
        hud.text(hud.Corner.Bottom, text "Balloon_NoGround", 120)
        return
    }
    guard let k = kind, let b = actor.spawn(k, ground) else {
        return
    }
    remove_balloon()
    balloon = ref(b)
    hidden_at = ground
    log fact "placed" ground
    log fact "drop" length(feet - ground)
    count()
    mode = Mode.Hinting
    if let l = line {
        hud.set_text(l, text "Balloon_AskHint")
    }
    asking = input.text_request(input.TextRequest { kind: input.TEXT_ANY, max_length: 40 }, hint_given)
    if asking == none {
        send("")
    }
}

fn hint_given(e: input.TextEvent) {
    asking = none
    var hint = ""
    if e.status == none {
        hint = e.data
    }
    send(hint)
}

fn send(hint: text) {
    clear()
    mode = Mode.Busy
    let h = Hidden { stage: star(), scenario: MR::getCurrentScenarioNo() as int, at: hidden_at, hint: hint }
    start send_task(h)
}

task send_task(h: Hidden) {
    guard let answer = wait request("POST", "/balloons", json.encode(h)), let kept = json.decode<Hidden>(answer) else {
        remove_balloon()
        wait outcome(text "Balloon_NotSent")
        return
    }
    log fact "hidden" kept.id
    wait outcome(text "Balloon_Sent")
}

// ---- finding: the star's balloons from the server, one chosen by its hint, placed in the stage and raced against
// the clock

fn find() {
    if mode != Mode.Idle {
        return
    }
    if server == "" {
        start outcome(text "Balloon_NoServer")
        return
    }
    menu.close_pause()
    mode = Mode.Busy
    start find_task()
}

task find_task() {
    let path = "/balloons?stage={escape(star())}&scenario={MR::getCurrentScenarioNo()}"
    guard let answer = wait request("GET", path, "") else {
        wait outcome(text "Balloon_NoAnswer")
        return
    }
    guard let all = json.decode<list<Hidden>>(answer), all.len() > 0 else {
        wait outcome(text "Balloon_NoneHere")
        return
    }
    log fact "listed" all.len()
    let hints = all.map(fn(h: Hidden) -> text {
        if h.hint == "" {
            return text "Balloon_NoHint"
        }
        return h.hint
    })
    guard let i = wait screen.choose(text "Balloon_Pick", hints), let k = kind else {
        mode = Mode.Idle
        return
    }
    let h = all[i]
    remove_balloon()
    guard let b = actor.spawn(k, h.at) else {
        mode = Mode.Idle
        return
    }
    balloon = ref(b)
    hunted = h
    mode = Mode.Finding
    show(find_seconds, text "Balloon_Hint"(hint: hints[i]))
    log fact "finding" h.id
    log fact "spawned" h.at
    count()
}

fn touched(b: LiveActor) {
    if mode != Mode.Finding {
        return
    }
    guard let h = hunted else {
        return
    }
    let steps = given - left
    MR::startSound(b, "SE_OJ_BIG_BUBBLE_BREAK", -1, -1)
    remove_balloon()
    clear()
    hunted = none
    log fact "found" steps
    mode = Mode.Busy
    start report(Found { id: h.id, steps: steps })
}

task report(f: Found) {
    let sent = wait request("POST", "/found", json.encode(f))
    if sent == none {
        log "the time was not sent"
    }
    wait outcome(text "Balloon_Found"(time: clock(f.steps)))
}

// ---- the clock: world steps, so it stops while the game is paused or a screen holds it

on step {
    if mode != Mode.Hiding and mode != Mode.Finding {
        return
    }
    if left > 0 {
        left -= 1
    }
    if let t = timer {
        hud.set_timer(t, left as u64)
    }
    if left > 0 {
        return
    }
    clear()
    if mode == Mode.Hiding {
        log fact "time_up" "hide"
        start outcome(text "Balloon_HideTimeUp")
    } else {
        remove_balloon()
        hunted = none
        log fact "time_up" "find"
        start outcome(text "Balloon_FindTimeUp")
    }
}

on frame {
    let now = time.now()
    if now.state == time.State.Loading and mode != Mode.Idle and mode != Mode.Busy {
        // The stage ends: what it held goes with it.
        if let r = asking {
            input.text_cancel(r)
        }
        asking = none
        clear()
        balloon = none
        hunted = none
        mode = Mode.Idle
        return
    }
    if mode == Mode.Hiding and now.steps > 0 {
        if let a = place {
            if input.read(a).pressed != 0 {
                put_balloon()
            }
        }
    }
}

// ---- HTTP: the server on this computer or the local network through net.local, anywhere else through
// net.internet; one request at a time. The body of a 200 answer, else none.

var waiting = false
var status: u64 = 0
var body: bytes = b""

task request(method: text, path: text, data: text) -> bytes? {
    while waiting {
        wait 1 frames
    }
    waiting = true
    status = 0
    body = b""
    let url = "{server}{path}"
    var made = false
    if is_local(url) {
        made = local.http(local.HttpRequest {
            method: method
            url: url
            headers: [local.Header { name: "Content-Type", value: "application/json" }]
            body: data.bytes()
            timeout_ms: 10000
            flags: 0
        }, heard_local) != none
    } else {
        made = internet.http(internet.HttpRequest {
            method: method
            url: url
            headers: [internet.Header { name: "Content-Type", value: "application/json" }]
            body: data.bytes()
            timeout_ms: 10000
            flags: 0
        }, heard_internet) != none
    }
    if made {
        wait until not waiting
    }
    waiting = false
    if status != 200 {
        return none
    }
    return body
}

fn heard_local(e: local.Event) {
    match e {
        local.Event.Headers(socket, value, data) => status = value
        local.Event.Data(socket, data) => add(data)
        local.Event.Done(socket, value) => waiting = false
        local.Event.Error(socket, s, value) => {
            status = 0
            waiting = false
        }
        else => return
    }
}

fn heard_internet(e: internet.Event) {
    match e {
        internet.Event.Headers(socket, value, data) => status = value
        internet.Event.Data(socket, data) => add(data)
        internet.Event.Done(socket, value) => waiting = false
        internet.Event.Error(socket, s, value) => {
            status = 0
            waiting = false
        }
        else => return
    }
}

fn add(data: bytes) {
    for i in 0..data.len() {
        body.push(data[i])
    }
}

// The URL's host: this computer, or an address of the local network (net.local's reach).
fn is_local(url: text) -> bool {
    guard let from = url.find("://") else {
        return false
    }
    var host = url.slice(from + 3, url.len())
    if let end = host.find("/") {
        host = host.slice(0, end)
    }
    if host.starts_with("[") {
        let h = host.lower()
        return h.starts_with("[::1]") or h.starts_with("[fc") or h.starts_with("[fd") or h.starts_with("[fe80")
    }
    if let colon = host.find(":") {
        host = host.slice(0, colon)
    }
    if host.lower() == "localhost" {
        return true
    }
    let parts = host.split(".")
    if parts.len() != 4 {
        return false
    }
    guard let a = parts[0].parse_int(), let b = parts[1].parse_int() else {
        return false
    }
    return a == 127 or a == 10 or (a == 172 and b >= 16 and b <= 31) or (a == 192 and b == 168) or (a == 169 and b == 254)
}

// A URL query's value: letters, digits and - . _ ~ as they are, every other byte %XX (RFC 3986).
fn escape(t: text) -> text {
    let hex = "0123456789ABCDEF".bytes()
    let b = t.bytes()
    var out: bytes = b""
    for i in 0..b.len() {
        let c = b[i]
        if (c >= 48 and c <= 57) or (c >= 65 and c <= 90) or (c >= 97 and c <= 122) or c == 45 or c == 46 or c == 95 or c == 126 {
            out.push(c)
        } else {
            out.push(37)
            out.push(hex[(c >> 4) as int])
            out.push(hex[(c & 15) as int])
        }
    }
    return out.text() ?? ""
}
```

- `actor.define` makes the balloon a kind of object: its model, its clip, a body 60 units across that the player
  touches, and `touched` called when he does. `actor.spawn` stands one in the running stage, at the ground under
  Mario when hiding and at the server's position when finding; it falls onto the ground there and stands upright to
  the stage's gravity.
- The ground under Mario is `MR::getFirstPolyOnLineToMap`: a line from 50 units above his feet, 1000 units along his
  gravity. With no ground (over a pit) the balloon is not placed and the clock runs on.
- `menu.add_pause_entry` adds the two entries to the pause menu. The clock starts there but runs only in play: it
  counts world steps (`on step`), which stop while the game is paused or a screen holds it.
- The hint is typed with the system keyboard (`input.text_request`); Enter sends it, Escape sends the balloon with
  no hint.
- `screen.choose` lists the hints; `screen.message` shows each result until A or B.
- `request` sends one HTTP request at a time through `net.local` when the server is on this computer or the local
  network, through `net.internet` otherwise, and gives the body of a 200 answer.
- Leaving the stage during a hide or a find ends it.

## 5. The server

The mod talks JSON over HTTP to the address in `server`:

| request | body | answer (200) |
|---|---|---|
| `POST /balloons` | `{"id": "", "stage": "EggStarGalaxy", "scenario": 1, "at": [x, y, z], "hint": "..."}` | the balloon kept, with its `id` |
| `GET /balloons?stage=<stage>&scenario=<star>` | none | a list of balloons in the same form |
| `POST /found` | `{"id": "<id>", "steps": <world steps, 60 a second>}` | anything |

Any other answer counts as a failure: the result screen says so.

## 6. Test it

`tests/server/` is a second mod that is such a server, on this computer's port 18741: it starts with one balloon
ahead of Good Egg Galaxy's start, keeps what it is sent while the game runs, and writes each request it takes as
facts. Each test gives the mod `"server": "http://127.0.0.1:18741"`, so no test reaches the internet.

- `tests/hide`: Mario walks ahead, Hide a balloon is chosen, he walks on and presses Place. It passes when the
  balloon stands on the ground under him, the server got Good Egg Galaxy, star 1, a position on the start's path and
  the typed hint, the mod got the id back, and the result screen says the balloon is hidden.
- `tests/hide-time-up`: 10 seconds to hide and Place never pressed: the result screen says time is up and nothing
  reaches the server.
- `tests/find`: Find a balloon asks for this stage and star, the hint is chosen, the balloon stands where the server
  said, Mario walks into it: it is gone, the server got its id and the time, and the result screen shows the time.
- `tests/find-time-up`: 10 seconds to find and Mario stays at the start: the balloon goes, the result screen says
  time is up and no time is sent.

```sh
lumalink test balloon-world
```

Not yet: the line at the top does not show the hint while it is typed (`input.text_peek` cannot be called from
LumaScript yet).

## 7. Play it

```sh
lumalink pack balloon-world
```

Drop `balloon-world.luma` on the game's window, switch it on, allow what it asks for and set Balloon server on the
Mods screen. In a stage, press + and choose Hide a balloon or Find a balloon, then go back to the game: the clock
starts.

## See also

`lumakit/actor.md`, `lumakit/menu.md`, `lumakit/screen.md`, `lumakit/hud.md`, `services/net.md` (`net.local`,
`net.internet`, HTTP), `services/input.md` (actions, text entry), `format/luma.md` (permissions, options),
`lumalink/test-harness.md`.
