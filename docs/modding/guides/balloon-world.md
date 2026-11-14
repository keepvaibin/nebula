# A game mode of your own: Balloon World
State: generated from the worked program in the language's acceptance set; its code builds once the modules it uses are merged.

## What it does

A player hides a balloon somewhere in a stage (it is placed on the ground under them and uploaded with the stage and
position); others download balloons for the stage they are in, see a hint, and race to pop one; the time is uploaded.

## What it uses

- `kit.actor`: lumakit/actor.md
- `kit.hud`: lumakit/hud.md
- `kit.menu`: lumakit/menu.md
- `kit.screen`: lumakit/screen.md
- `svc.input`: services/input.md
- `svc.net.http`: services/net.http.md
- `svc.time`: services/time.md

## The files

```lumascript
use svc.net.http
use svc.time
use svc.input
use kit.actor
use kit.hud
use kit.menu
use kit.screen

option server: text

struct Hidden { id: text, stage: text, scenario: int, at: vec3, hint: text, by: text }

let balloon_kind = actor.define(actor.Kind {
    name: "LumaBalloon"
    look: model "Balloon"
    sensors: [actor.Sensor.Body(radius: 80.0)]
    on_touch_player: pop
    idle_action: "Float"
})

var hunting: Hidden? = none
var hunt_start: u64 = 0
var placed: ref<LiveActor>? = none

on start {
    menu.add_pause_entry(text "Balloon.Hide", hide)
    menu.add_pause_entry(text "Balloon.Find", find)
}

fn hide() { start hide_task() }

task hide_task() {
    let player = MR::getPlayerPos()                       // view of the player's position, copied below
    var ground = vec3(0, 0, 0)
    var normal = vec3(0, 0, 0)
    var gravity = vec3(0, -1, 0)
    MR::calcGravityVector(MarioAccess::getPlayerActor(), var gravity, none, none)
    if not MR::getFirstPolyOnLineToMap(var ground, var normal, player, gravity * 1000.0) {
        screen.message(text "Balloon.NoGround")
        return
    }
    let hint = wait input.text(text "Balloon.AskHint", max_chars: 40) ?? ""
    let body = json.encode(Hidden { id: "", stage: MR::getCurrentStageName(), scenario: MR::getCurrentScenarioNo(),
                                    at: ground, hint, by: "" })
    let reply = wait http.post("{server}/hide", body)
    screen.message(if reply.status == 200 { text "Balloon.Hidden" } else { text "Balloon.Failed" })
}

fn find() { start find_task() }

task find_task() {
    let stage = MR::getCurrentStageName()
    let reply = wait http.get("{server}/list?stage={net.url_escape(stage)}&scenario={MR::getCurrentScenarioNo()}")
    guard reply.status == 200, let list = json.decode<list<Hidden>>(reply.body), list.len() > 0 else {
        screen.message(text "Balloon.NoneHere")
        return
    }
    let choice = wait screen.choose(text "Balloon.Pick", list.map(fn(h: Hidden) -> text { return h.hint }))
    guard let i = choice else { return }
    hunting = list[i]
    placed = ref(balloon_kind.spawn(at: list[i].at))       // a mod's actor made mid-stage (idea 194's check)
    hunt_start = time.frame()
    hud.timer(hud.Corner.TopRight).follow(fn() -> u64 { return time.frame() - hunt_start })
}

fn pop(b: LiveActor) {
    guard let h = hunting else { return }
    let frames = time.frame() - hunt_start
    MR::emitEffect(b, "Pop")                               // the balloon model's own effect, by name
    b.kill()
    hunting = none
    start report(h.id, frames as u32)
}

task report(id: text, frames: u32) {
    let reply = wait http.post("{server}/found", json.encode({ "id": id, "frames": frames }))
    if reply.status != 200 { log "balloon: {reply.status}" }
}
```

Needs: `kit.actor.define` with touch callback and `spawn`, raycast `MR::getFirstPolyOnLineToMap` (part C checks the
row), `MR::calcGravityVector` with null pointers as `none`, `screen.choose` (waited), `hud.timer().follow`.

## Check, build, pack, try

```sh
lumalink check <folder>
lumalink build <folder>
lumalink pack <folder>
```

Drop the `.luma` on the game's window, switch it on in the mods screen, and read the mod log for its lines. A finding names its file and line: `format/rules.md` has every code.

## See also

`guides/first-mod.md`, `language/programs.md` (every worked program), `lumakit/overview.md`.
