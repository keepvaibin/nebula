---
title: Playing a video inside a galaxy
shortTitle: A video in a galaxy
intro: Play your mod's movie when Mario walks up to a screen placed in a galaxy.
contentType: tutorials
example: a-video-in-a-galaxy
---

## Introduction

In this tutorial, you place a screen in front of the starting point of Good Egg Galaxy. When Mario walks up to it, the mod's movie plays full screen with sound. The player can skip it with <kbd>+</kbd>, and the game continues where it was.

You learn how to:

* Add a movie to a mod.
* Play a movie from code with the `movie` service.
* React when the player touches an object, and run code for an object every frame.

The finished mod uses a placeholder screen model and a test movie with a tone. To use your own, replace them with files of the same names.

## Prerequisites

* Nebula, installed with Nebula Setup.
* LumaLink. For more information, see [Installing LumaLink](../get-started/installing-lumalink.md).
* [FFmpeg](https://ffmpeg.org/), to convert a video into the format the game plays.

## Mod folder layout

When you finish, the mod folder contains these files:

```text
a-video-in-a-galaxy/
  luma.json
  source/main.lum
  layers/base/assets/Screen.glb
  layers/base/assets/movies/Trailer.webm         the picture: AV1 in WebM
  layers/base/assets/movies/Trailer.ogg          the sound
  layers/base/assets/movies/Trailer.movie.json
  layers/base/patches/StageData/EggStarGalaxy.arc/jmp/placement/common/objinfo.json
  tests/movie/test.json
```

## Creating the mod

1. Create the mod folder, and add LumaKit to the mod's needs.

   ```sh
   lumalink new a-video-in-a-galaxy --id galaxy-screen --name "Galaxy Screen" --summary "A screen in a galaxy that plays the mod's movie when Mario walks up to it." --author YOUR-NAME
   lumalink add need lumakit a-video-in-a-galaxy
   ```

1. Optionally, add the mod's name and summary in each of the game's languages. The finished `luma.json` looks like this:

   ```json luma.json
   {
     "format": "1.0",
     "id": "galaxy-screen",
     "version": "1.0.0",
     "name": "Galaxy Screen",
     "summary": "A screen in a galaxy that plays the mod's movie when Mario walks up to it.",
     "text": {
       "UsEnglish": {"name": "Galaxy Screen", "summary": "A screen in a galaxy that plays the mod's movie when Mario walks up to it."},
       "EuEnglish": {"name": "Galaxy Screen", "summary": "A screen in a galaxy that plays the mod's movie when Mario walks up to it."},
       "UsFrench": {"name": "Écran galactique", "summary": "Un écran dans une galaxie qui passe la vidéo du mod quand Mario s'en approche."},
       "EuFrench": {"name": "Écran galactique", "summary": "Un écran dans une galaxie qui passe la vidéo du mod quand Mario s'en approche."},
       "UsSpanish": {"name": "Pantalla galáctica", "summary": "Una pantalla en una galaxia que reproduce el vídeo del mod cuando Mario se acerca."},
       "EuSpanish": {"name": "Pantalla galáctica", "summary": "Una pantalla en una galaxia que reproduce el vídeo del mod cuando Mario se acerca."},
       "EuGerman": {"name": "Galaxie-Bildschirm", "summary": "Ein Bildschirm in einer Galaxie, der das Video der Mod zeigt, wenn Mario hingeht."},
       "EuItalian": {"name": "Schermo galattico", "summary": "Uno schermo in una galassia che riproduce il video della mod quando Mario si avvicina."},
       "EuDutch": {"name": "Melkwegscherm", "summary": "Een scherm in een sterrenstelsel dat de video van de mod speelt als Mario erheen loopt."},
       "UsPortuguese": {"name": "Tela galáctica", "summary": "Uma tela numa galáxia que passa o vídeo do mod quando Mario se aproxima."},
       "JpJapanese": {"name": "ギャラクシースクリーン", "summary": "マリオが近づくと、MODのムービーを流すギャラクシーのスクリーン。"},
       "KrKorean": {"name": "갤럭시 스크린", "summary": "마리오가 다가가면 모드의 영상을 틀어 주는 갤럭시 안의 스크린."},
       "CnSimpChinese": {"name": "银河屏幕", "summary": "马力欧走近时播放模组影片的银河屏幕。"},
       "TwTradChinese": {"name": "銀河螢幕", "summary": "瑪利歐走近時播放模組影片的銀河螢幕。"}
     },
     "authors": ["you"],
     "game": { "id": "RMG" },
     "needs": [{ "id": "lumakit", "version": ">=1.0.0" }],
     "layers": [{ "name": "base" }],
     "code": { "platforms": ["windows-x64", "windows-arm64", "linux-x64", "linux-arm64", "macos-arm64", "macos-x64", "android-arm64", "android-x64", "ios-arm64"]}
   }
   ```

## Adding the movie

A movie is an AV1 video in a WebM file, with its sound in a separate file and a `.movie.json` file that names both.

1. Convert your video. These commands write the picture and the sound from any video file:

   ```sh
   ffmpeg -i trailer.mp4 -c:v libsvtav1 -an Trailer.webm
   ffmpeg -i trailer.mp4 -vn -c:a libvorbis Trailer.ogg
   ```

1. Copy `Trailer.webm` and `Trailer.ogg` to `layers/base/assets/movies/`.
1. Create `layers/base/assets/movies/Trailer.movie.json`. `skip: true` lets the player skip the movie with <kbd>+</kbd>.

   ```json
   { "file": "Trailer.webm", "audio": "Trailer.ogg", "skip": true }
   ```

## Placing the screen

Create `layers/base/patches/StageData/EggStarGalaxy.arc/jmp/placement/common/objinfo.json`. This patch adds the screen to Good Egg Galaxy, a few steps in front of the starting point.

```json
{
  "patch": 1,
  "key": ["l_id"],
  "add": [
    {
      "name": "galaxy-screen/LumaScreen",
      "l_id": 9413,
      "pos_x": -3439.8, "pos_y": -13081.3, "pos_z": -15575.5,
      "dir_x": 0.0, "dir_y": 0.0, "dir_z": 0.0,
      "scale_x": 1.0, "scale_y": 1.0, "scale_z": 1.0,
      "Obj_arg0": -1, "Obj_arg1": -1, "Obj_arg2": -1, "Obj_arg3": -1,
      "Obj_arg4": -1, "Obj_arg5": -1, "Obj_arg6": -1, "Obj_arg7": -1
    }
  ]
}
```

## Writing the code

Create `source/main.lum` with this code:

```lumascript
use kit.actor
use svc.movie

var playing: movie.Movie? = none
var ready = true   // false from the start of a movie until Mario steps away from the screen

fn ended(e: movie.Event) {
    playing = none
    log fact "playing" false
}

fn near(screen: LiveActor) {
    if not ready or playing != none { return }
    ready = false
    // Full screen with its sound; + skips it.
    playing = movie.play((movie "movies/Trailer").name(), ended)
    log fact "playing" playing != none
}

// Once Mario has stepped away from the screen, touching it plays the movie again.
fn watch(screen: LiveActor) {
    if not ready and playing == none and not actor.near_player(screen, 300.0) {
        ready = true
    }
}

on start {
    actor.define(actor.Kind {
        name: "LumaScreen"                     // placed in a stage as "galaxy-screen/LumaScreen"
        look: model "Screen"
        sensors: [actor.Sensor.Body(150.0)]
        on_touch_player: near
        on_frame: watch
    })
}
```

* `actor.define` defines the screen. Its body sensor is 150 units across, and `on_touch_player` calls `near` when Mario touches it.
* `near` plays the movie, unless it is already playing or has played since Mario last stepped away. Mario is still touching the screen when the movie ends, so without the `ready` flag the movie would start again immediately. LumaLink checks `movie "movies/Trailer"` when it builds the mod, so a missing movie is a build error rather than a problem for players.
* `watch` runs every frame. When Mario is more than 300 units from the screen, the next touch plays the movie again.
* `ended` runs when the movie ends or the player skips it.
* `log fact` writes values that the mod's test reads.

## Building, testing and packing the mod

```sh
lumalink check a-video-in-a-galaxy
lumalink build a-video-in-a-galaxy
lumalink test a-video-in-a-galaxy
lumalink pack a-video-in-a-galaxy
```

`lumalink test` starts the game hidden with your mod and LumaKit, walks Mario into the screen, checks that the movie's sound plays, presses <kbd>+</kbd>, and prints `pass` or the check that failed.

To play the mod, drag the `.luma` file onto the game's window, turn the mod on in the **Mods** page, and enter Good Egg Galaxy.

## Next steps

* [Adding a new galaxy](a-new-galaxy.md)
* [Movie service](../reference/services/movie.md)
* [LumaKit: actor](../reference/lumakit/actor.md)
