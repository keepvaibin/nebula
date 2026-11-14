# LumaKit: item

Gives: things picked up, placed in stages by name or made by code, in containers, carried in an inventory.

```lumascript skip a listing from the part's contract, not a module that builds alone
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
pub enum Gives { Form(form.FormId), Coins(u32), StarBits(u32), Lives(u32), Health(u32), Carry(u32),
                 Call(fn(MarioActor)) }        // Carry: n into the inventory under the item's id
pub enum Container { None, Bubble, Crystal }
pub struct ItemId { fn spawn(self, at: vec3) -> LiveActor; fn give(self); fn carried(self) -> u32;
                    fn use_one(self) -> bool }
pub fn define(i: Item) -> ItemId
pub fn find(id: text) -> ItemId?
pub fn carried_all() -> list<(ItemId, u32)>  // the inventory, in the order first carried (idea 56's wheel)
```

`form.Form` refers to `item.ItemId` and `item.Gives` to `form.FormId`: the two are one library and the cycle is
between types of one build. Data `kit/item/<id>.json`. Built on: `K.derive` (`LiveActor`), `K.call` (`MR::incCoin`,
`incStarPiece`, `incPlayerLife`, `MR::addHitSensor`), `C.model`, `C.fx`, `C.sound`, `save` (the inventory per file).
Conformance: an item placed by name and one spawned, each kind of `Gives`, both containers, an inventory kept across
a save and reload, an item of a mod that is off (not made, logged). Corpus: 1, 38, 39, 51, 56. B: F. A: G.

LumaKit saves every kit value itself, in its own part of each save file under the owning mod's id: a value follows the
file as the game's progress does, stays while its mod is off or removed, and stays when the player forgets the mod,
until LumaKit is forgotten.
