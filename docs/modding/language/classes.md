# Classes derived from the game's

## What a mod does

`define_class(mod, &def)` on the game thread, usually in `start`. `def` names the class (the name a stage places it
by), the game class it derives from (`base`), the factory row that makes the base (`maker`, default the base's name; `LumaObj` makes a plain `LiveActor`; `LumaAreaCube`, `LumaAreaSphere` and `LumaAreaCylinder` a plain `AreaObj` that no game code reads),
optionally a name the game's stages already place whose objects become this class instead (`replaces`), the size and
alignment of its own data per object, a `made` callback, and its overrides (a virtual of the base by `method`'s name and
signature, and a wrap hook).

| Call | Does | Refuses |
|---|---|---|
| `define_class` | Takes each override's slot in the class's own table (below); makes the factory answer the class's name (and `replaces`) with the kernel's maker. Returns the class's number, at or above the game's record count. Logs `class <name>: from <base> (made by the factory's <maker>[; in place of <replaces>]), <n> bytes of its own, <k> overrides`. | -1 the definition is not whole: null, `size` below luma.h's, name empty or over 63 bytes, base not a polymorphic class with a row or without NameObj at its start, data_align not a power of two up to 4096, 1024 classes already this session; -2 the name is a game class's, a factory row's or a live class's, or `replaces` is replaced already; -3 not the game thread; -4 an override names no virtual, one not of the base (or the base's twice), one declared by a class the factory does not make, one of a base not at the base's start, has no hook, returns a class with a destructor or no default constructor, names one slot twice, or the build gives no layout of the base's table (a template's instance) or its slot; -5 no factory row named `maker`, or `replaces` names nothing the factory makes. Each is logged `define_class <name>: <why>`; nothing of the class is left. |
| `class_data` | The object's own data (from the object or any subobject) when it is one of that class's, else null. Any thread. | |
| `class_of`, `own_class`, `as_class`, `is_a` | Know the class: `class_of(name)` its number (a game record of the name wins: such names are refused); `own_class` of one of its objects is the class's number; `as_class(object, class)` the whole object; `is_a` true for the class and every game class of the base's. | |

## How an object is made

The stage asks `NameObjFactory::getCreator(name)`; a kernel after hook gives the kernel's maker for a live class's name
or `replaces`. The maker calls the base's maker with the maker row's own name, checks the object's own class is the base
(else it logs once `class <name>: the factory's <maker> made a <class>, not a <base>; its objects are the game's` and the
objects stay the game's), gives it zeroed data from the mod's memory (`alloc`; none left: logged, the object is a plain
base object), records it, gives a class with overrides its own table, and calls `made(object, data, user)` before the
stage calls its init, on the thread that makes the stage's objects.

A class's data is zero-filled before `made`: every byte of it is 0 when `made` (or any override) first reads it.

## What an override is

A slot of the class's own table. The build gives every virtual of a class the factory makes, inline ones
too, an entry: code with the virtual's own parameters and result that hands the call to the kernel (gen_types.py
slot_code), and the layout of each such class's table at its start (the compiler's -fdump-vtable-layouts; TypeRow
vtablePrefix and vtableCount). The class's first object has its table copied whole (the prefix and RTTI with it, so
typeid and the kernel's own_class read the base), each overridden slot taking its entry; every object of the class gets
that table before `made`. A virtual call on one of them runs the mod's hook with the call (`function` is the virtual's
name and signature, args[0] the object); `core->proceed(call)` runs what the base's table held in the slot, so other
mods' hooks on that function still run. A hook that faults (or whose mod is off) before it proceeded is proceeded for.
Only the class's objects reach its entries: no other call pays anything. A virtual of a second base (not at the base's
start) is refused for now. The game's own tables are never written.

## When objects and classes end

- NameObj's destructor (a kernel before hook) forgets the object and frees its data; the scene's destruction
  (`GameSystemSceneController::destroyScene` calls `luma::classes::sceneGone`) forgets the rest.
- The mod goes (taken out, start refused, unloaded): after its hooks, its classes end, a make in progress finishes, and
  their objects are the base's again (each gets the base's table back; a call already in an entry runs the base's code). Logged `its <n> classes ended; their <m> objects are the game's own now`. The
  objects' data is the mod's memory, freed with it after the grace (counted in `takeout.<id>`).
- A class number is never reused in a session.
- gone (LumaClassDef's last field): `void (*gone)(void* object, void* data, void* user)`, called once for each object the class kept (whose `made` ran), before the kernel frees its data, through the guard as `made` is: in NameObj's destructor (object: its address, only its NameObj part still whole: read `data`), at the scene's destruction (object null: the scene's memory went with it), and when an object is made at an address one of the class's had without its destructor seen (object null). On the thread that runs them. Not called when the mod goes (its memory goes whole). A definition whose `size` ends before `gone` (offsetof) is taken as before, with none; smaller than that is refused as before. Tests f/tests/gone 1405 (once each, its own data, through a scene change) and 1406 (a definition without it: never called).

## make

`int32_t (*make)(const LumaMod* mod, const char* name, uint32_t count, const LumaPlace* place, uint32_t flags, void objects)`,
the last field of LumaCore. `count` (1 to 1024) objects by a name the factory makes, made as PlacementInfoOrdered makes a
placed one: NameObjFactory::getCreator (a game object, a mod class, a mod's object, a mod's change of the name), the
creator given the mod object's name or the Japanese name, initStep to the end with a placement row, then the old
object_classes attach. The row is a table the kernel writes in the scene's memory (one per call, a row per object):
name, pos_x/y/z, dir_x/y/z (degrees), scale_x/y/z, Obj_arg0 to 7 from `place[i]` (`LumaPlace` {size, pos[3], rot[3],
scale[3], args[8]}; an array of `count` with the first's size as the stride; null: the origin, unturned, scale 1, every
argument -1). Every other column reads as absent (no switch, link, rail, view group), as for an object made with no
placement; the placement file (self-description, 1.8 places) names what each class reads. Its two reserved words mark
it, and StageDataHolder::findPlacedStageDataHolder answers the main stage for it: made objects stand in zone 0, the
placement zone set to 0 for their init and the caller's put back after. While the scene makes its objects
(isInitializeStatePlacementSomething: after initPlacementMario, during the placements) or once it is made
(isInitializeStateEnd: during play, the scene's arena current for the table and the objects, each object's
initAfterPlacement run in the call, as LumaPlayer runs an extra player's; NameObjExecuteHolder and DrawBufferExecuter
take objects made after the scene connected). No scene, or the scene between the two (running the after-placement
steps): -3, said, as off the game thread. Clipping system info: when no placement has actors waiting for theirs, ours are taken as given none; made inside
another placement's init they get that placement's, as the objects it makes. LUMA_MAKE_DEAD: makeActorDead after init
(actors only; another object is made alive and that is said). Returns the count made (fewer when a creator makes
nothing, said); -1 no factory row by the name; -2 its files are not in this game (NebulaMods::isMissingFiles); -4 a bad
argument (count, a place smaller than LumaPlace or not finite, an unknown flag) or no scene memory. Test f/tests/make
1407 (eight Kuribo with their pos, scale and Obj_arg0, of class Kuribo, alive; one dead that appears; one made during
play; the refusals).

## alias (K3, LumaKit's actor, area and ride kinds)

`int32_t (*alias_class)(const LumaMod* mod, const char* name, const char* class_name, const int32_t args[8])`, after
`make`. Another name for a live mod class (any mod's): the factory answers `name` with the class's maker, so a stage
that places `name` and `make` by it make an object of the class. While that object initialises (PlacementInfoOrdered's
initStep, make's), its placement row's Obj_argN read as args[N] where args[N] is not -1; -1 leaves the row's own. The
override is in PlacementUtil's one Obj_arg reader (getPlacementArgNoInit), on the thread that places, for that row only
(nested placements each their own); the stage's tables are never written (they may be read-only file mappings). The
alias is the calling mod's; it goes when that mod goes or the class does; the same mod calling again for the name
changes it. 0 stands; -1 a bad argument (null, a name empty or over 63 bytes, null args, 4096 aliases standing, no
memory); -2 the name is a game class's, a factory row's, a mod class's or another mod's alias; -3 not on the game
thread; -5 no live mod class `class_name`. Each said `alias_class <name>: <why>`; a standing one `alias <name>: places
<class> (arguments ...)`.

## placement (K7, LumaKit's actor placement / find_placed)

`LumaPlacement` {size, stage, zone, layer, index}: the scene's main stage, the zone (a stage name; the main stage's
own), the layer ("Common", "LayerA" to "LayerAE"), the index among that zone's placements of that layer (its
Placement files, then its MapParts files, in the archive's order, each file's rows in order); the same on every
machine with the same game and mods. `placement_of(mod, object, out)`: 0 and *out (names valid while the scene
lives), -1 none (no scene, made by code or `make`, not a placement's object), -3 not the game thread, -4 null object
or out, or out smaller than LumaPlacement (said). `placed_at(mod, place)`: the object the scene made from that
placement (stage null or the main stage), or null (none in this scene, nothing made there,
not made yet, not the game thread, a bad place said). Each placement (PlacementInfoOrdered::Index) keeps the object
its creator made (one pointer); the key is worked out on demand from the layer masks each zone read its files with.
The scene's objects live as long as the scene.

## gravity (K8, LumaKit's gravity shapes)

A class whose `base` is "PlanetGravity" is a gravity class (maker and replaces null, else -1): no stage places it and no
alias names it (-5). `void* (*make_gravity)(const LumaMod* mod, const char* class_name, const LumaGravityPlace* place)`,
after `placed_at`, makes one: `new PlanetGravity`, then `place`'s range (mRange, below 0 everywhere), priority
(mPriority), types (mGravityType bits 1 normal, 2 shadow, 4 magnet, 8 launcher; 0 is 1) and inverse (mIsInverse), the
class's data zeroed, its table (its overrides) and `made`, then MR::registerGravity: the scene's gravity manager asks
its calcOwnGravityVector (pDest the direction, pScalar the distance the pull falls off with, 4000000 / distance²) at
every point anything asks its gravity at; the highest priority that answers wins, equal ones add. Range is not tested
for the class: an override that wants one calls isInRangeDistance, as the game's gravities do. It lives as long as the
scene (no destructor: `gone` with a null object at the scene's end); when its mod goes its table is the base's again
and it answers nowhere. Null: not the game thread, or said `make_gravity <class>: <why>` (no live class of the name, its
base is not PlanetGravity, no scene with gravities, no memory, a null or short place, a range that is no number, a type
bit above 8). gen_types.py lays PlanetGravity's table out (a pointer to one of its virtuals in the vtables unit) and
gives its virtuals slot entries, as NameObj's line has.

## Costs

Nothing until a mod defines a class. Then: the factory's lookup by name (at a stage's load), NameObj's destructor (one
load when no object lives), and each overridden row (one vtable read per call; a locked table lookup only for objects of
the base class).

## Not in this (later, by need)

A class derived from a mod's class; virtuals the base runs
through a function with no row (refused, -4: the generator's coverage decides).
