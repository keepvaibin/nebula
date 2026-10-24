# Changing Nebula's text and graphics

Nebula adds a few things to the game's screens: the Quit to desktop button, the quit and cutscene questions, and the
achievement pop-up. They are built from the game's own layouts, fonts and textures, and their text comes from one table
in every language the game was released in. A mod can change any of it, and a translation into a language the game
never had can bring its own.

## What is on screen and where it comes from

| Element | Text ids | Layout and art (from the game files) |
|---|---|---|
| Quit to desktop button (file select) | `QuitToDesktop` | `LayoutData/FileSelect.arc`, its Start button |
| Quit question | `QuitQuestion` | the game's question window (`LayoutData/SysInfoWindow.arc`), its own Yes and No |
| Cutscene question | `SkipCutsceneQuestion` | the same window |
| Achievement pop-up | `AchievementGet`, `Achievement.<id>` | `LayoutData/InformationWindow.arc` (window `InfoWinC`, text `ConfirmC`, icon `ConfirmU`); icons from the picture font |
| Achievement descriptions (settings, achievement board) | `AchievementHint.<id>` | |

The ids and every built-in translation are in [`localization/NebulaText.tsv`](../localization/NebulaText.tsv).

## Languages

Each language is the game's language folder. The Wii versions have the first twelve; Portuguese and Traditional
Chinese come from the Switch release and have no Wii folder.

| Folder | Language | Folder | Language |
|---|---|---|---|
| `UsEnglish` | English (US) | `EuEnglish` | English (UK) |
| `UsFrench` | French (Canada) | `EuFrench` | French (Europe) |
| `UsSpanish` | Spanish (Latin America) | `EuSpanish` | Spanish (Europe) |
| `EuGerman` | German | `EuItalian` | Italian |
| `EuDutch` | Dutch | `UsPortuguese` | Portuguese (Brazil) |
| `JpJapanese` | Japanese | `KrKorean` | Korean |
| `CnSimpChinese` | Chinese (Simplified) | `TwTradChinese` | Chinese (Traditional) |

A text missing in one language falls back to the same language of the other region (UsFrench and EuFrench, ...), then
to American English.

## Changing the text: NebulaText.txt

Put a file named `NebulaText.txt` in the played language's `MessageData` folder, next to the game's `Message.arc`:

```
/UsEnglish/MessageData/NebulaText.txt
```

UTF-8, one text per line:

```
# Lines starting with # are comments.
AchievementGet = Achievement unlocked!
QuitToDesktop = Leave the game
SkipCutsceneQuestion = Skip this scene?
Achievement.first_star = Your First Power Star
```

- `\n` is a line break (text boxes do not wrap; break long questions yourself), `\\` a backslash.
- `{galaxy:InternalName}` is that galaxy's name from the game's own text, for example `{galaxy:HeavensDoorGalaxy}`
  (Gateway Galaxy). The internal names are the folder names in `StageData`.
- Ids not in the file keep Nebula's own text.
- `language = EuGerman` (any folder from the table) uses Nebula's built-in German for every id the file does not
  list. A German translation mod that replaces the files in `UsEnglish` needs only this line.

The game reads the file once, when Nebula's first text is shown. Its log (`game: NebulaText: ...`) names the folder it
used and how many texts it read.

### As a Nebula mod

```
mods/
  My Translation/
    files/
      UsEnglish/
        MessageData/
          NebulaText.txt
```

Import the folder (or a zip of it) from the launcher's Mods tab or Settings > Mods. A mod whose text covers other
languages than the one played stands in for it (Settings > Mods, its Text choice); Nebula's text follows it, and the
mod's own `NebulaText.txt`, if it has one, comes along.

### As a Riivolution patch

```xml
<wiidisc version="1">
  <id game="RMG" />
  <options>
    <section name="Translation">
      <option name="Nebula text">
        <choice name="On"><patch id="nebulatext" /></choice>
      </option>
    </section>
  </options>
  <patch id="nebulatext">
    <file disc="/UsEnglish/MessageData/NebulaText.txt" external="/MyTranslation/NebulaText.txt" create="true" />
  </patch>
</wiidisc>
```

`create="true"` adds the file; the disc has none.

### A language the game never had

A translation replaces the game's own text in a folder the disc has (usually `UsEnglish/MessageData/Message.arc`) and,
for letters the game's font lacks, `LayoutData/Font.arc`. Add a `NebulaText.txt` beside it with every id translated,
so nothing falls back to English. The full list of ids is the first column of `localization/NebulaText.tsv`.

## Changing the graphics

Nebula draws nothing of its own: the button, the questions and the pop-up use the game's layouts and textures, so a
texture pack that replaces those textures changes Nebula's elements too.

1. Turn on Settings > Mods > Dump textures and open the screen (the file select for the button, any stage for the
   pop-up). Every texture the game shows is saved under its texture-pack name.
2. Settings > Mods > Open texture dumps folder. The pop-up's window is the information window's frame texture; the
   icons are the picture font's sheets.
3. Edit the PNG, keep its name, and put it in a mod's `textures/` folder.

A mod can also replace the layout archives themselves (`files/LayoutData/InformationWindow.arc`, ...). The pop-up
finds its panes by name (`InfoWindowCenter`, `InfoWinC`, `ConfirmC`, `ConfirmU`); a replacement layout must keep them.

### Achievement icons

Each achievement shows one picture-font glyph, chosen in `native/petari/src/Game/System/NebulaAchievements.cpp`:

| Glyph code | Picture | Used for |
|---|---|---|
| `7` | Power Star | First Star, Halfway There |
| `K` | Grand Star | Gateway and the Bowser galaxies |
| `L` | Luigi | Luigi Found, Luigi's Turn, Enter Luigi |
| `X` | Peach | The Universe Reborn |
| `Y` | letter | Bedtime Story |
| `P` | crown | Every Power Star |
| `W` | rainbow star | Grand Finale |
| `e` | shooting star | comets |
| `B` | Mario | Cosmic Rival |
| `N` | purple coin | Purple Coin Master |
| `Z` | question mark | Hidden Treasures |
| `O` | Green Star | Green Stars |
| `J` | Silver Star | Trial Run |
| `c` | Hungry Luma | Well Fed, Snack Time |
| `;` | Star Bit | Star Bit Bank, Pocketful of Star Bits |
| `a` | 1-Up Mushroom | Extra Lives |
| `S` | Red Star | Taking Flight |

Replacing a glyph's art in the picture font sheet (a texture pack, or `Font.arc`) changes it everywhere the game uses
that picture.

## Adding a translation to Nebula itself

Edit `localization/NebulaText.tsv` (UTF-8, tab-separated: an id column, then one column per language folder; an empty
cell falls back as above), then run:

```
python tools/nebula_text.py
```

It writes `native/petari/src/Game/System/NebulaTextTable.inc`, which the game compiles; `--check` fails when the two
differ. Use each version's own words for the game's things (its name for Power Stars, Lumas, comets, Rosalina) and
leave galaxy names as `{galaxy:...}` tokens.
