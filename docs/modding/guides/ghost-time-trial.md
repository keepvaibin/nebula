# A ghost time trial with a replay viewer
State: generated from the worked program in the language's acceptance set; its code builds once the modules it uses are merged.

## What it does

Every attempt at a star is recorded (the player's pose every frame). The best time per star is kept; on the next
attempt a translucent Mario runs the best attempt beside the player, with a timer. A screen in the pause menu lists
saved attempts and plays any of them back with a free camera, at chosen speeds.

## What it uses

- `kit.camera`: lumakit/camera.md
- `kit.hud`: lumakit/hud.md
- `kit.menu`: lumakit/menu.md
- `kit.replay`: lumakit/replay.md
- `kit.screen`: lumakit/screen.md
- `svc.draw`: services/draw.md
- `svc.fs`: services/fs.md
- `svc.time`: services/time.md

## The files

```lumascript
use kit.replay
use kit.hud
use kit.menu
use kit.screen
use kit.camera
use svc.fs
use svc.time
use svc.draw

struct Best { frames: u32, file: text }

var bests: map<text, Best> = {}          // "<stage>/<scenario>" -> best
var recorder: replay.Recorder? = none
var ghost: replay.Puppet? = none
var ghost_track: replay.Track? = none
var timer: hud.Timer? = none
var attempt_start: u64 = 0

fn key() -> text { return "{MR::getCurrentStageName()}/{MR::getCurrentScenarioNo()}" }

on start {
    if let data = fs.read("bests.json") { bests = json.decode<map<text, Best>>(data) ?? {} }
    menu.add_pause_entry(text "Menu.Replays", open_viewer)
}

hook after GameScene::startStagePlayFirst() { begin_attempt() }
hook after GameScene::startStagePlayRetry() { begin_attempt() }

fn begin_attempt() {
    recorder = replay.record(replay.What.Player)        // pose, animation, form, every frame
    attempt_start = time.frame()
    timer = hud.timer(hud.Corner.TopRight)
    ghost = none
    if let best = bests.get(key()) {
        if let data = fs.read(best.file) {
            ghost_track = replay.Track.decode(data)
            let p = replay.puppet(model "MarioGhost")      // Mario's model, the ghost's own look
            draw.opacity(p.view(), 0.45)
            ghost = p
        }
    }
}

on frame {
    if let t = timer { t.set(time.frame() - attempt_start) }
    if let p = ghost, let track = ghost_track {
        p.pose(track, (time.frame() - attempt_start) as u32)  // hides itself past the end
    }
}

hook before GameSceneFunction::requestPowerStarGetDemo() {
    guard let rec = recorder else { return }
    let track = rec.stop()
    recorder = none
    let frames = (time.frame() - attempt_start) as u32
    let file = "runs/{key()}/{time.real_date()}.replay"
    fs.write(file, track.encode())
    if bests.get(key())?.frames ?? u32.max > frames {
        bests[key()] = Best { frames, file }
        fs.write("bests.json", json.encode(bests))
    }
}

hook before GameScene::requestStartGameOverDemo() { recorder?.discard(); recorder = none }

fn open_viewer() {
    let files = fs.list("runs", recursive: true)
    screen.list(screen.List {
        title: text "Replays.Title"
        rows: files.map(fn(f: text) -> text { return f })
        on_choose: play_back
    })
}

fn play_back(file: text) {
    guard let data = fs.read(file), let track = replay.Track.decode(data) else {
        screen.message(text "Replays.Unreadable")
        return
    }
    start viewer(track)
}

task viewer(track: replay.Track) {
    let p = replay.puppet(model "MarioGhost")
    let cam = camera.free()                       // a mod camera (kit.camera), moved with the stick
    var frame: f32 = 0.0
    var speed: f32 = 1.0
    let bar = hud.text(hud.Corner.Bottom, "")
    while frame < track.frames as f32 {
        p.pose(track, frame as u32)
        if input.pressed(input.Action.Right) { speed = min(speed * 2.0, 8.0) }
        if input.pressed(input.Action.Left) { speed = max(speed / 2.0, 0.125) }
        if input.pressed(input.Action.Back) { break }
        bar.set("{speed}x  {frame as u32} / {track.frames}")
        frame += speed
        wait 1 frames
    }
    cam.end(); p.remove(); bar.remove()
}
```

Needs: `kit.replay` (record, encode/decode, puppet), `kit.camera.free`, `kit.hud`, `kit.menu.add_pause_entry`,
`kit.screen.list`, `fs`, `time`, `draw.opacity` (draw.inst), `json` (the language's standard module, part C).
Language features used beyond A.1: `guard let ... else`, generics on built-ins (`json.decode<T>`), `?.` and `??`,
`u32.max`, interpolation with calls.

## Check, build, pack, try

```sh
lumalink check <folder>
lumalink build <folder>
lumalink pack <folder>
```

Drop the `.luma` on the game's window, switch it on in the mods screen, and read the mod log for its lines. A finding names its file and line: `format/rules.md` has every code.

## See also

`guides/first-mod.md`, `language/programs.md` (every worked program), `lumakit/overview.md`.
