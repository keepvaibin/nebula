---
title: kit.player.character
shortTitle: player.character
intro: "A playable character (name, icon, model, animations, voice, constants), switching mid-level, a second player."
contentType: reference
---

## About kit.player.character

The `kit.player.character` module of LumaKit gives you a playable character (name, icon, model, animations, voice, constants), switching mid-level, a second player.

To use the module, add `lumakit` to the `needs` of your mod's `luma.json` and write:

```lumascript
use kit.player.character
```

### How the module works

A playable character (name, icon, model, animations, voice, constants), switched to mid-level and back. Built on the game's player by name: its animations through MarioActor:: changeAnimation (and the non-stop and upper-body forms), its voice through MarioActor::playSound, the game's Luigi checks through MR::isPlayerLuigi, its constants through kit.player.state's tune. The newest `play` holds; when its mod stops, or `mario()` is played by it, the one before holds again. Characters and plays are recorded under caller(); `on mods.stopped` takes a stopped mod's away and the game's own Mario (or the character played before) comes back. The look: the character's model drawn with the player through content.model's attach, every joint following the player's joint of its name (its own skeleton's or Mario's: LumaLink binds and scales it), and the game's own body, hands and face not drawn while it shows (MarioActor::drawMarioModel skipped). The icon: a draw.Texture the character carries for what shows the player (the hud, a select screen). A second player: an extra MarioActor from svc.player, driven by the game's second controller, playing its own character (its look, clips and voice) or the game's own. `voice` maps the game's sound names to the names of the sounds played instead.

The module's source is `lumakit/source/player/character.lum`.

## Declarations

| Name | Kind | Description |
|---|---|---|
| [`Character`](#character) | Struct |  |
| [`CharacterId`](#characterid) | Struct | A defined character: its slot + 1; 0 is the game's own Mario or Luigi. |
| [`Second`](#second) | Struct | Look: the second player's model (none: its character's look, or the game's own body). |
| [`SecondPlayer`](#secondplayer) | Struct |  |
| [`define`](#define) | Function |  |
| [`find`](#find) | Function | A character of the calling mod by its id. |
| [`current`](#current) | Function | The character holding now; none: the game's own Mario or Luigi. |
| [`mario`](#mario) | Function | The game's own, to switch back. |
| [`play_character`](#play_character) | Function | The calling mod's character plays from now (mid-level too: the next animation and sound are its own); `mario()` gives the game's own back for this mod's plays. The newest play of any mod holds. |
| [`is_current_character`](#is_current_character) | Function |  |
| [`second_player`](#second_player) | Function | A second player: an extra MarioActor of the game's (svc.player), standing where player 1 stands, driven by the game's second controller (`pad` 1; svc.player drives none other from a pad) and playing `character` (none: the game's own; svc.player makes the scene's own brother, so as_luigi does not change it), shown as `look`, else its character's look. It falls and comes back at that place, and goes with its scene. The handle is empty (view none, alive false) when it could not be made: past 3 extra players, or with no scene with a player running (the reason in the mod log). |
| [`second_view`](#second_view) | Function |  |
| [`remove_second`](#remove_second) | Function |  |
| [`second_alive`](#second_alive) | Function |  |

### `Character`

```lumascript
pub struct Character {
    id: text
    name: text                                // C.text
    icon: draw.Texture? = none
    look: model? = none                       // its own skeleton or Mario's (LumaLink binds and scales it)
    actions: map<text, text> = {}             // the game's action name -> this character's clip
    voice: map<text, text> = {}               // the game's voice name -> this character's sound
    tuning: pst.Tuning? = none
    as_luigi: bool = false                    // the game's Luigi checks answer true (MR::isPlayerLuigi)
}
```

### `CharacterId`

```lumascript
pub struct CharacterId {
    n: int
    fn play(self) { play_character(self) }
    fn is_current(self) -> bool { return is_current_character(self) }
}
```

A defined character: its slot + 1; 0 is the game's own Mario or Luigi.

### `Second`

```lumascript
pub struct Second { look: model? = none, pad: u32 = 1, character: CharacterId? = none }
```

Look: the second player's model (none: its character's look, or the game's own body).

### `SecondPlayer`

```lumascript
pub struct SecondPlayer {
    n: int
    fn view(self) -> MarioActor? { return second_view(self) }
    fn remove(self) { remove_second(self) }
    fn alive(self) -> bool { return second_alive(self) }
}
```

### `define`

```lumascript
pub fn define(c: Character) -> CharacterId
```

### `find`

```lumascript
pub fn find(id: text) -> CharacterId?
```

A character of the calling mod by its id.

### `current`

```lumascript
pub fn current() -> CharacterId?
```

The character holding now; none: the game's own Mario or Luigi.

### `mario`

```lumascript
pub fn mario() -> CharacterId
```

The game's own, to switch back.

### `play_character`

```lumascript
pub fn play_character(id: CharacterId)
```

The calling mod's character plays from now (mid-level too: the next animation and sound are its own); `mario()` gives the game's own back for this mod's plays. The newest play of any mod holds.

### `is_current_character`

```lumascript
pub fn is_current_character(id: CharacterId) -> bool
```

### `second_player`

```lumascript
pub fn second_player(s: Second) -> SecondPlayer
```

A second player: an extra MarioActor of the game's (svc.player), standing where player 1 stands, driven by the game's second controller (`pad` 1; svc.player drives none other from a pad) and playing `character` (none: the game's own; svc.player makes the scene's own brother, so as_luigi does not change it), shown as `look`, else its character's look. It falls and comes back at that place, and goes with its scene. The handle is empty (view none, alive false) when it could not be made: past 3 extra players, or with no scene with a player running (the reason in the mod log).

### `second_view`

```lumascript
pub fn second_view(p: SecondPlayer) -> MarioActor?
```

### `remove_second`

```lumascript
pub fn remove_second(p: SecondPlayer)
```

### `second_alive`

```lumascript
pub fn second_alive(p: SecondPlayer) -> bool
```

## Further reading

* [LumaKit reference](index.md)
* [About LumaKit](../../concepts/lumakit.md)
