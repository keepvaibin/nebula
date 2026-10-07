Nebula 0.1.1 Beta 3 fixes the reproduced crash when entering the Terrace in the Comet Observatory. It also corrects a render-completion timeout and checks valid memory ranges for thread wakeup stacks. Previous performance and Mario model fixes are retained.

### Install and play

1. Download **Nebula-Beta-0.1.1-beta.3-win64.zip** and extract the whole folder to a new location.
2. Open **Nebula.exe** and click **Play**. Select `game.pak` from your existing installation if prompted.

This portable build uses your existing saves and shader cache. Progress saves normally, and a backup is made before the first launch. Keep your previous installation. Update the entire portable folder; do not replace an older installation's runtime EXE alone.

### Optional recording

- **No recording**: normal play; this is the default.
- **Lightweight recording**: basic performance logs.
- **Detailed recording**: additional timing for investigating problems; it can reduce performance.

After closing the game, use **Open diagnostics** to find recording ZIPs. If a scene crashes or slows down, record a short reproduction and describe the location and your settings.

Sustained performance on lower-end PCs remains under investigation. This update does not establish 60 FPS on low-end hardware or a performance advantage over Dolphin. Other Observatory scenes, targeting responsiveness and full graphics/audio behavior still require testing.
