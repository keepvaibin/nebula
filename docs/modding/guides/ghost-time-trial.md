# A ghost time trial with a replay viewer

Every attempt at a star is timed and recorded: Mario's place, turn and animation at each step of the world. A timer
in the top right corner counts from the moment the stage's play begins until the star is got. The fastest run of
each star is kept in the mod's own files, so it is still there after the game is started again. On the next attempt
at that star a translucent Mario, the game's own Cosmic Mario figure, runs the kept run beside the player, step for
step. Replays, a new line of the pause menu, lists every kept run and plays the one chosen with a free camera, at
1/8x to 8x.

The whole mod is the folder `ghost-time-trial/` beside this page.

## The folder

```text
ghost-time-trial/
  luma.json
  source/main.lum
  layers/base/merge/MessageData/messages.json   the four lines it shows, in the game's 14 languages
  tests/run/test.json                           a timed run, kept, then raced by its ghost
  tests/run-kept/test.json                      the kept run raced after the game starts again
  tests/viewer/test.json                        the pause menu's Replays, played and left
```

The mod has no model of its own: the ghost is the game's.

## 1. Start the folder

```sh
lumalink new ghost-time-trial --id ghost-time-trial --name "Ghost Time Trial" --summary "Times each star; a ghost runs your best time beside you. Replays in the pause menu." --author you
lumalink add need lumakit ghost-time-trial
```

`luma.json` then holds the mod's name and its need of LumaKit. Its `text` block gives the name and the summary in
each of the game's 14 languages (`lumalink add text JpJapanese ghost-time-trial --name ... --summary ...` writes one).

## 2. The lines it shows

`layers/base/merge/MessageData/messages.json` adds four lines to the game's messages: the pause menu's line, the
list's title, and the two things the list can say. `default` is English; the other keys are the game's languages
(`fr` and `es` serve both of their regions).

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

The code names each line as `text "GhostTrial_Replays"`; the game shows it in the player's language.

## 3. The code

Put this in `ghost-time-trial/source/main.lum`:

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
    ghost = replay.player_puppet()                      // the game's translucent Mario, animated as the player was
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
    let p = replay.player_puppet()
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

What each part does:

- The attempt. `startStagePlayFirst` and `startStagePlayRetry` run when a stage starts and when it starts again after
  a miss. They arm the attempt; it begins at the first `GameScene::exeSceneAction`, the game's play, so an opening
  camera or a star's start scene is not timed and skipping one changes nothing. The file select and the observatory
  are not timed.
- The time. `on step` runs once each step of the world, so the timer stops while the game is paused and the time is
  the same at any speed of the machine. The timer is made with `hud.timer`, held (`hud.run_timer(t, false)`) and set
  each step to the attempt's steps. The game's star scene (`requestPowerStarGetDemo`) ends the attempt; a game over
  throws it away.
- The recording. `replay.record(replay.What.Player)` takes Mario's pose each step; `replay.stop_recording` gives
  the track, and `replay.encode` its bytes, which read the same on every platform.
- The files. `fs.write` writes into the mod's own data folder. Each finished run is
  `runs/<stage> <star>/<date> <time> <steps>.replay`; `bests.json` names each star's fastest run. `on start` reads
  `bests.json` back, so a best survives a restart of the game.
- The ghost. `replay.player_puppet()` makes the game's translucent Mario; `replay.pose(p, track, step)` puts it at the
  step the player is at. Past the end of the track it is hidden.
- The viewer. `menu.add_pause_entry` adds Replays to the pause menu. `fs.list` lists the runs, `screen.choose` shows
  them (up and down, A takes one, B backs out). `menu.close_pause()` closes the pause menu, as its own Continue does,
  and the run plays: `camera.free()` takes the camera, the player is held still and hidden, RIGHT and LEFT double and
  halve the speed, B ends it. The bar at the bottom shows the speed and where the run is. The playback holds while
  the pause menu is open; choosing another run or starting a stage ends it (`end_watch`). Watching a run ends the
  attempt in progress; the next starts when a stage starts.

## 4. Build and check

```sh
lumalink build ghost-time-trial
lumalink check ghost-time-trial
```

`check` ends `Errors: 0`. The mod asks for no permission: the data folder is its own.

## 5. Test it

```sh
lumalink test ghost-time-trial
```

- `tests/run`: Good Egg, star 1. The stick walks Mario; at frame 300 the harness calls the star scene. It passes when
  the time is 389 steps (the attempt began 89 frames before the test's frame 0), the timer reads 0:01.48 at frame 0
  and stops at 0:06.48, the run is written and is the best; then, with the stage entered again, the ghost stands
  within 20 units of where Mario stood at three steps of the first run.
- `tests/run-kept`: starts the game again with the files `tests/run` left. It passes when the first attempt has the
  ghost of the kept run.
- `tests/viewer`: makes a run, enters the stage again, opens the pause menu, takes Replays and the one run in the
  list. It passes when the run plays, RIGHT, RIGHT and LEFT give 2x, 4x and 2x, and B ends it.

Each test runs the game hidden and silent and ends `pass` or `fail` with its run folder.

## 6. Play it

```sh
lumalink pack ghost-time-trial
```

Drop `ghost-time-trial.luma` on the game's window and switch it on in the Mods screen. Enter a galaxy: the timer
starts with the play. Get the star; enter the galaxy again and the ghost runs beside you. Pause and choose Replays
to watch any kept run.

## See also

`lumakit/replay.md`, `lumakit/hud.md`, `lumakit/menu.md`, `lumakit/screen.md`, `lumakit/camera.md`,
`services/fs.md`, `services/time.md`, `language/language.md` (`on step`, hooks, tasks), `lumalink/test-harness.md`.
