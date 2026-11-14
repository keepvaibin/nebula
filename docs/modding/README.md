# Modding Nebula

A mod is a folder with a `luma.json`, packed by LumaLink into one `.luma` file the game imports. Start with
[your first mod](guides/first-mod.md).

## Guides

- [Your first mod](guides/first-mod.md)
- [A small tweak: moon jump](guides/a-small-tweak.md)
- [Changing the screen: hide the HUD](guides/hide-the-hud.md)
- [A new power-up: the Gold Flower](guides/a-new-power-up.md)
- [A new character to talk to, with its own materials and textures](guides/a-new-npc.md)
- [A new galaxy with its own enemy, boss, cutscene and music](guides/a-new-galaxy.md)
- [A video on a screen inside a galaxy](guides/a-video-in-a-galaxy.md)
- [A game mode of your own: Balloon World](guides/balloon-world.md)
- [A ghost time trial with a replay viewer](guides/ghost-time-trial.md)
- [Talking to another program: a LiveSplit link](guides/livesplit-link.md)
- [Online: a leaderboard](guides/online-leaderboard.md)

## The mod file

- [The .luma format](format/luma.md): folders, layers, patches, code
- [luma.json](format/manifest.md): every key
- [Table patches](format/table-keys.md): how a row is named
- [The validator](format/validator.md) and [its rules](format/rules.md): every code a check can give

## LumaScript

- [Text and service values](language/text-and-services.md): service structs, enums, constants, `svc.Answer`, text rows, `log`
- [Message tags](language/message-tags.md): the bracket words for the game's message tags
- [The language](language/language.md)
- [Worked programs](language/programs.md)
- [Classes derived from the game's](language/classes.md)
- [References to game objects](language/references.md)
- [Making and owning game objects](language/objects.md)
- [Types across the boundary](language/types.md)
- [Numbers](language/numbers.md)
- [Functions with a variable argument list](language/varargs.md)
- [The game's description of itself](language/game-description.md)
- [What the compiler emits](language/compiled-form.md)

## LumaLink

- Commands: [new](lumalink/cmd-new.md), [add](lumalink/cmd-add.md), [check](lumalink/cmd-check.md),
  [build](lumalink/cmd-build.md), [pack](lumalink/cmd-pack.md), [run](lumalink/cmd-run.md),
  [test](lumalink/cmd-test.md), [convert](lumalink/cmd-convert.md), [format](lumalink/cmd-format.md),
  [list](lumalink/cmd-list.md), [extract](lumalink/cmd-extract.md), [verify](lumalink/cmd-verify.md),
  [explain](lumalink/cmd-explain.md), [lsp](lumalink/cmd-lsp.md)
- [The test harness](lumalink/test-harness.md)
- [How the game loads a mod](lumalink/loader.md)
- [What is checked at each start](lumalink/start-check.md)
- [Permissions and consent](lumalink/consent.md)
- [The guard: a failing mod is switched off, the game goes on](lumalink/guard.md)
- [The mod log](lumalink/mod-log.md)
- [Reloading a mod while the game runs](lumalink/reload.md)
- [The game's two modules](lumalink/modules.md)
- [The build environment](lumalink/environment.md)

## Services

[The registry](services/registry.md) (how every service is called), then one page each:
[assets](services/assets.md), [audio and capture](services/audio.md), [draw](services/draw.md),
[files](services/fs.md), [input](services/input.md), [other mods](services/mods.md), [network](services/net.md),
[saves](services/save.md), [system](services/system.md), [time](services/time.md), [ui](services/ui.md).

## Content a mod brings

[Models](content/models.md), [animations](content/animation.md), [materials](content/materials.md),
[particle effects](content/effects.md), [sound](content/sound.md), [screens and fonts](content/screens.md),
[stages](content/stages.md).

## LumaKit

[The rules every module keeps](lumakit/overview.md) and [the modules](lumakit/modules.md), then one page each:
[actor](lumakit/actor.md), [area](lumakit/area.md), [camera](lumakit/camera.md), [cutscene](lumakit/cutscene.md),
[gravity](lumakit/gravity.md), [hud](lumakit/hud.md), [item](lumakit/item.md), [menu](lumakit/menu.md),
[net.kit](lumakit/net-kit.md), [player.character](lumakit/player-character.md),
[player.form](lumakit/player-form.md), [player.state](lumakit/player-state.md), [pointer](lumakit/pointer.md),
[progress](lumakit/progress.md), [projectile](lumakit/projectile.md), [replay](lumakit/replay.md),
[ride](lumakit/ride.md), [screen](lumakit/screen.md), [shop](lumakit/shop.md), [stage](lumakit/stage.md),
[talk](lumakit/talk.md), [ui.kit](lumakit/ui-kit.md).

## Lists made from the game

[Classes](CLASSES.md), [events](EVENTS.md), [nerves](NERVES.md) and [tables](TABLES.md), written by the tools in
`tools/lumacore` and `tools/modding`. Every name a mod can use is in the reference a game build writes
(`luma_docs/reference` in the build folder; the release package carries it).
