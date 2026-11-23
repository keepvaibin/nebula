---
title: 'Nebula v2 Beta'
date: 2026-11-23
summary: What is native now, what else was added, what is broken and why it isn't fixed yet, and what is left out of this beta.
author: keepvaibin
image: version.jpg
---

Like the project? Consider donating to help speed up development times and support me! My Ko-fi is at [https://ko-fi.com/keepvaibin](https://ko-fi.com/keepvaibin).

Join the official Nebula Discord at [https://discord.gg/2fJEdhAw2f](https://discord.gg/2fJEdhAw2f), come ask questions or hang out with the rest of the community! Spreading the word around also helps, as it lets more people see what's going on!

## Downloads

| System | Download |
| --- | --- |
| Windows 10 or 11 (x64) | [Nebula-Beta-Setup.exe](https://github.com/keepvaibin/nebula/releases/download/v2.0.0-beta.1/Nebula-Beta-Setup.exe) |
| Linux (x86-64) | [Nebula-Beta-Linux-x64.tar.gz](https://github.com/keepvaibin/nebula/releases/download/v2.0.0-beta.1/Nebula-Beta-Linux-x64.tar.gz) |
| macOS 12 or later (Intel and Apple silicon) | [Nebula-Beta-macOS.zip](https://github.com/keepvaibin/nebula/releases/download/v2.0.0-beta.1/Nebula-Beta-macOS.zip) |
| Android 12 or later (64-bit ARM) | [Nebula-Beta-Android.apk](https://github.com/keepvaibin/nebula/releases/download/v2.0.0-beta.1/Nebula-Beta-Android.apk) |

Hello! The first Nebula v2 beta is out. I prefer not to release beta builds but we're starting to see more and more projects based off of Petari released, and while I am a perfectionist, I understand that people have been waiting for a while, so I want to get out what i've been able to do so far.

![The Super Mario Galaxy title screen in Nebula, with v2.0.0-beta.1 in the bottom right corner](version.jpg 'The title screen of the beta, with the version in the corner.')

This post is the long version of the release notes. It covers what I changed in the game since v1.0.2, what got added, what's still broken (and why it isn't fixed yet), and what I left out of the beta on purpose. It's long, so feel free to skip around! If you haven't read [the last post](../nebula-v2/), it explains why I took the game's graphics apart in the first place.

A beta is so that you can try the game, and not really a polished or perfected release version. That being said, you will run into issues, so you should keep a copy of your saves before you start.

## What you need

You will need your own copy of Super Mario Galaxy for the Wii, from North America, Europe or Japan, as a disc image (ISO, RVZ or WBFS) or an extracted folder. Nebula doesn't include the game.

Here are the system requirements:

- **Windows:** Windows 10 (1803 or later) or 11, 64-bit, with a graphics card that supports Direct3D 11 or newer. (Direct3D, Vulkan and Metal are the different ways a program talks to a graphics card. Nebula uses all three, depending on the system.)
- **Linux:** x86-64 with glibc 2.39 and libstdc++ 3.4.32 or newer (the system libraries every Linux program leans on), so Ubuntu 24.04 or Fedora 40 are fine and Ubuntu 22.04 or Debian 12 will probably not start it. You also need the Vulkan loader and a driver, NVIDIA's own or Mesa's.
- **macOS:** 12 or later, Intel or Apple silicon.
- **Android:** 12 or later, 64-bit ARM, Vulkan 1.1.

The install steps for each system are in the [release notes](https://github.com/keepvaibin/nebula/releases/tag/v2.0.0-beta.1). One thing worth knowing up front: Nebula isn't signed by Apple, so on a Mac the first open goes through System Settings > Privacy & Security > Open Anyway (the download has a "How to open Nebula.txt" with the steps).

## What it has actually been run on

I don't have a computer or device for every OS, and so I can't guarantee performance on every device so far. As of today, the beta has been run on:

- **Windows:** one computer with two graphics cards, on all three renderers.
- **Android:** one tablet.
- **Linux:** one laptop with an RTX 4070.
- **macOS:** one Intel MacBook Pro from 2017.

## What was made native

I know that I explained it a little last time, but if you're interested in learning about it in greater detail, here you go! Here's everything that was changed from Petari and the original source to make it a more modernized build of SMG.

### The frame list

The Wii's graphics chip took a long stream of tiny commands, and the first version of Nebula on Petari had to read that stream back to work out what the game wanted. That's gone, and nothing in the game calls the Wii's graphics library anymore.

What replaced it is a **frame list**. Every game step (the game runs 60 of them a second), the game writes a list of real things: this model with these joint positions, this material, these lights, this camera, drawn in this order. Then it hands the whole list to the drawing side by pointer.

The drawing side keeps the list it just drew and the one before it, which is how in-between pictures work (more on those below). The game never waits for an old list to be cleared away. If the one it wants is still in use, it just starts a fresh one. All four renderers (Direct3D 12, Direct3D 11, Vulkan and Metal) read the list with one shared piece of code as well so any updates to this (thankfully) update all four renderers at once.

The game and the drawing also run on separate threads (a thread is one strand of work running next to the others), and so do the window, texture packs, movies and sound. The game's step itself is still on one thread.

### Models and materials

At Setup, every model is rewritten into one flat block laid out the way it will sit in memory, so the game reads it where it lies and doesn't unpack anything. The Wii's whole model library (J3D) is gone from the game.

Materials went part of the way. I described them last time as uploaded once and only updated when something changes. That's currently the goal, and I haven't been able to perfect that yet, so it will ship in a future beta or the eventual v2 release. However, as of right now, setup writes every material as a proper record and decides each texture's filtering and wrap settings once, where the game used to work them out from the Wii's codes itself. A model's textures also get their ID when the model loads, so a texture's first upload to the graphics card happens during the loading screen instead of the frame it first shows up. However, the game still sets up a material draw by draw, the console's way. 

### Graphics programs

The Wii had no real shaders, and instead used a fixed recipe of colour mixing for each material. Now, every material has a shader written for exactly what it does, for all 2,401 of them, made ahead of time and built into Nebula. Setup writes a list of every combination the game can ask for, and when you start the game those get prepared in the background before you need them. Your system keeps a saved copy of what it built, so the next start is much quicker.

However, during the first run, if the game draws something the background hasn't finished yet, that program gets built on the spot and you see a hitch. That's the stutter in the known issues below. Last time, I made it sound like nothing is ever built in the middle of play. Everything is prepared ahead of time, but on a first run the background can still lose that build race. (Shadows with the stencil buffer and the 10-bit colour are as I described them last time.)

### The game stopped looking at its own picture

On the Wii, the game could peek at the picture it was drawing and ask things like "how far away is whatever is under the pointer?", and Nebula was still doing that. It asked the graphics card for the depth under the star pointer, under a talk balloon and for the sun's lens flare, then waited about two frames for the answer. In one measurement, the thread that runs the game spent roughly a sixth of its time just waiting for those answers. And the answer depended on your resolution, screen shape and card, so the same play could come out a little differently on different machines.

All of that is gone. The pointer now finds what's under it by shooting a line from the camera through the stage's collision, the same collision Mario walks on. The talk balloon asks the picture nothing. The lens flare is decided on the graphics card as it's drawn, so it fades in and out smoothly and doesn't flash on behind a wall when the machine is busy. One thing you might notice is that where the picture's depth and the collision used to disagree, the pointer can now pick something different from before.

### How a picture gets to your screen

The game's own rules still run at exactly 60 steps a second. What you see on your screen now, however, is a different thing now. With the default settings, the game shows 60 pictures a second, one per step. On a 59.94 Hz display it follows the display and runs 0.1% slower, the same way the console did.

If you turn on in-between pictures (Settings > Graphics > INTERPOLATION > NATIVE, then pick a target frame rate), the game draws extra pictures between two steps. It can do that because the drawing side kept last step's list, so it can move each thing partway between where it was and where it is. They're off by default. Linux is different, which is why you may experience stutters as it has no display clock, so the game follows the computer's own clock and not the screen's refreshes.

![The Graphics tab of the settings, with Interpolation set to Native and the target frame rate set to match a 240 Hz display](frame-interpolation.jpg 'In-between pictures switched on in the Graphics settings.')

### Sound

The Wii had a separate sound chip running its own program, and v2 doesn't run that. The game's sound engine hands the host its voices (every sound playing is one voice, and there's room for 4,096) and four effect lines (the echo kind). The host mixes them in decimal-number precision at whatever rate your sound device uses, with each voice resampled to that rate and run through the same filters the sounds were made with. The music is still the disc's own sequence data, so it's the same music.

Output is stereo on Linux, Android and Mac. The Surround choice in the sound settings only does something on Windows right now, with future plans for surround (3.1/5.1/7.1) in a future beta or the official v2 release. 

### Loading, memory and saves

**Loading.** At Setup your disc is read once and turned into one big store file. While you play, the game asks for a file by name and gets it straight out of that store, which is mapped into memory. Nothing is unpacked mid-play now and the Wii's archive code, disc reader and loading thread aren't in the game anymore. However, if the game asks for something that hasn't been paged in yet, it waits on your drive, which can show up as a small hitch.

**Memory.** The Wii's memory heaps are gone. The game uses arenas: each scene gets its own, and when the scene ends the whole thing is thrown away at once instead of freeing objects one by one. There's no size to run out of except your computer's memory. Lists that count things (coins, Star Bits, collision hits) grow when they're full, though a few fixed sizes are still left.

**Saves.** Your save is a file called `Nebula.sav`, written to a new file first and then swapped in, with the two copies before it kept, so a crash mid-save can't leave you half a file. A save from a Wii, Dolphin or the Switch release is converted once when you import it.

### Input

The game doesn't know what controller you have. Each step it asks for one record per player: which buttons are down, where the stick is, where you're pointing, how the controller is tilted, and how many spins happened since it last asked. A keyboard, a PlayStation controller and a Wii Remote all get turned into that record, and spins are counted on Nebula's side, not worked out from raw accelerometer numbers inside the game. The record is still shaped like a Wii Remote with a Nunchuk though (buttons 1 and 2, C and Z), and the game still reads it that way.

### The screen

The Wii drew everything for a 640 by 456 picture. Now, the game works out a width from the shape of your window. The Wii's widescreen mode already used 832 units over 640, other shapes keep that relationship and change the width, and the camera and the on-screen display both follow it. The settings offer shapes from 4:3 to 48:9, and effects like bloom and blur are made at the picture's real size. However, with menus, the game's own 2D screens are still drawn on the old canvas, stretched for 16:9, and Nebula's screens can correct for it.

### Text, particles, Miis and maths

**Text.** Messages are rewritten at Setup into a table of strings. Fonts are now signed distance fields: instead of storing a letter as pixels, the font stores how far every point is from the letter's edge, so the letter can be drawn sharp at any size, 4K included. The Wii's layout and font library (nw4r) is gone.

**Particles and Miis.** Particles are rewritten at Setup and the Wii's particle library is gone. So is its Mii library, and a Mii's face is drawn on the graphics card from its parts.

**Maths.** The game's sine, cosine and the rest come from Nebula's own code and not from your OS (so that everyone has the same experience), and the build fails if the game ever takes one from the system. The compiler is also told not to fuse a multiply and an add into one step (some processors do that and others don't), so the same input gives the same game on every processor.

**Movies.** The disc's movies are decoded by Nebula's own code, on a thread of their own, from a copy made at Setup.

### What's still the old way

I don't want you to think that *everything* is finished. The music's sequence data is still stored the way the Wii stored it and byte-swapped every time it's read. The game's data tables find each field by hashing its column name at every read. Text is 16 bits a character, the codes that change colours or play a sound inside a message are still the Wii's, the save keeps the Wii's name codes inside, and the menus' canvas is stretched. Lights are loaded draw by draw the way the Wii did it, and the shader programs still follow the Wii's colour mixing stage by stage.

This beta is the game as it stands. The final 2.0.0 comes after another round of rework on all of that.

## What else is in the beta

This is the feature list from the last post, now with the systems each thing applies to.

- **Platforms.** Windows has three renderers in one install (Direct3D 12, Direct3D 11 for older cards, and Vulkan), switched in Settings > System and applied at the next start. Linux uses Vulkan, x86-64 only. macOS uses Metal, one app for Intel and Apple silicon. Android uses Vulkan, and you can import a graphics driver like Turnip on Adreno GPUs.
- **Discs and languages.** North America, Europe and Japan. The game offers the languages your disc holds, and Nebula's own menus are translated into 14.
- **Setup and updates.** Setup reads your disc and prepares the game, with nothing compiled on your computer. Updates are signed and checked. On Windows the game installs the update for you, on Mac and Linux it tells you there's a new version and you install it yourself, and on Android you install the new app over the old one.
- **Upscaling** (drawing the game smaller and scaling it up smartly, with AMD's FSR, NVIDIA's DLSS, Intel's XeSS, Qualcomm's SGSR or Apple's MetalFX). Windows has FSR 1, FSR 3, DLSS and XeSS, and Vulkan adds SGSR 2 (Direct3D 11 has fewer). Linux has FSR 1, SGSR 2 and FSR 3 (the upscaler only, not its frame generation). Mac has FSR 1 and MetalFX. Android has FSR 1, FSR 3 and SGSR 2. DLSS needs an NVIDIA RTX card, and the first time you pick it the game asks before downloading NVIDIA's files.
- **In-between pictures.** NATIVE is the game's own. On cards that have them you can also pick AMD's frame generation or NVIDIA's DLSS frame generation (NVIDIA Reflex comes with the second one).
- **Picture options.** Anti-aliasing with FXAA, SMAA or MSAA (plus DLAA on cards with DLSS), ambient occlusion, per-vertex, per-pixel or enhanced lighting, bloom from 0 to 200%, anisotropic filtering up to 16x, curved surfaces, dynamic resolution and a Performance preset.
- **HDR.** On Windows, with a calibration screen that shows a live stage with Mario in it. There's no HDR on Direct3D 11 and none on Mac yet.
- **The benchmark.** It warms up each scene, plays a set of stages by itself with your input off, and shows your average frame rate, your lows, a graph and where the frame's time went. It recommends settings and applies them with one button.
- **Controllers.** Keyboard and mouse, Xbox, Switch Pro and Switch 2 Pro, Joy-Con (a pair works as one controller), PlayStation (DualShock 4, DualSense and DualSense Edge, with the touchpad as a pointer), the Steam Deck and both Steam Controllers, plus generic controllers. Gyro pointing works on anything with motion sensors.
- **A real Wii Remote** works on Windows, and on Android through a DolphinBar. There's no link for Linux or Mac yet. Co-Star mode (a second controller gets its own pointer to collect Star Bits) is in the settings.
- **Quality of life.** Cutscene skipping with a confirmation, and dialogue speed up to Instant. 35 achievements with pop-ups, and after your first Grand Star a Luma in the Comet Observatory shows them along with your stats. Mii import from a Wii, 3DS, Wii U or Switch, and save import and export from inside the game. Seven separate volume sliders. Discord status on Windows, Linux and macOS. Every setting is in the game's own menu, and errors show a code and save a report.

![The summary page of the benchmark, with a graph of the frame rate across eight scenes and the average, the lows and the stutters listed under it](diagnostics-1.jpg 'The summary page of the benchmark.')

![The Display tab of the settings with HDR on, the white level at 400 nits and the HDR calibration row under it](hdr-menu.jpg 'HDR in the Display settings.')

![The HDR calibration screen: a live stage with Mario standing in it, and a white level slider set to 80 nits](hdr-calibration.jpg 'The HDR calibration screen, on a live stage.')

## Known issues, and why they're not fixed yet

Here's what I know is wrong:

**In-between pictures are uneven on a 120 Hz phone or tablet.** In the worst scene measured on the tablet the game showed about 72 uneven pictures a second, and only 42% of them arrived on time. They can also switch themselves off after a loading screen, because the phone lowers its graphics clock during the load and the game reads that slow time as "can't keep up". Fixing it means redoing how a picture is timed and handed to the screen (one judge that acts on the resolution, and the phone's clock held steady). That's one piece of the timing rework and I haven't done it yet. I would recommend keeping 60fps mode on for now.

**A 60 Hz phone has a little extra delay.** At 60 Hz Android shows each picture three refreshes after its step where two would be the target. One refresh is Android's own and one is lost because the graphics chip runs at a low clock. It's part of the same timing rework.

**The first minutes can stutter while the game builds its graphics programs.** That's Linux, Windows with Vulkan and Android, and a little on Direct3D 12. On a Mac the first start shows a bar while it prepares. The fix is to build every program a stage needs while its loading screen is up, so a draw never has to build one. That's its own piece of work and I haven't done it yet.

**Pacing on Linux can be less exact.** NVIDIA owners report programs freezing inside the driver's present-wait feature on some driver versions (those reports are from NVIDIA's forum, not from NVIDIA), so Nebula leaves it off on NVIDIA cards on Linux. To switch it back on, someone with such a card has to run it both ways and show it doesn't freeze. On top of that, Linux has no display clock (I mentioned it earlier), which goes with the timing rework.

**Controllers.** A very short tap can get lost. On Windows a small push on a stick snaps to a straight line, because the dead zone there is a square and not round. And the pointer you aim with follows your hand 60 times a second, so above 60 Hz it trails by a step or more. The first two come with the rework of how the game reads controllers, and the pointer's timing waits for it.

**The same play can come out slightly different on a different screen shape.** What the game keeps running follows what the camera could see, and that depends on how wide your screen is. When the same play was run on a tablet and on a PC, the game's steps differed at 100 of the 101 points I compared. The fix is for everything in a stage to always run, on every screen, and that only stays if the game's step is fast enough first, so making the step faster has to come before it.

**Sound can crackle when your computer is busy.** On Windows the sound output starts with a small buffer and only makes it bigger after the first gap. On Linux, Android and Mac a cutscene's picture can run ahead of its sound by whatever the device still has queued. And Surround does nothing off Windows. These belong to the sound parts of the rework.

And a few smaller things that are simply not done yet: replacement movies play only on Windows, HDR is missing on Direct3D 11 and on Mac (and untested on Linux and Mac), and the in-game update installs itself only on Windows.

## What's not in this beta, and why

**Mods.** There's no Mods page, no `.luma` files and no LumaKit in the beta, and the mod achievements that came with them aren't here either. LumaLink can't make anything the beta will load. I want the base game finished and made right before anything gets built on it, so mods and LumaLink wait until the rework is done.

**Ray tracing.** Last time I said it was coming in a very soon release. That changed. It isn't in the beta, the code is out of the game's main code (it lives on its own branch), and it waits until the game itself is right.

**DLSS and XeSS on Linux.** The Linux version doesn't have them. They need a different route than on Windows, so they come at the very end.

**Linux on ARM and a native Windows on ARM build.** Not in this beta.

## If something breaks

Report it on the [issue tracker](https://github.com/keepvaibin/nebula/issues) or in the Discord. Tell me your system, your graphics card or device, your driver or system version, what you were doing, and which renderer you were on if you're on Windows (Settings > System, the RENDERER row) or whether you're on X11 or Wayland if you're on Linux.

Please attach the log of the run:

- **Windows:** `%LOCALAPPDATA%\Nebula\logs\latest.log`. If Nebula stops, it shows an error code and saves a report in `%LOCALAPPDATA%\Nebula\crashes`, so attach both files in there.
- **Linux:** `~/.local/share/Nebula/logs/game.log` (`game.previous.log` is the run before). If the game couldn't start at all, also attach `~/.local/share/Nebula/last_error.txt`.
- **macOS:** `~/Library/Application Support/Nebula/logs/game.log`.
- **Android:** choose **Share the game's log** in the settings.

If the game is slow, run the benchmark (Settings > System > Benchmark) and send the result when the game offers it. It asks first, and the default answer is No. If you say yes, it sends your graphics chip, processor, memory and system version, your graphics settings and the benchmark's measurements, sealed so only I can read them. It never sends your user or computer name, file paths, serial numbers, saves or Miis, and "Show the full report" shows you the exact text first.

![The screen that asks before a benchmark report is sent, with a list of what the report holds and what it never sends](diagnostics-2.jpg 'What the game asks before it sends a benchmark report.')

## Thanks

Thank you again to SMGCommunity for Petari, this wouldn't exist without it, and to everyone who's been waiting on v2 and trying the builds.

See you in the stars! ⭐
