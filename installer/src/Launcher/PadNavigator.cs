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

    /// <summary>
    /// A controller on Nebula's settings window, as on the game's settings menu: Up and Down move from row to row (in a
    /// list, through its items), Left and Right change the focused row's value (a choice, a number, a slider, the page on
    /// the page tabs), the shoulder buttons change the page, A presses a button, ticks a box or opens a choice's list,
    /// and B closes an open list, else returns to the page tabs. The focus frame shows from the first press; a page that
    /// scrolls keeps the focused row in view. Nothing acts while another window is in front or while <c>paused</c> says
    /// so (a binding waiting for a button).
    /// </summary>
    internal sealed class SettingsPadNavigator : IDisposable
    {
        private readonly Form form;
        private readonly TabControl tabs;
        private readonly Func<bool> paused;
        private readonly Action<Control> activate;
        private readonly Timer timer = new Timer { Interval = 16 };
        private readonly Stopwatch clock = Stopwatch.StartNew();
        private readonly PadMenuInput input = new PadMenuInput(true);
        private bool cuesShown;

        public SettingsPadNavigator(Form form, TabControl tabs, Func<bool> paused, Action<Control> activate)
        {
            this.form = form;
            this.tabs = tabs;
            this.paused = paused;
            this.activate = activate;
            timer.Tick += delegate { Poll(); };
            form.Shown += delegate
            {
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

        private void Poll()
        {
            PadState state = PadReader.Read();
            if (Form.ActiveForm != form || (paused != null && paused()))
            {
                input.HoldCurrent(state);
                return;
            }
            PadMenuInput.Action action = input.Step(state, clock.ElapsedMilliseconds);
            if (action == PadMenuInput.Action.None) return;
            if (!cuesShown)
            {
                const int ChangeUiState = 0x0127, Clear = 2, HideFocus = 1;
                SendMessage(form.Handle, ChangeUiState, (IntPtr)(Clear | (HideFocus << 16)), IntPtr.Zero);
                cuesShown = true;
            }
            Act(action);
        }

        /// <summary>One pad action on the focused control (the tests drive this directly).</summary>
        internal void Act(PadMenuInput.Action action)
        {
            Control focused = Focused();
            var combo = focused as ComboBox;
            switch (action)
            {
                case PadMenuInput.Action.Previous:
                case PadMenuInput.Action.Next:
                {
                    int step = action == PadMenuInput.Action.Next ? 1 : -1;
                    if (combo != null && combo.DroppedDown) { Change(combo, step); return; }
                    var list = focused as ListView;
                    if (list != null && list.Items.Count > 0)
                    {
                        int index = list.SelectedIndices.Count > 0 ? list.SelectedIndices[0] : (step > 0 ? -1 : list.Items.Count);
                        int next = index + step;
                        if (next >= 0 && next < list.Items.Count)
                        {
                            list.SelectedIndices.Clear();
                            list.Items[next].Selected = true;
                            list.Items[next].Focused = true;
                            list.EnsureVisible(next);
                            return;
                        }
                    }
                    Move(focused, step > 0);
                    return;
                }
                case PadMenuInput.Action.Decrease:
                case PadMenuInput.Action.Increase:
                {
                    int step = action == PadMenuInput.Action.Increase ? 1 : -1;
                    if (combo != null) { Change(combo, step); return; }
                    var number = focused as NumericUpDown;
                    if (number != null)
                    {
                        number.Value = Math.Max(number.Minimum, Math.Min(number.Value + step * number.Increment, number.Maximum));
                        return;
                    }
                    var slider = focused as TrackBar;
                    if (slider != null)
                    {
                        slider.Value = Math.Max(slider.Minimum, Math.Min(slider.Value + step * slider.SmallChange, slider.Maximum));
                        return;
                    }
                    var pages = focused as TabControl;
                    if (pages != null) { Page(pages, step); return; }
                    // A row with nothing to change (a button, a box): along the row, as in a dialog.
                    Move(focused, step > 0);
                    return;
                }
                case PadMenuInput.Action.PagePrevious:
                case PadMenuInput.Action.PageNext:
                    if (combo != null) combo.DroppedDown = false;
                    Page(tabs, action == PadMenuInput.Action.PageNext ? 1 : -1);
                    tabs.Focus();
                    return;
                case PadMenuInput.Action.Confirm:
                {
                    if (combo != null) { combo.DroppedDown = !combo.DroppedDown; return; }
                    var check = focused as CheckBox;
                    if (check != null) { check.Checked = !check.Checked; return; }
                    var button = focused as IButtonControl;
                    if (button != null) { button.PerformClick(); return; }
                    if (focused != null && activate != null) activate(focused);
                    return;
                }
                case PadMenuInput.Action.Back:
                    if (combo != null && combo.DroppedDown) { combo.DroppedDown = false; return; }
                    tabs.Focus();
                    return;
            }
        }

        // The innermost focused control: Form.ActiveControl stops at a container control. A NumericUpDown is one and is
        // the row itself; a user control holding rows is not.
        private Control Focused()
        {
            Control active = form.ActiveControl;
            while (active is ContainerControl && !(active is UpDownBase) && ((ContainerControl)active).ActiveControl != null)
                active = ((ContainerControl)active).ActiveControl;
            return active;
        }

        private static void Change(ComboBox combo, int step)
        {
            if (combo.Items.Count == 0) return;
            int index = Math.Max(0, Math.Min(combo.SelectedIndex + step, combo.Items.Count - 1));
            if (index != combo.SelectedIndex) combo.SelectedIndex = index;
        }

        private static void Page(TabControl pages, int step)
        {
            if (pages.TabCount == 0) return;
            int index = Math.Max(0, Math.Min(pages.SelectedIndex + step, pages.TabCount - 1));
            if (index != pages.SelectedIndex) pages.SelectedIndex = index;
        }

        // The next control that takes the focus (hidden pages' and disabled rows are skipped, the end wraps), then every
        // scrolling page around it scrolled to show it.
        private void Move(Control from, bool forward)
        {
            if (!form.SelectNextControl(from ?? tabs, forward, true, true, true)) return;
            Control now = Focused();
            for (Control c = now == null ? null : now.Parent; c != null && c != form; c = c.Parent)
            {
                var scroll = c as ScrollableControl;
                if (scroll != null && scroll.AutoScroll) scroll.ScrollControlIntoView(now);
            }
        }
    }

    /// <summary>The pad as a menu reads it: one direction, the confirm and back buttons, and the shoulder buttons.</summary>
    internal struct PadState
    {
        public bool Up, Down, Left, Right, Confirm, Back, PagePrevious, PageNext;
    }

    /// <summary>
    /// The game's menu timing (NebulaMenuFocus): a direction acts when pressed, then repeats after 300 ms held, every
    /// 83 ms (18 and 5 frames at 60 Hz). In a dialog Up and Left go to the previous control, Down and Right to the next;
    /// on a settings page (rows) Up and Down move between rows and Left and Right change the row's value, as the game's
    /// settings menu does. Confirm, Back and the shoulder buttons act when pressed.
    /// </summary>
    internal sealed class PadMenuInput
    {
        internal enum Action { None, Previous, Next, Confirm, Back, Decrease, Increase, PagePrevious, PageNext }

        private readonly bool rows;
        private bool pagePreviousHeld;
        private bool pageNextHeld;

        public PadMenuInput() : this(false) { }

        public PadMenuInput(bool rows)
        {
            this.rows = rows;
        }

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
            pagePreviousHeld = state.PagePrevious;
            pageNextHeld = state.PageNext;
        }

        public Action Step(PadState state, long nowMs)
        {
            bool confirm = state.Confirm && !confirmHeld;
            bool back = state.Back && !backHeld;
            bool pagePrevious = state.PagePrevious && !pagePreviousHeld;
            bool pageNext = state.PageNext && !pageNextHeld;
            confirmHeld = state.Confirm;
            backHeld = state.Back;
            pagePreviousHeld = state.PagePrevious;
            pageNextHeld = state.PageNext;
            if (back) return Action.Back;
            if (confirm) return Action.Confirm;
            if (pagePrevious) return Action.PagePrevious;
            if (pageNext) return Action.PageNext;

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

        private Action DirectionOf(PadState state)
        {
            if (rows)
            {
                if (state.Up) return Action.Previous;
                if (state.Down) return Action.Next;
                if (state.Left) return Action.Decrease;
                if (state.Right) return Action.Increase;
                return Action.None;
            }
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
                    (pad.Buttons & 0x0008) != 0, x, y, (pad.Buttons & 0x1000) != 0, (pad.Buttons & 0x2000) != 0,
                    (pad.Buttons & 0x0100) != 0, (pad.Buttons & 0x0200) != 0);
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
                // The shoulder buttons are buttons 5 and 6 on all three (L1 and R1, L and R, LB and RB).
                Merge(ref state, up, down, left, right, x, y, (info.Buttons & (1 << confirmBit)) != 0,
                    (info.Buttons & (1 << backBit)) != 0, (info.Buttons & (1 << 4)) != 0, (info.Buttons & (1 << 5)) != 0);
            }
            return state;
        }

        private static float Normalize(int value, int min, int max)
        {
            if (max <= min) return 0f;
            return (value - min) * 2f / (max - min) - 1f;
        }

        private static void Merge(ref PadState state, bool up, bool down, bool left, bool right, float x, float y,
            bool confirm, bool back, bool pagePrevious, bool pageNext)
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
            state.PagePrevious |= pagePrevious;
            state.PageNext |= pageNext;
        }
    }
}
