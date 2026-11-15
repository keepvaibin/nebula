---
title: Keyboard and controller controls
shortTitle: Controls
intro: Play with a keyboard and mouse or almost any controller, change the default buttons, and tune the pointer, gyro and vibration.
contentType: reference
---

## About controls

The game sees one Wii Remote with a Nunchuk. Nebula turns your keyboard, mouse and controllers into that remote, and you can rebind every Wii input.

These devices work at the same time:

* Keyboard and mouse
* Xbox (XInput) controllers
* Nintendo Switch Pro Controller, Joy-Con (a left and a right half play as one controller) and Nintendo Switch 2 Pro Controller
* PlayStation controllers: DualShock 4, DualSense and DualSense Edge
* Steam Controller (2026 and 2015) and the Steam Deck's controls
* Generic HID game controllers
* A real Wii Remote. For more information, see [Using a Wii Remote](using-a-wii-remote.md).

Nebula reads each controller report as it arrives, and the game takes the newest state when it reads its remotes each frame. A press made just before the game reads its remotes shows in that frame.

## Default controls

Controllers follow Super Mario Galaxy on Nintendo Switch (Super Mario 3D All-Stars), with these changes:

* Star Bits are on L, LB or L1. Nintendo uses ZR.
* ZR, RT or R2 is a spin button.
* ZL, LT or L2 is crouch.

| Action (Wii input) | Keyboard and mouse | Xbox | Switch Pro and Switch 2 Pro | PlayStation | Steam |
|---|---|---|---|---|---|
| Jump, confirm (<kbd>A</kbd>) | <kbd>Space</kbd>, <kbd>Enter</kbd>, left mouse button | A | A | Cross | A |
| Jump in play, back in menus | None | B | B | Circle | B |
| Star Bits, back (<kbd>B</kbd>) | <kbd>Left Shift</kbd>, <kbd>Right Shift</kbd>, right mouse button | LB | L | L1 | LB |
| Crouch, ground pound (<kbd>Z</kbd>) | <kbd>Z</kbd>, <kbd>Left Ctrl</kbd>, <kbd>Right Ctrl</kbd> | LT | ZL | L2 | LT |
| Reset camera (<kbd>C</kbd>) | <kbd>C</kbd> | Left stick click | Left stick click | L3 | Left stick click |
| Spin (shake the Wii Remote) | <kbd>Q</kbd>, <kbd>E</kbd> | X, Y, RB, RT | X, Y, ZR, shake the controller | Square, Triangle, R2, shake the controller | X, Y, RT, shake the controller |
| Move (Nunchuk stick) | <kbd>W</kbd> <kbd>A</kbd> <kbd>S</kbd> <kbd>D</kbd> | Left stick | Left stick | Left stick | Left stick |
| Camera, first person (D-pad) | Arrow keys | D-pad | D-pad, or the right stick while the gyro points | D-pad, or the right stick while the gyro points | D-pad, or the right stick while the gyro points |
| Pointer | Mouse, <kbd>I</kbd> <kbd>J</kbd> <kbd>K</kbd> <kbd>L</kbd> | Right stick | Gyro, or the right stick when the gyro is off | Gyro, or the right stick when the gyro is off | Gyro, or the right stick when the gyro is off |
| Recenter the pointer | <kbd>R</kbd> | Right stick click | R | R1 | RB |
| Pause (<kbd>Plus</kbd>) | <kbd>=</kbd>, <kbd>P</kbd> | Menu | Plus | Options | Menu |
| <kbd>Minus</kbd> | <kbd>-</kbd>, <kbd>M</kbd> | View | Minus | Create (Share) | View |
| HOME Menu | <kbd>H</kbd> | None | Home | PS | Steam |
| <kbd>1</kbd>, <kbd>2</kbd> | <kbd>1</kbd>, <kbd>2</kbd> (and the number pad) | None | None | None | None |
| Shake the Nunchuk | None | None | None | None | None |

Switch buttons follow their labels: Nintendo A is Wii A. Xbox, PlayStation and Steam buttons follow their position: Cross is the bottom button, like Xbox A.

These buttons have no default action: Capture, GL, GR and C (Switch 2 Pro Controller); the touchpad click, mute and the paddles (PlayStation); and QAM, L4, R4, L5, R5 and the trackpad clicks (Steam).

### How B works on a controller

B on a controller never shoots Star Bits. It is Wii <kbd>B</kbd> (back) while a screen with a back action is open, and Wii <kbd>A</kbd> (jump) otherwise. Menus include the HOME Menu, the pause menu, the galaxy map, the title screen, file select, star select, yes and no questions, the storybook and the galaxy selector in the dome. Nebula decides when you press B and keeps that choice until you release it. L, LB and L1 always send Wii <kbd>B</kbd>.

### Pausing

A tap of <kbd>Plus</kbd> or <kbd>Minus</kbd> pauses the game. If you press one while holding <kbd>A</kbd> or <kbd>B</kbd>, the game pauses once you release them.

### Button pictures

The game's button pictures (<kbd>A</kbd>, <kbd>B</kbd>, <kbd>C</kbd>, <kbd>Z</kbd>, <kbd>1</kbd>, <kbd>2</kbd>, <kbd>Plus</kbd> and <kbd>Minus</kbd> in the title screen, dialogue, tutorials and menus) show the button each one is bound to on the controller in use:

* Xbox buttons show in their colors.
* Switch and PlayStation buttons show in white.
* Shoulders, triggers and stick clicks show by name.
* A generic controller shows Xbox buttons by position.
* A DualShock 3 shows Select and Start. A GameCube controller shows L, R, ZL, Z and Start.

To keep one style whatever you use, set **Button icons** on the **GENERAL** page to **Wii**, **Xbox**, **Nintendo Switch** or **PlayStation**. With **Xbox**, **Nintendo Switch** or **PlayStation**, the settings menu's hints use that style too. With **Wii** or **Auto**, they follow the device you used last.

A texture pack that replaces the game's button pictures still shows with the **Wii** style. Nebula's own button pictures show as drawn unless a pack replaces them by name.

## Input modes

Choose the input mode on the **CONTROLS** page, in its first row.

| Mode | `input_mode` value | What the game reads |
|---|---|---|
| **INPUT: AUTO** (default) | `auto` | Every device at once. The row shows the device in use. |
| **INPUT: KEYBOARD+MOUSE ONLY** | `keyboard_mouse` | The keyboard and mouse only. The settings menu still answers controllers. |
| **INPUT: CONTROLLER ONLY** | `controller` | Controllers only. |
| **INPUT: WII REMOTE (SENSOR BAR)** | `real_wiimote` | A real Wii Remote, pointing at a sensor bar. |
| **INPUT: WII REMOTE (MOTION)** | `wiimote_motion` | A real Wii Remote, pointing with Wii MotionPlus. |
| **INPUT: PHONE** | `phone` | Android only. The on-screen controls. |

If the chosen mode has nothing to play with, for example **CONTROLLER ONLY** with no controller connected, the game stops and says so until a device is back. Press <kbd>F1</kbd> or <kbd>Enter</kbd> to open the settings menu and choose another mode.

### How Auto mode shares the pointer

In **INPUT: AUTO**, the pointer belongs to the source that moved last: the mouse, the pointer keys, a right stick or a gyro. Another source takes over once the current one has been still for 250 milliseconds. A connected controller keeps the pointer on the screen.

With a controller connected, Auto mode ignores keys and mouse movement that software generates, such as Steam's desktop controller layout. This keeps a press from counting twice.

A real Wii Remote takes over at its first button press, and hands back when you use a key, a mouse button or a controller.

## Pointer, gyro and motion

### Mouse

The mouse points while the cursor is inside the game picture, even when another window has focus. Keys and mouse buttons need the game window to have focus. The mouse pointer never hides on its own.

### Right stick

The right stick moves the pointer slowly near the center and at `pointer_speed` at full tilt. The pointer stops at the edge of the picture.

### Gyro

Switch, PlayStation and Steam controllers point with their gyro:

* Turning the controller left or right moves the pointer sideways.
* Tilting it up or down moves the pointer up and down.

This works with the controller held flat or upright. One picture width is 40 degrees of turn at `gyro_sensitivity=1`. Nebula measures the gyro's drift again whenever the controller rests. When you point past the picture, the pointer disappears, as with a Wii Remote, and comes back when you turn back.

### Touchpad

PlayStation controllers can also point with their touchpad, and Steam controllers with their right trackpad. Slide a finger on it to move the pointer, as on a laptop's touchpad. When you lift the finger, the pointer stays where it is. A finger on the touchpad takes the pointer at once, even while the gyro is pointing.

To choose what points, open the **CONTROLS** page and set **POINTER**:

* **STICK**: the right stick only.
* **MOTION**: the gyro.
* **TOUCHPAD**: the touchpad.
* **MOTION + TOUCHPAD**: the gyro and the touchpad. This is the default.

The **POINTER** row lists only the choices the connected controller supports.

**TOUCHPAD SENSITIVITY** sets how far a slide moves the pointer. At `1.0`, a slide across the whole touchpad moves the pointer one picture width. The range is `0.2` to `5.0`.

### Recentering the pointer

Press R (Switch), R1 or RB (PlayStation, Steam), the right stick click (Xbox) or <kbd>R</kbd> (keyboard) to move the pointer to the middle of the picture. This also brings back a hidden pointer, on any menu. While you hold the button, the pointer stays in the middle and ignores gyro and stick movement. When you release it, the pointer moves from the middle, so the controller's direction at that moment becomes the new center.

### Hiding a still pointer

A controller pointer that stays within `pointer_hide_radius` of one place for `pointer_hide_seconds` disappears, as in the Switch release. The defaults are 0.12 (2.4 degrees of turn) and 4 seconds. Hand tremor inside the radius does not restart the timer.

* A hidden gyro pointer comes back only with the recenter button, at the center.
* A hidden stick pointer also comes back when you push the stick.

### Shake

On Switch, PlayStation and Steam controllers, a jolt of the controller or a fast flick of the wrist is a shake. A shake lasts 60 milliseconds, with at most one every 280 milliseconds.

### Tilt

The game steers the Star Ball and Ray Surfing with the Wii Remote's tilt.

* Switch, PlayStation and Steam controllers: the way you hold the controller when the section starts is level. Tilt to roll the ball, or tilt left or right to steer the ray. Press R to take the current hold as level again.
* Xbox controllers: steer with the left stick.
* Keyboard: steer with <kbd>W</kbd> <kbd>A</kbd> <kbd>S</kbd> <kbd>D</kbd>.

The controller you used last decides. Outside these sections, the remote's tilt stays at rest, and the pointer never rotates with the controller.

### Camera on the right stick

The game turns the camera one step for each press of the D-pad left or right. One push of the stick is one step, like one press of the Wii D-pad.

## Vibration

When the game runs the Wii Remote's motor, for example for a Star Bit shot, a hit or an earthquake, the controller in use vibrates:

* Xbox, PlayStation and Steam controllers use their light (high-frequency) motor.
* Switch Pro Controllers and Joy-Con use HD Rumble.
* The Switch 2 Pro Controller (USB) uses its rumble report.
* A real Wii Remote gets the game's own motor commands.

Nebula copies the Wii Remote motor's feel: it spins up over about 30 milliseconds and down over about 60 milliseconds, so the game's short pulses feel like one soft buzz. A key or mouse button bound to a Wii input gives vibration to the keyboard, which has none.

The motor stops at once when any of these happens:

* The game window loses focus.
* A pause, HOME or settings menu opens.
* The game stalls for 100 milliseconds.
* The controller disconnects.
* Nebula exits or crashes.

Turn vibration on or off with `vibration`, and set its strength with `rumble`. For more information, see [Tuning values](#tuning-values).

## Joy-Con

1. Pair each Joy-Con in Windows Bluetooth settings: hold the sync button on its rail. Or attach both halves to the charging grip by USB.
1. Start Nebula. One left and one right half pair by themselves, as Joy-Con (L/R), at every start.

A Joy-Con pair uses the Switch layout and button pictures: the left half's stick, D-pad, L, ZL, Minus and Capture, and the right half's buttons, stick, R, ZR, Plus and Home. The right half points and tilts, a shake of either half counts, and both halves vibrate.

### Pairing more than two halves

With more halves connected, for two players or to swap halves, open **PAIR JOY-CON** on the **CONTROLS** page. Hold L on a left half and R on a right half to pair them, as on the Switch's **Change Grip/Order** screen. One held shoulder alone never splits another pair. Pairs are not kept between starts.

Nebula checks an empty rail on the charging grip every 2 seconds, so it finds a Joy-Con you slide in later.

## Steam Deck, Steam Controller and 8BitDo

The Steam column of the default controls covers the Steam Deck's controls and both Steam Controllers. Nebula turns off their keyboard and mouse emulation itself.

* Steam Deck: sticks, triggers, gyro, both trackpads' clicks, L4, R4, L5, R5 and QAM (no default action) and vibration.
* Steam Controller (2015), by USB or its wireless receiver: the stick; the right trackpad as the right stick (the pointer); the left trackpad's clicks as the D-pad; the grips as L4 and R4; gyro; and vibration through the trackpads. Its Bluetooth LE mode is not supported. Use the receiver or USB.
* Steam Controller (2026), by USB, Bluetooth LE or its wireless receivers: buttons, sticks, gyro and vibration. The trackpads' surfaces are not read, only their clicks.

When Steam starts Nebula as a game, Steam Input reads its own controllers and passes them to Nebula as an Xbox controller. Nebula leaves those controllers to Steam, so a press never counts twice. When you start Nebula outside Steam with Steam's controller support on, Steam keeps its desktop layout on Nebula's window. Auto mode ignores the keys and mouse movement that layout makes while a controller is connected.

8BitDo controllers work in every mode:

* X-input works as an Xbox controller.
* Switch works as a Pro Controller, with stick calibration and motion only where the controller supports them.
* D-input works as a generic controller. Rebind it on the **CONTROLS** page.

## Supported controllers

| Controller | Connection | Notes |
|---|---|---|
| Xbox | XInput, slots 0 to 3 | Read while the game window has focus. |
| Switch Pro Controller | USB or Bluetooth | Nebula reads the stick and motion calibration from the controller, and sets player light 1. |
| Switch 2 Pro Controller | USB only | Needs the WinUSB driver on its second USB interface ("Interface 1"), for example installed with Zadig. Without it, the **CONTROLS** page shows "NEEDS WINUSB". Its Bluetooth LE connection is not supported. |
| DualShock 4, DualSense, DualSense Edge | USB or Bluetooth | Nebula reads the motion calibration, sets the light bar to gray once and uses the light motor for vibration. Buttons, sticks, analog triggers, the touchpad click, mute and the Edge's paddles and Fn buttons are read. The touchpad surface, Sony's USB wireless adapter and third-party copies are not supported. |
| Steam Controller (2026) | USB, Bluetooth LE or wireless receiver | See [Steam Deck, Steam Controller and 8BitDo](#steam-deck-steam-controller-and-8bitdo). |
| Steam Controller (2015) | USB or wireless receiver | See [Steam Deck, Steam Controller and 8BitDo](#steam-deck-steam-controller-and-8bitdo). |
| Generic controller | USB or Bluetooth (HID) | Any HID joystick or gamepad that no other row covers, listed by its product name. No vibration. |

### Generic controller layout

Nebula reads a generic controller's buttons by number. Layouts differ between controllers, so rebind it on the **CONTROLS** page if the layout is wrong.

| Input | Meaning |
|---|---|
| Buttons 1 to 4 | Face buttons. Button 1 jumps, and button 2 goes back in menus. |
| Buttons 5 and 6 | Shoulders |
| Buttons 7 and 8 | Triggers, when the controller reports them as buttons |
| Button 9 | Back |
| Button 10 | Start |
| Buttons 11 and 12 | Stick clicks |
| Button 13 | Home |
| X and Y axes | Left stick |
| Z and Rz axes (or Rx and Ry) | Right stick |
| The remaining pair of axes (or Brake and Accelerator) | Triggers |
| Hat | D-pad |

## Changing controls

To change controls in the game, open the settings menu and go to the **CONTROLS** page. To open the settings menu, press <kbd>F1</kbd>, View+Menu (Xbox, Steam), Share+Options (PlayStation) or Minus+Plus (Switch). Nebula Settings has the same choices on its **Controls** tab.

| Row | Keyboard | Controller |
|---|---|---|
| **INPUT**: choose the input mode | <kbd>Left</kbd> or <kbd>Right</kbd> | D-pad or stick |
| **EDIT**: choose the device to edit | <kbd>Left</kbd> or <kbd>Right</kbd> | D-pad or stick |
| A binding row: add the next input you press | <kbd>Enter</kbd> or <kbd>Right</kbd> | A |
| A binding row: clear it | <kbd>Backspace</kbd> | X |
| Stop waiting for an input | <kbd>Esc</kbd> | None |
| Apply the page | <kbd>Space</kbd> | Y |

The **EDIT** row lists the keyboard and mouse, and each controller connected now, by model name. A model connected more than once is numbered, with its connection: USB, Bluetooth or wireless. "ACTIVE" marks the device that gave input last. A controller edits the bindings and tuning of its family: Xbox, Switch Pro, Switch 2 Pro, PlayStation or Steam.

### Binding rules

* Each Wii input holds up to four inputs. Adding a fifth replaces the last.
* Stick and pointer directions accept stick directions and buttons. Mouse buttons bind only to button rows.
* A stick direction bound to a button row presses at half tilt and releases below 35%.
* A right stick direction bound to a pointer row and to another row is the pointer while the gyro is off, and the other row while the gyro points.
* Controller-only inputs can be chosen only while a controller is connected.

## The controls file

Nebula saves controls in `runtime_settings.ini`, next to `config.ini`.

```ini
input_mode=auto
controls_version=7
bind.keyboard.a=Space, Enter, MouseLeft
bind.xbox.jump_back=B
bind.switch_pro.b=L
tune.switch_pro.gyro_sensitivity=1
```

Each `bind.SCHEME.TARGET` line lists the inputs bound to one Wii input. Each `tune.SCHEME.NAME` line sets one tuning value.

When Nebula loads a file with an older `controls_version`, it updates the file: bindings and tuning values that still hold an older version's defaults take the current defaults, and values you changed stay.

### Schemes

`keyboard`, `xbox`, `switch_pro`, `switch_pro2`, `playstation`, `steam` and `generic`.

### Targets

`a`, `b`, `c`, `dpad_down`, `dpad_left`, `dpad_right`, `dpad_up`, `home`, `jump_back`, `minus`, `nunchuk_shake`, `one`, `plus`, `pointer_down`, `pointer_left`, `pointer_recenter`, `pointer_right`, `pointer_up`, `shake`, `stick_down`, `stick_left`, `stick_right`, `stick_up`, `two` and `z`.

### Input names

| Device | Names |
|---|---|
| Keyboard | Key names, for example `Space`, `LShift`, `W`, `Num1`, `F5`, `Equals`, or `VK2E` for a virtual-key code |
| Mouse | `MouseLeft`, `MouseRight`, `MouseMiddle`, `Mouse4`, `Mouse5` |
| Xbox | `A`, `B`, `X`, `Y`, `LB`, `RB`, `LT`, `RT`, `LS`, `RS`, `Menu`, `View`, `DpadUp`, `DpadDown`, `DpadLeft`, `DpadRight` |
| Switch | `A`, `B`, `X`, `Y`, `L`, `R`, `ZL`, `ZR`, `LS`, `RS`, `Plus`, `Minus`, `Home`, `Capture`, `GL`, `GR`, `C`, `DpadUp`, `DpadDown`, `DpadLeft`, `DpadRight` |
| PlayStation | `Cross`, `Circle`, `Square`, `Triangle`, `L1`, `R1`, `L2`, `R2`, `L3`, `R3`, `Options`, `Share`, `Create`, `PS`, `Touchpad`, `LeftPaddle`, `RightPaddle`, `Mute`, `LeftFn`, `RightFn`, `DpadUp`, `DpadDown`, `DpadLeft`, `DpadRight` |
| Steam | `A`, `B`, `X`, `Y`, `LB`, `RB`, `LT`, `RT`, `LS`, `RS`, `Menu`, `View`, `Steam`, `QAM`, `L4`, `R4`, `L5`, `R5`, `LPad`, `RPad`, `DpadUp`, `DpadDown`, `DpadLeft`, `DpadRight` |
| Generic | `Button1` to `Button19`, `LT`, `RT`, `DpadUp`, `DpadDown`, `DpadLeft`, `DpadRight` |
| Sticks (every controller) | `LX+`, `LX-`, `LY+`, `LY-`, `RX+`, `RX-`, `RY+`, `RY-` |

### Tuning values

| Value (`tune.SCHEME.`) | Applies to | Range | Default |
|---|---|---|---|
| `gyro_pointer` | Gyro controllers | `0` or `1` | `1` |
| `gyro_sensitivity` | Gyro controllers | `0.2` to `5.0`. At `1`, one picture width is 40 degrees. | `1` |
| `invert_pointer_y` | Every scheme | `0` or `1` | `0` |
| `motion` | Gyro controllers | `off`, `remote` or `nunchuk`: which Wii controller the controller's motion acts as | `remote` |
| `pointer_deadzone` | Every scheme | `0` to `0.5` of full tilt, measured around the center | Xbox: `0.27`. Other controllers: `0.12`. |
| `pointer_hide_radius` | Controllers | `0` to `0.5` pointer units (2 is the picture width) | `0.12` |
| `pointer_hide_seconds` | Controllers | `0` to `60` seconds. `0` never hides the pointer. | `4` |
| `pointer_speed` | Every scheme | `0.2` to `6.0` pointer units each second at full tilt (2 is the picture width) | Keyboard: `1.6`. Controllers: `2.4`. |
| `rumble` | Controllers | `10` to `100` percent, in steps of 10 | `50` |
| `shake_sensitivity` | Gyro controllers | `1` (hard shake) to `5` (light shake) | `3` |
| `stick_deadzone` | Every scheme | `0` to `0.5` of full tilt | Xbox: `0.24`. Other controllers: `0.12`. |
| `touch_pointer` | PlayStation and Steam | `0` or `1` | `1` |
| `touch_sensitivity` | PlayStation and Steam | `0.2` to `5.0`. At `1`, a slide across the whole touchpad is one picture width. | `1` |
| `vibration` | Controllers | `0` or `1` | `1` |

## Settings menu

To open or close the settings menu, press <kbd>F1</kbd>, View+Menu (Xbox, Steam), Share+Options (PlayStation) or Minus+Plus (Switch), or select **Settings** in the pause menu. The menu opens once you have chosen a save file, not over the logos, the title screen or file select. While it is open, the game holds still: the scene stops, the music pauses, the game's sounds fade out and a movie waits. Buttons you hold when the menu closes stay hidden from the game until you release them.

| Keyboard | Controller | Action |
|---|---|---|
| <kbd>F1</kbd> | View+Menu, Share+Options, Minus+Plus | Open or close the menu |
| <kbd>Tab</kbd> or <kbd>Page Down</kbd>, <kbd>Page Up</kbd> | RB or R, LB or L | Next page, previous page |
| <kbd>Up</kbd>, <kbd>Down</kbd> | D-pad or left stick up and down | Select a row |
| <kbd>Left</kbd>, <kbd>Right</kbd> | D-pad or left stick left and right, A | Change the selected row |
| <kbd>Left</kbd>, <kbd>Right</kbd>, <kbd>Space</kbd> | D-pad or left stick left and right, A | In a row of buttons, such as **Import file** and **Open**: select one and press it |
| <kbd>Enter</kbd> | Y | Apply. On the **CONTROLS** page, use <kbd>Space</kbd> or Y. |
| <kbd>Backspace</kbd> | X | Undo the changes. On a binding row, clear it. |
| <kbd>Esc</kbd> | B, or Menu or Plus | Close the menu |

On Switch controllers, A changes and B closes. On PlayStation controllers, Cross changes, Circle closes, Square clears and Triangle applies.

### Using a pointer in the settings menu

The mouse or a Wii Remote also works the menu: the row under the pointer is selected.

* A setting's arrows step it, and repeat while held. Clicking elsewhere on the row also steps it.
* Volumes, sharpening and the controls' numbers are sliders: drag the knob, or click the track.
* Hold A or the left mouse button on the list and move to scroll it.
* The mouse wheel moves the selection.

### Applying changes

Every change waits for **Apply**, including the sound mix and text speed. Closing the menu or pressing **Undo** drops changes you did not apply. If you close the menu with changes not applied, Nebula asks first:

* <kbd>Enter</kbd> or Y applies them and closes.
* <kbd>Backspace</kbd> or X discards them.
* <kbd>Esc</kbd> or B goes back to the page.

A new language applies the next time Nebula starts, because the game reads it once at start.

### Pages

The settings menu has these pages: **DISPLAY**, **GRAPHICS**, **CONTROLS**, **AUDIO**, **GENERAL** (text speed, button icons, language, Discord and Miis), **MODS** and **SYSTEM**. Texture packs, texture dumps and the movies folder are on the **GRAPHICS** page.

### The MODS page

The **MODS** page has two lists: mods that are off on the left, and mods that are on on the right. Where two mods change the same file or texture, the higher one wins.

To move a mod, do one of these:

* Drag it with the pointer: hold A or the left mouse button on it, and release where the line shows.
* Press A (<kbd>Space</kbd>) on it, move it with the D-pad or stick (left turns it off, right turns it on, up and down change its place), and press A again to put it down.

The buttons under the lists import a file or a folder, open the selected mod's options, open the mods folder and read the folder again. Changes wait for **Apply**, which then offers to restart, because mods load when Nebula starts. To remove a mod, use Nebula Settings.

## HOME Menu

Press <kbd>H</kbd>, Home (Switch), PS (PlayStation) or Steam (Steam controllers) to open the HOME Menu over the paused game. It has **Return to game**, **Reset** and **Quit**. **Reset** and **Quit** ask first.

| Keyboard | Controller | Action |
|---|---|---|
| <kbd>Up</kbd>, <kbd>Down</kbd>, <kbd>Left</kbd>, <kbd>Right</kbd> | D-pad or left stick | Select |
| <kbd>Enter</kbd>, <kbd>Space</kbd> | A | Choose |
| <kbd>Esc</kbd>, <kbd>Backspace</kbd> | B | Go back, or close the menu from the list |

The pointer selects the item under it, and a click chooses it.

## Acknowledgements

Nebula's controller code is its own implementation. Its protocol details follow SDL's zlib-licensed HIDAPI drivers for Switch, Switch 2, PlayStation 4, PlayStation 5 and the Steam Controller, Valve's zlib-licensed `controller_structs.h` and `controller_constants.h`, the Linux `hid-steam.c` driver, and dekuNukem's Nintendo Switch reverse-engineering notes. The gyro pointer's axes follow JoyShockMapper's description of player space and world space.

## Further reading

* [Using a Wii Remote](using-a-wii-remote.md)
* [Diagnostics](diagnostics.md)
