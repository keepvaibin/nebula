# Controller checklist

For each controller on real hardware, before a release. The unit tests cover the report layouts and the mapping;
these checks cover what only a real controller shows. Mark each cell pass, fail (with a note) or n/a.

## Every controller

1. Connects: its name shows under CONTROLS > EDIT within a second (by each link it has: USB, Bluetooth, receiver).
2. Icons: the game's button icons and the settings menu's hints show its family's buttons.
3. Buttons: every row of the Default maps table in [KEYBOARD_CONTROLS.md](KEYBOARD_CONTROLS.md) does what it says,
   in play and in menus (B backs out of menus, jumps in play).
4. Sticks and triggers: full travel both ways, no drift at rest, crouch on a half pull.
5. Pointer: the right stick (and the gyro where it has one) moves it; recenter brings it to the middle.
6. Motion (where it has a gyro): shake spins; the Star Ball rolls with tilt (Rolling Green Galaxy) and the ray
   steers with it (Beach Bowl Galaxy, Surfing 101).
7. Vibration: a Star Bit shot and a hit buzz briefly; none in a menu, on pause or with the window in the background.
8. Hot-plug: unplugged (or switched off) in play, the game goes on; back, it works again within two seconds.
9. Two at once: two of this family, then this one with an Xbox controller: both move Mario; neither repeats a press.
10. Latency: a fast jump-spin-jump feels as immediate as with an Xbox controller.

## Families

| Controller | Links | Notes for the checks |
|---|---|---|
| Xbox Series / One / 360 | USB, Bluetooth, receiver | XInput; vibration on the light motor |
| DualShock 4 | USB, Bluetooth | gyro; light bar set once |
| DualSense, DualSense Edge | USB, Bluetooth | gyro; Edge paddles and Fn buttons unbound by default |
| Switch Pro Controller | USB, Bluetooth | gyro, HD rumble |
| Joy-Con (L) + (R) | Bluetooth, charging grip | pair by themselves; CONTROLS > PAIR JOY-CON with two pairs; pointer from the right half; both vibrate; slide one into the grip after it is plugged in |
| Steam Controller (2026) | USB, Bluetooth, puck | no mouse or keyboard from it; gyro |
| Steam Deck | built in | trackpads not the mouse, R2/L2 not mouse buttons, for minutes on end (the watchdog); gyro; vibration |
| Steam Controller (2015) | USB, receiver | right pad is the pointer; left pad clicks are the D-pad; no mouse; pad haptics as vibration |
| Steam Input on | any of the above, started from Steam | the log says "left to Steam Input"; every press once |
| 8BitDo | X-input, Switch, D-input modes | X: as Xbox; Switch: as Pro (motion if the pad has it); D: rebind on CONTROLS |
| Generic HID pad | USB | buttons by number; rebind on CONTROLS |
| Wii Remote (+ Nunchuk) | Bluetooth | read directly; IR pointer, speaker sounds, its motor |

## Device list and proof

The missing-controller screen, virtual DS4 in Controller mode: shown with no pad, gone within 2 s of it arriving, back
within 2 s of it leaving, twice over (`hotplug_ds4ctl`). In Auto, no screen.

Decoder tested: a unit test feeds the documented report bytes (`galaxy_pad_controller_tests`). No row below has been on
real hardware unless it says so; the checks above are what prove it.

Virtual pad: a ViGEm device on the Windows device path, a hidden run (`vigem_hotplug.sh`). It shows connecting, the
reports reaching play and reconnecting; it shows nothing about a real pad's timing, sensors or Bluetooth.

| Device | Ids | Read by | Motion, touch to POINTER | Vibration | Proof |
|---|---|---|---|---|---|
| Joy-Con 2 (L), (R) | 057e:2067, 2066 | USB vendor interface (WinUSB), report 0x05; a pair is one pad | right half's motion | report 0x01 | decoder tested, unproven on a real pad |
| Joy-Con 2, grip id | 057e:2068 | as a Pro Controller 2 | motion | report 0x02 | no source shows hardware with this id |
| GameCube (Switch 2) | 057e:2073 | USB vendor interface, report 0x05, triggers at 61, 62 | none | on or off, pulsed | decoder tested, unproven on a real pad |
| Joy-Con 2, Pro 2, GameCube over Bluetooth | 057e:2067, 2066, 2069, 2073 by their advertising | custom GATT, Windows' GATT API; the USB report without its id | as over USB | vibration characteristic | frames decoder tested; the link never run on a real pad. Connects while the pad is in pairing mode (hold its sync button): no reconnecting by itself |
| DualShock 4 adapter | 054c:0ba0 | as DualShock 4; neutral with no pad | gyro, touchpad | yes | decoder tested, unproven on a real pad |
| DualSense Access Controller | 054c:0e5f | as DualSense | none | none | decoder tested, unproven on a real pad |
| Licensed DS4 and DualSense pads | Hori, Razer, Nacon, PDP, Backbone, ... (playstation_controller.cpp) | as DS4 or DualSense, capabilities from feature 0x03 | what the pad reports | what the pad reports | decoder tested, unproven on a real pad |
| DualShock 3 | 054c:0268 | report 0x01; started by features 0xf2, 0xf5 | accelerometer: tilt and shake; pointer on the stick | report 0x01 | decoder tested, unproven on a real pad; Windows may need a driver for it |
| NSO SNES, N64, Genesis | 057e:2017, 2019, 201e | Pro Controller protocol | none | as a Pro | shares the Pro's tested decoder; unproven on a real pad |
| NES, Famicom (Bluetooth) | 057e:2007 by device type | Pro Controller protocol, one pad each | none | as a Pro | unproven on a real pad |
| Hori Wireless Switch Pad | 0f0d:00f6, Bluetooth | Pro Controller protocol | gyro where it reports it | as a Pro | shares the Pro's tested decoder; unproven on a real pad |
| PDP Afterglow, Wave | 0e6f:0186, 018b, Bluetooth | Pro Controller protocol | gyro where it reports it | as a Pro | shares the Pro's tested decoder; unproven on a real pad |
| PDP REALMz | 0e6f:018c, Bluetooth | Pro Controller protocol | none | as a Pro | shares the Pro's tested decoder; unproven on a real pad |
| Afterglow 0193 | 0e6f:0193 | generic HID: no source gives its protocol | none | none | not established |
| Input-only Switch pads | Hori 00c1, 0092, 00aa, 00f0; PDP 0180, 0181, 0184, 0185, 0187, 0188; PowerA a711-a716, a718; Zuiki 0001-0003 | fixed report | none | none | decoder tested, unproven on a real pad |
| Stadia | 18d1:9400 | report 0x03 | none | report 0x05 | decoder tested, unproven on a real pad |
| Luna | 1949:0419 USB, 0171:0419 Bluetooth; 1949:041a is XInput | fixed reports | none | Bluetooth only | decoder tested, unproven on a real pad |
| Hori Steam pad | 0f0d:01ab, 0196 | fixed report | gyro | none | decoder tested, unproven on a real pad |
| Steam Controller (2015), Bluetooth | 28de:1105, 1106 | report 0x03 in segments | gyro, right pad | pad pulses | decoder tested, unproven on a real pad |
| Wii U Pro | 057e:0330 named RVL-CNT-01-UC | report 0x3d, extension bytes | none | yes | decoder tested, unproven on a real pad; the name test unproven |
| DualShock 4 | 054c:05c4 | report 0x01 | gyro, touchpad | yes | decoder tested; virtual pad: connects, calibrates, reads, leaves and returns, Controller and Auto; not on a real pad in this work |
| Xbox over XInput | | XInput: four pads; no paddles, Share or trigger motors | none | two motors | virtual pad: its input reaches play in Auto; XInput logs no connection. Real pad, Bluetooth LE 045e:0b13, by hand (`realpad_hands`): every button, the D-pad, both stick clicks, sticks to full travel, both triggers reach the game's sample. The motor command is accepted (`realpad_x`); nobody reported feeling it. XInput shows no Guide button |
| Switch Pro over Bluetooth | 057e:2009 | report 0x30 | gyro | HD rumble | real pad: connects, 66.7 reports a second, none lost; motion on, sticks centred; the game's motor felt by its own accelerometer, peak 0.05 g to 0.09 g (`realpad_c`); no drift at rest; report mode and motion recover after another program changes them (`realpad_g`, `realpad_h`). By hand (`realpad_hands`): every button, the D-pad, both stick clicks, sticks to full travel, ZL and ZR reach the game's sample; Home reaches the decoder and is taken before the sample; one shake counted. Tilt by hand and USB not run |
| Joy-Con, DualSense, Steam controllers, Deck | | their own reports | as listed under Families | as listed | decoder tested; not on a real pad in this work |

Not read, on any device: Joy-Con 2's optical mouse sensor, the magnetometer, a second finger on a touchpad, a Steam pad's left trackpad as a pointer, Elite paddles.

The log lines to look for start with `[controller]`: the scan, each controller connected or disconnected, and a
Joy-Con pair made.
