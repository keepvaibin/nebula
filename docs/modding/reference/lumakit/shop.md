---
title: kit.shop
shortTitle: shop
intro: "Buying things with coins or star bits."
contentType: reference
---

## About kit.shop

The `kit.shop` module of LumaKit gives you buying things with coins or star bits.

To use the module, add `lumakit` to the `needs` of your mod's `luma.json` and write:

```lumascript
use kit.shop
```

### How the module works

Buying things with coins or star bits. A shop is a list screen of kit.screen: a row a ware, its name, its price and what is left of it; the title carries what the player holds. Taking a row the player can pay for takes the price from the game's own counter (MR::incCoin and MR::addStarPiece with the price as a negative count, the counters the HUD shows), gives the ware through kit.item, and counts its stock down. A ware the player cannot pay for, or one sold out, is not sold: its row answers with the game's own refusal sound and the shop stays open. The shop writes no words of its own (the names and the title are the mod's; prices and counts are numbers), so it needs no translation. Stock is kept per save file (svc.save: a file's copy takes it along, an erased file forgets it), under the shop's id and the ware's name, for the mod that opened the shop. A shop without an id keeps its stock under its title. A keeper, when given and alive, says the title in the game's balloon first (kit.talk) and the shop closes if the keeper is gone. One shop is open at a time for all mods: a newer one closes the one before, whose waiter gets none. Data: kit/shop/<id>.json of the calling mod, read by find(); `gives` as json writes an enum's case ({"Coins": {"n": 5}}).

The module's source is `lumakit/source/shop.lum`.

## Declarations

| Name | Kind | Description |
|---|---|---|
| [`Currency`](#currency) | Enum |  |
| [`Ware`](#ware) | Struct |  |
| [`Shop`](#shop) | Struct |  |
| [`find`](#find) | Function | The shop of the calling mod's kit/shop/<id>.json; none when the mod has no such file, it does not read as a shop, or a ware gives a form the mod does not have. |
| [`open`](#open) | Task | The shop opened for the calling mod. The ware bought (the shop closes with the first sale); none when the player left without buying, the keeper went, a newer shop opened, or the mod stopped. |
| [`stock_left`](#stock_left) | Function | What is left of a ware of a shop of the calling mod on this file (none: it never runs out). For a mod that shows its stock elsewhere (a sign, a HUD row). |
| [`restock`](#restock) | Function | A ware of a shop of the calling mod filled again to its first stock on this file (a daily restock is the mod's rule: it calls this when its day turns). |

### `Currency`

```lumascript
pub enum Currency { Coins, StarBits }
```

### `Ware`

```lumascript
pub struct Ware { name: text, price: u32, gives: item.Gives, stock: u32? = none }
```

### `Shop`

```lumascript
pub struct Shop { title: text, wares: list<Ware>, currency: Currency, keeper: ref<LiveActor>? = none, id: text = "" }
```

### `find`

```lumascript
pub fn find(id: text) -> Shop?
```

The shop of the calling mod's kit/shop/<id>.json; none when the mod has no such file, it does not read as a shop, or a ware gives a form the mod does not have.

### `open`

```lumascript
pub task open(s: Shop) -> Ware?
```

The shop opened for the calling mod. The ware bought (the shop closes with the first sale); none when the player left without buying, the keeper went, a newer shop opened, or the mod stopped.

### `stock_left`

```lumascript
pub fn stock_left(s: Shop, ware: u32) -> u32?
```

What is left of a ware of a shop of the calling mod on this file (none: it never runs out). For a mod that shows its stock elsewhere (a sign, a HUD row).

### `restock`

```lumascript
pub fn restock(s: Shop, ware: u32)
```

A ware of a shop of the calling mod filled again to its first stock on this file (a daily restock is the mod's rule: it calls this when its day turns).

## Further reading

* [LumaKit reference](index.md)
* [About LumaKit](../../concepts/lumakit.md)
