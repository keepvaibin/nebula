---
title: Reading the mod log
shortTitle: Read the mod log
intro: Find out why a mod did not start or did not do what it should, from the notices in the game, the mod log page and the game's log.
contentType: how-tos
---

## About the mod log

Nebula keeps one log of mod problems for each session. Every entry names the mod, says what went wrong in the player's language, and gives the place: a file of the mod and a line, or a game function.

Each entry is an **error** or a **note**:

* Error: Something the mod meant does not happen. The mod was refused or turned off, a placeholder stands in for a missing file, or a call did nothing.
* Note: Nothing was lost, but something is worth knowing. For example, two mods changed the same file and the load order settled which one is used.

Entries come from six places, and the mod log says which one:

| Source | Shown as |
|---|---|
| The check of the mod | "Found when checking the mod" |
| Starting the mod's code | "Found when starting the mod's code" |
| Preparing the mod's files | "Found when preparing the mod's files" |
| The mod's code, or a service it called | "Reported by the mod's code" |
| The game, while playing | "Found while playing" |
| The guard, when the mod's code failed | "The mod's code failed and was stopped" |

## Notices while playing

When a new error happens while you play, a one-line notice shows at the bottom left of the screen for six seconds. It shows the mod's ID and the problem. At most three notices show at once. The notices never pause the game or take input.

Problems found while the game starts are not shown one by one. When the title screen appears, one notice gives their count.

## Reading the mod log page

1. Open **Settings**, then select **Mods**.
1. Select **Log**, then **Open**. The entries are grouped under each mod's name.
1. Select an entry. The line under the page gives its source, its code, and its text.
1. To read the whole entry, press <kbd>A</kbd> or <kbd>Space</kbd>. Long entries, such as a list of the signatures the game has for a function, show here in full.

A mod's own page on the **Mods** page shows that mod's entries under its options.

An entry that happened more than once shows its count, such as "×12". The log keeps at most 256 entries for each mod. When a mod has more, the last line says how many were not kept.

## Reading the game's log

Every entry is also written in English to the game's log, with everything else the game reports. Your mod's `log` lines go there too, as `[mod MOD-ID] TEXT`.

On Windows, Nebula started from the Start menu writes the log to `logs\latest.log` in Nebula's settings folder. The previous start's log is `logs\previous.log`. Nebula started from a terminal prints the log there instead.

In a test run, the game's log is `run.log` in the run's folder. For more information, see [Testing a mod](testing-a-mod.md).

## Writing to the log from your mod

In LumaScript, `log` writes any text to the game's log:

```lumascript
on start {
    log "started"
}
```

`log fact NAME VALUE` also records a value that a test can check. For more information, see [Testing a mod](testing-a-mod.md).

## Looking up a code

Entries from the check of a mod have a code such as `L505`. LumaLink explains any code:

```shell
lumalink explain L505
```

For the list of codes, see [Validator rules](../reference/validator-rules.md).

## Further reading

* [When a mod fails](../concepts/when-a-mod-fails.md)
* [How Nebula loads mods](../concepts/how-nebula-loads-mods.md)
