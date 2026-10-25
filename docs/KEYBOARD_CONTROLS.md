# Controls

The game sees one Wii Remote with a Nunchuk on channel 0. Keyboard, mouse,
Xbox (XInput) controllers, Switch Pro Controllers, Joy-Con (a pair as one controller), Switch 2 Pro Controllers,
PlayStation controllers (DualShock 4, DualSense, DualSense Edge), the Steam
Controller (2026 and 2015), the Steam Deck's controls and generic HID game controllers work at the same time; every
Wii input can be rebound. The game gets their state the moment it reads its remotes each frame (a report built then),
and every 10 ms between frames: a press made just before a frame's read is in that frame. Measured from the retrace a
scripted press begins on to the first frame that shows it, at 60 fps: spin 37.3 -> 20.4 ms, pause 37.0 -> 20.3 ms
(the i7-6500U stand-in 39.7 -> 22.7 and 39.7 -> 22.1 ms); a press at a random moment gains about 5 ms on average.

A real Wii Remote (RVL-CNT-01 or -TR, with or without a Nunchuk) paired in Windows' Bluetooth settings is read
directly: the game gets its own reports as they arrive (buttons, accelerometer, IR camera, Nunchuk), its own
calibration, its battery level, and drives its motor; the game's pictures of a Wii Remote show while it is in use.
With INPUT on auto it takes over at its first button press and hands back when a key, a mouse button or a controller
is used; INPUT on Wii Remote uses it alone. Its speaker's sounds play from the PC. A test session never opens it.

## Default maps

Controllers follow Super Mario Galaxy on Nintendo Switch (3D All-Stars; the
Switch 2 edition keeps the layout), with Star Bits on L/LB/L1 (Nintendo uses ZR),
ZR/RT/R2 as a spin button and ZL/LT/L2 as crouch.

| Action (Wii input) | Keyboard and mouse | Xbox | Switch Pro / Pro Controller 2 | PlayStation | Steam |
|---|---|---|---|---|---|
| Jump, confirm (A) | `Space`, `Enter`, left mouse button | A | A | Cross | A |
| Jump in play, back in menus | - | B | B | Circle | B |
| Star Bits, back (B) | `Left Shift`, `Right Shift`, right mouse button | LB | L | L1 | LB |
| Crouch, ground pound (Z) | `Z`, `Left Ctrl`, `Right Ctrl` | LT | ZL | L2 | LT |
| Camera reset (C) | `C` | Left stick click | Left stick click | L3 | Left stick click |
| Spin (Wii Remote shake) | `Q`, `E` | X, Y, RB, RT | X, Y, ZR, shake the controller | Square, Triangle, R2, shake | X, Y, RT, shake |
| Move (Nunchuk stick) | `W` `A` `S` `D` | Left stick | Left stick | Left stick | Left stick |
| Camera, first person (D-pad) | Arrow keys | D-pad | D-pad; right stick while the gyro points | same as Switch | same as Switch |
| Pointer | Mouse, `I` `J` `K` `L` | Right stick | Gyro; right stick when the gyro is off | same as Switch | same as Switch |
| Pointer recenter | `R` | Right stick click | R | R1 | RB |
| Pause (Plus) | `=`, `P` | Menu | Plus | Options | Menu |
| Minus | `-`, `M` | View | Minus | Share (Create) | View |
| Home | `H` | - | Home | PS | Steam |
| 1, 2 | `1`, `2` (and numpad) | - | - | - | - |
| Shake (Nunchuk) | - | - | - | - | - |

The game's button icons (A, B, C, Z, 1, 2, Plus and Minus in its text: the title screen, dialogue, tutorials, menus)
show the button each is bound to on the controller in use: Xbox's in their colours, Switch and PlayStation buttons
white, shoulders, triggers and stick clicks by name. A keyboard or a controller of no known family shows the Wii's.
General > Button icons keeps one style (Wii, Xbox, Nintendo Switch, PlayStation) whatever is in use; Xbox, Nintendo
Switch and PlayStation show in the settings menu's hints too (with Wii and Auto they follow what was last used).

The icons are textures of Nebula's own beside the game's, which keep their content: a texture pack's replacements of the
game's icons still show with the Wii style, and Nebula's show as drawn unless a pack names them. The Nebula HD Button
Icons pack has them at eight times their size; `GALAXY_EXPORT_ART=<folder>` writes it again (`pack/`, with previews in
`native/` and `hd/`).

A tap of Plus or Minus pauses (the console waited for the button to be held 12 frames and for the remote to be still);
a press made while A or B is held pauses once they are let go.

B (controllers) never shoots Star Bits: it is Wii B (back) only while a
screen with a Back action is open, Wii A (jump) otherwise. The runtime reads
the game's pointer mode (`StarPointerOnOffController::mMode`): HOME menu,
error window, pause menu and galaxy map, logo, title, file select, star
select, yes/no prompts, picture book and the dome galaxy selector are menus;
every other mode, and a mode it cannot read, is play. The choice is made when
B goes down and kept until it is released. L/LB always send Wii B. Switch
buttons are by label (Nintendo A is Wii A); Xbox, PlayStation and Steam buttons
by position (Cross is the south button, as Xbox A). Capture, GL, GR and C (Pro
Controller 2), the touchpad click, mute and paddles (PlayStation), and QAM, L4,
R4, L5, R5 and the trackpad clicks (Steam) are unbound by default.

Pointer recenter: press R (Switch), R1/RB (PlayStation, Steam), the right stick
click (Xbox) or `R` (keyboard) to put the pointer in the middle of the picture; it also brings a
hidden pointer back, on any menu. While the button is held the pointer stays
in the middle and gyro or stick motion is ignored; on release it moves from
the middle, so the controller's direction at release is the new centre.

Still pointer: a controller pointer that stays within
`tune.<scheme>.pointer_hide_radius` (default 0.12, 6% of the picture width,
2.4 degrees of turn) of where it settled for `pointer_hide_seconds` (default
4; 0 never hides) disappears, as in the Switch ports. Hand tremor and small
jerks inside the radius do not restart the timer; the position is smoothed
over 0.1 s, so a single-report spike does not count. A hidden gyro pointer
comes back only with the recenter button, at the centre. A hidden stick
pointer also comes back with a stick push. The mouse never hides this way.
Camera on the right stick: the game turns the camera one step per D-pad
left/right press (`CameraLocalUtil::testCameraPadTriggerRoundLeft/Right`
test the press edge), so one push of the stick is one step, as one press of
the Wii D-pad.

## Input sources

`input_mode=auto` (default): all sources are live. The pointer belongs to
whichever source moved last (mouse, pointer keys, right stick, gyro); another
source takes it over after the current one has been still for 250 ms. A
connected controller keeps the pointer on screen. With a controller attached,
key and mouse input generated by SendInput (Steam's desktop controller
layout, which turns the same buttons into keys and the right stick into mouse
motion) is ignored. `keyboard_mouse` and `controller` keep only that source.
`GALAXY_INPUT_MODE` overrides the file.

- Mouse: the position points while the cursor is inside the game picture,
  also when another window has focus; keys and mouse buttons need the game
  window focused.
- Right stick: radial dead zone, slow near the centre and `pointer_speed` at
  full deflection, integrated with the real time between input reports;
  the pointer stops at the picture edge.
- Gyro (Switch, PlayStation, Steam): turning the controller about the vertical moves the
  pointer sideways, tilting it moves it up and down, held flat or upright.
  One picture width is 40 degrees at `gyro_sensitivity=1`. Gyro bias is
  re-measured whenever the controller rests. Pointed past the picture the
  pointer disappears, as with a Wii Remote, and returns when turned back.
- Shake (Switch, PlayStation, Steam): a jolt of the controller (acceleration with gravity
  removed) or a fast wrist flick; held 60 ms, at most one per 280 ms.
- Tilt (Star Ball, ray surfing): the game steers these with the Wii Remote's
  accelerometer only. Pro Controllers, PlayStation and Steam controllers: the hold when the
  section starts is level (R takes the current hold as level again); tilt
  left, right, forward or back to roll the ball, tilt left or right to steer
  the ray. Xbox controllers steer with the left stick, the keyboard with `W`
  `A` `S` `D`. The controller used last decides. Outside these sections the
  remote's accelerometer stays at rest, and the pointer never rotates with
  the controller.
- Vibration: when the game runs the Wii Remote's motor (a Star Bit shot,
  a hit, an earthquake), the controller in use vibrates: Xbox, PlayStation and
  Steam controllers on the light (high-frequency) motor, Pro Controllers with HD
  rumble, Pro Controller 2 (USB) with its rumble report. The output follows a model of the Wii Remote's motor,
  which spins up over about 30 ms and down over about 60 ms, so the game's
  short pulses feel like one soft buzz. A key or mouse button bound to a Wii
  input hands vibration to the keyboard (none). The motor stops at once when
  the game window loses focus, a pause, HOME or settings menu opens, the game
  stalls for 100 ms, the controller disconnects, or Nebula exits or crashes.
  `tune.<controller>.vibration` turns it on or off and
  `tune.<controller>.rumble` sets the strength (10-100 %). A real Wii Remote
  receives the game's own motor commands.

## Joy-Con

Pair each Joy-Con in Windows' Bluetooth settings first (hold the sync button on its rail), or attach both to the
charging grip by USB. A left and a right half then play as one controller, Joy-Con (L/R), with the Switch layout and
icons: the left half's stick, D-pad, L, ZL, Minus and Capture, the right half's buttons, stick, R, ZR, Plus and HOME.
The right half points and tilts, a shake of either half counts, and both halves vibrate.

One left and one right half pair by themselves, at every start. With more halves connected (two players, or to swap
halves), CONTROLS > PAIR JOY-CON: hold L on a left half and R on a right half. One held shoulder alone never splits
another pair; two held halves pair, as on the Switch's Change Grip/Order screen. Pairs are not kept between starts.
An empty rail on the charging grip is checked every 2 s, so a Joy-Con slid in later is found.

## Steam Deck, Steam Controller (2015), 8BitDo

The Steam column covers the Steam Deck's controls and both Steam Controllers. Nebula turns off their keyboard and
mouse emulation itself (the Deck's every 0.8 s, as it asks). The Deck: sticks, triggers, gyro, both pads' clicks,
L4/R4/L5/R5 and QAM (unbound), vibration. The 2015 Steam Controller, by USB or its wireless receiver: the stick, the
right pad as the right stick (the pointer), the left pad's clicks as the D-pad, grips as L4/R4, gyro, and vibration
through its pads' haptics. Its Bluetooth LE mode (28de:1106) is not read; use the receiver or USB. A game Steam
starts gets the controllers Steam Input reads in `SDL_GAMECONTROLLER_IGNORE_DEVICES`; Nebula leaves those to Steam
(they arrive as an Xbox controller), so a press never comes twice.

8BitDo controllers work in each of their modes: X-input as an Xbox controller, Switch as a Pro Controller (stick
calibration and motion only where the pad answers for them), D-input as a generic controller (rebind on the
CONTROLS page).

## How controllers are read

One thread waits on every controller and handles each report as it arrives (decoding, a Joy-Con pair's two halves,
motion: 43 ns a report, timed in `galaxy_controls_tests`); the game takes the newest state when it reads its remotes.
Listing devices and setting up a newly connected controller (up to about a second over Bluetooth) run on a second
thread, and vibration reports are sent without waiting for them, so a controller connecting, a USB device plugged in
or a busy radio never delays another controller's input. Reports come at the controller's own rate (Joy-Con and Pro
Controllers send report 0x30 60 times a second over Bluetooth).

## Remapping

In game: `F1`, View+Menu (Xbox, Steam), Share+Options (PlayStation) or Minus+Plus
(Switch), page CONTROLS.

| Row | Keyboard | Controller |
|---|---|---|
| INPUT: auto, keyboard+mouse only, controller only | Left/Right | D-pad/stick |
| EDIT: keyboard and mouse or an attached controller by name | Left/Right | D-pad/stick |
| Binding row: add the next pressed input | `Enter` or Right | A |
| Binding row: clear | `Backspace` | X |
| Cancel waiting for an input | `Esc` | - |
| Apply the page | `Space` | Y |

The EDIT row lists keyboard and mouse and the controllers attached now, by model
name (numbered when a model repeats, with its link: USB, Bluetooth or wireless);
ACTIVE marks the device that last gave input. The list follows hot plug. A
controller edits the bindings and tuning of its family (Xbox, Switch Pro, Switch 2
Pro, PlayStation, Steam); capture reads the listed controller only.

Each Wii input holds up to four inputs; adding a fifth replaces the last.
Stick and pointer directions accept stick directions and buttons; the mouse
buttons bind only to button rows. A stick direction bound to a button row
presses at half deflection and releases below 35%. A right-stick direction
bound both to a pointer row and to another row is the pointer while the gyro
is off and the other row while it points. Controller-only input is selected
only while a controller is attached. Nebula settings (NebulaSettings.exe,
first launch) has the same choices on its Controls tab.

Settings are stored in `runtime_settings.ini` next to `config.ini`:

```
input_mode=auto
controls_version=5
bind.keyboard.a=Space, Enter, MouseLeft
bind.xbox.jump_back=B
bind.switch_pro.b=L
tune.switch_pro.gyro_sensitivity=1
```

Schemes: `keyboard`, `xbox`, `switch_pro`, `switch_pro2`, `playstation`, `steam`,
`generic` (files from before the last three load unchanged). Targets: `a b one
two plus minus home dpad_up dpad_down dpad_left dpad_right stick_up
stick_down stick_left stick_right c z shake nunchuk_shake pointer_up
pointer_down pointer_left pointer_right pointer_recenter jump_back`. Input
names: key names (`Space`, `LShift`, `W`, `Num1`, `F5`, `Equals`, `VK2E`),
`MouseLeft` `MouseRight` `MouseMiddle` `Mouse4` `Mouse5`; Xbox `A B X Y LB RB
LT RT LS RS Menu View DpadUp...`; Switch `A B X Y L R ZL ZR LS RS Plus Minus
Home Capture GL GR C DpadUp...`; PlayStation `Cross Circle Square Triangle L1 R1
L2 R2 L3 R3 Options Share Create PS Touchpad LeftPaddle RightPaddle Mute
DpadUp...`; Steam `A B X Y LB RB LT RT LS RS Menu View Steam QAM L4 R4 L5 R5
LPad RPad DpadUp...`; generic `Button1`..`Button19 LT RT DpadUp...`; sticks
`LX+ LX- LY+ LY- RX+ RX- RY+ RY-`.

Files with an older `controls_version` are migrated on load: bindings and tuning values that
still hold an earlier version's defaults take the current defaults, a `rumble=0` from version 4
turns vibration off, and values the player changed stay.

| Tuning (`tune.<scheme>.`) | Values | Default |
|---|---|---|
| `pointer_speed` | 0.2-6.0 (pointer units per second at full deflection; 2 = picture width) | keyboard 1.6, controllers 2.4 |
| `invert_pointer_y` | 0/1 | 0 |
| `stick_deadzone`, `pointer_deadzone` | 0-0.5 of full deflection (pointer: radial) | Xbox: XInput defaults (0.24, 0.27); other controllers 0.12 |
| `motion` (gyro controllers) | `off`, `remote`, `nunchuk` | `remote` |
| `shake_sensitivity` (gyro controllers) | 1 (hard shake) - 5 (light) | 3 |
| `gyro_pointer` (gyro controllers) | 0/1 | 1 |
| `gyro_sensitivity` (gyro controllers) | 0.2-5.0 (1: a picture width per 40 degrees) | 1 |
| `pointer_hide_seconds` (controllers) | 0-60 seconds within the radius before the pointer hides; 0 never | 4 |
| `pointer_hide_radius` (controllers) | 0-0.5 pointer units (2 = picture width) | 0.12 |
| `vibration` (controllers) | 0/1 | 1 |
| `rumble` (controllers) | 10-100 percent vibration strength, steps of 10 | 50 |

`GALAXY_XINPUT_LEFT_DEADZONE`, `GALAXY_XINPUT_POINTER_DEADZONE`,
`GALAXY_XINPUT_POINTER_SPEED`, `GALAXY_KEYBOARD_POINTER_SPEED` and
`GALAXY_XINPUT_TRIGGER_THRESHOLD` (default 96 of 255) override these.

## Controllers

- Xbox: every XInput slot (0-3) is read. `GALAXY_DISABLE_XINPUT=1` disables
  it, `GALAXY_XINPUT_REQUIRE_FOCUS=1` reads it only while the window has
  focus.
- Switch Pro Controller: USB or Bluetooth (HID). Nebula reads the stick and
  IMU calibration from the controller, switches it to full reports with the
  IMU on, rumble off and player LED 1.
- Switch 2 Pro Controller: USB only. It sends input only after a vendor
  command on its second USB interface, which needs the WinUSB driver (for
  example installed for "Interface 1" with Zadig); without it the CONTROLS
  page shows NEEDS WINUSB. Over Bluetooth it is a Bluetooth LE device that
  Windows does not expose as HID; not supported.
- PlayStation: DualShock 4 (`054c:05c4`, `054c:09cc`), DualSense (`054c:0ce6`) and
  DualSense Edge (`054c:0df2`), USB or Bluetooth (HID). Nebula reads the sensor
  calibration from the controller (over Bluetooth this also switches it to full
  reports), sets the light bar to a neutral grey once (a DualSense over Bluetooth
  after its pairing animation), and drives the light motor for vibration. Buttons,
  sticks, analog triggers, the touchpad click, mute and Edge paddles are read; the
  touchpad surface is not. The USB wireless adapter and third-party clones are not
  claimed.
- Steam Controller (2026): USB (`28de:1302`), Bluetooth LE (`28de:1303`) and the
  wireless pucks (`28de:1304`, `28de:1305`, controller interfaces 2-5). Nebula
  turns off the controller's keyboard/mouse emulation and starts its sensors with a
  settings feature report repeated every 3 s (the emulation returns on its own
  when the reports stop), and drives the haptic motors for vibration. Trackpad
  surfaces are not read, only their clicks.
- Generic controllers: any HID device with a Joystick or Game Pad collection that
  none of the above reads (not Microsoft, Nintendo, Sony or Valve hardware, no
  XInput interface), listed by its product name. Buttons are read by number through
  the Windows HID parser: 1-4 face buttons (1 jumps, 2 backs out of menus), 5/6
  shoulders, 7/8 the triggers when a pad reports them as buttons, 9 Back, 10 Start,
  11/12 stick clicks, 13 Home. X/Y are the left stick, Z/Rz (or Rx/Ry) the right
  one, the remaining pair (or Brake/Accelerator) the triggers, the hat the d-pad.
  Layouts differ between pads: rebind on the CONTROLS page. No vibration (HID has no
  standard motor report).
- Steam client: with Steam's controller support on, Steam keeps its desktop layout
  on Nebula's window (Nebula is not a Steam game). Auto mode ignores the
  keys and mouse motion that layout generates while a controller is
  attached; the overrides do not.

Protocol details follow SDL's zlib-licensed `SDL_hidapi_switch.c`,
`SDL_hidapi_switch2.c`, `SDL_hidapi_ps4.c`, `SDL_hidapi_ps5.c`,
`SDL_hidapi_steam_triton.c` and Valve's `controller_structs.h` /
`controller_constants.h` (zlib), with the Linux `hid-steam.c` (GPL-2.0+) checked for
the Steam Controller's interfaces, report sizes and button bits, and dekuNukem's
Nintendo_Switch_Reverse_Engineering notes. The code is an independent
implementation; no source was copied. The gyro pointer's axes follow
JoyShockMapper's description of player and world space.

## Settings menu

`F1`, View+Menu (Xbox, Steam), Share+Options (PlayStation), Minus+Plus (Switch) or the pause menu's Settings button
opens the settings menu, once a file is chosen (not over the logos, the title or the file select); a one-line hint
names the keys for the first seconds of a session. Autorepeat never toggles it. Moving through the rows is silent;
changes and presses sound. The game draws it in its own style (the quit question's window, its buttons and message font) and holds still
while it is shown: the scene stops, the music pauses and the game's sounds fade out, a movie waits; all of it resumes
once the menu has risen away. While it is open the game sees no keyboard or controller input, and buttons held when
it closes stay hidden from the game until released.

| Keyboard | Controller | Settings action |
|---|---|---|
| `F1` | View+Menu, Share+Options, Minus+Plus | Open or close |
| `Tab` / Page Down, Page Up | RB/R, LB/L | Next, previous page |
| Up / Down | D-pad or left stick up/down | Select row |
| Left / Right | D-pad or left stick left/right, A | Change the selected row |
| Left / Right, `Space` | D-pad or left stick left/right, A | A row of buttons (Import file, Open): select one, press it |
| `Enter` | Y | Apply (CONTROLS: `Space`/Y) |
| `Backspace` | X | Undo the draft (CONTROLS binding row: clear) |
| `Esc` | B or Menu/Plus | Close |

The pointer (mouse or Wii Remote) works it too: the row under it is selected. A setting's arrows step it (held, they
repeat) and go round at the ends, where the keys stop; a click elsewhere on the row steps it on. Volumes, sharpening
and the controls' numbers are sliders: drag the knob, or click the track. Holding A or the left button on the list and
moving drags it; a page's button, Apply and Back are clicked; the mouse wheel moves the selection. Closing
with changes not applied asks first: `Enter`/Y applies them and closes, `Backspace`/X discards them, `Esc`/B goes
back to the page (each is also a button to click). The hints along the bottom name the keys or buttons of whatever
was used last.

The Mods page is two lists, as in Nebula Settings: the mods off on the left, the mods on on the right (the higher one
wins where two change the same file or texture). Drag a mod with the pointer (hold A or the left button on it, let go
where the line shows), or press A (`Space`) on it, move it with the D-pad or stick (left: off, right: on, up and down:
its place) and press A again to put it down. The buttons under the lists import a file or a folder, open the selected
mod's options, open the mods folder and read it again. Changes wait for Apply, which then offers to restart (mods load
at the start); removing a mod is in Nebula Settings. Texture packs, texture dumps and the movies folder are on the
Graphics page.

On Switch controllers the menu uses the console's meaning: A changes, B closes. PlayStation: Cross changes, Circle
closes, Square clears, Triangle applies. Pages: Display, Graphics, Controls, Audio, General (text speed, button
icons, language, Discord, Miis), Mods and System. Every change waits for Apply, the mix and text speed included;
closing the menu or Undo drops what was not applied. A new language applies when Nebula next starts, as the game
reads it once at boot. Settings files are written through flushed sibling temporaries
and atomically replaced; a failed save stays dirty and is retried on the next
close or at shutdown.

## HOME Menu

Home (`H`; Home on Switch, PS on PlayStation, Steam on Steam controllers) opens the HOME Menu over the paused game:
Return to game, Reset and Quit to desktop (the console's Wii Menu); Reset and Quit ask first, as the console does. Up
and Down (or Left and Right) select, `Enter`/`Space`/A chooses, `Esc`/`Backspace`/B goes back (from the list: closes
it); the pointer selects what it is over and a click chooses it. The game draws it in its own style, in
the played language, with the console HOME Menu's sounds.

Diagnostic environment variables for input and audio are listed in
[DIAGNOSTICS.md](DIAGNOSTICS.md).
