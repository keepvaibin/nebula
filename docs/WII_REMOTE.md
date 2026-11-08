# Real Wii Remote

How Nebula reads a real Wii Remote (RVL-CNT-01, RVL-CNT-01-TR, with or without a Nunchuk) on every system, and the two
ways it points: at the sensor bar (the IR camera) or by motion (Wii MotionPlus).

## Input modes

| Mode (settings row) | `runtime_settings.ini` | Pointer | Buttons |
|---|---|---|---|
| `INPUT: AUTO` | `auto` | the sensor bar, while a remote is in use | all to the game |
| `INPUT: WII REMOTE (SENSOR BAR)` | `real_wiimote` | the sensor bar | all to the game |
| `INPUT: WII REMOTE (MOTION)` | `wiimote_motion` | Wii MotionPlus; the sensor bar when the remote has none | 2 is the pointer's (below) |

Both Wii Remote modes keep every menu usable without a pointer: the D-pad and the Nunchuk stick move the menus' focus
(NebulaMenuFocus), A takes it, B goes back. A remote that never sees a sensor bar plays the
whole game this way. Only the Star Pointer's own uses (Star Bits, pull stars) need a pointer.

## Links

| System | Link | What reaches Nebula |
|---|---|---|
| Windows | HID (paired in Windows, or a DolphinBar) | raw reports: buttons, accelerometer, IR, extensions, MotionPlus, motor, speaker |
| Android | USB host, DolphinBar in mode 4 (WiimoteUsb.java) | raw reports, as on Windows |

### Android: the DolphinBar, not the phone's Bluetooth

On Android a real Wii Remote goes through a DolphinBar in mode 4 on the phone's USB port: the launcher's USB intent
filter opens Nebula when it is plugged in, WiimoteUsb.java asks for the device once (Android's own "allow" with its
"always" box) and carries the raw reports both ways, and wiimote_link_android.cpp hands them to the same worker as on
Windows, so both pointer modes, the Nunchuk and the motor work as they do there. While the remote is taken up, the phone's on-screen controls step aside as they do for a controller.

The phone's own Bluetooth cannot carry a Wii Remote for an app, on any phone (checked 11-07 on Android 17 and in
Android's source):

- An app cannot open the remote's two HID channels itself. BluetoothDevice.createInsecureL2capSocket is hidden, and
  even when reached, Android's classic L2CAP sockets always ask for enhanced retransmission mode
  (btif_sock_l2cap.cc, `obex_l2c_etm_opt`); l2c_fcr_chk_chan_modes ends the channel before the connect request when
  the peer lacks it, and a Wii Remote has basic mode only. The app sees "Connection failed for unknown reason" about
  100 ms after the link comes up.
- Paired, the remote goes to Android's HID host, then through uhid to the kernel's hid-wiimote driver (built into the
  GKI kernels). Its probe allocates `struct wiimote_data` with kzalloc: 32 output buffers of HID_MAX_BUFFER_SIZE,
  16384 bytes since that limit was raised, so one physically contiguous 1 MB block (page order 8). On a phone that has
  run for a while there is none; the kernel log shows "page allocation failure: order:8" and "wiimote ...: Can't alloc
  device", and no input device appears.
- Even when the driver binds, Android's EventHub keeps only devices with keys or gamepad buttons: the remote's buttons
  and the Nunchuk reach apps, the Accelerometer, IR and Motion Plus devices (axes only) never do.

## Motion pointer (Wii MotionPlus)

### The MotionPlus on the link (wiimote_hid.cpp)

1. After the remote's calibration and the Nunchuk's setup (which needs the Nunchuk's own registers, before the
   MotionPlus hides them), read 6 bytes at 0xA600FA. `xx xx A6 20 00 05` is an inactive MotionPlus.
2. In the motion mode only: write 0x55 to 0xA600F0 (initialise), then 0x05 (Nunchuk passthrough) or 0x04 (none) to
   0xA600FE. The remote answers with a status report (an extension arrived), which is the MotionPlus: it is not set up
   again.
3. Read the 32-byte calibration at 0xA40020 (fast block, then slow block: big-endian yaw, roll and pitch zero, then
   scale, then degrees / 6). A block whose scale is not 0x1000..0x8000 counts above its zero, or whose degrees are 0,
   uses the typical values (zero 0x7c00, scale 0x4400 above zero for 1200 or 270 deg/s).
4. Report mode 0x35 (buttons, accelerometer, 16 extension bytes), the IR camera off.

A data report's 6 extension bytes are then either MotionPlus data (byte 5 bit 1 set) or the Nunchuk's, reshaped by the
passthrough (bits moved as WiiBrew's Motion Plus page lists; undone before the Nunchuk reader). MotionPlus data: three
14-bit rates (yaw, roll, pitch), each with a slow flag; byte 4 bit 0 says a Nunchuk is in the passthrough port. A
Nunchuk plugged in later: its calibration is read from the MotionPlus's copy at 0xA40040.

The rate of an axis, in degrees per second: (raw x 4 - zero) / (scale - zero) x degrees, with the slow or fast block as
its flag says; signs (-1 pitch, +1 roll, -1 yaw) put it in the accelerometer's frame by the right-hand rule.

### From rates to the pointer (wiimote_motion_pointer.cpp)

One gyro pointer for every device: the remote's accelerometer and MotionPlus rates go into the same SwitchMotion as the
Pro Controller's gyro, in its frame (x right, y up, z toward the player, for a remote held level pointing at the
screen):

| SwitchMotion axis | Remote axis (accelerometer and rates alike) |
|---|---|
| x (right) | - x (x is out of the remote's left side) |
| y (up) | z (out of the A face) |
| z (toward the player) | y (toward the expansion port) |

The remote's frame is right-handed and the MotionPlus rates, after their signs, turn by the right-hand rule in it:
positive pitch (about x, the left side) tips the nose down, positive roll (about y) tips the top to the left, positive
yaw (about z) turns the nose left, as Dolphin's real-remote inputs name them ("Pitch Down", "Roll Left", "Yaw Left").

SwitchMotion keeps gravity by the gyro and the accelerometer (0.5 s), learns the gyro's bias from every half second at
rest, and turns yaw about the world's vertical and pitch about the horizontal right axis into pointer movement (40
degrees of turn per picture width, the picture's aspect for height), so the pointer follows the hand held level or
upright. Rates under 1.5 deg/s shrink toward zero, so a resting remote holds still.

The pointer then works as the gyro pointer of the game's Switch release, through pointer_control.h:

- **2 recenters and shows it** (RecenterHold): pressing 2 puts the pointer in the middle of the picture; while 2 is held
  it stays there; on release it follows the remote from where the remote now points. 2 never reaches the game in this
  mode (the game reads 2 only among "any button", isHeldAnyWithoutHome and isPressedAnyWithoutHome, which A also
  answers).
- It **hides after 5 seconds** within 2.4 degrees of one place (PointerRestHide), and stays hidden until 2.
- It **leaves the picture** when the remote turns past the screen (the game hides it, as a remote pointed past the
  screen), and comes back where the remote points when it turns back, up to a picture's width past each edge
  (kGyroPointerLimit).
- The remote's roll as the game sees it is its gravity's (the sensor bar's roll without the bar); its distance the
  virtual pointer's.
- A remote held upright (the Star Ball) has no pointer, as with the bar (remote_faces_screen).
- The first 2 after connecting is what starts the pointer: until then it is hidden, so the remote's start-up pose never
  sets the middle.

A remote without MotionPlus in this mode points at the sensor bar instead (IR camera on, report 0x37 or 0x33), and the
CONTROLS page says so on its Wii Remote line, which reads (NebulaText, every language): no Wii Remote connected; Wii
Remote (and Nunchuk) connected; in the motion mode, press 2 to center the pointer, turning on the MotionPlus, or no
MotionPlus, pointing at the sensor bar. The line is a state, not a setting: it has no arrows.

## Code map

| File | Role |
|---|---|
| runtime/src/wiimote_hid.cpp | the worker: link, set-up, MotionPlus, report mode, IR, motor, battery |
| runtime/src/wiimote_protocol.cpp | output reports, status, reads, MotionPlus data and calibration |
| runtime/src/input/wiimote_input.cpp | raw reports to PlayerInput (buttons, accelerometer, IR pointer, Nunchuk) |
| runtime/src/input/wiimote_motion_pointer.cpp | MotionPlus rates to the pointer (SwitchMotion, RecenterHold, PointerRestHide) |
| runtime/src/input/wiimote_motion.cpp | tilt, swings and spins for the game |
| platform/android/.../WiimoteUsb.java | the DolphinBar over USB host (SET_REPORT out, interrupt in) |
| platform/sdl/wiimote_link_android.cpp | the DolphinBar link for the worker |
