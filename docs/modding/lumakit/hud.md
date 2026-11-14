# LumaKit: hud

Gives: counters, timers, meters, text, pictures in corners; text in 3D; popups; subtitles; the game's own HUD parts
hidden.

```lumascript skip a listing from the part's contract, not a module that builds alone
pub enum Corner { TopLeft, Top, TopRight, Left, Right, BottomLeft, Bottom, BottomRight }
pub struct Timer { fn set(self, frames: u64); fn follow(self, f: fn() -> u64); fn remove(self) }
pub struct Text { fn set(self, s: text); fn remove(self) }
pub struct Meter { fn set(self, value: int); fn remove(self) }
pub struct Picture { fn set(self, tex: draw.Texture); fn remove(self) }
pub struct Counter { fn set(self, value: i64); fn remove(self) }
pub fn timer(at: Corner, frames: u64 = 0) -> Timer
pub fn text(at: Corner, s: text, frames: u32? = none) -> Text   // frames: removes itself after
pub fn meter(at: Corner, max: int, label: text? = none) -> Meter
pub fn picture(at: Corner, tex: draw.Texture, size: vec2) -> Picture
pub fn counter(at: Corner, value: i64, icon: draw.Texture? = none) -> Counter
pub fn above(of: LiveActor, s: text, height: f32 = 150.0) -> Text   // follows the actor (idea 54)
pub fn float_text(at: vec3, s: text, frames: u32 = 60) // rises and fades (idea 135)
pub fn popup(title: text, line: text, icon: draw.Texture? = none)  // the achievement popup (idea 127)
pub fn subtitle(line: text, speaker: text? = none, frames: u32 = 180)   // idea 172
pub enum GamePart { Lives, Coins, StarBits, Stars, Health, All }
pub fn hide_game(part: GamePart, hidden: bool)
```

Built on: `ui` (layouts), `draw` (2D at the HUD point, text in the game's font), `C.layout`, `C.font`, `C.text`,
`K.hook` (the game's counters, `CounterLayoutController`). Text drawn with the disc's message font; a
mod's text is checked against it by `nfnt_glyphs.py` and by the validator. Conformance: each element in
each corner, stacking, removal, `follow`, the game's lives hidden, a popup at 1.3x, a mod off with five
elements up (all gone).
