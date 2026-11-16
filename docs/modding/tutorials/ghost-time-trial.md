---
title: 'Racing a ghost: a time trial with a replay viewer'
shortTitle: Ghost time trial
intro: Time and record every attempt at a star, race a ghost of your best run, and watch saved runs from the pause menu.
contentType: tutorials
example: ghost-time-trial
---

## Introduction

In this tutorial, you make a time trial mod. Every attempt at a star is timed and recorded: Mario's position, rotation and animation at each step of the world.

* A timer in the top-right corner counts from the moment play begins in a stage until Mario collects the star.
* The fastest run for each star is saved in the mod's own files, so it is kept after the game restarts.
* On the next attempt at that star, a ghost follows the saved run beside the player, step for step. The ghost is the game's own Cosmic Mario model, drawn solid and in a single pose with its arms out, because it does not play animations.
* A new **Replays** entry in the pause menu lists every saved run and plays the one you choose with a free camera, at speeds from 1/8x to 8x.

You learn how to:

* Time an attempt with world steps, so the time is the same on any computer and stops while the game is paused.
* Record and play back the player's movement with LumaKit's `replay` module.
* Save and read files in the mod's own data folder with the `fs` service.
* Take control of the camera with the `camera` module.

## Prerequisites

* Nebula, installed with Nebula Setup.
* LumaLink. For more information, see [Installing LumaLink](../get-started/installing-lumalink.md).
* We recommend that you complete [Making a game mode: Balloon World](balloon-world.md) first.

## Mod folder layout

When you finish, the mod folder contains these files:

```text
ghost-time-trial/
  luma.json
  source/main.lum
  layers/base/merge/MessageData/messages.json   the four lines it shows, in the game's 14 languages
  tests/run/test.json                           a timed run, kept, then raced by its ghost
  tests/run-kept/test.json                      the kept run raced after the game starts again
  tests/viewer/test.json                        the pause menu's Replays, played and left
```

The mod has no model of its own. The ghost uses the game's Cosmic Mario model.

## Creating the mod

Create the mod folder, and add LumaKit to the mod's needs.

```sh
lumalink new ghost-time-trial --id ghost-time-trial --name "Ghost Time Trial" --summary "Times each star; a ghost runs your best time beside you. Replays in the pause menu." --author YOUR-NAME
lumalink add need lumakit ghost-time-trial
```

To add the name and summary in another language, run `lumalink add text LANGUAGE ghost-time-trial --name NAME --summary SUMMARY`.

## Adding the mod's text

Create `layers/base/merge/MessageData/messages.json`. It adds four lines to the game's text: the pause menu entry, the title of the list of runs, and the two messages the list can show. `default` is English, and the other keys are the game's languages. `fr` and `es` are used for both regions of each language.

```json
{
  "lines": {
    "GhostTrial_Replays": { "default": "Replays", "fr": "Rediffusions", "ja": "リプレイ", "...": "..." },
    "GhostTrial_Title": { "default": "Saved runs", "...": "..." },
    "GhostTrial_None": { "default": "No saved runs yet.", "...": "..." },
    "GhostTrial_Unreadable": { "default": "This run cannot be read.", "...": "..." }
  }
}
```

The code refers to each line by name, for example `text "GhostTrial_Replays"`, and the game shows it in the player's language.

## Writing the code

Create `ghost-time-trial/source/main.lum` with this code:

```lumascript
use kit.replay
use kit.hud
use kit.menu
use kit.screen
use kit.camera
use svc.fs
use svc.time

// A star's best run: its length in world steps and the file that holds it.
struct Best { steps: u32, file: text }

var bests: map<text, Best> = {}            // "<stage> <scenario>" -> the best run
var armed = false                          // a stage started: the attempt begins with its play
var running = false                        // an attempt is being timed
var steps: u32 = 0                         // world steps since the attempt began
var star = ""                              // the attempt's star
var recorder: replay.Recorder? = none
var ghost: replay.Puppet? = none
var ghost_track: replay.Track? = none
var timer: hud.Timer? = none
var in_pause = false
var watching = false                       // a run is being played back
var watch_no: u32 = 0                      // the playback that is on; a new one or a stage start ends it
var view_puppet: replay.Puppet? = none
var view_camera: camera.Control? = none
var view_bar: hud.Text? = none
var view_track: replay.Track? = none

// The viewer's speeds, slowest first, and how the bar shows them.
let speeds: list<f32> = [0.125, 0.25, 0.5, 1.0, 2.0, 4.0, 8.0]
let speed_names: list<text> = ["1/8x", "1/4x", "1/2x", "1x", "2x", "4x", "8x"]

fn star_now() -> text {
    return "{MR::getCurrentStageName() ?? ""} {MR::getCurrentScenarioNo()}"
}

fn two(n: u32) -> text {
    if n < 10 {
        return "0{n}"
    }
    return "{n}"
}

// World steps as minutes, seconds and hundredths, at 60 steps a second.
fn clock(n: u32) -> text {
    let seconds = n / 60
    return "{seconds / 60}:{two(seconds % 60)}.{two((n % 60) * 100 / 60)}"
}

on start {
    menu.add_pause_entry(text "GhostTrial_Replays", open_viewer)
    start load_bests()
}

task load_bests() {
    if let data = wait fs.read("bests.json") {
        bests = json.decode<map<text, Best>>(data) ?? {}
    }
}

// The attempt left behind: its recording thrown away, its ghost and timer gone.
fn end_attempt() {
    if let r = recorder {
        replay.discard(r)
    }
    recorder = none
    if let p = ghost {
        replay.remove(p)
    }
    ghost = none
    if let t = ghost_track {
        replay.free(t)
    }
    ghost_track = none
    if let t = timer {
        hud.remove_timer(t)
    }
    timer = none
    running = false
    armed = false
}

// A stage starts, or starts again after a miss: the attempt begins when its play does (after the opening camera and
// the star's start), so a skipped opening changes nothing.
hook after GameScene::startStagePlayFirst() {
    end_watch()
    end_attempt()
    armed = a_star_stage()
}

hook after GameScene::startStagePlayRetry() {
    end_watch()
    end_attempt()
    armed = a_star_stage()
}

// A galaxy with a player in it: not the file select, not the observatory.
fn a_star_stage() -> bool {
    if watching or MR::isStageFileSelect() or MR::isStageAstroLocation() {
        return false
    }
    return MarioAccess::getPlayerActor() != none
}

hook before GameScene::exeSceneAction() {
    if armed {
        armed = false
        begin_attempt()
    }
}

fn begin_attempt() {
    star = star_now()
    recorder = replay.record(replay.What.Player)        // the player's pose, every world step
    steps = 0
    running = true
    let t = hud.timer(hud.Corner.TopRight)
    hud.run_timer(t, false)                             // it shows the attempt's steps, set each step
    timer = t
    log fact "attempt" star
    if let best = bests.get(star) {
        start load_ghost(best.file)
    }
}

task load_ghost(file: text) {
    guard let data = wait fs.read(file), let track = replay.decode(data) else {
        return
    }
    if not running {
        replay.free(track)
        return
    }
    ghost_track = track
    ghost = replay.puppet(model "GhostMario")           // the game's Cosmic Mario model (ObjectData/GhostMario)
    log fact "ghost" track.frames
}

// One step of the world's play: the ghost at the step the player is at, the timer on by one. The steps the game runs
// with its own pause menu open are not counted.
on step(now: time.Now) {
    if not running or now.state != time.State.Play {
        return
    }
    if let p = ghost, let t = ghost_track {
        replay.pose(p, t, steps)                        // hidden past the end of the best run
    }
    steps += 1
    if let t = timer {
        hud.set_timer(t, steps as u64)
    }
}

hook before GameSceneFunction::requestPowerStarGetDemo() {
    guard let r = recorder else {
        return
    }
    let track = replay.stop_recording(r)
    recorder = none
    running = false
    log fact "time" steps
    start keep(star, track, steps)
}

hook before GameScene::requestStartGameOverDemo() {
    end_attempt()
}

// The finished run written to its own file; the star's best when it is faster than the one kept.
task keep(of: text, track: replay.Track, taken: u32) {
    let w = time.wall()
    let file = "runs/{of}/{w.year}-{two(w.month as u32)}-{two(w.day as u32)} {two(w.hour as u32)}.{two(w.minute as u32)}.{two(w.second as u32)} {taken}.replay"
    let data = replay.encode(track)
    replay.free(track)
    guard let written = wait fs.write(file, data) else {
        log "could not write {file}"
        return
    }
    log fact "kept" file
    if let old = bests.get(of) {
        if old.steps <= taken {
            return
        }
    }
    bests[of] = Best { steps: taken, file: file }
    guard let saved = wait fs.write("bests.json", json.encode(bests).bytes()) else {
        log "could not write bests.json"
        return
    }
    log fact "best" taken
}

hook after PauseMenu::appear() {
    in_pause = true
    log fact "paused" true
}

hook before PauseMenu::kill() {
    in_pause = false
    log fact "paused" false
}

fn open_viewer() {
    start choose_run()
}

// One row of the list: the star, the time, the day and hour it was run.
fn row_of(path: text) -> text {
    let parts = path.split("/")
    if parts.len() != 2 {
        return path
    }
    let name = parts[1].split(" ")
    if name.len() != 3 {
        return path
    }
    let taken = name[2].slice(0, name[2].len() - 7).parse_int() ?? 0
    let hour = name[1].split(".")
    if hour.len() != 3 {
        return path
    }
    return "{parts[0]}   {clock(taken as u32)}   {name[0]} {hour[0]}:{hour[1]}:{hour[2]}"
}

task choose_run() {
    let listing = wait fs.list("runs", 1) ?? ""
    var files: list<text> = []
    var rows: list<text> = []
    for line in listing.split("\n") {
        if line.ends_with(".replay") {
            files.push(line)
            rows.push(row_of(line))
        }
    }
    log fact "runs_listed" files.len()
    if files.len() == 0 {
        wait screen.message(text "GhostTrial_None")
        return
    }
    guard let i = wait screen.choose(text "GhostTrial_Title", rows) else {
        return
    }
    guard let data = wait fs.read("runs/{files[i]}"), let track = replay.decode(data) else {
        wait screen.message(text "GhostTrial_Unreadable")
        return
    }
    log fact "chosen" files[i]
    menu.close_pause()
    wait until not in_pause
    end_watch()
    start watch(track)
}

// The run played back: the camera free (the stick moves and turns it, 1 and 2 raise and lower it), right and left
// change the speed, B ends it. Watching ends the attempt in progress. While the pause menu is open it holds still.
task watch(track: replay.Track) {
    end_attempt()
    watch_no += 1
    let mine = watch_no
    watching = true
    MR::offPlayerControl()
    MR::hidePlayer()
    let p = replay.puppet(model "GhostMario")
    let bar = hud.text(hud.Corner.Bottom, "")
    view_puppet = p
    view_camera = camera.free()
    view_bar = bar
    view_track = track
    var at: f32 = 0.0
    var speed = 3
    log fact "watching" true
    while watch_no == mine and at < track.frames as f32 {
        if not in_pause {
            if MR::isPressedFire(0) {
                break
            }
            if MR::isPressedRight(0) and speed < speeds.len() - 1 {
                speed += 1
                log fact "speed" speed_names[speed]
            }
            if MR::isPressedLeft(0) and speed > 0 {
                speed -= 1
                log fact "speed" speed_names[speed]
            }
            replay.pose(p, track, at as u32)
            hud.set_text(bar, "{speed_names[speed]}   {clock(at as u32)} / {clock(track.frames)}")
            at += speeds[speed]
        }
        wait 1 frames
    }
    if watch_no == mine {
        end_watch()
    }
}

// The playback ended: its figure, camera and bar gone, the player back. Nothing when none is on.
fn end_watch() {
    if not watching {
        return
    }
    watching = false
    watch_no += 1
    if let b = view_bar {
        hud.remove_text(b)
    }
    view_bar = none
    if let c = view_camera {
        camera.end(c)
    }
    view_camera = none
    if let p = view_puppet {
        replay.remove(p)
    }
    view_puppet = none
    if let t = view_track {
        replay.free(t)
    }
    view_track = none
    MR::showPlayer()
    MR::onPlayerControl(true)
    log fact "watching" false
}
```

### Timing an attempt

`startStagePlayFirst` and `startStagePlayRetry` run when a stage starts, and when it starts again after Mario loses a life. They prepare the attempt, which begins at the first call to `GameScene::exeSceneAction`, when play begins. An opening camera or a star's introduction is not timed, so skipping one does not change the time. The file select screen and the observatory are not timed.

`on step` runs once for each step of the world. The timer stops while the game is paused, and the time is the same on any computer. The timer is created with `hud.timer`, stopped with `hud.run_timer(t, false)`, and set to the attempt's step count at each step. The game's star cutscene (`requestPowerStarGetDemo`) ends the attempt. A game over discards it.

### Recording and saving runs

`replay.record(replay.What.Player)` records Mario's pose at each step. `replay.stop_recording` returns the recorded track, and `replay.encode` returns its bytes, which read the same on every platform.

`fs.write` writes to the mod's own data folder. Each finished run is saved as `runs/STAGE STAR/DATE TIME STEPS.replay`, and `bests.json` names the fastest run for each star. `on start` reads `bests.json`, so the best runs are kept when the game restarts.

### Showing the ghost

`replay.puppet(look)` creates a figure from a model. Here the model is the game's Cosmic Mario (`ObjectData/GhostMario`), referred to as `model "GhostMario"`. `replay.pose(p, track, step)` moves the figure to where the player was at that step, facing the same way. The figure does not play Mario's animations, so it moves along the run in a single pose. After the end of the track, the figure is hidden.

### Watching saved runs

`menu.add_pause_entry` adds **Replays** to the pause menu. `fs.list` lists the saved runs, and `screen.choose` shows them. The player moves through the list with up and down, chooses a run with <kbd>A</kbd>, and goes back with <kbd>B</kbd>.

`menu.close_pause()` closes the pause menu in the same way as its own **Continue** entry, and the run plays. `camera.free()` takes control of the camera, and the player character is held still and hidden. Right on the D-pad doubles the speed, left halves it, and <kbd>B</kbd> stops the playback. A bar at the bottom of the screen shows the speed and the position in the run.

Playback pauses while the pause menu is open. Choosing another run or starting a stage ends it (`end_watch`). Watching a run also ends the attempt in progress, and the next attempt begins when a stage starts.

## Building and checking the mod

```sh
lumalink build ghost-time-trial
lumalink check ghost-time-trial
```

The last line of the `check` output is `Errors: 0`. The mod asks for no permissions, because its data folder belongs to the mod.

## Testing the mod

The finished mod includes three tests:

* `tests/run` plays Good Egg Galaxy, star 1. The control stick walks Mario, and at frame 300 the test calls the star cutscene. The test passes when the time is 389 steps (the attempt began 89 frames before the test's frame 0), the timer reads 0:01.48 at frame 0 and stops at 0:06.48, and the run is saved as the best run. Then the test enters the stage again and passes when the ghost is within 20 units of Mario's position at three steps of the first run.
* `tests/run-kept` starts the game again with the files that `tests/run` saved. The test passes when the first attempt shows the ghost of the saved run.
* `tests/viewer` records a run, enters the stage again, opens the pause menu, chooses **Replays**, and chooses the run in the list. The test passes when the run plays, pressing right, right and left gives speeds of 2x, 4x and 2x, and <kbd>B</kbd> stops the playback.

To run the tests, copy the `tests` folder from the finished mod into `ghost-time-trial`, then run:

```sh
lumalink test ghost-time-trial
```

Each test runs the game hidden and silent, and ends with `pass` or `fail` and the path of its run folder.

## Playing the mod

1. Pack the mod.

   ```sh
   lumalink pack ghost-time-trial
   ```

1. Drag the `.luma` file onto the game's window, and turn the mod on in the **Mods** page.
1. Enter a galaxy. The timer starts when play begins.
1. Collect the star, then enter the galaxy again. The ghost runs beside you.
1. To watch a saved run, pause the game and choose **Replays**.

## Next steps

* [Connecting to LiveSplit](livesplit-link.md)
* [LumaKit: replay](../reference/lumakit/replay.md)
* [File system service](../reference/services/fs.md)
