# M2: calls proved at build time, and handlers that prove them (N-2023, N-2024)

Agent 2, 2026-11-17. Plan file 02, M2 items 2, 3 and 5.

## What is there

- The build finds what each game function needs (959dc09b1): `luma_functions.json` gives each row `needs` (99 kinds:
  `scene`, a scene object such as `ScenePlayingResult`, `MarioActor`, `UserFile`, `CometScheduler`), `null_when` (a
  pointer result that is null when an object is missing) and `makes`. 11,891 of 27,149 rows need something.
- At run time the kernel checks a row's needs before a mod's call (`luma::present`, LumaHook.cpp). A call whose needs
  are missing is not made: the empty value, and `Call.NotNow` in the mod log.
- Handlers today: `on start`, `on stop`, `on frame` (every frame, boot included), `on step`, `on option`, `on load`,
  `on layers`, `on draw`, `on stage "NAME"` (the stage NAME is ready) and the 28 events (delivered at the next frame's
  start, so they prove nothing about the moment they run).

What is missing: the build does not know the needs, so a mod author learns of a call made too early only from the mod
log. There is no handler for "a stage is ready" (any stage), "a frame of play", "the stage ended" or "the player is
ready".

## The sets

A set is a list of needs, named. `tools/lumacore/guarantees.json` holds them, each need with the source line that
makes it:

| Set | Holds when | Needs |
|---|---|---|
| `system` | the game system's files are read (some frames after boot; never undone) | `UserFile`, `CometScheduler` |
| `stage` | a game scene's making is finished (GameScene::initAfterScenarioChosen done) and it is not being taken down | `system`, `scene`, the scene objects GameScene::init, SceneFunction::initForNameObj and initForLiveActor, initEffect and initAfterScenarioChosen make without a condition |
| `player` | `stage`, and the player's actor is in the MarioHolder | `stage`, `MarioActor` |

`gen_types.py` checks every name against the needs it knows, and writes the sets into `luma_functions.json`
(`"guarantees"`) and into the kernel's table.

The sets are true by construction: the kernel checks every need of a set before it runs a handler that proves it. A
set that names too much shows as a handler that never runs; the in-game test below runs each handler in every stage of
the tour, so it cannot.

## Handlers (N-2024)

Each runs at a frame's start, as `on frame` does, and proves its set in its body up to its first `wait`:

- `on stage ready { }`: once a stage, the first frame `stage` holds and the scenario's opening is over (as
  `on stage "NAME"` today, which proves `stage` too).
- `on play { }`: every frame the time service says PLAY and `player` holds (not in the pause menu, a movie or the skip).
- `on player ready { }`: once a stage, the first frame `player` holds.
- `on stage ended { }`: once, the first frame after a stage whose `stage ready` ran no longer holds `stage`. Proves
  `system` only: the scene is gone or going. A mod that must act while the scene lives hooks a game function.

## The proof (N-2023)

At each call of a game function the checker knows the needs proved there. A call whose needs are not all proved is
refused (a new code), naming the needs missing and what would prove them. Proofs:

1. The handler's set, in its body up to its first `wait` (after a wait the stage may have ended).
2. A hook's body proves what the hooked function needs: the game is calling it, so they are there. A hook with
   overrides proves what all of them need. An `override fn` of a game class proves its base row's needs and `stage`:
   the game makes the mod's object while a stage is made (LumaCore.make, placement), and calls it while it lives.
3. `if can(F) { }` proves F's needs in its first branch, up to a `wait`. `can` is the kernel's check of F's needs
   (a new LumaCore entry, `luma_can_call`, for C too).
4. `if let x = G() { }`, where G's result is null when objects are missing (`null_when`), proves those objects in
   its first branch.
4a. `if holds("SET") { }` proves the set's needs as `can` does a function's (LumaCore.holds), for code in `on frame`,
   events and tasks that works on many game functions at once.
4b. A call proves what it makes (`makes`, as the build's merge counts it): `MR::declareCoin` then `MR::appearCoinPop`.
5. A function of the mod proves nothing of its own: what its body needs and does not prove becomes its needs, to a
   fixed point over the mod's functions, and each call of it must prove them. A function given away (a closure, an
   export, a function a service calls back) has no caller the checker sees: its body must prove its own needs.
6. `on frame`, `on step`, `on start`, the events and every task after a `wait` prove nothing.

The run-time check stays: C mods, and a call from a mod built before this, still meet it.

C mods: `LumaCore.can_call(f)` is the kernel's check of a row's needs and `LumaCore.holds("stage")` of a set's
(`LUMA_CAN_CALL` in luma.h says the entries are there). The reference pages list each function's needs and when its
result is null.

## What a call needs (N-2050)

A need found in a function's body goes to its callers. Through a call of a member function (a method or destructor of
an object that already exists) only the needs of the sets and the game system's go on: an object only some stages make
(KarikariDirector, WaterAreaHolder, BlueChipHolder) is made by the objects whose code reaches it, so an existing
object's member reaches it only while it is there. A free or static function's call, and a constructor's, pass on every
need. Without this, 3431 rows needed an object outside `player` and the kernel refused them in most stages; with it,
657.

A call under a test that a scene object is there (`MR::isExistSceneObj(id)` around it, or an earlier
`if (...) return;`) passes on none of the callee's needs on that object, nor `scene` (N-2051):
`MR::getKarikariClingNum` tests KarikariDirector and then calls the file's helper that reads it.

## Tests

- Compiler: each proof form proves; each refused form names the needs and the proofs (lumascript tests).
- In game: a mod with each handler checks, each frame it runs, every need of its set with `luma_can_call`'s check,
  in every stage of the tour, as facts; each handler ran in every stage.
- The existing mods (tests/mods, LumaLink tests/game, LumaKit) build. What the new rule refuses in them is changed
  to prove its calls; each change is listed in PROGRESS-AGENT-2.
