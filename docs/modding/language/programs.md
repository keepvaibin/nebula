# LumaScript: worked programs

## More rules

The definition is accepted when every reference mod of phase 9 and twenty
ideas drawn at random from the idea corpus are written out in it with nothing missing. This file holds those
programs. Each is complete as code; the content it names (`model "..."`, `effect "..."`) is the mod's own, made by
LumaLink `add` from sources, and each `luma.json` is given where options or permissions matter.

What "nothing missing" is checked against.

---------------------------------------------------------------------------------------------------------------------

### A.1 Gold Flower

Picking the flower turns Mario gold (a real gold material, an aura).
A spin throws a gold ball (the SM3DW fireball model). A ball that hits an enemy defeats it with a gold flash and
coins. Damage ends the form.

`luma.json` (the parts that matter here):

```json
{ "format": "1.0", "id": "gold-flower", "version": "1.0.0",
  "options": [ { "id": "balls_at_once", "type": "number", "min": 1, "max": 4, "default": 2 },
               { "id": "flash_frames", "type": "number", "min": 5, "max": 60, "default": 20 } ],
  "needs": [ { "id": "lumakit", "version": "^1" } ] }
```

`source/main.lum`:

```lumascript skip a listing from the part's contract, not a module that builds alone
use kit.player.form
use kit.item
use kit.projectile
use svc.draw

option balls_at_once: int
option flash_frames: int

const BALL_SPEED = 35.0          // game units per frame
const BALL_LIFE = 180            // frames

pub let gold = form.define(form.Form {
    id: "gold"
    look: model "GoldMario"                  // bound to Mario's skeleton (LumaLink add character)
    aura: effect "GoldAura"                  // kept on the player while the form lasts
    ends_on_damage: true
    on_spin: throw_ball
})

let flower = item.define(item.Item {
    id: "GoldFlower"                         // placeable in a stage by this name, and given by code
    look: model "GoldFlower"
    shine: effect "GoldFlowerShine"
    sound_get: sound "GetGold"
    gives: item.Gives.Form(gold)
})

var balls: list<ref<LiveActor>> = []

fn throw_ball(player: MarioActor) {
    balls.retain(fn(b: ref<LiveActor>) -> bool { return not b.empty() })
    if balls.len() >= balls_at_once { return }
    var front = vec3(0, 0, 0)
    MR::getPlayerFrontVec(var front)
    let up = player.mGravity * -1.0
    let ball = projectile.throw(projectile.Shot {
        look: model "GoldBall"
        from: player.mPosition + front * 80.0 + up * 60.0
        velocity: front * BALL_SPEED
        bounces: 3
        life_frames: BALL_LIFE
        trail: effect "GoldBallTrail"
        burst: effect "GoldBallBurst"
        sound_throw: sound "ThrowGold"
        on_hit: hit
    })
    balls.push(ball)
}

fn hit(shot: projectile.Hit) -> projectile.Then {
    if not MR::sendMsgEnemyAttackFire(shot.sensor, shot.target) {
        return projectile.Then.Bounce                     // a wall, or something that is not hurt by fire
    }
    let enemy = shot.target.mHost
    start flash(ref(enemy))
    MR::appearCoinPop(enemy, enemy.mPosition, 3)
    return projectile.Then.Burst
}

task flash(e: ref<LiveActor>) {                         // draw.inst (DECISIONS 3)
    for f in 0..flash_frames {
        let fade = 1.0 - f as f32 / flash_frames as f32
        if let a = e.get() { draw.tint(a, multiply: #FFFFFF, add: #FFD700 * fade) } else { stop }
        wait 1 frames
    }
    if let a = e.get() { draw.untint(a) }
}
```

What the language needed and has: closures (`retain`), a game field through a view (`player.mGravity`,
`shot.target.mHost`), out parameter (`var front`), handles across frames, a task, content names checked at build.
The material, the size check (a glTF metre is 100 units) and the skeleton binding are content and LumaLink's, not
code.

### A.2 Ghost time trial with replay viewer

Every attempt at a star is recorded (the player's pose every frame). The best time per star is kept; on the next
attempt a translucent Mario runs the best attempt beside the player, with a timer. A screen in the pause menu lists
saved attempts and plays any of them back with a free camera, at chosen speeds.

```lumascript skip a listing from the part's contract, not a module that builds alone
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

### A.3 LiveSplit link

Starts, splits, pauses game time during loads, and resets, through LiveSplit's server (TCP 16834 on the same machine,
or its named pipe `LiveSplit` on Windows). A split on every star or grand star got.

```lumascript skip a listing from the part's contract, not a module that builds alone
use svc.net
use svc.time
use svc.system

option port: int                  // default 16834
option use_pipe: bool             // Windows: LiveSplit's pipe instead of TCP

var link: net.Stream? = none
var running = false

on start { connect() }

fn connect() {
    link = if use_pipe and system.platform() == system.Platform.Windows {
        net.pipe_open("LiveSplit")                      // net.ipc (DECISIONS 3)
    } else {
        net.tcp_connect("127.0.0.1", port as u16)
    }
}

on net.closed(s, why) {
    if same_stream(s, link) { link = none; log "LiveSplit link closed: {why}; retrying in 5 s"; start retry() }
}
task retry() { wait 5 seconds; connect() }

fn say(line: text) { link?.send("{line}\r\n".bytes()) }

hook after GameScene::startStagePlayFirst() {
    if not running { say("starttimer"); running = true }
}
on time.loading_started { if running { say("pausegametime") } }
on time.loading_ended { if running { say("unpausegametime") } }
hook before GameSceneFunction::requestPowerStarGetDemo() { if running { say("split") } }
hook before GameSceneFunction::requestGrandStarGetDemo() { if running { say("split") } }
hook before GameSceneFunction::requestStaffRoll() { if running { say("split"); running = false } }
on option port { link?.close(); connect() }
```

Needs: `net.tcp_connect`, `net.pipe_open` (net.ipc), stream `send`/`close`, events `net.closed`; `time`'s loading
events; `system.platform`.

### A.4 Online leaderboard

Each star's time (from A.2's kind of timing, here its own) is sent to a leaderboard server; the player chooses a name
once (text entry); a screen shows the top ten for the current star and the player's place.

`luma.json`: `"permissions": ["net.http"]`, option `server` (text, default `https://example.invalid/board`).

```lumascript skip a listing from the part's contract, not a module that builds alone
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

### A.5 Balloon World

A player hides a balloon somewhere in a stage (it is placed on the ground under them and uploaded with the stage and
position); others download balloons for the stage they are in, see a hint, and race to pop one; the time is uploaded.

```lumascript skip a listing from the part's contract, not a module that builds alone
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

### A.6 A new galaxy with its own enemy, boss, cutscene and music

The stage itself (collision, placement, zones, cameras, gravity, lights, sky) is content: glTF scenes and JSON
placement in `layers/`, cooked on the device. The code below is the enemy, the boss, the cutscene and the galaxy's
registration.

```lumascript skip a listing from the part's contract, not a module that builds alone
use kit.stage
use kit.actor
use kit.cutscene
use kit.hud
use svc.audio

let galaxy = stage.define_galaxy(stage.Galaxy {
    name: "LumaNebulaGalaxy"                  // the stage folder in layers/
    title: text "Galaxy.Name"
    dome: stage.Dome.Kitchen                  // which observatory dome lists it
    unlock: stage.Unlock.Stars(20)
    stars: [ stage.Star { scenario: 1, title: text "Star1" },
             stage.Star { scenario: 2, title: text "Star2", comet: none } ]
    music: sound "NebulaTheme"                // a stream (OGG) in layers/
})

// The enemy: a spiked orb that rolls toward the player and is defeated by a spin or a stomp.
class Spikeroll : LiveActor {
    var speed: f32 = 0.0
    override fn init(iter: TableIter) {
        initModelManagerWithAnm("Spikeroll", none, false)  // the mod's model, placed by name in the stage JSON
        actor.add_body(self, radius: 90.0)                   // kit.actor: sensor + binder + shadow in one call
        makeActorAppeared()
    }
    override fn attackSensor(mine: HitSensor, other: HitSensor) {
        if MR::isSensorPlayer(other) { MR::sendMsgEnemyAttack(other, mine) }
    }
    override fn receiveMsgPlayerAttack(msg: u32, other: HitSensor, mine: HitSensor) -> bool {
        if MR::isMsgPlayerSpinAttack(msg) or MR::isMsgPlayerHipDrop(msg) {
            MR::emitEffect(self, "Burst")
            MR::appearCoinPop(self, self.mPosition, 2)
            kill()
            return true
        }
        return false
    }
}

states Rolling for Spikeroll {
    state Wait { update { if MR::isNearPlayer(self, 1200.0) { go Chase } } }
    state Chase {
        enter { MR::startAction(self, "Roll") }
        update {
            self.speed = min(self.speed + 0.2, 9.0)
            actor.move_toward_player(self, self.speed)       // along the surface, under gravity
            if not MR::isNearPlayer(self, 2000.0) { go Slow }
        }
    }
    state Slow {
        update { self.speed *= 0.9; actor.move_forward(self, self.speed); if self.speed < 0.1 { go Wait } }
    }
    start Wait
}

// The boss: three hits, a health meter, a phase change, its own music.
class OrbKing : LiveActor {
    var health: int = 3
    var meter: hud.Meter? = none
    override fn init(iter: TableIter) {
        initModelManagerWithAnm("OrbKing", none, false)
        actor.add_body(self, radius: 300.0)
        makeActorAppeared()
    }
    override fn receiveMsgPlayerAttack(msg: u32, other: HitSensor, mine: HitSensor) -> bool {
        if not MR::isMsgPlayerHipDrop(msg) or not isState(OrbKingStates.Dizzy) { return false }
        health -= 1
        meter?.set(health)
        go OrbKingStates.Hurt
        return true
    }
}

states OrbKingStates for OrbKing {
    state Intro {
        enter {
            wait cutscene.play(cutscene.Scene {                 // kit.cutscene: shots, lines, a wait for the player
                shots: [ cutscene.Shot.Look(at: self, from_offset: vec3(0, 400, 900), frames: 120),
                         cutscene.Shot.Line(speaker: self, line: text "Boss.Greeting") ]
                letterbox: true
            })
            meter = hud.meter(hud.Corner.Top, max: 3, label: text "Boss.Name")
            audio.play_music(sound "BossTheme")
            go Attack
        }
    }
    state Attack {
        enter { MR::startAction(self, "Slam") }
        wait 90 frames
        actor.shockwave(self, radius: 1500.0, effect: effect "Shockwave")   // a ring the player must jump
        go Dizzy
    }
    state Dizzy { enter { MR::startAction(self, "Dizzy") } wait 180 frames; go Attack }
    state Hurt {
        enter { MR::startAction(self, "Hurt"); MR::emitEffect(self, "Hit") }
        wait 60 frames
        if self.health <= 0 { go Defeated } else { go Attack }
    }
    state Defeated {
        enter {
            meter?.remove()
            wait cutscene.play(cutscene.Scene { shots: [cutscene.Shot.Look(at: self, from_offset: vec3(0, 200, 700),
                                                                              frames: 90)] })
            audio.stop_music(fade_frames: 60)
            stage.appear_star(self.mPosition, scenario: 1)
            kill()
        }
    }
    start Intro
}
```

Needs: `K.derive` with overrides of `init`, `attackSensor`, `receiveMsgPlayerAttack` (the rows exist; part C checks
that they are virtual); `states` reading `isState`; `kit.stage.define_galaxy`, `appear_star`; `kit.actor` helpers;
`kit.cutscene.play` (waited); `kit.hud.meter`; `audio.play_music` / `stop_music`.

### A.7 The changes nebula-coop makes, one by one

Not yet written: nebula-coop (github NebulaCoop/nebula-coop, a host-authoritative online fork)
has no copy on this machine. Its changes must be listed from its source, each as a hook or call that exists,
before this section can be written. Open item P-2 (part D).

### A.8 Two unrelated mods together

A.1 (Gold Flower) and A.3 (LiveSplit link) loaded together: they hook no common function, share no file, and their
ids, options and content keys differ. A second pair that does touch a common point: A.2 (ghosts) and A.4
(leaderboard) both hook `GameSceneFunction::requestPowerStarGetDemo` as `before` hooks; neither skips, so both run in
load order and each sees the call. The language guarantees nothing more is needed: hooks of different mods compose by
the kernel's order, and a `replace` held by two would be refused at load naming both (the case the combination test
of phase 9 shows, with a third mod written to conflict).

---------------------------------------------------------------------------------------------------------------------

### B. Twenty ideas drawn at random

Drawn by `random.Random(20261111).sample(sorted(rows), 20)` over ideas.md version 2's 198 rows:
11, 19, 29, 30, 37, 41, 47, 65, 69, 74, 81, 89, 93, 114, 118, 122, 123, 150, 167, 193. Row 81 is Balloon World (A.5).

### B.11 Moon jump (Gecko)

```lumascript skip a listing from the part's contract, not a module that builds alone
use svc.input
on frame {
    let p = MarioAccess::getPlayerActor()
    if input.held(input.Pad.A) and not MR::isOnGroundPlayer() {
        p.mVelocity = p.mGravity * -12.0          // straight up, against this place's gravity
    }
}
```

### B.19 Ice breath: a new attack that freezes enemies

```lumascript skip a listing from the part's contract, not a module that builds alone
use kit.player.form
use kit.projectile
use svc.input

let breath = form.define(form.Form {
    id: "ice-breath"
    look: model "IceMario"
    item: none                                   // given by the mod's own flower, placed like any item
    on_action: input.Action.Attack               // B, or the key bound in the controls menu
    on_act: blow
    timer_frames: 1200
})

fn blow(player: MarioActor) {
    var front = vec3(0, 0, 0)
    MR::getPlayerFrontVec(var front)
    for i in 0..5 {                              // a cone: five short-lived puffs
        let side = cross(front, player.mGravity) * ((i - 2) as f32 * 0.15)
        projectile.throw(projectile.Shot {
            look: none, trail: effect "IceBreath"
            from: player.mPosition + front * 60.0, velocity: (front + side) * 18.0
            life_frames: 25, bounces: 0
            on_hit: fn(h: projectile.Hit) -> projectile.Then {
                MR::sendMsgEnemyAttackFreeze(h.sensor, h.target)   // the game's own freeze message
                return projectile.Then.Burst
            }
        })
    }
}
```

### B.29 Swimming in lava (a new liquid)

```lumascript skip a listing from the part's contract, not a module that builds alone
use kit.area

// A new area kind, placed in stages like the game's own areas; inside it the player swims, and burns slowly.
let lava = area.define(area.Kind { name: "LumaLavaSwimArea", shape_from_placement: true })

hook after MR::isInWater(at: vec3) -> inside {          // the game asks this to decide swimming
    if not inside and lava.contains(at) { result = true }
}

var burn = 0
on frame {
    if MR::isPlayerSwimming() and lava.contains(MR::getPlayerPos()) {
        burn += 1
        if burn % 120 == 0 { MarioAccess::getPlayerActor().decLife(1) }
    } else { burn = 0 }
}
```

`MR::isInWater` has two overloads; the hook names the one taking only a position by its parameter list. Water's
look on the surface comes from the area's own model in the mod's stage files (content).

### B.30 Spin and pointer from the start (Gecko)

```lumascript skip a listing from the part's contract, not a module that builds alone
hook after GameScene::start() {                          // a file is loaded and a stage has begun
    MR::onGameEventFlagEnableToSpinAndStarPointer()
}
```

### B.37 Hard mode

```lumascript skip a listing from the part's contract, not a module that builds alone
option factor: int        // 2 by default; 1 to 3
wrap MarioActor::decLife(amount) {
    amount = wrap(amount as u32 * factor as u32 as u16)  // u16 kept on purpose
    proceed()
}
```

### B.41 A new NPC with dialogue, its own materials and textures

```lumascript skip a listing from the part's contract, not a module that builds alone
use kit.actor
use kit.talk

let keeper = actor.define(actor.Kind {
    name: "LumaKeeper"                     // placed in stages by name
    look: model "Keeper"                   // its glTF materials and textures, cooked on the device
    idle_action: "Wait"
    sensors: [actor.Sensor.Body(radius: 70.0)]
    on_talk: chat
})

fn chat(self: LiveActor) {
    start chat_task(ref(self))
}

task chat_task(k: ref<LiveActor>) {
    let answer = wait talk.ask(k, text "Keeper.Hello", [text "Keeper.Yes", text "Keeper.No"])
    match answer {
        0 => wait talk.say(k, text "Keeper.Thanks")
        else => wait talk.say(k, text "Keeper.Bye")
    }
}
```

### B.47 Destructible terrain

```lumascript skip a listing from the part's contract, not a module that builds alone
use kit.actor

class Crumble : LiveActor {
    var hits: int = 0
    override fn init(iter: TableIter) {
        initModelManagerWithAnm("Crumble", none, false)
        actor.add_collision(self, "Crumble")     // the model's own collision, from the glTF's collision mesh
        makeActorAppeared()
    }
    override fn receiveMsgPlayerAttack(msg: u32, other: HitSensor, mine: HitSensor) -> bool {
        if not MR::isMsgPlayerSpinAttack(msg) { return false }
        hits += 1
        MR::startAction(self, "Crack{hits}")     // its own clips: Crack1, Crack2
        if hits >= 3 {
            MR::invalidateCollisionParts(self)
            MR::emitEffect(self, "Break")
            kill()
        }
        return true
    }
}
```

### B.65 Archipelago multiworld

```lumascript skip a listing from the part's contract, not a module that builds alone
use svc.net.websocket
use svc.save
use svc.time

option server: text           // "wss://archipelago.gg:38281"
option slot: text

var ws: websocket.Socket? = none
var checked: set<text> = {}

on start { start connect() }

task connect() {
    ws = wait websocket.open(server)
    ws?.send_text(json.encode([{ "cmd": "Connect", "game": "Super Mario Galaxy", "name": slot,
                                  "items_handling": 7, "version": { "major": 0, "minor": 5, "build": 0,
                                  "class": "Version" }, "tags": [], "uuid": "", "password": "" }]))
}

on websocket.received_text(s, msg) {
    guard let packets = json.decode<list<map<text, json.Value>>>(msg) else { log "AP: unreadable"; return }
    for p in packets {
        if p["cmd"]?.text() == "ReceivedItems" {
            for item in p["items"]?.list() ?? [] { grant(item["item"]?.int() ?? 0) }
        }
    }
}

hook before GameSceneFunction::requestPowerStarGetDemo() {
    let id = "{MR::getCurrentStageName()}/{MR::getCurrentScenarioNo()}"
    if checked.insert(id) {
        ws?.send_text(json.encode([{ "cmd": "LocationChecks", "locations": [location_id(id)] }]))
    }
}

fn grant(item: int) {
    match item {
        1 => MR::incPlayerLeft()                                  // a 1-up
        2 => MR::incPlayerLife(1)
        3 => GameDataFunction::addStockedStarPiece(50)
        else => log "AP: item {item} has no grant yet"
    }
}

fn location_id(star: text) -> int { return hash32(star) as int }   // the APWorld's table in the real mod
```

### B.69 Christmas decorations on 25 December

`luma.json`: `"layers": [ { "name": "base" }, { "name": "xmas", "when": { "code": true } } ]`.

```lumascript skip a listing from the part's contract, not a module that builds alone
use svc.system
on layers -> list<text> {                    // once a start; a changed answer starts the game again once
    let d = system.local_date()
    return if d.month == 12 and d.day == 25 { ["xmas"] } else { [] }
}
```

### B.74 Gravity flip on a button

```lumascript skip a listing from the part's contract, not a module that builds alone
use kit.gravity
use svc.input

let flip = input.action("FlipGravity", default: input.Pad.Two)   // listed in the controls menu
var flipped: gravity.Override? = none

on frame {
    if input.pressed(flip) {
        if let f = flipped { f.end(); flipped = none }
        else {
            let down = MarioAccess::getPlayerActor().mGravity
            flipped = gravity.override_player(gravity.Parallel { direction: down * -1.0 })
        }
    }
}
```

### B.81 Balloon World

Written as A.5.

### B.89 Split-screen local co-op

```lumascript skip a listing from the part's contract, not a module that builds alone
use kit.player.character
use kit.camera
use svc.draw
use svc.input

// The second player is a second instance of the game's own player, updated with the player globals switched
// around it (the sm64coopdx method, idea 195); kit.player.character gives that instance and its pad.
let second = character.second_player(character.Second { look: model "LuigiCoop", pad: input.pad(2) })
let view = draw.view(draw.View {                         // draw.view (DECISIONS 3)
    camera: camera.follow(second.view(), distance: 1200.0)
    region: draw.Region { x: 0.5, y: 0.0, w: 0.5, h: 1.0 }  // right half
})
draw.set_main_region(draw.Region { x: 0.0, y: 0.0, w: 0.5, h: 1.0 })
```

### B.93 A 3D minimap rendered from above

```lumascript skip a listing from the part's contract, not a module that builds alone
use kit.hud
use svc.draw

let map_view = draw.view(draw.View {
    camera: draw.ViewCamera.Above(of: fn() -> vec3 { return MR::getPlayerPos() }, height: 6000.0, ortho_width: 8000.0)
    target: draw.Target.Texture(256, 256)
})
let panel = hud.picture(hud.Corner.BottomRight, map_view.texture(), size: vec2(200, 200))
```

### B.114 Voice-acted dialogue

```lumascript skip a listing from the part's contract, not a module that builds alone
use svc.audio

hook after TalkMessageCtrl::startTalk() -> started {
    if not started { return }
    let id = self.getMessageID()                 // the message being shown
    if let voice = sound_named("voice/{id}") {   // content in layers/, one file per message id (optional)
        audio.play(voice, bus: audio.Bus.Voice)
    }
}
hook after TalkMessageCtrl::endTalk() -> ended { if ended { audio.stop_bus(audio.Bus.Voice) } }
```

`sound_named` is the content lookup by a name made at run time (part C): it gives `none` when the mod has no such
sound, which the build cannot check, so it is the one content call whose name is not checked when building.

### B.118 Karaoke lyrics on the staff roll

```lumascript skip a listing from the part's contract, not a module that builds alone
use svc.draw
use svc.time

struct Line { at: f32, words: text }      // seconds from the staff roll's start
let lines: list<Line> = json.decode<list<Line>>(data "lyrics.json")!

var started: f64? = none
hook before GameScene::exeStaffRoll() { if started == none { started = time.game_seconds() } }

on draw draw.Point.HudAfter {            // a 2D drawing point (draw)
    guard let s = started else { return }
    let t = (time.game_seconds() - s) as f32
    if let l = lines.last_where(fn(l: Line) -> bool { return l.at <= t }) {
        draw.text(l.words, at: vec2(0.5, 0.88), align: draw.Align.Center, font: font "Lyrics", size: 36.0,
                  colour: #FFFFFF)
    }
}
```

### B.122 Hide the HUD (Gecko)

```lumascript skip a listing from the part's contract, not a module that builds alone
option hide: bool
hook before CounterLayoutController::appear() { if hide { skip } }    // every counter the HUD brings in
hook before CoinCounter::appear() { if hide { skip } }
```

A change of the option applies from the next time the HUD appears (a counter already shown stays until it goes):
the game has no "hide every counter now" function, and the language does not invent one. Finding P-10: say in the
mod's option text that it applies from the next appearance, or find the HUD's own hide path in 1.x.

### B.123 A custom title screen and logo

No code. `layers/base/files/LayoutData/Title.arc/...` replaced by pictures (PNG sources at the game's texture paths)
and `layers/base/patches/LayoutData/Title.arc/Title.brlyt.json` moving panes by key (3.8's layout patch).

### B.150 Cloud save sync

`luma.json`: permissions `net` (hosts: the mod's server) and `save.game` (DECISIONS 3).

```lumascript skip a listing from the part's contract, not a module that builds alone
use svc.save.game
use svc.net.http

option server: text
option token: text            // the player's own key on the sync server

hook after GameSequenceFunction::startSaveAllUserFileSequence() { start upload() }

task upload() {
    wait until not GameSequenceFunction::isActiveSaveDataHandleSequence()
    if not GameSequenceFunction::isSuccessSaveDataHandleSequence() { return }
    let bytes = game.export()                          // save.game: the save files as bytes
    let reply = wait http.put("{server}/save", bytes, headers: { "Authorization": "Bearer {token}" })
    if reply.status != 200 { log "sync: upload {reply.status}" }
}

on start { start download() }

task download() {
    let reply = wait http.get("{server}/save", headers: { "Authorization": "Bearer {token}" })
    if reply.status == 200 and reply.body != game.export() {
        game.offer_import(reply.body, why: text "Sync.Newer")   // the player confirms in the game's screen
    }
}
```

### B.167 Rumble on hits and landings

```lumascript skip a listing from the part's contract, not a module that builds alone
hook after Mario::checkDamage() -> hit { if hit { MR::tryRumblePadStrong(MarioAccess::getPlayerActor(), 0) } }
hook after Mario::doLanding() { MR::tryRumblePadWeak(MarioAccess::getPlayerActor(), 0) }
```

The game's own rumble calls, through the host's pads: no new part.

### B.193 A video on a screen inside a galaxy

```lumascript skip a listing from the part's contract, not a module that builds alone
use kit.actor
use svc.draw
use svc.assets

let screen_kind = actor.define(actor.Kind { name: "LumaScreen", look: model "Screen" })
let film = assets.movie(movie "Trailer")                 // the mod's WebM, decoded by the game's movie player
let tex = draw.texture(film.width(), film.height())      // a texture from pixels, updated each frame

on frame {
    if film.next_frame() { tex.update(film.pixels()) }   // whole frame; a sub-rectangle update also exists
    for s in screen_kind.all() { draw.swap_texture(s, material: "ScreenFace", texture: tex) }   // draw.inst
}
```

---------------------------------------------------------------------------------------------------------------------

### C. What the programs use beyond the language

Game names. Nothing missing.

LumaKit and services. Every name the programs use, by the module or service that must give it (generated from the code
blocks).

| Module or service | Names the programs use |
|---|---|
| `kit.actor` | `Kind`, `Sensor`, `add_body`, `add_collision`, `define`, `move_forward`, `move_toward_player`, `shockwave` |
| `kit.area` | `Kind`, `define` |
| `kit.camera` | `follow`, `free` |
| `kit.cutscene` | `Scene`, `Shot`, `play` |
| `kit.gravity` | `Override`, `Parallel`, `override_player` |
| `kit.hud` | `Corner`, `Meter`, `Timer`, `meter`, `picture`, `text`, `timer` |
| `kit.item` | `Gives`, `Item`, `define` |
| `kit.menu` | `add_pause_entry` |
| `kit.player.character` | `Second`, `second_player` |
| `kit.player.form` | `Form`, `define` |
| `kit.projectile` | `Hit`, `Shot`, `Then`, `throw` |
| `kit.replay` | `Puppet`, `Recorder`, `Track`, `What`, `puppet`, `record` |
| `kit.screen` | `List`, `choose`, `list`, `message` |
| `kit.stage` | `Dome`, `Galaxy`, `Star`, `Unlock`, `appear_star`, `define_galaxy` |
| `kit.talk` | `ask`, `say` |
| `standard module json` | `Value`, `decode`, `encode` |
| `svc.assets` | `movie` |
| `svc.audio` | `Bus`, `play`, `play_music`, `stop_bus`, `stop_music` |
| `svc.draw` | `Align`, `Point`, `Region`, `Target`, `View`, `ViewCamera`, `opacity`, `set_main_region`, `swap_texture`, `text`, `texture`, `tint`, `untint`, `view` |
| `svc.fs` | `list`, `read`, `write` |
| `svc.input` | `Action`, `Pad`, `action`, `held`, `pad`, `pressed`, `text` |
| `svc.net` | `Stream`, `closed`, `event closed`, `http`, `pipe_open`, `tcp_connect`, `url_escape`, `websocket` |
| `svc.net (http)` | `get`, `post`, `put` |
| `svc.net (websocket)` | `Socket`, `event received_text`, `open`, `received_text` |
| `svc.save` | `game`, `global_text`, `set_global_text` |
| `svc.save (save.game)` | `export`, `offer_import` |
| `svc.system` | `Platform`, `local_date`, `platform` |
| `svc.time` | `event loading_ended`, `event loading_started`, `frame`, `game_seconds`, `loading_ended`, `loading_started`, `real_date` |

Besides these, the language's own: `same`, `ref`, `hash32`, `min`/`max`/`cross`, `wrap`, `data "<file>"` (a mod's
data file read at build, checked), `font "..."`, `movie "..."`, `sound_named(...)` (the one run-time content lookup),
`on layers` (the kernel's `layers` call), `on draw <point>`.

### D. Findings against the definition

- P-1: tuples (`(int, Entry)`, `.0`, `.1`) are used by `indexed()` and are not in section 3. Add them.
- P-2: nebula-coop's source is not on this machine; A.7 waits for a survey of its changes.
- P-3: `guard let x = ... else { }` is used in A.2, A.4, A.5 and is not in section 4. Add it.
- P-4: `color * f32` and `color + color` (A.1) are not in section 3's operators. Add them.
- P-5: `same_stream(a, b)` in A.3 shows that identity of service handles (streams) needs `same`; section 3 gives
  `same` for views only. Extend it to handles.
- P-6: `wait <expression>` that yields a value (`let reply = wait http.get(...)`) is used everywhere and section 11.1
  lists only `wait event`. Define `wait` on a pending result as an expression whose value is the result.
- P-7: overrides in a `class` (A.6) call the game's own virtuals by their names with the base's signature; section 5.5
  should say the parameter types may be omitted when the override's name has one virtual.
- P-8: a game function returning `Vec3f *` (`MR::getPlayerPos`) gives a `vec3` copy (B.29, A.5); section 7.2 lists
  pointers to vectors only as `var` arguments. Add results.
- P-9 (against the idea corpus, not the language): idea 167 (rumble) needs no `input.dev`: the game's own
  `MR::tryRumblePad*` calls rumble the player's pad through the host. Row 167 becomes Yes; `input.dev` keeps 119, 166,
  168, 175 (motion, touchpads, HID/MIDI) and rumble beyond the game's patterns.
- P-10: B.122 cannot hide counters already shown without a function the game lacks; the program applies its option
  from the next appearance. Not a language gap; 1.x may find the HUD's own hide path.
- P-11: `on draw <point>`, `on layers`, `data "<file>"`, `font`, `movie`, `sound_named` are used and not in the
  definition (sections 10, 12). Add them.
- P-12:

P-2 (nebula-coop) and P-9, P-10 (not language) stay open here.

---------------------------------------------------------------------------------------------------------------------

### A.7: the changes nebula-coop makes, one by one

Replaces the placeholder A.7 above and closes P-2. Source surveyed: github.com/NebulaCoop/nebula-coop (a fork of the
whole game, 3,399 commits; read on the web, nothing downloaded), its `docs/COOP_PLAN.md` and folder list. A name the
program needs that no row, kit module or service gives is a finding (part D2 below), never written as if it existed.

One mod, `coop`, in several files of one module. Host-authoritative: the host's game decides the shared world; each
guest runs its own game and sends only its own player and its own hits.

```lumascript skip a listing from the part's contract, not a module that builds alone
use svc.net
use svc.save
use svc.time
use svc.draw
use svc.input
use svc.mods
use kit.replay
use kit.actor
use kit.screen
use kit.menu
use kit.hud

const MAX: u32 = 16

struct Peer { id: u8, name: text, character: text, pose: replay.Pose?, puppet: replay.Puppet?, stars: u32 }
struct Room { host: bool, peers: map<u8, Peer>, me: u8, mods: list<text>, cheats: bool, rewind_ms: u32 }

var room: Room? = none
var link: net.Datagrams? = none          // P-13: svc.net has tcp, websocket and http only
var current: LiveActor? = none           // the actor whose movement is running (2.5 below)
```

### A.7.1 Input: the second player's pad and the 2P code are off

nebula-coop removed the Co-Star code and ignores Wii Remote channels 1 to 3. A mod cannot remove code, but it can
make the game never take that path: the Co-Star pointer is never pointing, the 2P manual never opens.

```lumascript skip a listing from the part's contract, not a module that builds alone
hook before MR::isStarPointerPointing2P(a, joint, z, p) -> bool { if room != none { skip false } }
hook before MR::isPlayerPointedBy2POnTriggerButton() -> bool { if room != none { skip false } }
hook before Manual2P::appear() { if room != none { skip } }
```

`input.pad(n)` for n > 0 is simply not used: the remote players' input arrives as poses (A.7.5), not as pads.

### A.7.2 Settings, saves and the menu

```lumascript skip a listing from the part's contract, not a module that builds alone
// The co-op menu instead of the file selector's title step, as nebula-coop does.
hook before FileSelector::exeTitle() {
    if room == none && coop_menu_open {
        skip                                       // the file selector waits while the co-op screens run
    }
}

fn open_coop_menu() {
    screen.choose(screen.Choice {
        title: text "Coop.Title"
        rows: [text "Coop.Host", text "Coop.Join", text "Coop.Character", text "Coop.Back"]
        on_choose: fn(i: int) { match i { 0 => host(), 1 => join(), 2 => pick_character(), _ => {} } }
    })
}

// Each room plays on its own save set, kept apart from the player's own files.
fn use_room_saves(id: text) { save.use_set("coop/{id}") }     // P-17: svc.save has no save sets

// The character a player chose; the game's one question about it.
hook after MR::isPlayerLuigi() -> luigi {
    if let r = room { result = r.peers[r.me].character == "luigi" }
}
```

Settings (`coop_name`, `coop_character`, port, timeout) are the mod's options (luma.json `options`, 6.3), shown by
the game's mod settings page; no code.

### A.7.3 Room mods

A host's room names the mods it plays with; a guest lacking one is offered it.

```lumascript skip a listing from the part's contract, not a module that builds alone
fn join_room_mods(r: Room) {
    for id in r.mods {
        if !mods.installed(id) {
            // P-18: a mods service that fetches a .luma, runs the validator on it, and asks the player, naming
            // its permissions (a mod with `native` code above all), before it is installed for this room only.
            let offer = wait mods.offer_for_session(id, from: host_address())
            if !offer.accepted { leave(text "Coop.ModRefused"); return }
        }
    }
    wait mods.use_for_session(r.mods)           // P-18: the set for one start, the player's list untouched
}
```

### A.7.4 The online pause menu, galaxy select and Quit

```lumascript skip a listing from the part's contract, not a module that builds alone
// The world does not stop for one player: while the pause menu is up, the scene still moves.
hook after GameScene::exePauseMenu() {
    if room != none { self.exeSceneAction() }   // written, not proven: the 1.x run shows whether the scene's
}                                               // action may run under the pause menu (P-21)

// Galaxy select and the grand stars only when the room allows cheats.
hook before GameSceneFunction::requestShowGalaxyMap() { if let r = room, !r.cheats { skip } }

menu.add_pause_entry(text "Coop.Quit", fn() { leave(text "Coop.YouLeft") })

on frame {
    if let r = room, time.real_seconds() - last_heard > 10.0 { leave(text "Coop.TimedOut") }
}
```

### A.7.5 Remote players

nebula-coop draws each remote player with a GhostPlayer and a MarioAnimator, lit as Mario is. `kit.replay`'s puppet
is that: Mario's model and animations, posed from data, drawn in the player's light group.

```lumascript skip a listing from the part's contract, not a module that builds alone
fn pose_peer(p: Peer, pose: replay.Pose) {
    let puppet = p.puppet ?? replay.puppet(model p.character == "luigi" ? "Luigi" : "Mario")
    puppet.set_pose(pose)                       // P-19: one pose, not only a track's frame
    p.puppet = puppet
}

on frame {
    guard let r = room, let l = link else { return }
    let mine = replay.current_pose(replay.What.Player)   // P-19
    l.send_all(encode_pose(r.me, mine))
}
```

### A.7.6 Snapshots

```lumascript skip a listing from the part's contract, not a module that builds alone
// The host sends the shared world once a frame after the scene has moved; a guest applies what it receives.
hook after GameScene::update() {
    guard let r = room, let l = link else { return }
    if r.host { l.send_all(world_snapshot()) }
    for packet in l.received() { apply(packet) }
}
```

### A.7.7 Chat, nametags, the player list, pop-ups, the star counter

```lumascript skip a listing from the part's contract, not a module that builds alone
on draw draw.Point.HudAfter {
    guard let r = room else { return }
    for (id, p) in r.peers {
        guard let pose = p.pose, id != r.me else { continue }
        var at: vec2 = vec2(0, 0)
        if MR::calcScreenPosition(var at, pose.position + vec3(0, 180, 0)) {
            draw.text(p.name, at, draw.Align.Center)
        }
    }
}

hook after CounterLayoutController::control() {          // the room's stars next to the player's own
    if let r = room { star_line.set("{r.peers.values().sum(fn(p: Peer) -> u32 { return p.stars })}") }
}
```

Chat is `input.text` and `hud.text`; the player list a `screen.list`; pop-ups `hud.text` with a time.

### A.7.8 Cutscenes and demos follow the host

```lumascript skip a listing from the part's contract, not a module that builds alone
hook before GameScene::stageClear() { if let r = room, !r.host { if !host_said("clear") { skip } } }
hook before GameSceneFunction::requestPowerStarGetDemo() { if let r = room, r.host { tell_all("star") } }
hook before GameSequenceProgress::requestGalaxyMove(arg) {
    if let r = room {
        if r.host { tell_all("move {arg.mStageName} {arg.mScenarioNo}") }
        else if !host_said("move") { skip }      // a guest moves only when the host does
    }
}
```

### A.7.9 Boss phases follow the host

```lumascript skip a listing from the part's contract, not a module that builds alone
// One hook per phase step; the host tells its phase, a guest whose boss is in another phase is set to the host's.
hook after DinoPackunVs1::exeBattleLv2() { phase(self, "DinoPackunVs1", "BattleLv2") }
hook after BossKameckVs1::exeBattleLv2() { phase(self, "BossKameckVs1", "BattleLv2") }
hook after PoltaActionSequencer::exeBattleLv2() { phase(self, "PoltaActionSequencer", "BattleLv2") }
hook after BossStinkBugActionSequencer::exeFlyHigh() { phase(self, "BossStinkBugActionSequencer", "FlyHigh") }
// ... the same for each exe the four classes have (rows: 12, 12, 13 and 15 functions)

fn phase(boss: NerveExecutor, class: text, step: text) {
    guard let r = room else { return }
    if r.host { tell_all("phase {class} {step}") }
    else if let want = host_phase(class), want != step {
        boss.setNerve(nerve "{class}.{want}")      // P-14: nerves by name; only `const Nerve *` addresses exist
    }
}
```

### A.7.10 Hits between players, knockback, the bubble on death, rewind

```lumascript skip a listing from the part's contract, not a module that builds alone
hook before MR::isPlayerDead() -> dead {
    if let r = room, in_bubble { skip false }    // a player waiting in a bubble is not dead to the game
}
hook before MarioActor::forceKill(how) {
    if let r = room, any_peer_alive() { enter_bubble(); skip }
}
hook after MarioActor::receiveMsgPush(from, to) -> taken {
    if room != none && is_peer_sensor(from) { knockback(from) }
}
```

`rewind_ms` (late hits are judged at the time they were sent) is the host's own bookkeeping of poses by time: mod
code, no game part.

### A.7.11 Races and the shared comet clock

```lumascript skip a listing from the part's contract, not a module that builds alone
hook before RaceManager::startRace() { if let r = room, !r.host, !host_said("race") { skip } }
hook after RaceManager::goalRace() { if let r = room { tell_all("goal {r.me} {time.frame()}") } }
hook before Syati::exeStart() { if let r = room, !r.host, !host_said("race") { skip } }
hook after CometEventExecutorTimeLimit::getTimeLimit() -> limit {
    if let r = room, !r.host { result = host_time_left() }
}
```

### A.7.12 The shared world: objects, hits and stars; switches stay local

```lumascript skip a listing from the part's contract, not a module that builds alone
// Every object the host has is named the same on every machine by its placement.
fn object_id(a: LiveActor) -> actor.PlacementKey { return actor.placement(a) }  // P-15: stable placement ids

hook after LiveActor::makeActorDead() {
    if let r = room, r.host { tell_all("dead {object_id(self)}") }
}
fn apply_dead(key: actor.PlacementKey) { if let a = actor.find_placed(key) { a.makeActorDead() } }   // P-15

// The rabbits' chase is shared (nebula-coop's DemoRabbit, RunawayRabbitCollect); switches are each player's own:
// SwitchArea and ClipFieldSwitch are not hooked.
hook after RunawayRabbitCollect::exeActive() { if let r = room, r.host { tell_all(rabbit_state(self)) } }
hook before DemoRabbit::exeGoal() { if let r = room, !r.host, !host_said("rabbit") { skip } }
```

### A.7.13 Each enemy chases its nearest player

nebula-coop added `MR::getTargetPlayerPos` and changed the single-player calls to it. As a mod: know which actor is
moving, and answer the player questions for that actor with the nearest player.

```lumascript skip a listing from the part's contract, not a module that builds alone
wrap every override of LiveActor::movement() {
    current = self
    proceed()
    current = none
}

hook after MR::getPlayerPos() -> p {
    if let r = room, let a = current { result = nearest_player(r, a.mPosition) }   // P-20: a changed pointer result
}
hook after `MR::calcDistanceToPlayer(const LiveActor *)`(a) -> d {
    if let r = room { result = length(nearest_player(r, a.mPosition) - a.mPosition) }
}
hook after `MR::isNearPlayer(const LiveActor *, f32)`(a, near) -> yes {
    if let r = room { result = length(nearest_player(r, a.mPosition) - a.mPosition) < near }
}
// ... the same for the 43 MR::getPlayer*, calcDistanceToPlayer* and isNearPlayer* rows that answer "where is the
// player" (the list is generated from the rows by name, not written by hand)
```

### A.7.14 Versions, fingerprints, the network layer

The room's handshake compares the mod's version, `time.game_version()`, and each room mod's id and version
(`mods.installed(id)?.version`). The network: UDP datagrams between the host and up to 15 guests, a STUN request to
learn the address a router gives, and NebulaNet's relay when the two ends cannot meet.

```lumascript skip a listing from the part's contract, not a module that builds alone
fn host() {
    link = net.datagrams_bind(port: option("coop_port"))       // P-13
    let outside = wait net.stun(option("coop_stun"))           // P-13: the address and port seen from outside
    room = Room { host: true, peers: {}, me: 0, mods: mods.list_on().map(fn(m) { return m.id }),
                  cheats: option("coop_cheats"), rewind_ms: option("coop_rewind_ms") }
    screen.message(text "Coop.Hosting", outside)
}
```

The mod's own helpers the fragments call, in one file of the module (no game names):

```lumascript skip a listing from the part's contract, not a module that builds alone
var said: map<text, u64> = {}           // what the host last told, by its first word, and when
var in_bubble: bool = false
var last_heard: f64 = 0.0
var coop_menu_open: bool = false

fn tell_all(what: text) { link?.send_all(encode_text(what)) }
fn host_said(word: text) -> bool { return said.get(word) != none }
fn host_phase(class: text) -> text? { return said_text("phase {class}") }
fn host_time_left() -> u32 { return said_number("comet") as u32 }
fn any_peer_alive() -> bool { return room?.peers.values().any(fn(p: Peer) -> bool { return p.pose?.alive ?? false }) ?? false }
fn nearest_player(r: Room, from: vec3) -> vec3 {
    var best = MR::getPlayerPos()        // the local player: P-22 (a hook calling the function it hooks)
    for p in r.peers.values() {
        if let pose = p.pose, length(pose.position - from) < length(best - from) { best = pose.position }
    }
    return best
}
fn leave(why: text) { link?.close(); link = none; room = none; save.use_set(none); screen.message(why) }
```

`encode_pose`, `encode_text`, `apply`, `world_snapshot`, `said_text`, `said_number`, `rabbit_state`,
`is_peer_sensor`, `knockback`, `enter_bubble`, `join`, `pick_character`, `host_address` are packet code and screens
of the same kind (bytes in and out with the standard `bytes` module, part C), not written here.

`svc.net` datagrams and STUN; nerve names; `kit.actor.placement`, `find_placed`; `svc.save.use_set`; `svc.mods`;
`kit.replay.Pose`, `current_pose`, `Puppet.set_pose`; `screen.choose` with a `Choice`; `time.real_seconds`,
`time.game_version`.

### D2. Findings from A.7

- P-2 closed: A.7 is written from nebula-coop's own plan and folders; every game name it uses is a row.
- P-13 (svc.net): datagrams (`datagrams_bind`, `send_to`, `send_all`, `received`) and a STUN request; a relay for two
  ends that cannot meet is a server the mod names, not a game part. Every program before A.7 needed only tcp,
  websocket and http. Permission: the same `net` permission, the player shown the port.
- P-14 (self-description 1.8): nerves by name (`nerve "DinoPackunVs1.BattleLv2"`, a compile-time check that the class
  has it), so a mod can set a phase on another machine. Today only `const Nerve *` addresses, which differ per build
  and platform.
- P-15 (kit.actor, kernel K.actor): a stable key for a placed object (zone, layer, index in its placement list) and a
  lookup from the key, the same on every machine with the same mods. `PlacementId` exists in the game only as a value
  built from a `TableIter`, not callable from LumaScript (7.2).
- P-16: none needed for the current actor: `wrap every override of LiveActor::movement()` gives it (cost: one wrap per
  moving actor per frame, only while the mod is on; measured with phase 1's hook cost).
- P-17 (svc.save): save sets: a mod may switch the game's save files to a folder of its own (`coop/<room>`) and back,
  the player's own files untouched.
- P-18 (svc.mods): the list of mods on (`list_on`, `installed`); fetching a mod for one start (`offer_for_session`)
  runs the validator on it and asks the player, naming every permission, before it is used; `use_for_session` loads a
  set for one start. Security: a fetched mod's `native` permission is shown as such; nothing installs silently.
- P-19 (kit.replay): a single pose (`Pose`), the player's pose now (`current_pose`) and `Puppet.set_pose`, beside
  tracks; up to 15 puppets lit as Mario.
- P-20 (language 8): `result =` in an `after` hook of a function returning `Vec3f *`: where the written vector lives.
- P-21 (kernel 1.x run): `GameScene::exeSceneAction` called from `exePauseMenu` must be shown to move the scene
  without breaking the pause menu, or the scene's pause needs a general switch (the game's `isPermitToPauseMenu` says
  when a pause may start, not whether the scene stops).
- P-22 (kernel K.hook, language 8): a hook that calls the function it hooks (A.7.13's `nearest_player` calls
  `MR::getPlayerPos` from inside its `after`). Proposal: a mod's own hooks are not run again for calls made from
  inside them (each other mod's hooks are); the 1.1 run proves it with a hook that calls its own function. Until then
  A.7.13 is written, not proven.

### A.7 corrections

Checked against lumascript-language.md after appending A.7: the lines below broke the definition. Each is replaced as
shown; A.7 reads with these replacements. (the name checker does not check syntax. 4.8's test suite parses every
program of this file, which would have found them.)

| A.7 part | As written | Replaced by | Rule |
|---|---|---|---|
| 7.1 | `hook before MR::isPlayerDead() -> dead` (7.10) | `hook before MR::isPlayerDead() -> bool` | 8: a `before` names the result type |
| 7.2 | `match i { 0 => host(), 1 => join(), 2 => pick_character(), _ => {} }` | `... 2 => pick_character() else => {} }` | 4: `else =>` |
| 7.3, 7.4, 7.8, 7.11, 7.12 | `!x`, `a && b` | `not x`, `a and b` | 3: operators |
| 7.5 | `model p.character == "luigi" ? "Luigi" : "Mario"` | `if p.character == "luigi" { model "Luigi" } else { model "Mario" }` | 4: `if` as an expression; `model` takes a literal |
| 7.9 | `fn phase(boss: NerveExecutor, class: text, step: text)` | `fn phase(boss: NerveExecutor, kind: text, step: text)` and `kind` for `class` in its body | 2: `class` is a keyword |
| 7.14 | `.map(fn(m) { return m.id })` | `.map(fn(m: mods.Mod) -> text { return m.id })` | 5.4: closure parameters are typed |
| 7.4 to 7.12 | `if let r = room, not r.host {` | stays | `if let` takes the same list as `guard` |

- P-23 (language): the programs (A.1 to A.7, B) call container functions (`len get contains push remove map indexed
  values any sum`) that section 3 does not list.

### Revision from 5.1

The LumaKit interfaces fix these uses. A block whose first line is `// replaces <id>` stands
in for that program's block; `// amends <id>` changes only the part shown.

```lumascript skip a listing from the part's contract, not a module that builds alone
// replaces B.74
use kit.gravity
use svc.input

let flip = input.action("FlipGravity", default: input.Pad.Two)   // listed in the controls menu
var flipped: gravity.Override? = none

on frame {
    if input.pressed(flip) {
        if let f = flipped { f.end(); flipped = none }
        else {
            let down = MarioAccess::getPlayerActor().mGravity
            flipped = gravity.override_player(gravity.Field.Parallel { direction: down * -1.0 })
        }
    }
}
```

```lumascript skip a listing from the part's contract, not a module that builds alone
// amends A.2: a list's on_choose takes the row's index
use kit.screen
fn open_viewer() {
    let files = fs.list("runs", recursive: true)
    screen.list(screen.List {
        title: text "Replays.Title"
        rows: files
        on_choose: fn(i: int) { play_back(files[i]) }
    })
}
```

```lumascript skip a listing from the part's contract, not a module that builds alone
// amends A.6: a scene keeps refs, not views (lumakit rule 3)
use kit.cutscene
wait cutscene.play(cutscene.Scene {
    shots: [ cutscene.Shot.Look(at: ref(self), from_offset: vec3(0, 400, 900), frames: 120),
             cutscene.Shot.Line(speaker: ref(self), line: text "Boss.Greeting") ]
    letterbox: true
})
```

```lumascript skip a listing from the part's contract, not a module that builds alone
// amends A.7.2: a list that closes when a row is chosen
use kit.screen
fn open_coop_menu() {
    screen.list(screen.List {
        title: text "Coop.Title"
        rows: [text "Coop.Host", text "Coop.Join", text "Coop.Character", text "Coop.Back"]
        on_choose: fn(i: int) { match i { 0 => host(), 1 => join(), 2 => pick_character(), _ => {} } }
        closes: true
    })
}
```

```lumascript skip a listing from the part's contract, not a module that builds alone
// amends B.193: all() gives refs
use kit.actor
on frame {
    if film.next_frame() { tex.update(film.pixels()) }
    for s in screen_kind.all() { if let a = s.get() { draw.swap_texture(a, material: "ScreenFace", texture: tex) } }
}
```

A.7.2's `screen.message(text "Coop.Hosting", outside)` waits on the language's way of filling a text row's
placeholders.

Correction: the `screen.message(text "Coop.Hosting", outside)` above is A.7.14's (line 1300), not A.7.2's.

### Changes (D5.E)

A.7.13 now reads:

```lumascript skip a listing from the part's contract, not a module that builds alone
// The actor whose movement runs. An override that calls its base runs this wrap again inside it, so each wrap puts
// back what it found (D-9). The player's own actor asks for itself: `none`, the local player.
wrap every override of LiveActor::movement() {
    let was = current
    current = if is_mine(self) { none } else { self }
    proceed()
    current = was
}
fn is_mine(a: LiveActor) -> bool {
    if let me = MarioAccess::getPlayerActor() { return same(a, me) }
    return false
}

// Where the player is, chosen by meaning (D-8; g/tools/player_rows.py lists the rows with each one's reason).
hook after MR::getPlayerPos() -> p {
    if let r = room, let a = current, let local = p { result = nearest(r, a.mPosition, local) }   // P-20
}
hook after MR::getPlayerCenterPos() -> c {
    if let r = room, let a = current, let me = MarioAccess::getPlayerActor() {
        // A peer's centre: its place lifted as the local player's is (Pose carries no centre; exact under one gravity).
        result = nearest(r, a.mPosition, me.mPosition) + (me.mCenterPos - me.mPosition)
    }
}
// An NPC's talk range is the local player's own: the game asks it through TalkMessageCtrl.
wrap TalkMessageCtrl::isNearPlayer(distance: f32) { let was = current; current = none; proceed(); current = was }
wrap `TalkMessageCtrl::isNearPlayer(const TalkMessageCtrl *)` { let was = current; current = none; proceed(); current = was }
```

Why these two rows and no others.
- The game asks where the player is through `MR::getPlayerPos` (279 calls outside the util files) and
  `MR::getPlayerCenterPos` (100). One direct read of the actor's position is left, in MarioAccess itself.
- Every "towards the player" function reads through them (ActorMovementUtil.cpp): calcDistanceToPlayer and its H
  form, isNearPlayer (actor and point), isNearPlayerAnyTime, isNearPlayerPose, isNearPlayerHorizontal,
  isFaceToPlayerDegree(HV), isFaceToPlayerHorizontalDegree, isClockwiseToPlayer, isPlayerLeftSide,
  isPlayerExistUp/Down/Side, isInSightConePlayer and isInSightFanPlayer, calcVecToPlayerH and calcVecFromPlayerH,
  turnDirectionTo/FromPlayerDegree(Horizon), moveAndTurnToPlayer, escapeFromPlayer, flyAndTurnAlongRailSearchingPlayer,
  setVelocityJumpAwayFromPlayer, addVelocityClockwiseToPlayer and calcRailPosNearestPlayer.
- So hooking the two sources moves all of these, and hooking them as well would answer twice. No game code writes
  through the pointer getPlayerPos gives (the seven non-const users read it), so the hook's own vector is
  safe.
- Not hooked, each the local player's own: the stick and buttons (getPlayerStickX/Y, Pressed*, Held*,
  getPlayerInput), getPlayerMaxCount, getPlayerCurrentBckName, getPlayerXanimeResource, getPlayerRestartIdInfo,
  isPlayerInAreaObj (areas trigger the local player's events), `isPlayerHidden` inside isNearPlayer, and the talk
  range (the two wraps above).
- The player's directions (getPlayerGravity, Up/Front/SideVec, getPlayerVelocity) are left local: Pose carries a
  rotation, and an enemy facing the nearest peer needs only its place.

Messages an actor sends from its movement (its attack sensor, LiveActor.cpp:137 doObjCol, to a receiveMsg*) run
while `current` is the sender. A receiver asking where the player is gets the player nearest the sender, which is
what the sender's hit means.

Left for the run (A.7 is written, not proven): an actor that acts on the local player with a place it read
in the same movement (NoteFairy.cpp:278 turns the local player towards the fairy from the place it read) uses the
nearest peer's place. The co-op test by playing is what shows whether any such actor looks wrong; each found is
then wrapped like the talk range.
