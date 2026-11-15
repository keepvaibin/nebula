---
title: Packing and sharing a mod
shortTitle: Pack and share a mod
intro: Pack a mod folder into one .luma file that players add to Nebula on any platform.
contentType: how-tos
---

## About .luma files

A `.luma` file holds a whole mod: its `luma.json`, layers, files, assets and code for every platform. One `.luma` works on Windows, Linux, macOS, Android and iOS. Players never need LumaLink.

## Packing a mod

1. Set the version in `luma.json`. Use `MAJOR.MINOR.PATCH`, such as `1.2.0`, and raise it with every release, so players and other mods can tell versions apart.
1. If the mod has code, build it for every platform.

   ```shell
   lumalink build MOD-FOLDER
   ```

1. Pack the mod.

   ```shell
   lumalink pack MOD-FOLDER
   ```

   LumaLink checks the mod with every rule first. If the check finds an error, nothing is written. When the check passes, LumaLink writes `MOD-ID-VERSION.luma` beside the folder. To choose another file, use `-o FILE`.

To also check that every mod your mod needs is present, pass the folder that holds them with `--mods FOLDER`. For every option, see [lumalink pack](../reference/lumalink-commands/pack.md).

## Checking a .luma

Before you share a `.luma`, check the file itself the way Nebula will:

```shell
lumalink check MOD-ID-VERSION.luma
```

To confirm that a `.luma` is exactly what its folder packs to, run `lumalink verify MOD-ID-VERSION.luma --source MOD-FOLDER`. For more information, see [lumalink check](../reference/lumalink-commands/check.md) and [lumalink verify](../reference/lumalink-commands/verify.md).

## Sharing a mod

Share the `.luma` file itself. When your mod needs another mod, such as LumaKit, say so where you share it. Nebula ships LumaKit, so players already have it.

If your mod asks for permissions, say what each one is for. Players see your reasons, from the `--why` text, when they turn the mod on.

## How players add a mod

Players add a `.luma` in one of these ways:

* Windows, Linux and macOS: Drop the file on the game's window. Or open **Settings**, select **Mods**, then **Import file**.
* Android: Open the file with Nebula from a file manager. Or open **Settings**, select **Mods**, then **Import file**.

Nebula checks the mod and shows the result. A mod with an error is not added. A new mod is on at the next start. If it has code or asks for permissions, Nebula asks the player first.

## Further reading

* [Creating a mod](creating-a-mod.md)
* [About permissions](../concepts/permissions.md)
* [When a mod fails](../concepts/when-a-mod-fails.md)
