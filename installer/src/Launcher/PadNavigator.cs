using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Windows.Forms;

namespace Nebula.Launcher
{
    /// <summary>
    /// A controller in Nebula's dialogs, as in the game's menus: the D-pad or left stick moves the focus from control to
    /// control (a press, then repeats while held, at the game's pace), A (Cross on a PlayStation pad) presses the
    /// focused one, B (Circle) is the dialog's cancel. Reads XInput pads (Xbox, and pads Steam presents as one), else
    /// any other game controller Windows lists. The dialog shows its focus frame from the start (as after a Tab).
    /// </summary>
    internal sealed class PadNavigator : IDisposable
    {
        private readonly Form form;
        private readonly Timer timer = new Timer { Interval = 16 };
        private readonly Stopwatch clock = Stopwatch.StartNew();
        private readonly PadMenuInput input = new PadMenuInput();

        public PadNavigator(Form form)
        {
            this.form = form;
            timer.Tick += delegate { Poll(); };
            form.Shown += delegate
            {
                ShowFocusCues();
                input.HoldCurrent(PadReader.Read());
                timer.Start();
            };
            form.FormClosed += delegate { Dispose(); };
        }

        public void Dispose()
        {
            timer.Dispose();
        }

        [DllImport("user32.dll")]
        private static extern IntPtr SendMessage(IntPtr window, int message, IntPtr wParam, IntPtr lParam);

        // The whole dialog's focus cues on (WM_CHANGEUISTATE, UIS_CLEAR UISF_HIDEFOCUS): its buttons draw the focus
        // frame a pad moves, which Windows hides until the keyboard is used.
        private void ShowFocusCues()
        {
            const int ChangeUiState = 0x0127, Clear = 2, HideFocus = 1;
            SendMessage(form.Handle, ChangeUiState, (IntPtr)(Clear | (HideFocus << 16)), IntPtr.Zero);
        }

        private void Poll()
        {
            PadState state = PadReader.Read();
            // Only the dialog in front: a message box over it takes the pad, and what is held meanwhile is no press.
            if (Form.ActiveForm != form)
            {
                input.HoldCurrent(state);
                return;
            }
            switch (input.Step(state, clock.ElapsedMilliseconds))
            {
                case PadMenuInput.Action.Previous:
                    form.SelectNextControl(form.ActiveControl, false, true, true, true);
                    break;
                case PadMenuInput.Action.Next:
                    form.SelectNextControl(form.ActiveControl, true, true, true, true);
                    break;
                case PadMenuInput.Action.Confirm:
                    var check = form.ActiveControl as CheckBox;
                    if (check != null) check.Checked = !check.Checked;
                    var button = form.ActiveControl as IButtonControl;
                    if (button != null) button.PerformClick();
                    break;
                case PadMenuInput.Action.Back:
                    if (form.CancelButton != null) form.CancelButton.PerformClick();
                    break;
            }
        }
    }

    /// <summary>The pad as a menu reads it: one direction, and the confirm and back buttons.</summary>
    internal struct PadState
    {
        public bool Up, Down, Left, Right, Confirm, Back;
    }

    /// <summary>
    /// The game's menu timing (NebulaMenuFocus): a direction acts when pressed, then repeats after 300 ms held, every
    /// 83 ms (18 and 5 frames at 60 Hz). Up and Left go to the previous control, Down and Right to the next. Confirm and
    /// Back act when pressed.
    /// </summary>
    internal sealed class PadMenuInput
    {
        internal enum Action { None, Previous, Next, Confirm, Back }

        internal const long RepeatDelayMs = 300;
        internal const long RepeatIntervalMs = 83;

        private Action held = Action.None;
        private long heldSince;
        private long lastRepeat;
        private bool confirmHeld;
        private bool backHeld;

        /// <summary>What is held as the dialog opens is not a press (A from the game's own menu, a held stick).</summary>
        public void HoldCurrent(PadState state)
        {
            held = DirectionOf(state);
            heldSince = long.MinValue / 2;
            lastRepeat = long.MaxValue / 2;
            confirmHeld = state.Confirm;
            backHeld = state.Back;
        }

        public Action Step(PadState state, long nowMs)
        {
            bool confirm = state.Confirm && !confirmHeld;
            bool back = state.Back && !backHeld;
            confirmHeld = state.Confirm;
            backHeld = state.Back;
            if (back) return Action.Back;
            if (confirm) return Action.Confirm;

            Action direction = DirectionOf(state);
            if (direction != held)
            {
                held = direction;
                heldSince = nowMs;
                lastRepeat = nowMs;
                return direction;
            }
            if (direction == Action.None || nowMs - heldSince < RepeatDelayMs) return Action.None;
            if (nowMs - lastRepeat < (lastRepeat == heldSince ? RepeatDelayMs : RepeatIntervalMs)) return Action.None;
            lastRepeat = nowMs;
            return direction;
        }

        private static Action DirectionOf(PadState state)
        {
            if (state.Up || state.Left) return Action.Previous;
            if (state.Down || state.Right) return Action.Next;
            return Action.None;
        }
    }

    /// <summary>
    /// Every controller Windows offers, read as one pad. Reading a slot with nothing in it is slow (XInput and winmm
    /// both take a millisecond or more), so the empty ones are tried again only every two seconds.
    /// </summary>
    internal static class PadReader
    {
        private const float StickHold = 0.6f;  // the game's menus: the stick's deflection that counts as a press
        private const int RescanMs = 2000;

        private static int lastScan = int.MinValue;
        private static readonly bool[] XInputConnected = new bool[4];
        private static readonly List<int> Joysticks = new List<int>();

        public static PadState Read()
        {
            int now = Environment.TickCount;
            bool rescan = unchecked(now - lastScan) >= RescanMs || lastScan == int.MinValue;
            if (rescan) lastScan = now;
            PadState state;
            if (ReadXInput(rescan, out state)) return state;
            return ReadJoysticks(rescan);
        }

        // XInput: every pad that is connected, merged.
        [StructLayout(LayoutKind.Sequential)]
        private struct XInputState
        {
            public uint Packet;
            public ushort Buttons;
            public byte LeftTrigger, RightTrigger;
            public short ThumbLX, ThumbLY, ThumbRX, ThumbRY;
        }

        [DllImport("xinput1_4.dll", EntryPoint = "XInputGetState")]
        private static extern int XInputGetState(int index, out XInputState state);

        private static bool ReadXInput(bool rescan, out PadState state)
        {
            state = new PadState();
            bool any = false;
            for (int i = 0; i < 4; i++)
            {
                if (!rescan && !XInputConnected[i]) continue;
                XInputState pad;
                try { XInputConnected[i] = XInputGetState(i, out pad) == 0; }
                catch (DllNotFoundException) { return false; }
                if (!XInputConnected[i]) continue;
                any = true;
                float x = pad.ThumbLX / 32767f, y = pad.ThumbLY / 32767f;
                Merge(ref state, (pad.Buttons & 0x0001) != 0, (pad.Buttons & 0x0002) != 0, (pad.Buttons & 0x0004) != 0,
                    (pad.Buttons & 0x0008) != 0, x, y, (pad.Buttons & 0x1000) != 0, (pad.Buttons & 0x2000) != 0);
            }
            return any;
        }

        // Other game controllers (winmm): DirectInput pads, PlayStation and Nintendo ones among them.
        [StructLayout(LayoutKind.Sequential)]
        private struct JoyInfoEx
        {
            public int Size, Flags, X, Y, Z, R, U, V, Buttons, ButtonNumber, Pov, Reserved1, Reserved2;
        }

        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        private struct JoyCaps
        {
            public ushort Mid, Pid;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string Name;
            public int XMin, XMax, YMin, YMax, ZMin, ZMax, NumButtons, PeriodMin, PeriodMax, RMin, RMax, UMin, UMax,
                VMin, VMax, Caps, MaxAxes, NumAxes, MaxButtons;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 32)] public string RegKey;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 260)] public string OemVxd;
        }

        [DllImport("winmm.dll")] private static extern int joyGetNumDevs();
        [DllImport("winmm.dll")] private static extern int joyGetPosEx(int id, ref JoyInfoEx info);
        [DllImport("winmm.dll", CharSet = CharSet.Unicode, EntryPoint = "joyGetDevCapsW")]
        private static extern int joyGetDevCaps(int id, ref JoyCaps caps, int size);

        private const int JoyReturnAll = 0xFF;
        private const int PovCentered = 0xFFFF;
        private const ushort SonyVendor = 0x054C;
        private const ushort NintendoVendor = 0x057E;
        private static readonly Dictionary<int, JoyCaps> Caps = new Dictionary<int, JoyCaps>();

        private static PadState ReadJoysticks(bool rescan)
        {
            var state = new PadState();
            if (rescan)
            {
                // Which ids hold a controller now, and what each is (another pad may have taken an id).
                Joysticks.Clear();
                Caps.Clear();
                int count;
                try { count = Math.Min(joyGetNumDevs(), 16); }
                catch (DllNotFoundException) { return state; }
                for (int id = 0; id < count; id++)
                {
                    var probe = new JoyInfoEx { Size = Marshal.SizeOf(typeof(JoyInfoEx)), Flags = JoyReturnAll };
                    var found = new JoyCaps();
                    if (joyGetPosEx(id, ref probe) != 0 || joyGetDevCaps(id, ref found, Marshal.SizeOf(typeof(JoyCaps))) != 0) continue;
                    Joysticks.Add(id);
                    Caps[id] = found;
                }
            }
            foreach (int id in Joysticks)
            {
                var info = new JoyInfoEx { Size = Marshal.SizeOf(typeof(JoyInfoEx)), Flags = JoyReturnAll };
                if (joyGetPosEx(id, ref info) != 0) continue;
                JoyCaps caps = Caps[id];
                float x = Normalize(info.X, caps.XMin, caps.XMax), y = -Normalize(info.Y, caps.YMin, caps.YMax);
                bool pov = info.Pov != PovCentered;
                int angle = info.Pov;
                bool up = pov && (angle >= 31500 || angle <= 4500), right = pov && angle >= 4500 && angle <= 13500,
                    down = pov && angle >= 13500 && angle <= 22500, left = pov && angle >= 22500 && angle <= 31500;
                // Confirm and back as each maker's pads label them: Cross and Circle (Sony: buttons 2 and 3), A and B
                // (Nintendo: buttons 2 and 1); others in the usual order, A then B.
                int confirmBit = caps.Mid == SonyVendor || caps.Mid == NintendoVendor ? 1 : 0;
                int backBit = caps.Mid == SonyVendor ? 2 : caps.Mid == NintendoVendor ? 0 : 1;
                Merge(ref state, up, down, left, right, x, y, (info.Buttons & (1 << confirmBit)) != 0,
                    (info.Buttons & (1 << backBit)) != 0);
            }
            return state;
        }

        private static float Normalize(int value, int min, int max)
        {
            if (max <= min) return 0f;
            return (value - min) * 2f / (max - min) - 1f;
        }

        private static void Merge(ref PadState state, bool up, bool down, bool left, bool right, float x, float y,
            bool confirm, bool back)
        {
            if (Math.Abs(x) >= StickHold || Math.Abs(y) >= StickHold)
            {
                if (Math.Abs(y) >= Math.Abs(x)) { up |= y > 0f; down |= y < 0f; }
                else { right |= x > 0f; left |= x < 0f; }
            }
            state.Up |= up;
            state.Down |= down;
            state.Left |= left;
            state.Right |= right;
            state.Confirm |= confirm;
            state.Back |= back;
        }
    }
}
