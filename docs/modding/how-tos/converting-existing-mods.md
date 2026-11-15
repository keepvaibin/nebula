---
title: Converting existing mods
shortTitle: Convert existing mods
intro: Turn a mod made for the Wii, such as a Riivolution pack, a texture pack, or a Gecko code list, into a .luma that Nebula uses.
contentType: how-tos
---

## About converting mods

Most Super Mario Galaxy mods were published for the Wii and for Dolphin. LumaLink reads them and writes a mod in Nebula's form. Each file is cooked the way the game cooks it, so a file the game can't use is found when you convert, not when you play.

LumaLink converts these kinds of mods:

| Kind | What LumaLink reads |
| ---- | ------------------- |
| Riivolution pack | The `<wiidisc>` XML file and the files it names, with its options. |
| Disc-layout folder | A folder laid out like the game disc, for example `files/StageData/...`. |
| Loose files | Game files without a folder structure. LumaLink places each one against your game. |
| Texture pack | Dolphin texture packs (`tex1_...` file names), loose or with a `manifest.json`, and packs that use Nebula's texture names. |
| Gecko code list | `.gct` files and Dolphin game settings files. Plain memory writes become table changes. |

A mod can be a folder, an archive (`.zip`, `.7z`, or `.rar`), or a single file.

### Options and regions

* Each option in a Riivolution pack becomes a choice the player makes on the game's **Mods** page. Each choice is a layer of the mod.
* An option that only picks the game's region is resolved for you: what all regions share is always used, and what differs is used only for that region.
* Patches written for the European or Japanese release are mapped to the matching places in the game.

### Gecko codes

LumaLink reads each plain memory write in a code list as a change to the game, the same way it reads a Riivolution memory patch.

Some codes are left out, and LumaLink says why for each one:

* Popular codes, such as moon jump, infinite health, or fast text, are recognized by their names. When Nebula has a setting that does the same, LumaLink names the setting. Otherwise, LumaLink tells you to write the behavior in LumaScript.
* Codes with conditions, jumps, or inserted machine code can't be read as changes. LumaLink counts them. To bring that behavior over, write it in LumaScript in your mod's `source/` folder.

For more information on writing LumaScript, see [Creating a mod](creating-a-mod.md).

## Prerequisites

* LumaLink. For more information, see [Installing LumaLink](../get-started/installing-lumalink.md).
* Nebula, installed with Nebula Setup. LumaLink places files and names textures against your game. Without it, files whose place depends on the game can't be placed.

## Converting a mod in the LumaLink window

1. Open LumaLink.
1. Click **Add mods**, then select the mod: its folder, archive, or file. You can also drag the mod onto the LumaLink window.

   LumaLink shows the mod as "A mod made for the Wii."
1. Select the mod to open its screen.
1. Click **Make a .luma**, then choose where to save the file.

When the conversion works, LumaLink shows "Your .luma is ready." To add the mod to Nebula, open the `.luma` file, or drag it onto the game's window.

### When some files can't be used

When the game can't use some of the mod's files, LumaLink lists each file and the reason, and makes no `.luma`. To make the `.luma` without those files:

1. On the mod's screen, turn on **Leave out the files the game cannot use**.
1. Click **Make a .luma** again.

### Editing the converted mod

To change the mod before you share it, convert it to a folder instead of a `.luma`:

1. On the mod's screen, click **Make an editable folder**.
1. Choose where to put the folder.

LumaLink writes the folder and checks it. The folder opens in LumaLink as "A mod you are making." Edit its files, then click **Play in Nebula** to try it, or make a `.luma` to share it. For more information, see [Packing and sharing a mod](packing-and-sharing-a-mod.md).

## Converting a mod from the command line

1. Open a terminal.
1. Run `lumalink convert` with the mod and a new folder name:

   ```shell
   lumalink convert MyRiivolutionMod.zip my-mod
   ```

   LumaLink writes the folder `my-mod` and checks it. The folder must not exist yet.
1. Pack the folder into a `.luma`:

   ```shell
   lumalink pack my-mod
   ```

To set the mod's name or authors, use `--name` and `--author`. To leave out files the game can't use, add `--accept-losses`. LumaLink names each file it leaves out.

For every option, see [lumalink convert](../reference/lumalink-commands/convert.md).

## Troubleshooting

* "left out: FILE: REASON" The game can't use this file. The reason names the problem, for example a format the game doesn't read. Fix the file in the editable folder, or leave it out.
* Textures keep their Dolphin names. LumaLink found no installed game, or the game has none of those textures. Install Nebula with Nebula Setup, then convert again.
* A Gecko code is left out. Nebula does what the code does with a setting, or the code needs to be written again in LumaScript. Follow the advice LumaLink shows for it.

## Further reading

* [Packing and sharing a mod](packing-and-sharing-a-mod.md)
* [Mod files and layers](../concepts/mod-files-and-layers.md)
* [lumalink convert](../reference/lumalink-commands/convert.md)
