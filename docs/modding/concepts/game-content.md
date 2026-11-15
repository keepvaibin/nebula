---
title: About game content in mods
shortTitle: Game content
intro: Learn which kinds of content a mod can bring, which source formats it uses, and how Nebula turns them into the game's own data.
contentType: concepts
---

## About content

A mod brings content as ordinary source files: glTF models, PNG pictures, WAV sounds, JSON descriptions. When the game starts, Nebula prepares each source into the game's own format and stores it with the game's files. The game then loads, draws and plays a mod's content with the same code it uses for its own. A mod's model costs no more to draw than a game model of the same size, and every renderer draws it.

Prepared content is kept between starts, so a mod's files are prepared once. When a source file cannot be prepared, the mod is off for that start, and the mod log names the file and the reason in the player's language.

## Kinds of content

| Content | Source | Becomes | For more information |
|---|---|---|---|
| Models | glTF (`.glb`, `.gltf`) with PNG or KTX2 textures | The game's model, with its materials and joints | [Adding models](../how-tos/adding-models.md) |
| Animations | The animations in a glTF model | The game's clips: joints, texture movement, colors and texture swaps | [Adding animations](../how-tos/adding-animations.md) |
| Materials | HLSL with a `.shader.json` description | A material program for every renderer | [Writing material shaders](../how-tos/writing-material-shaders.md) |
| Particle effects | JSON | Particles in the game's own particle data | [Adding particle effects](../how-tos/adding-particle-effects.md) |
| Sounds and music | WAV, FLAC or Ogg Vorbis, with a `.sound.json` description | Sound effects and music streams | [Adding sounds and music](../how-tos/adding-sounds-and-music.md) |
| Screens and fonts | Layout JSON, PNG, TrueType and OpenType fonts | The game's layouts and fonts | [Adding screens and fonts](../how-tos/adding-screens-and-fonts.md) |
| Stages | Placement table patches, collision models | Objects placed in the game's stages, or new stages | [Placing objects in stages](../how-tos/placing-objects-in-stages.md) |
| Text | Message patches | Lines in the game's message files, in each language | [Changing Nebula's text](../how-tos/changing-nebulas-text.md) |
| Movies | WebM or MP4 | Movies the game plays, on the screen or on a model | [A video in a galaxy](../tutorials/a-video-in-a-galaxy.md) |
| Textures | PNG or DDS, by the game's texture names | Replacements for the game's textures | [About mod files and layers](mod-files-and-layers.md) |

## Replacing or adding

A mod can replace a piece of the game's content or add a new one.

* To replace, put the file at the game's path in `files/`, such as `files/ObjectData/Kuribo.arc/Kuribo.glb`. The game loads your model wherever it loaded its own.
* To add, put the file in `assets/`. The game knows it as `MOD-ID:PATH`, and your code or your stage placements refer to it by that name.

## Content and code together

Content alone can change how the game looks and sounds, and table patches can change how it plays. To make new behavior, such as a new enemy that uses your model, add code. LumaKit's `actor`, `item` and `stage` modules connect your content to new kinds of objects. For more information, see [About LumaKit](lumakit.md).

## Further reading

* [About mod files and layers](mod-files-and-layers.md)
* [A new galaxy](../tutorials/a-new-galaxy.md)
