# LumaKit: progress

Gives: stars beyond 120 and new star kinds counted, comets added to any galaxy, per-file flags and counters,
what a star gives, new-file rules.

```lumascript skip a listing from the part's contract, not a module that builds alone
pub fn stars() -> u32                                  // the game's and every mod's, on this file
pub fn has_star(galaxy: text, scenario: u32) -> bool
pub fn give_star(galaxy: text, scenario: u32)
pub fn on_star(f: fn(text, u32) -> StarGet)       // (galaxy, scenario)   // what getting a star does
pub enum StarGet { Star, Nothing, Instead(item.Gives) }
pub fn add_comet(galaxy: text, scenario: u32, comet: stage.Comet)
pub struct Flag { fn get(self) -> bool; fn set(self, on: bool) }
pub struct Number { fn get(self) -> i64; fn set(self, v: i64); fn add(self, d: i64) -> i64 }
pub fn flag(id: text) -> Flag                          // per file, following its copy and erase
pub fn number(id: text) -> Number                      // experience, achievements
pub fn on_new_file(f: fn())                            // New Game+
```

Built on: `save` (per-file values following copy and erase), `K.hook` (star counting, the comet table,
`GameDataFunction`), `K.call`, `kit.stage` (comet kinds). Conformance: stars counted past 120, `on_star` giving an
item instead, a comet added, flags and numbers following a file copy and erase, a new file rule.

LumaKit saves every kit value itself, in its own part of each save file under the owning mod's id: a value follows the
file as the game's progress does, stays while its mod is off or removed, and stays when the player forgets the mod,
until LumaKit is forgotten.
