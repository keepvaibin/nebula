# Online: a leaderboard
State: generated from the worked program in the language's acceptance set; its code builds once the modules it uses are merged.

## What it does

Each star's time (from A.2's kind of timing, here its own) is sent to a leaderboard server; the player chooses a name
once (text entry); a screen shows the top ten for the current star and the player's place.

`luma.json`: `"permissions": ["net.http"]`, option `server` (text, default `https://example.invalid/board`).

## What it uses

- `kit.menu`: lumakit/menu.md
- `kit.screen`: lumakit/screen.md
- `svc.input`: services/input.md
- `svc.net.http`: services/net.http.md
- `svc.save`: services/save.md
- `svc.time`: services/time.md

## The files

```lumascript
use svc.net.http
use svc.input
use svc.save
use svc.time
use kit.screen
use kit.menu

option server: text

struct Entry { name: text, frames: u32 }
var name: text? = none
var started: u64 = 0

on start {
    name = save.global_text("name")
    menu.add_pause_entry(text "Menu.Leaderboard", show_board)
}

hook after GameScene::startStagePlayFirst() { started = time.frame() }

hook before GameSceneFunction::requestPowerStarGetDemo() {
    start submit("{MR::getCurrentStageName()}/{MR::getCurrentScenarioNo()}", (time.frame() - started) as u32)
}

task submit(star: text, frames: u32) {
    if name == none {
        let chosen = wait input.text(text "Board.AskName", max_chars: 16)   // the platform's keyboard
        guard let n = chosen else { return }                                // the player cancelled
        name = n
        save.set_global_text("name", n)
    }
    let reply = wait http.post("{server}/submit", json.encode({ "star": star, "name": name!, "frames": frames }),
                               headers: { "Content-Type": "application/json" })
    if reply.status != 200 { log "leaderboard: {reply.status} {reply.text()}" }
}

fn show_board() { start board("{MR::getCurrentStageName()}/{MR::getCurrentScenarioNo()}") }

task board(star: text) {
    let s = screen.list(screen.List { title: text "Board.Title", rows: [text "Board.Loading"] })
    let reply = wait http.get("{server}/top?star={net.url_escape(star)}")
    guard reply.status == 200, let top = json.decode<list<Entry>>(reply.body) else {
        s.set_rows([text "Board.Unreachable"])
        return
    }
    s.set_rows(top.indexed().map(fn(p: (int, Entry)) -> text {
        return "{p.0 + 1}. {p.1.name}  {format_time(p.1.frames)}"
    }))
}

fn format_time(frames: u32) -> text {
    let cs = frames * 100 / 60
    return "{cs / 6000}:{pad2(cs / 100 % 60)}.{pad2(cs % 100)}"
}
fn pad2(n: u32) -> text { return if n < 10 { "0{n}" } else { "{n}" } }
```

Needs: `http.get/post` (waited), `net.url_escape`, `input.text` (waited, cancellable), `save.global_text`,
`kit.screen.list` with rows updated, tuples `(int, Entry)` with `.0 .1` (in the definition: section 3 is amended to
list tuples, finding P-1 below).

## Check, build, pack, try

```sh
lumalink check <folder>
lumalink build <folder>
lumalink pack <folder>
```

Drop the `.luma` on the game's window, switch it on in the mods screen, and read the mod log for its lines. A finding names its file and line: `format/rules.md` has every code.

## See also

`guides/first-mod.md`, `language/programs.md` (every worked program), `lumakit/overview.md`.
