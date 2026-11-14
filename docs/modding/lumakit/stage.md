# LumaKit: stage

Gives: new galaxies with stars and comets in the domes, a new dome, music per galaxy, warping, stars appearing, the
hub replaced, stages built in code.

```lumascript skip a listing from the part's contract, not a module that builds alone
pub enum Dome { Terrace, Fountain, Kitchen, Bedroom, Engine, Garden, Mod(name: text) }
pub enum Unlock { Always, Stars(u32), Star(galaxy: text, scenario: u32), Code(fn() -> bool) }
pub enum Comet { Speedy, Daredevil, Cosmic, FastFoe, Purple }
pub struct Star { scenario: u32, title: text, comet: Comet? = none, hidden: bool = false }
pub struct Galaxy { name: text, title: text, dome: Dome, unlock: Unlock = Unlock.Always, stars: list<Star>,
                    music: sound? = none, icon: draw.Texture? = none }
pub struct GalaxyId { fn go(self, scenario: u32); fn name(self) -> text }
pub struct DomeDef { name: text, title: text, unlock: Unlock = Unlock.Always }
pub fn define_galaxy(g: Galaxy) -> GalaxyId
pub fn define_dome(d: DomeDef)
pub fn find(name: text) -> GalaxyId?
pub fn go_to(galaxy: text, scenario: u32)            // MR::requestChangeStageInGameMoving
pub fn appear_star(at: vec3, scenario: u32)
pub fn music(galaxy: text, scenario: u32?, s: sound)
pub fn hub(stage: text)                               // the hub replaced
pub fn build(name: text, make: fn(var Builder))      // a stage made in code when it loads
pub struct Builder { fn place(var self, kind: text, at: vec3, rotation: vec3 = vec3(0, 0, 0), args: list<int> = []);
                     fn planet(var self, look: model, collision: model, at: vec3); fn gravity(var self, f: gravity.Field) }
```

Data `kit/stage/<name>.json`: galaxies and domes. Built on: `C.stage`, `C.text`, `C.sound`, `assets.provide`†
(`build`: the stage made at run time goes through the cooker and validator), `K.hook` (`AstroDomeGalaxySelector`,
`ScenarioSelectLayout`, `ScenarioSelectScene`, `MR::startStageBGM`), `K.call` (`MR::requestChangeStageInGameMoving`, `MR::requestAppearPowerStar`,
`PowerStarEventKeeper`), `kit.progress`. Conformance: a galaxy listed in its dome after 20 stars, its stars got and
counted, a comet star, warp, music, a stage built in code with a planet and two objects, a downloaded stage (through
`svc.mods` offer for one start, P-18) as a galaxy.
