---
title: Using a Wii Remote
shortTitle: Wii Remote
intro: Play Nebula with a real Wii Remote, pointing at a sensor bar or by motion with Wii MotionPlus.
contentType: how-tos
---

## About Wii Remote support

Nebula reads a real Wii Remote (RVL-CNT-01 or RVL-CNT-01-TR), with or without a Nunchuk, on Windows and Android. The remote points in one of two ways:

* At a sensor bar, with the remote's IR camera.
* By motion, with Wii MotionPlus, either built in (RVL-CNT-01-TR) or as an attachment.

Nebula reads the remote's buttons, accelerometer, IR camera, Nunchuk and MotionPlus, and drives its rumble motor.

## Connecting a Wii Remote on Windows

1. Pair the Wii Remote in Windows Bluetooth settings, or plug in a Mayflash DolphinBar.
1. Start Nebula. The **CONTROLS** page shows "WII REMOTE CONNECTED", or "WII REMOTE AND NUNCHUK CONNECTED".

## Connecting a Wii Remote on Android

Android connects a Wii Remote through a DolphinBar on the USB port. The phone's own Bluetooth cannot carry a Wii Remote to an app.

1. Set the DolphinBar to mode 4.
1. Plug the DolphinBar into the phone's USB port, with an adapter if needed. Android offers to open Nebula.
1. When Android asks to allow Nebula to use the device, select **Always** and allow it.

Both pointer modes, the Nunchuk and rumble work as they do on Windows. While the remote is in use, the on-screen controls hide, as they do for a controller.

### Why the phone's Bluetooth does not work

* An app cannot open a Wii Remote's two HID channels. Android's classic L2CAP sockets require enhanced retransmission mode, and a Wii Remote supports only basic mode, so the connection fails about 100 milliseconds after the link comes up.
* When paired in Android settings, the remote goes to the kernel's `hid-wiimote` driver. The driver needs one contiguous 1 MB block of memory, which a phone that has run for a while rarely has, so it fails with "Can't alloc device".
* Even when the driver starts, Android passes only devices with keys or gamepad buttons to apps. The remote's buttons and the Nunchuk reach apps, but its accelerometer, IR camera and MotionPlus do not.

## Choosing an input mode

Choose the mode on the **Controls** page of the settings, in its first row. These are the modes for a Wii Remote:

| Mode | `runtime_settings.ini` value | Pointer | Buttons |
|---|---|---|---|
| **INPUT: AUTO** | `auto` | The sensor bar, while a Wii Remote is in use. | Every button goes to the game. |
| **INPUT: WII REMOTE (SENSOR BAR)** | `real_wiimote` | The sensor bar. | Every button goes to the game. |
| **INPUT: WII REMOTE (MOTION)** | `wiimote_motion` | Wii MotionPlus. A remote without MotionPlus points at the sensor bar. | <kbd>2</kbd> controls the pointer. |

In both Wii Remote modes, you can use every menu without a pointer:

* The D-pad and the Nunchuk stick move the menu focus.
* <kbd>A</kbd> selects.
* <kbd>B</kbd> goes back.

A remote that never sees a sensor bar can play the whole game this way. Only the Star Pointer itself, for Star Bits and pull stars, needs a pointer.

## Pointing by motion

In the **INPUT: WII REMOTE (MOTION)** mode, the pointer works like the gyro pointer of the game's Nintendo Switch release.

* Press <kbd>2</kbd> to start the pointer. The pointer is hidden until the first press of <kbd>2</kbd> after the remote connects.
* Press <kbd>2</kbd> to recenter. The pointer moves to the middle of the picture and stays there while you hold <kbd>2</kbd>. When you release it, the pointer follows the remote from where it now points.
* Hold still to hide it. The pointer hides after 5 seconds within 2.4 degrees of one place, and stays hidden until you press <kbd>2</kbd>.
* Point past the screen to leave it. The pointer leaves the picture when the remote turns past the screen, and comes back where the remote points when it turns back, up to a picture's width past each edge.
* Hold the remote upright for the Star Ball. An upright remote has no pointer, as with a sensor bar.

Turning the remote 40 degrees moves the pointer one picture width. Nebula learns the gyro's drift while the remote rests, and a remote turning slower than 1.5 degrees each second holds still.

In this mode <kbd>2</kbd> never reaches the game. The game reads <kbd>2</kbd> only as "any button", which <kbd>A</kbd> also answers.

### The Wii Remote line on the CONTROLS page

The **CONTROLS** page shows the remote's state on its Wii Remote line. The line has no arrows, because it is not a setting.

| Line | Meaning |
|---|---|
| "NO WII REMOTE CONNECTED" | Nebula sees no Wii Remote. |
| "WII REMOTE CONNECTED" or "WII REMOTE AND NUNCHUK CONNECTED" | The remote is ready. |
| "PRESS 2 TO CENTER THE POINTER" | Motion mode: MotionPlus is on. |
| "TURNING ON THE MOTIONPLUS" | Motion mode: Nebula is setting up MotionPlus. |
| "NO MOTIONPLUS. POINTING AT THE SENSOR BAR" | Motion mode: the remote has no MotionPlus. |

## Further reading

* [Keyboard and controller controls](keyboard-and-controller-controls.md)
