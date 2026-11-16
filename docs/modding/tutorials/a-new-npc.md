---
title: Adding a new character to talk to
shortTitle: A new character to talk to
intro: Add a character with its own model, materials and dialogue, and place it in Good Egg Galaxy.
contentType: tutorials
example: a-new-npc
---

## Introduction

In this tutorial, you add a character named the Keeper in front of the starting point of Good Egg Galaxy. When Mario walks up to the Keeper, the game shows its <kbd>A</kbd> prompt. When the player presses <kbd>A</kbd>, the Keeper greets Mario and asks a question with two answers, then replies with a different line for each answer.

You learn how to:

* Add a model of your own to a mod.
* Add lines of text in every language the game supports.
* Place a new object in one of the game's stages with a table patch.
* Define a new kind of object with LumaKit's `actor` module, and run a conversation with the `talk` module.

The finished mod uses a simple placeholder model with its own materials, a cloth texture and a `Wait` animation. To use your own model, replace `Keeper.glb` with a model of the same name.

## Prerequisites

* Nebula, installed with Nebula Setup.
* LumaLink. For more information, see [Installing LumaLink](../get-started/installing-lumalink.md).
* A model in glTF format (`.glb`), or the placeholder model from the finished mod.

## Mod folder layout

When you finish, the mod folder contains these files:

```text
a-new-npc/
  luma.json
  source/main.lum
  layers/base/assets/Keeper.glb                     the model: meshes, materials, textures, the Wait clip
  layers/base/merge/MessageData/messages.json       his lines in every language
  layers/base/patches/StageData/EggStarGalaxy.arc/jmp/placement/common/objinfo.json
  tests/talk/test.json
```

## Creating the mod

1. Create the mod folder, and add LumaKit to the mod's needs.

   ```sh
   lumalink new a-new-npc --id luma-keeper --name "The Keeper" --summary "A new character to talk to, with its own model, materials and lines." --author YOUR-NAME
   lumalink add need lumakit a-new-npc
   ```

1. Optionally, add the mod's name and summary in each of the game's languages. The finished `luma.json` looks like this:

   ```json luma.json
   {
     "format": "1.0",
     "id": "luma-keeper",
     "version": "1.0.0",
     "name": "The Keeper",
     "summary": "A new character to talk to, with its own model, materials and lines.",
     "text": {
       "UsEnglish": {"name": "The Keeper", "summary": "A new character to talk to, with its own model, materials and lines."},
       "EuEnglish": {"name": "The Keeper", "summary": "A new character to talk to, with its own model, materials and lines."},
       "UsFrench": {"name": "Le Gardien", "summary": "Un nouveau personnage à qui parler, avec son modèle, ses matériaux et ses répliques."},
       "EuFrench": {"name": "Le Gardien", "summary": "Un nouveau personnage à qui parler, avec son modèle, ses matériaux et ses répliques."},
       "UsSpanish": {"name": "El Guardián", "summary": "Un personaje nuevo con quien hablar, con su propio modelo, materiales y frases."},
       "EuSpanish": {"name": "El Guardián", "summary": "Un personaje nuevo con quien hablar, con su propio modelo, materiales y frases."},
       "EuGerman": {"name": "Der Hüter", "summary": "Eine neue Figur zum Reden, mit eigenem Modell, eigenen Materialien und Sätzen."},
       "EuItalian": {"name": "Il Custode", "summary": "Un nuovo personaggio con cui parlare, con modello, materiali e battute propri."},
       "EuDutch": {"name": "De Hoeder", "summary": "Een nieuw personage om mee te praten, met een eigen model, materialen en zinnen."},
       "UsPortuguese": {"name": "O Guardião", "summary": "Um novo personagem para conversar, com modelo, materiais e falas próprios."},
       "JpJapanese": {"name": "キーパー", "summary": "話しかけられる新しいキャラクター。モデル、マテリアル、セリフはすべて独自です。"},
       "KrKorean": {"name": "키퍼", "summary": "말을 걸 수 있는 새 캐릭터. 모델, 재질, 대사가 모두 새것입니다."},
       "CnSimpChinese": {"name": "守护者", "summary": "可以交谈的新角色，拥有自己的模型、材质和台词。"},
       "TwTradChinese": {"name": "守護者", "summary": "可以交談的新角色，擁有自己的模型、材質和台詞。"}
     },
     "authors": ["you"],
     "game": { "id": "RMG" },
     "needs": [{ "id": "lumakit", "version": ">=1.0.0" }],
     "layers": [{ "name": "base" }],
     "code": { "platforms": ["windows-x64", "windows-arm64", "linux-x64", "linux-arm64", "macos-arm64", "macos-x64", "android-arm64", "android-x64", "ios-arm64"]}
   }
   ```

## Adding the model

Copy your model to `layers/base/assets/Keeper.glb`. When LumaLink packs the mod, it converts the model's meshes, materials, textures and animations into the game's formats. The code refers to the model as `model "Keeper"` and to its animation as `"Wait"`.

For more information, see [Adding models](../how-tos/adding-models.md).

## Adding the character's lines

Create `layers/base/merge/MessageData/messages.json`. Each line has a `default` text, used for English, and one text for each other language: `fr`, `es`, `de`, `it`, `nl`, `pt`, `ja`, `ko`, `zh-Hans` and `zh-Hant`. Together they cover all 14 of the game's languages.

```json
{
  "lines": {
    "Keeper_Hello": { "default": "Welcome, [player]. Shall I keep your star bits safe?", "fr": "Bienvenue, [player]. Je garde tes fragments d'étoile ?", "es": "Bienvenido, [player]. ¿Te guardo los trozos de estrella?", "de": "Willkommen, [player]. Soll ich deine Sternenteile verwahren?", "it": "Benvenuto, [player]. Custodisco i tuoi astrini?", "nl": "Welkom, [player]. Zal ik je sterrenstukjes bewaren?", "pt": "Bem-vindo, [player]. Guardo os teus pedaços de estrela?", "ja": "ようこそ、[player]。スターピースを預かろうか？", "ko": "어서 와, [player]. 스타칩을 맡아 줄까?", "zh-Hans": "欢迎，[player]。要我替你保管星星碎片吗？", "zh-Hant": "歡迎，[player]。要我替你保管星星碎片嗎？" },
    "Keeper_Yes": { "default": "Yes", "fr": "Oui", "es": "Sí", "de": "Ja", "it": "Sì", "nl": "Ja", "pt": "Sim", "ja": "はい", "ko": "예", "zh-Hans": "好", "zh-Hant": "好" },
    "Keeper_No": { "default": "No", "fr": "Non", "es": "No", "de": "Nein", "it": "No", "nl": "Nee", "pt": "Não", "ja": "いいえ", "ko": "아니요", "zh-Hans": "不用", "zh-Hant": "不用" },
    "Keeper_Thanks": { "default": "They are safe with me.", "fr": "Ils sont en sécurité avec moi.", "es": "Conmigo están a salvo.", "de": "Bei mir sind sie sicher.", "it": "Con me sono al sicuro.", "nl": "Bij mij zijn ze veilig.", "pt": "Comigo estão seguros.", "ja": "確かに預かったよ。", "ko": "내가 잘 맡아 둘게.", "zh-Hans": "放在我这里很安全。", "zh-Hant": "放在我這裡很安全。" },
    "Keeper_Bye": { "default": "Come back any time.", "fr": "Reviens quand tu veux.", "es": "Vuelve cuando quieras.", "de": "Komm jederzeit wieder.", "it": "Torna quando vuoi.", "nl": "Kom gerust terug.", "pt": "Volta quando quiseres.", "ja": "いつでもおいで。", "ko": "언제든 다시 와.", "zh-Hans": "随时再来。", "zh-Hant": "隨時再來。" }
  }
}
```

The text `[player]` is replaced with the player's name. For more information, see [Message tags](../reference/lumascript/message-tags.md).

## Placing the character

Create `layers/base/patches/StageData/EggStarGalaxy.arc/jmp/placement/common/objinfo.json`. This patch adds one row to Good Egg Galaxy's object table: the Keeper, under the mod's ID, a few steps in front of the starting point.

```json
{
  "patch": 1,
  "key": ["l_id"],
  "add": [
    {
      "name": "luma-keeper/LumaKeeper",
      "l_id": 9412,
      "pos_x": -3439.8, "pos_y": -13081.3, "pos_z": -15575.5,
      "dir_x": 0.0, "dir_y": 0.0, "dir_z": 0.0,
      "scale_x": 1.0, "scale_y": 1.0, "scale_z": 1.0,
      "Obj_arg0": -1, "Obj_arg1": -1, "Obj_arg2": -1, "Obj_arg3": -1,
      "Obj_arg4": -1, "Obj_arg5": -1, "Obj_arg6": -1, "Obj_arg7": -1
    }
  ]
}
```

For more information, see [Placing objects in stages](../how-tos/placing-objects-in-stages.md).

## Writing the code

Create `source/main.lum` with this code:

```lumascript
use kit.actor
use kit.talk

fn chat(keeper: LiveActor) {
    start chat_task(ref(keeper))
}

task chat_task(k: ref<LiveActor>) {
    let answer = wait talk.ask(k, text "Keeper_Hello", [text "Keeper_Yes", text "Keeper_No"])
    log fact "answer" answer
    match answer {
        0 => wait talk.say(k, text "Keeper_Thanks")
        else => wait talk.say(k, text "Keeper_Bye")
    }
    log fact "talked" true
}

on start {
    actor.define(actor.Kind {
        name: "LumaKeeper"                     // placed in a stage as "luma-keeper/LumaKeeper"
        look: model "Keeper"                   // its materials and textures are cooked from the glTF
        idle_action: "Wait"                    // the model's own clip, looped
        sensors: [actor.Sensor.Body(70.0)]
        on_talk: chat
    })
}
```

* `actor.define` defines the new kind of object. By default, an object stands on the ground: it falls onto the ground below it and stays upright relative to the planet's gravity. `on_talk` gives the object the game's <kbd>A</kbd> prompt, and calls `chat` when the player presses <kbd>A</kbd> near it.
* `chat` starts a task, because a conversation lasts many frames. `talk.ask` shows the question and waits for the answer, which is `0` for the first choice. `talk.say` shows a line and waits until the player closes it.
* `log fact` writes values that the mod's test reads.

## Building, testing and packing the mod

```sh
lumalink check a-new-npc
lumalink build a-new-npc
lumalink test a-new-npc
lumalink pack a-new-npc
```

`lumalink test` starts the game hidden with your mod and LumaKit, walks Mario up to the Keeper, presses <kbd>A</kbd> through the conversation, and prints `pass` or the check that failed.

To play the mod, drag the `.luma` file onto the game's window, turn the mod on in the **Mods** page, and enter Good Egg Galaxy.

## Next steps

* [Adding a new power-up](a-new-power-up.md)
* [LumaKit: actor](../reference/lumakit/actor.md)
* [LumaKit: talk](../reference/lumakit/talk.md)
