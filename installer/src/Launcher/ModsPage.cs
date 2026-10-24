using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Windows.Forms;

namespace Nebula.Launcher
{
    /// <summary>
    /// The Mods tab: the game's mods, texture packs and HD movies (galaxy::mods) in two lists, disabled on the left and
    /// enabled on the right, where the higher one wins where two change the same file or texture. The game reads,
    /// imports and removes them (NebulaNative --list-mods, --import-mod, --remove-mod), so the launcher and the in-game
    /// menu share one implementation; the order and on/off state go to mods.ini as the game writes it.
    /// </summary>
    internal sealed class ModsPage
    {
        private sealed class Mod
        {
            public string Id;
            public string Kinds = "";
            public string Notes = "";
            public bool Unusable;
            public List<ModOption> Options = new List<ModOption>();
        }

        // A Riivolution option: its choice is 0 (off) or 1.. (Choices).
        private sealed class ModOption
        {
            public string Key, Section, Name;
            public List<string> Choices = new List<string>();
            public List<bool> OtherRegion = new List<bool>();
            public int Selected;
        }

        // What a drag between or within the lists carries.
        private sealed class DragSet
        {
            public ListView From;
            public List<Mod> Mods;
        }

        private const int ListTop = 72, ListHeight = 320, SideWidth = 300;
        private readonly string native, pak;
        private readonly List<Mod> on = new List<Mod>(), off = new List<Mod>();
        private readonly ListView onList = new ListView(), offList = new ListView();
        private readonly TextBox onSearch = new TextBox(), offSearch = new TextBox();
        private readonly Panel dropLine = new Panel();
        private readonly Label details = new Label(), status = new Label();
        private readonly List<Control> actions = new List<Control>();
        private readonly ToolTip tips = new ToolTip();
        private readonly TabPage page;
        private Button optionsButton;
        // The window thread's context: worker results come back through it (it exists before the window does).
        private readonly SynchronizationContext window;
        private ListView lastList;
        private string modsFolder;
        private bool busy, reading, readAgain;
        // Where a drag into the enabled list puts its mods: an index into `on`, -1 for none.
        private int dropAt = -1;

        [DllImport("user32.dll", CharSet = CharSet.Unicode)]
        private static extern IntPtr SendMessage(IntPtr control, int message, IntPtr wParam, string lParam);

        /// <summary>The game (NebulaNative.exe) is beside the launcher: the tab shows only then.</summary>
        public static bool Available(string app)
        {
            return File.Exists(Path.Combine(app, "NebulaNative.exe"));
        }

        public ModsPage(TabPage page, string app, string content)
        {
            this.page = page;
            window = SynchronizationContext.Current ?? new WindowsFormsSynchronizationContext();
            native = Path.Combine(app, "NebulaNative.exe");
            pak = content != null ? Path.Combine(content, "game.pak") : null;
            lastList = offList;

            Column("Disabled", offSearch, offList, 12, false);
            Column("Enabled (top has priority)", onSearch, onList, 368, true);
            dropLine.Height = 2;
            dropLine.BackColor = SystemColors.Highlight;
            dropLine.Visible = false;
            onList.Controls.Add(dropLine);

            int middle = ListTop + ListHeight / 2;
            Action(SmallButton("→", 320, middle - 40, "Enable (Enter)")).Click += delegate { Safe(() => Enable(Selected(offList), 0)); };
            Action(SmallButton("←", 320, middle + 4, "Disable (Enter)")).Click += delegate { Safe(() => Disable(Selected(onList))); };
            Action(SmallButton("↑", 676, middle - 40, "Move up (Alt+Up)")).Click += delegate { Safe(() => Move(-1)); };
            Action(SmallButton("↓", 676, middle + 4, "Move down (Alt+Down)")).Click += delegate { Safe(() => Move(1)); };

            details.Location = new Point(12, ListTop + ListHeight + 8);
            details.Size = new Size(704, 44);
            page.Controls.Add(details);

            int x = 12, y = ListTop + ListHeight + 58;
            Action(WideButton("Import file…", ref x, y, 130)).Click += delegate { Safe(ImportFiles); };
            Action(WideButton("Import folder…", ref x, y, 130)).Click += delegate { Safe(ImportFolder); };
            optionsButton = Action(WideButton("Options…", ref x, y, 100));
            optionsButton.Click += delegate { Safe(EditOptions); };
            optionsButton.Enabled = false;
            Action(WideButton("Remove", ref x, y, 100)).Click += delegate { Safe(Remove); };
            WideButton("Open mods folder", ref x, y, 150).Click += delegate { Safe(OpenFolder); };

            status.Location = new Point(12, y + 40);
            status.Size = new Size(704, 26);
            status.AutoEllipsis = true;
            status.ForeColor = SystemColors.GrayText;
            page.Controls.Add(status);

            // Mods dropped anywhere on the tab are imported.
            page.AllowDrop = true;
            page.DragEnter += delegate(object sender, DragEventArgs e) { Safe(() => OnDragOver(null, e)); };
            page.DragOver += delegate(object sender, DragEventArgs e) { Safe(() => OnDragOver(null, e)); };
            page.DragDrop += delegate(object sender, DragEventArgs e) { Safe(() => OnDragDrop(null, e)); };
            status.Text = "Reading mods…";
            Read();
        }

        // A list with its title and search box.
        private void Column(string title, TextBox search, ListView view, int x, bool ordered)
        {
            page.Controls.Add(new Label
            {
                Text = title, Location = new Point(x, 12), Size = new Size(SideWidth, 24),
                Font = new Font("Segoe UI", 10f, FontStyle.Bold)
            });
            search.Location = new Point(x, 40);
            search.Width = SideWidth;
            search.HandleCreated += delegate { SendMessage(search.Handle, 0x1501 /* EM_SETCUEBANNER */, (IntPtr)1, "Search"); };
            search.TextChanged += delegate { Safe(Show); };
            // Enter or Down goes to the list (Enter would otherwise press Save and play).
            search.PreviewKeyDown += delegate(object sender, PreviewKeyDownEventArgs e) { if (e.KeyCode == Keys.Enter) e.IsInputKey = true; };
            search.KeyDown += delegate(object sender, KeyEventArgs e)
            {
                if (e.KeyCode != Keys.Enter && e.KeyCode != Keys.Down) return;
                e.Handled = e.SuppressKeyPress = true;
                view.Focus();
                if (view.SelectedItems.Count == 0 && view.Items.Count != 0)
                {
                    view.Items[0].Selected = true;
                    view.Items[0].Focused = true;
                }
            };
            page.Controls.Add(search);

            view.Location = new Point(x, ListTop);
            view.Size = new Size(SideWidth, ListHeight);
            view.View = View.Details;
            view.FullRowSelect = true;
            view.HideSelection = false;
            view.MultiSelect = true;
            view.AllowDrop = true;
            view.ShowItemToolTips = true;
            view.HeaderStyle = ColumnHeaderStyle.Nonclickable;
            if (ordered) view.Columns.Add("#", 36, HorizontalAlignment.Right);
            view.Columns.Add("Name", 160);
            view.Columns.Add("Type", 100);
            view.ClientSizeChanged += delegate { FitColumns(view); };
            view.Enter += delegate
            {
                // One list's selection at a time: the buttons and keys act on it.
                lastList = view;
                ListView other = view == onList ? offList : onList;
                foreach (ListViewItem item in other.SelectedItems.Cast<ListViewItem>().ToList()) item.Selected = false;
                ShowDetails(view);
            };
            view.SelectedIndexChanged += delegate { if (view == lastList) ShowDetails(view); };
            view.MouseDoubleClick += delegate(object sender, MouseEventArgs e)
            {
                if (view.GetItemAt(e.X, e.Y) != null) Safe(() => Toggle(view));
            };
            // Enter and Alt+arrows are the list's (not Save and play, not the menu).
            view.PreviewKeyDown += delegate(object sender, PreviewKeyDownEventArgs e)
            {
                if (e.KeyCode == Keys.Enter || (e.Alt && (e.KeyCode == Keys.Up || e.KeyCode == Keys.Down))) e.IsInputKey = true;
            };
            view.KeyDown += delegate(object sender, KeyEventArgs e) { Safe(() => OnKey(view, e)); };
            view.ItemDrag += delegate
            {
                if (busy) return;
                var mods = Selected(view);
                if (mods.Count != 0) Safe(() => view.DoDragDrop(new DragSet { From = view, Mods = mods }, DragDropEffects.Move));
            };
            view.DragEnter += delegate(object sender, DragEventArgs e) { Safe(() => OnDragOver(view, e)); };
            view.DragOver += delegate(object sender, DragEventArgs e) { Safe(() => OnDragOver(view, e)); };
            view.DragLeave += delegate { dropLine.Visible = false; };
            view.DragDrop += delegate(object sender, DragEventArgs e) { Safe(() => OnDragDrop(view, e)); };
            page.Controls.Add(view);
        }

        // Name takes the width the number and type columns leave (the list grows with the display scale).
        private static void FitColumns(ListView view)
        {
            float scale;
            using (var g = view.CreateGraphics()) scale = g.DpiX / 96f;
            int fixedWidth = 0;
            foreach (ColumnHeader column in view.Columns)
            {
                if (column.Text == "Name") continue;
                column.Width = (int)((column.Text == "#" ? 30 : 90) * scale);
                fixedWidth += column.Width;
            }
            foreach (ColumnHeader column in view.Columns)
                if (column.Text == "Name") column.Width = Math.Max(60, view.ClientSize.Width - fixedWidth - 1);
        }

        private Button SmallButton(string text, int x, int y, string tip)
        {
            var button = new Button { Text = text, Location = new Point(x, y), Size = new Size(40, 36) };
            tips.SetToolTip(button, tip);
            page.Controls.Add(button);
            return button;
        }

        private Button WideButton(string text, ref int x, int y, int width)
        {
            var button = new Button { Text = text, Location = new Point(x, y), Size = new Size(width, 32) };
            page.Controls.Add(button);
            x += width + 8;
            return button;
        }

        // A control that changes the mods: off while an import or removal runs.
        private T Action<T>(T control) where T : Control
        {
            actions.Add(control);
            return control;
        }

        // Every action of the tab: a failure says why in the status line instead of stopping the launcher.
        private void Safe(Action action)
        {
            if (page.IsDisposed) return;
            try { action(); }
            catch (Exception error)
            {
                if (!status.IsDisposed) status.Text = "That did not work: " + error.Message;
            }
        }

        private void SetBusy(bool value)
        {
            busy = value;
            foreach (var control in actions) control.Enabled = !value;
            ShowDetails(lastList);
        }

        // The game as a tool: its output's one JSON line, or null with `error` set. Any thread.
        private Dictionary<string, object> Tool(IEnumerable<string> arguments, out string error)
        {
            error = null;
            var start = new ProcessStartInfo(native, string.Join(" ", arguments.Select(ProcessRunner.Quote)))
            {
                UseShellExecute = false,
                CreateNoWindow = true,
                RedirectStandardOutput = true,
                RedirectStandardError = true,
                StandardOutputEncoding = Encoding.UTF8,
                WorkingDirectory = Path.GetDirectoryName(native)
            };
            // The game's own settings location, as when it is played (no test or diagnostic variables).
            foreach (var name in start.EnvironmentVariables.Keys.Cast<string>()
                         .Where(n => n.StartsWith("GALAXY_", StringComparison.OrdinalIgnoreCase)).ToList())
                start.EnvironmentVariables.Remove(name);
            try
            {
                using (var process = Process.Start(start))
                {
                    process.ErrorDataReceived += delegate { };
                    process.BeginErrorReadLine();
                    string output = process.StandardOutput.ReadToEnd();
                    process.WaitForExit();
                    string line = output.Split('\n').Select(l => l.Trim()).LastOrDefault(l => l.StartsWith("{"));
                    if (line == null) error = "Nebula could not read the mods (exit code " + process.ExitCode + ").";
                    return line == null ? null : Json.Parse(line);
                }
            }
            catch (Exception failure)
            {
                error = "Nebula could not read the mods: " + failure.Message;
                return null;
            }
        }

        // The mods as the game sees them, read on a worker thread (each read takes a moment); `then` runs after, on
        // the window's thread. A read asked for while one runs follows it.
        private void Read(Action then = null)
        {
            if (reading)
            {
                readAgain = true;
                if (then != null) afterRead.Add(then);
                return;
            }
            reading = true;
            if (then != null) afterRead.Add(then);
            var arguments = new List<string> { "--list-mods" };
            if (pak != null && File.Exists(pak))
            {
                arguments.Add("--pak");
                arguments.Add(pak);
            }
            new Thread(() =>
            {
                string error;
                var result = Tool(arguments, out error);
                Post(() =>
                {
                    reading = false;
                    if (readAgain)
                    {
                        // A change came after this read began: the next one has it.
                        readAgain = false;
                        Read();
                        return;
                    }
                    if (result == null) status.Text = error;
                    else Merge(result);
                    var then2 = afterRead.ToList();
                    afterRead.Clear();
                    foreach (var action in then2) action();
                });
            }) { IsBackground = true }.Start();
        }

        private readonly List<Action> afterRead = new List<Action>();

        // Runs on the window's thread (never after the tab is gone).
        private void Post(Action action)
        {
            window.Post(delegate { Safe(action); }, null);
        }

        // The game's list into the two lists. The same mods as shown: only their types and notes change (the order
        // shown is the one saved). Others: the game's order.
        private void Merge(Dictionary<string, object> result)
        {
            modsFolder = Json.Str(result, "folder");
            object value;
            var entries = (result.TryGetValue("mods", out value) ? value as object[] : null) ?? new object[0];
            var read = new List<KeyValuePair<Mod, bool>>();
            foreach (var entry in entries)
            {
                var mod = entry as Dictionary<string, object>;
                if (mod == null || string.IsNullOrEmpty(Json.Str(mod, "id"))) continue;
                read.Add(new KeyValuePair<Mod, bool>(Describe(mod), Json.Bool(mod, "enabled")));
            }
            var shown = new HashSet<string>(on.Concat(off).Select(m => m.Id));
            if (shown.SetEquals(read.Select(r => r.Key.Id)) && shown.Count == read.Count)
            {
                var byId = read.ToDictionary(r => r.Key.Id, r => r.Key);
                foreach (var mod in on.Concat(off))
                {
                    Mod fresh = byId[mod.Id];
                    mod.Kinds = fresh.Kinds;
                    mod.Notes = fresh.Notes;
                    mod.Unusable = fresh.Unusable;
                    mod.Options = fresh.Options;
                }
                Show();
            }
            else
            {
                var selected = new HashSet<string>(Selected(onList).Concat(Selected(offList)).Select(m => m.Id));
                on.Clear();
                off.Clear();
                foreach (var r in read) (r.Value ? on : off).Add(r.Key);
                SortOff();
                Show();
                Select(onList, on.Where(m => selected.Contains(m.Id)));
                Select(offList, off.Where(m => selected.Contains(m.Id)));
            }
            if (on.Count + off.Count == 0) status.Text = "No mods yet. Import one, or drop it here.";
            else if (status.Text.StartsWith("No mods yet") || status.Text == "Reading mods…") status.Text = "";
        }

        private static Mod Describe(Dictionary<string, object> mod)
        {
            var kinds = new List<string>();
            if (Json.Bool(mod, "riivolution")) kinds.Add("Riivolution");
            if (Json.Bool(mod, "files")) kinds.Add("game files");
            if (Json.Bool(mod, "textures")) kinds.Add("textures");
            if (Json.Bool(mod, "movies")) kinds.Add("movies");
            var notes = new List<string>();
            string problem = Json.Str(mod, "problem");
            if (!string.IsNullOrEmpty(problem)) notes.Add(Capitalize(problem));
            long code = Json.Long(mod, "code_patches");
            if (code != 0) notes.Add(code + (code == 1 ? " code change" : " code changes") + " cannot run in Nebula");
            object overlaps;
            if (mod.TryGetValue("overlaps", out overlaps) && overlaps is object[])
            {
                foreach (var o in (object[])overlaps)
                {
                    var overlap = o as Dictionary<string, object>;
                    if (overlap == null) continue;
                    long files = Json.Long(overlap, "files"), textures = Json.Long(overlap, "textures");
                    string what = files != 0 ? files + (files == 1 ? " file" : " files") : "";
                    if (textures != 0) what += (what.Length != 0 ? " and " : "") + textures + (textures == 1 ? " texture" : " textures");
                    notes.Add(Json.Bool(overlap, "above")
                        ? Capitalize(what) + " replaced by " + Json.Str(overlap, "other")
                        : "Replaces " + what + " of " + Json.Str(overlap, "other"));
                }
            }
            var options = new List<ModOption>();
            object list;
            if (mod.TryGetValue("options", out list) && list is object[])
            {
                foreach (var o in (object[])list)
                {
                    var entry = o as Dictionary<string, object>;
                    if (entry == null || string.IsNullOrEmpty(Json.Str(entry, "key"))) continue;
                    var option = new ModOption
                    {
                        Key = Json.Str(entry, "key"),
                        Section = Json.Str(entry, "section") ?? "",
                        Name = Json.Str(entry, "name") ?? "",
                        Selected = (int)Json.Long(entry, "selected")
                    };
                    object choices;
                    if (entry.TryGetValue("choices", out choices) && choices is object[])
                    {
                        foreach (var c in (object[])choices)
                        {
                            var choice = c as Dictionary<string, object>;
                            if (choice == null) continue;
                            option.Choices.Add(Json.Str(choice, "name") ?? "");
                            option.OtherRegion.Add(Json.Bool(choice, "other_region"));
                        }
                    }
                    option.Selected = Math.Max(0, Math.Min(option.Selected, option.Choices.Count));
                    options.Add(option);
                }
            }
            string kindText = string.Join(", ", kinds);
            return new Mod
            {
                Id = Json.Str(mod, "id"),
                Kinds = kindText.Length != 0 ? char.ToUpperInvariant(kindText[0]) + kindText.Substring(1) : "Nothing usable",
                Notes = string.Join(". ", notes),
                Unusable = kinds.Count == 0,
                Options = options
            };
        }

        private void SortOff()
        {
            off.Sort((a, b) => string.Compare(a.Id, b.Id, StringComparison.OrdinalIgnoreCase));
        }

        private static bool Matches(Mod mod, string search)
        {
            search = search.Trim();
            return search.Length == 0 || mod.Id.IndexOf(search, StringComparison.OrdinalIgnoreCase) >= 0 ||
                   mod.Kinds.IndexOf(search, StringComparison.OrdinalIgnoreCase) >= 0;
        }

        private void Show()
        {
            Show(offList, off, offSearch.Text, false);
            Show(onList, on, onSearch.Text, true);
            ShowDetails(lastList);
        }

        // A list's mods that match its search, its selection kept.
        private static void Show(ListView view, List<Mod> mods, string search, bool numbered)
        {
            var selected = new HashSet<Mod>(Selected(view));
            int top = view.TopItem != null ? view.TopItem.Index : 0;
            view.BeginUpdate();
            view.Items.Clear();
            for (int i = 0; i < mods.Count; i++)
            {
                Mod mod = mods[i];
                if (!Matches(mod, search)) continue;
                var cells = numbered ? new[] { (i + 1).ToString(), mod.Id, mod.Kinds } : new[] { mod.Id, mod.Kinds };
                var item = new ListViewItem(cells) { Tag = mod, ToolTipText = mod.Notes.Length != 0 ? mod.Id + "\n" + mod.Notes : mod.Id };
                if (mod.Unusable) item.ForeColor = SystemColors.GrayText;
                view.Items.Add(item);
                item.Selected = selected.Contains(mod);
            }
            view.EndUpdate();
            if (top > 0 && view.Items.Count != 0) view.TopItem = view.Items[Math.Min(top, view.Items.Count - 1)];
        }

        private static List<Mod> Selected(ListView view)
        {
            return view.SelectedItems.Cast<ListViewItem>().Select(i => i.Tag as Mod).Where(m => m != null).ToList();
        }

        private static void Select(ListView view, IEnumerable<Mod> mods)
        {
            var chosen = new HashSet<Mod>(mods);
            ListViewItem first = null;
            foreach (ListViewItem item in view.Items)
            {
                item.Selected = chosen.Contains(item.Tag as Mod);
                if (item.Selected && first == null) first = item;
            }
            if (first == null) return;
            first.Focused = true;
            first.EnsureVisible();
        }

        private void ShowDetails(ListView view)
        {
            var mods = view != null ? Selected(view) : new List<Mod>();
            if (optionsButton != null) optionsButton.Enabled = !busy && mods.Count == 1 && mods[0].Options.Count != 0;
            if (mods.Count == 1)
                details.Text = mods[0].Id + ": " + mods[0].Kinds.ToLowerInvariant() + (mods[0].Notes.Length != 0 ? ". " + mods[0].Notes + "." : ".");
            else details.Text = mods.Count > 1 ? mods.Count + " selected." : "";
        }

        // Into the enabled list at `at` (0 is the top), in their order.
        private void Enable(List<Mod> mods, int at)
        {
            if (busy || mods.Count == 0) return;
            foreach (var mod in mods)
            {
                int index = on.IndexOf(mod);
                if (index >= 0)
                {
                    if (index < at) at--;
                    on.RemoveAt(index);
                }
                off.Remove(mod);
            }
            on.InsertRange(Math.Max(0, Math.Min(at, on.Count)), mods);
            Changed(onList, mods);
        }

        private void Disable(List<Mod> mods)
        {
            if (busy || mods.Count == 0) return;
            foreach (var mod in mods)
            {
                on.Remove(mod);
                if (!off.Contains(mod)) off.Add(mod);
            }
            SortOff();
            Changed(offList, mods);
        }

        private void Toggle(ListView view)
        {
            if (view == offList) Enable(Selected(offList), 0);
            else Disable(Selected(onList));
        }

        // One place up or down among the mods the search shows.
        private void Move(int delta)
        {
            var mods = Selected(onList);
            if (busy || mods.Count == 0) return;
            var moving = new HashSet<Mod>(mods);
            var order = (delta < 0 ? mods.OrderBy(m => on.IndexOf(m)) : mods.OrderByDescending(m => on.IndexOf(m))).ToList();
            foreach (var mod in order)
            {
                int index = on.IndexOf(mod), next = index + delta;
                while (next >= 0 && next < on.Count && !Matches(on[next], onSearch.Text)) next += delta;
                if (next < 0 || next >= on.Count || moving.Contains(on[next])) continue;
                on.RemoveAt(index);
                on.Insert(next, mod);
            }
            Changed(onList, mods);
        }

        // Shown and saved; the overlaps are read again (they follow the order).
        private void Changed(ListView view, List<Mod> mods)
        {
            status.Text = "";
            lastList = view;
            Show();
            ListView other = view == onList ? offList : onList;
            foreach (ListViewItem item in other.SelectedItems.Cast<ListViewItem>().ToList()) item.Selected = false;
            Select(view, mods);
            view.Focus();
            ShowDetails(view);
            Save();
            Read();
        }

        private void OnKey(ListView view, KeyEventArgs e)
        {
            if (e.KeyCode == Keys.Enter) Toggle(view);
            else if (e.KeyCode == Keys.Delete) Remove();
            else if ((e.KeyCode == Keys.Up || e.KeyCode == Keys.Down) && e.Alt && view == onList) Move(e.KeyCode == Keys.Up ? -1 : 1);
            else if (e.KeyCode == Keys.A && e.Control) foreach (ListViewItem item in view.Items) item.Selected = true;
            else return;
            e.Handled = true;
            e.SuppressKeyPress = true;
        }

        // mods.ini: the enabled mods from the top (order 1 wins), then the disabled ones; each mod's other keys (its
        // choices) kept. Nothing is written when nothing changed.
        private void Save()
        {
            if (modsFolder == null) return;
            string file = Path.Combine(Path.GetDirectoryName(modsFolder), "mods.ini");
            var sections = new Dictionary<string, List<string>>();
            string current = null;
            if (File.Exists(file))
            {
                foreach (var raw in File.ReadAllLines(file, Encoding.UTF8))
                {
                    string line = raw.TrimEnd('\r');
                    if (line.StartsWith("[") && line.EndsWith("]"))
                    {
                        current = Unescape(line.Substring(1, line.Length - 2));
                        if (!sections.ContainsKey(current)) sections[current] = new List<string>();
                    }
                    else if (current != null && line.Length != 0 && !line.StartsWith(";") && !line.StartsWith("enabled=") &&
                             !line.StartsWith("order="))
                    {
                        sections[current].Add(line);
                    }
                }
            }
            var output = new StringBuilder("; Nebula mods: their place in the list (order 1 is the top and wins), on/off and choices.\n");
            int place = 0;
            foreach (var mod in on.Concat(off))
            {
                output.Append('[').Append(Escape(mod.Id)).Append("]\n");
                output.Append("enabled=").Append(on.Contains(mod) ? 1 : 0).Append('\n');
                output.Append("order=").Append(++place).Append('\n');
                List<string> lines;
                if (sections.TryGetValue(mod.Id, out lines))
                    foreach (var line in lines)
                        if (mod.Options.Count == 0 || !line.StartsWith("option.")) output.Append(line).Append('\n');
                foreach (var option in mod.Options)
                    output.Append("option.").Append(Escape(option.Key)).Append('=').Append(option.Selected).Append('\n');
            }
            try
            {
                if (File.Exists(file) && File.ReadAllText(file, Encoding.UTF8).Replace("\r\n", "\n") == output.ToString()) return;
                FileUtil.WriteAllTextAtomic(file, output.ToString());
            }
            catch (Exception error) { status.Text = "mods.ini could not be written: " + error.Message; }
        }

        // galaxy::mods escape_key: %, =, line breaks and brackets as %XX.
        private static string Escape(string text)
        {
            var output = new StringBuilder();
            foreach (char c in text)
            {
                if (c == '%' || c == '=' || c == '\n' || c == '\r' || c == '[' || c == ']') output.Append('%').Append(((int)c).ToString("X2"));
                else output.Append(c);
            }
            return output.ToString();
        }

        private static string Unescape(string text)
        {
            var output = new StringBuilder();
            for (int i = 0; i < text.Length; i++)
            {
                int value;
                if (text[i] == '%' && i + 3 <= text.Length &&
                    int.TryParse(text.Substring(i + 1, 2), System.Globalization.NumberStyles.HexNumber, null, out value))
                {
                    output.Append((char)value);
                    i += 2;
                }
                else output.Append(text[i]);
            }
            return output.ToString();
        }

        // A mod's Riivolution options, each a list (Off and its choices); saved on OK.
        private void EditOptions()
        {
            var mods = Selected(lastList);
            if (busy || mods.Count != 1 || mods[0].Options.Count == 0) return;
            Mod mod = mods[0];
            using (var dialog = new Form
            {
                Text = mod.Id,
                Font = new Font("Segoe UI", 10f),
                AutoScaleMode = AutoScaleMode.None,
                FormBorderStyle = FormBorderStyle.FixedDialog,
                MaximizeBox = false,
                MinimizeBox = false,
                ShowInTaskbar = false,
                StartPosition = FormStartPosition.CenterParent
            })
            {
                var rows = new Panel { Location = new Point(12, 12), AutoScroll = true };
                var boxes = new List<ComboBox>();
                int y = 0;
                foreach (var option in mod.Options)
                {
                    string title = option.Name.TrimEnd(':', ' ');
                    if (option.Section.Length != 0 && option.Section != option.Name) title = option.Section + ": " + title;
                    rows.Controls.Add(new Label { Text = title, Location = new Point(0, y + 4), Size = new Size(260, 24), AutoEllipsis = true });
                    var box = new ComboBox { Location = new Point(268, y), Width = 230, DropDownStyle = ComboBoxStyle.DropDownList };
                    box.Items.Add("Off");
                    for (int c = 0; c < option.Choices.Count; c++)
                        box.Items.Add(option.Choices[c] + (c < option.OtherRegion.Count && option.OtherRegion[c] ? " (another region)" : ""));
                    box.SelectedIndex = Math.Max(0, Math.Min(option.Selected, box.Items.Count - 1));
                    rows.Controls.Add(box);
                    boxes.Add(box);
                    y += 36;
                }
                rows.Size = new Size(520, Math.Min(y, 360));
                dialog.Controls.Add(rows);
                int bottom = rows.Bottom + 12;
                var ok = new Button { Text = "OK", DialogResult = DialogResult.OK, Location = new Point(320, bottom), Size = new Size(96, 32) };
                var cancel = new Button { Text = "Cancel", DialogResult = DialogResult.Cancel, Location = new Point(424, bottom), Size = new Size(96, 32) };
                dialog.Controls.Add(ok);
                dialog.Controls.Add(cancel);
                dialog.AcceptButton = ok;
                dialog.CancelButton = cancel;
                dialog.ClientSize = new Size(532, bottom + 44);
                UiScale.Apply(dialog);
                if (dialog.ShowDialog(page.FindForm()) != DialogResult.OK) return;
                bool changed = false;
                for (int i = 0; i < boxes.Count; i++)
                {
                    int value = Math.Max(0, boxes[i].SelectedIndex);
                    changed = changed || value != mod.Options[i].Selected;
                    mod.Options[i].Selected = value;
                }
                if (!changed) return;
                status.Text = "";
                Save();
                Read();
            }
        }

        private void OnDragOver(ListView view, DragEventArgs e)
        {
            if (e.Data.GetDataPresent(DataFormats.FileDrop))
            {
                e.Effect = busy ? DragDropEffects.None : DragDropEffects.Copy;
                return;
            }
            var set = e.Data.GetData(typeof(DragSet)) as DragSet;
            if (set == null || view == null || busy || (view == offList && set.From == offList))
            {
                e.Effect = DragDropEffects.None;
                dropLine.Visible = false;
                return;
            }
            e.Effect = DragDropEffects.Move;
            if (view == offList) return;
            Point at = onList.PointToClient(new Point(e.X, e.Y));
            AutoScroll(at);
            // The line between the rows nearest the pointer; below the last row, the end.
            int y;
            ListViewItem target = onList.GetItemAt(8, at.Y);
            if (target != null)
            {
                Rectangle bounds = target.Bounds;
                bool after = at.Y > bounds.Top + bounds.Height / 2;
                int index = on.IndexOf((Mod)target.Tag);
                dropAt = after ? index + 1 : index;
                y = after ? bounds.Bottom - 1 : bounds.Top - 1;
            }
            else if (onList.Items.Count != 0 && at.Y > onList.Items[onList.Items.Count - 1].Bounds.Bottom)
            {
                var last = onList.Items[onList.Items.Count - 1];
                dropAt = on.IndexOf((Mod)last.Tag) + 1;
                y = last.Bounds.Bottom - 1;
            }
            else if (onList.Items.Count == 0)
            {
                dropAt = 0;
                y = onList.Font.Height * 2;
            }
            else return;
            int headerBottom = onList.TopItem != null ? onList.TopItem.Bounds.Top : 0;
            dropLine.SetBounds(0, Math.Max(headerBottom, y), onList.ClientSize.Width, 2);
            dropLine.Visible = true;
            dropLine.BringToFront();
        }

        // Near the top or bottom edge during a drag, the list scrolls.
        private void AutoScroll(Point at)
        {
            if (onList.TopItem == null) return;
            int top = onList.TopItem.Index;
            int rowHeight = onList.TopItem.Bounds.Height;
            if (at.Y < onList.TopItem.Bounds.Top + rowHeight / 2 && top > 0) onList.Items[top - 1].EnsureVisible();
            else if (at.Y > onList.ClientSize.Height - rowHeight / 2)
            {
                ListViewItem bottom = onList.GetItemAt(8, onList.ClientSize.Height - 2);
                if (bottom != null && bottom.Index + 1 < onList.Items.Count) onList.Items[bottom.Index + 1].EnsureVisible();
            }
        }

        private void OnDragDrop(ListView view, DragEventArgs e)
        {
            dropLine.Visible = false;
            if (e.Data.GetDataPresent(DataFormats.FileDrop))
            {
                Import((string[])e.Data.GetData(DataFormats.FileDrop));
                return;
            }
            var set = e.Data.GetData(typeof(DragSet)) as DragSet;
            if (set == null || view == null) return;
            if (view == offList)
            {
                if (set.From == onList) Disable(set.Mods);
                return;
            }
            int at = dropAt;
            dropAt = -1;
            if (at >= 0) Enable(set.Mods, at);
        }

        private void ImportFiles()
        {
            using (var dialog = new OpenFileDialog
            {
                Title = "Import mods",
                Filter = "Mods and texture packs (zip, 7z, rar, tar)|*.zip;*.7z;*.rar;*.tar;*.tgz;*.gz;*.txz;*.xz|All files (*.*)|*.*",
                Multiselect = true,
                CheckFileExists = true
            })
            {
                if (dialog.ShowDialog(page.FindForm()) == DialogResult.OK) Import(dialog.FileNames);
            }
        }

        private void ImportFolder()
        {
            using (var dialog = new FolderBrowserDialog { Description = "Import a mod folder" })
            {
                if (dialog.ShowDialog(page.FindForm()) == DialogResult.OK) Import(new[] { dialog.SelectedPath });
            }
        }

        // Imports on a worker thread (a large archive takes seconds); imported mods are enabled at the top.
        private void Import(string[] paths)
        {
            if (busy || paths == null || paths.Length == 0) return;
            SetBusy(true);
            status.Text = "Importing " + (paths.Length == 1 ? Path.GetFileName(paths[0].TrimEnd('\\', '/')) : paths.Length + " mods") + "…";
            new Thread(() =>
            {
                string error;
                var result = Tool(new[] { "--import-mod" }.Concat(paths), out error);
                Post(() =>
                {
                    var messages = new List<string>();
                    var imported = new HashSet<string>();
                    object value;
                    if (result != null && result.TryGetValue("results", out value) && value is object[])
                    {
                        foreach (var r in (object[])value)
                        {
                            var one = r as Dictionary<string, object>;
                            if (one == null) continue;
                            if (Json.Bool(one, "ok"))
                            {
                                imported.Add(Json.Str(one, "id"));
                                messages.Add((Json.Bool(one, "replaced") ? "Updated " : "Imported ") + Json.Str(one, "id"));
                            }
                            else messages.Add(Capitalize(Json.Str(one, "message")));
                        }
                    }
                    Read(() =>
                    {
                        SetBusy(false);
                        status.Text = messages.Count != 0 ? string.Join(". ", messages) + "." : error ?? "The import did not finish.";
                        lastList = onList;
                        Select(onList, on.Where(m => imported.Contains(m.Id)));
                        ShowDetails(onList);
                    });
                });
            }) { IsBackground = true }.Start();
        }

        private void Remove()
        {
            if (busy) return;
            var mods = Selected(lastList);
            if (mods.Count == 0)
            {
                status.Text = "Select a mod to remove.";
                return;
            }
            string what = mods.Count == 1 ? "\"" + mods[0].Id + "\"" : mods.Count + " mods";
            if (MessageBox.Show(page.FindForm(), "Move " + what + " to the Recycle Bin?", "Remove", MessageBoxButtons.OKCancel,
                    MessageBoxIcon.Question) != DialogResult.OK)
                return;
            SetBusy(true);
            var ids = mods.Select(m => m.Id).ToList();
            new Thread(() =>
            {
                var failures = new List<string>();
                foreach (var id in ids)
                {
                    string error;
                    var result = Tool(new[] { "--remove-mod", id }, out error);
                    if (result == null) failures.Add(error);
                    else if (!Json.Bool(result, "ok")) failures.Add(Capitalize(Json.Str(result, "message")));
                }
                Post(() => Read(() =>
                {
                    SetBusy(false);
                    status.Text = failures.Count != 0 ? string.Join(" ", failures) : "Moved " + what + " to the Recycle Bin.";
                }));
            }) { IsBackground = true }.Start();
        }

        private void OpenFolder()
        {
            if (modsFolder == null) return;
            Directory.CreateDirectory(modsFolder);
            Process.Start("explorer.exe", "\"" + modsFolder + "\"");
        }

        // The game's messages are in its menu's capitals.
        private static string Capitalize(string text)
        {
            if (string.IsNullOrEmpty(text)) return "";
            string lower = text.ToLowerInvariant();
            return char.ToUpperInvariant(lower[0]) + lower.Substring(1);
        }
    }
}
