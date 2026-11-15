---
title: kit.item
shortTitle: item
intro: "Things picked up, placed in stages by name or made by code, in containers, carried in an inventory."
contentType: reference
---

## About kit.item

The `kit.item` module of LumaKit gives you things picked up, placed in stages by name or made by code, in containers, carried in an inventory.

To use the module, add `lumakit` to the `needs` of your mod's `luma.json` and write:

```lumascript
use kit.item
```

### How the module works

Things picked up, placed in stages by name or made by code, in containers, carried in an inventory. An item is defined once by a mod (define, or its kit/item/<id>.json) and is an object of the kit's class KitItem wherever it stands: made by code (spawn: std make) or placed in a stage under "<mod id>/<id>" (the kernel's run-time alias row; alias names are one space for every mod). A mod's data items are defined on its first frame, so its stages can place them. The object shows the item's model, hovers and turns as the game's power-ups do when `floats`, and is picked up by the player's touch: its sound and shine play, what it gives is given (kit.item give; Carry counts into the inventory), and it is gone, to come back three seconds later when `respawns`. Containers: Crystal: the item stands in one of the game's own crystals (the object CrystalCage made around it) and cannot be taken until the crystal is broken; Bubble: the item hangs in the air above its place and drifts, and is taken as any other (the game's bubbles pop on touch). The inventory is per save file (svc.save): each carried count under the mod and the item's id, and the order the ids were first carried in. A file's copy takes it along; an erased file forgets it. Everything belongs to the mod that defined it: when it stops, its items' objects are killed with the game's own kill, its definitions and alias rows go; its inventory stays on the file for when it is on again.

The module's source is `lumakit/source/item.lum`.

## Declarations

| Name | Kind | Description |
|---|---|---|
| [`Gives`](#gives) | Enum | What a pickup, a defeated enemy or a script gives the player. An open enum: cases are only ever added. Carry: n into the inventory under the item's id. Call: the mod's function with the player. |
| [`Container`](#container) | Enum |  |
| [`Item`](#item) | Struct |  |
| [`ItemId`](#itemid) | Struct | An item's handle: its number, 0 for none. Its functions are also module functions (`item.spawn(i, at)`). |
| [`GivesData`](#givesdata) | Enum | An item as its data file writes it: content by name (the mod's own), what it gives without code. |
| [`give`](#give) | Function | Gives `g` to the player now: coins and star bits counted as the game counts its own, a life each, health as the game's heart pickups give it, a form through kit.player.form, a mod's function with the player. Carry is counted by the item that gives it (give_item, a pickup); given bare it is nothing. |
| [`save_key`](#save_key) | Function | The one form of every kit save key (kit.item, kit.progress, kit.shop, kit.talk): "<mod id>/kit.<module>.<kind>.<id>", a value a library keeps for another mod (svc.save: the owner's id, '/', a name of 1 to 64 of a-z 0-9 _ - .). The owner by its id (stable across starts and reloads), never by its caller() number (the kernel's pointer to the running mod, another at every start); the value's own id as hashes. A forgotten mod's "<id>/..." values go with it, in every file. none for a mod that is not running: nothing is read or written for it. |
| [`owns_key`](#owns_key) | Function | Whether `key` is a value of `owner`'s for this module and kind (a save_key of its): what a public setter checks before it writes a key a mod handed in. |
| [`kept`](#kept) | Function | The kit's saves: one rule for every mod's kit value. LumaKit saves it itself, in its own part of the save file (a library's service calls name the library), under the owning mod's id (save_key), whether the owner has code or only data and wherever the kit was entered from. |
| [`define`](#define) | Function | The item defined for the calling mod (a second of the same id replaces the first; objects of it already standing keep the look they were made with). Its alias row makes "<mod id>/<id>" placeable in any stage. |
| [`cage_of`](#cage_of) | Function | The crystal an item's object stands in (Container.Crystal), while it stands; none for another object, an item not in a crystal, or one already broken. A mod breaks it by code with the game's own kill. The module keeps each item's crystal beside it. |
| [`model_of`](#model_of) | Function | The model a data file of mod `owner` names, as a `model "..."` literal compiles: "<id>:<path>" is mod <id>'s "Luma/<id>/<path>"; a bare name is the mod's own "Luma/<owner's id>/<name>" when it has that archive, else the game's. none for an empty name or one the game has no archive for (the log names it). |
| [`has_model`](#has_model) | Function | Whether the game has model `m`'s archive, so a kit object with a missing model is refused by name where the game would stop ("<name> cannot be loaded", ResourceHolderManager). |
| [`gives_of`](#gives_of) | Function | What a data file's `gives` gives: a form by its id among the calling mod's; none for a form it does not have. |
| [`find`](#find) | Function | The calling mod's item `id`: one it defined, else the one of its kit/item/<id>.json, defined now (data and code make the same thing; code finds a data item to give it callbacks). none when there is neither, the file does not read as an item, or it names content or a form the mod does not have. |
| [`spawn`](#spawn) | Function | The item made at `at` in the stage playing now, for the calling mod: its object, none for a handle that is not the calling mod's or when the stage cannot make one. |
| [`give_item`](#give_item) | Function | What the item gives, given to the player now without an object (a shop's ware, a quest's reward); Carry counts into the inventory. |
| [`carried`](#carried) | Function | How many of the item the player carries on this file. |
| [`use_one`](#use_one) | Function | One of the item used up: false when none is carried. |
| [`carried_all`](#carried_all) | Function | The calling mod's inventory on this file: each carried item and its count, in the order first carried (items used up to 0 stay in their place; an id the mod no longer defines is left out). |

### `Gives`

```lumascript
pub enum Gives { Form(f: form.FormId), Coins(n: u32), StarBits(n: u32), Lives(n: u32), Health(n: u32), Carry(n: u32),
                 Call(f: fn(MarioActor)) }
```

What a pickup, a defeated enemy or a script gives the player. An open enum: cases are only ever added. Carry: n into the inventory under the item's id. Call: the mod's function with the player.

### `Container`

```lumascript
pub enum Container { None, Bubble, Crystal }
```

### `Item`

```lumascript
pub struct Item {
    id: text                                  // placeable as "<id>" in its mod's stages, "<mod id>/<id>" elsewhere
    look: model
    shine: effect? = none
    sound_get: sound? = none
    gives: Gives
    container: Container = Container.None
    floats: bool = true                       // hovers and turns, as the game's power-ups
    respawns: bool = false
}
```

### `ItemId`

```lumascript
pub struct ItemId {
    n: u32
    fn spawn(self, at: vec3) -> LiveActor? { return spawn(self, at) }
    fn give(self) { give_item(self) }
    fn carried(self) -> u32 { return carried(self) }
    fn use_one(self) -> bool { return use_one(self) }
}
```

An item's handle: its number, 0 for none. Its functions are also module functions (`item.spawn(i, at)`).

### `GivesData`

```lumascript
pub enum GivesData { Coins(n: u32), StarBits(n: u32), Lives(n: u32), Health(n: u32), Carry(n: u32), Form(id: text) }
```

An item as its data file writes it: content by name (the mod's own), what it gives without code.

### `give`

```lumascript
pub fn give(g: Gives)
```

Gives `g` to the player now: coins and star bits counted as the game counts its own, a life each, health as the game's heart pickups give it, a form through kit.player.form, a mod's function with the player. Carry is counted by the item that gives it (give_item, a pickup); given bare it is nothing.

### `save_key`

```lumascript
pub fn save_key(owner: u64, module: text, kind: text, id: text) -> text?
```

The one form of every kit save key (kit.item, kit.progress, kit.shop, kit.talk): "<mod id>/kit.<module>.<kind>.<id>", a value a library keeps for another mod (svc.save: the owner's id, '/', a name of 1 to 64 of a-z 0-9 _ - .). The owner by its id (stable across starts and reloads), never by its caller() number (the kernel's pointer to the running mod, another at every start); the value's own id as hashes. A forgotten mod's "<id>/..." values go with it, in every file. none for a mod that is not running: nothing is read or written for it.

### `owns_key`

```lumascript
pub fn owns_key(owner: u64, key: text, module: text, kind: text) -> bool
```

Whether `key` is a value of `owner`'s for this module and kind (a save_key of its): what a public setter checks before it writes a key a mod handed in.

### `kept`

```lumascript
pub fn kept(key: text) -> bytes?
```

The kit's saves: one rule for every mod's kit value. LumaKit saves it itself, in its own part of the save file (a library's service calls name the library), under the owning mod's id (save_key), whether the owner has code or only data and wherever the kit was entered from.

### `define`

```lumascript
pub fn define(i: Item) -> ItemId
```

The item defined for the calling mod (a second of the same id replaces the first; objects of it already standing keep the look they were made with). Its alias row makes "<mod id>/<id>" placeable in any stage.

### `cage_of`

```lumascript
pub fn cage_of(a: LiveActor) -> LiveActor?
```

The crystal an item's object stands in (Container.Crystal), while it stands; none for another object, an item not in a crystal, or one already broken. A mod breaks it by code with the game's own kill. The module keeps each item's crystal beside it.

### `model_of`

```lumascript
pub fn model_of(owner: u64, name: text) -> model?
```

The model a data file of mod `owner` names, as a `model "..."` literal compiles: "<id>:<path>" is mod <id>'s "Luma/<id>/<path>"; a bare name is the mod's own "Luma/<owner's id>/<name>" when it has that archive, else the game's. none for an empty name or one the game has no archive for (the log names it).

### `has_model`

```lumascript
pub fn has_model(m: model) -> bool
```

Whether the game has model `m`'s archive, so a kit object with a missing model is refused by name where the game would stop ("<name> cannot be loaded", ResourceHolderManager).

### `gives_of`

```lumascript
pub fn gives_of(d: GivesData) -> Gives?
```

What a data file's `gives` gives: a form by its id among the calling mod's; none for a form it does not have.

### `find`

```lumascript
pub fn find(id: text) -> ItemId?
```

The calling mod's item `id`: one it defined, else the one of its kit/item/<id>.json, defined now (data and code make the same thing; code finds a data item to give it callbacks). none when there is neither, the file does not read as an item, or it names content or a form the mod does not have.

### `spawn`

```lumascript
pub fn spawn(i: ItemId, at: vec3) -> LiveActor?
```

The item made at `at` in the stage playing now, for the calling mod: its object, none for a handle that is not the calling mod's or when the stage cannot make one.

### `give_item`

```lumascript
pub fn give_item(i: ItemId)
```

What the item gives, given to the player now without an object (a shop's ware, a quest's reward); Carry counts into the inventory.

### `carried`

```lumascript
pub fn carried(i: ItemId) -> u32
```

How many of the item the player carries on this file.

### `use_one`

```lumascript
pub fn use_one(i: ItemId) -> bool
```

One of the item used up: false when none is carried.

### `carried_all`

```lumascript
pub fn carried_all() -> list<(ItemId, u32)>
```

The calling mod's inventory on this file: each carried item and its count, in the order first carried (items used up to 0 stay in their place; an id the mod no longer defines is left out).

## Further reading

* [LumaKit reference](index.md)
* [About LumaKit](../../concepts/lumakit.md)
