---
title: kit.talk
shortTitle: talk
intro: "Lines and choices from an actor in the game's balloon, dialogue trees, voice for lines."
contentType: reference
---

## About kit.talk

The `kit.talk` module of LumaKit gives you lines and choices from an actor in the game's balloon, dialogue trees, voice for lines.

To use the module, add `lumakit` to the `needs` of your mod's `luma.json` and write:

```lumascript
use kit.talk
```

### How the module works

Lines and choices from an actor in the game's own balloon, dialogue trees of kit/talk, a voice for a line. A line is the game's talk: a TalkMessageCtrl made on the speaker (MR::createTalkCtrlDirect) whose message id is the kit's own ("LumaKit_Talk<n>"), its words given to the game's message lookup by the hook on NebulaMods::text below (the lookup every message goes through first), so the balloon, its sound, its camera and its paging are the game's. The talk runs by MR::tryTalkForceAtEnd each frame (the game's own: it asks for the talk and starts it, and is true once the talk can end; MR::tryTalkForce is true once it starts). A choice is the line, then kit.screen's choose with the choices (the game's balloons choose between two at most). Trees: kit/talk/<tree>.json of each enabled mod (mods.needing, mods.kit_names, mods.kit_read, json.decode), read at LumaKit's start, held under that mod; schema code/lumakit.kit/talk. A choice's flag is set through kit.progress.

The module's source is `lumakit/source/talk.lum`.

## Declarations

| Name | Kind | Description |
|---|---|---|
| [`prompt_message`](#prompt_message) | Function | The prompt's message id (kit.actor makes the control with it). |
| [`saying`](#saying) | Function | True while a line by `who` is being said. |
| [`say`](#say) | Task | The line said by `who` in its balloon; ends when the player has read it (or the speaker is gone). |
| [`ask`](#ask) | Task | The line, then the choices: the one taken (0 for the first); the player backing out takes the last. |
| [`run`](#run) | Task | A tree of the calling mod's kit/talk: its nodes said from "start", each choice's next node followed, its flag set through kit.progress; the name of the node it ended at, none when the mod has no such tree. A node named that the tree lacks ends it there (its name given); a tree that loops runs until it reaches an end. |
| [`voice`](#voice) | Function | The sound played when `line` shows, for the calling mod's lines. |

### `prompt_message`

```lumascript
pub fn prompt_message() -> text
```

The prompt's message id (kit.actor makes the control with it).

### `saying`

```lumascript
pub fn saying(who: LiveActor) -> bool
```

True while a line by `who` is being said.

### `say`

```lumascript
pub task say(who: ref<LiveActor>, line: text)
```

The line said by `who` in its balloon; ends when the player has read it (or the speaker is gone).

### `ask`

```lumascript
pub task ask(who: ref<LiveActor>, line: text, choices: list<text>) -> int
```

The line, then the choices: the one taken (0 for the first); the player backing out takes the last.

### `run`

```lumascript
pub task run(who: ref<LiveActor>, tree: text) -> text?
```

A tree of the calling mod's kit/talk: its nodes said from "start", each choice's next node followed, its flag set through kit.progress; the name of the node it ended at, none when the mod has no such tree. A node named that the tree lacks ends it there (its name given); a tree that loops runs until it reaches an end.

### `voice`

```lumascript
pub fn voice(line: text, s: sound)
```

The sound played when `line` shows, for the calling mod's lines.

## Further reading

* [LumaKit reference](index.md)
* [About LumaKit](../../concepts/lumakit.md)
