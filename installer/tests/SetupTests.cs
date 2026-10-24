using System;
using System.Collections.Generic;
using System.IO;

namespace Nebula.Setup
{
    // Folder moves while another program holds a file. Run by run-launcher-tests.ps1 with a scratch folder.
    internal static class SetupTests
    {
        static void Check(bool ok, string what)
        {
            if (!ok) throw new Exception("FAIL: " + what);
            Console.WriteLine("PASS: " + what);
        }

        static bool Logged(List<string> log, string text) { return log.Exists(line => line.Contains(text)); }

        static void Expect<T>(Action action, Func<T, bool> matches, string what) where T : Exception
        {
            try { action(); }
            catch (T error) { Check(matches(error), what); return; }
            throw new Exception("FAIL: " + what + " (no " + typeof(T).Name + ")");
        }

        public static int Main(string[] args)
        {
            try
            {
                MoveCases(Path.Combine(args[0], "moves"));
                return 0;
            }
            catch (Exception error)
            {
                Console.WriteLine(error.Message.StartsWith("FAIL:") ? error.Message : "FAIL: " + error);
                return 1;
            }
        }

        static string MakeTree(string root, string text)
        {
            Directory.CreateDirectory(Path.Combine(root, "sub"));
            File.WriteAllText(Path.Combine(root, "a.dll"), text);
            File.WriteAllText(Path.Combine(root, "sub", "b.txt"), text);
            return root;
        }

        static bool Holds(string root, string text)
        {
            return File.Exists(Path.Combine(root, "a.dll")) && File.ReadAllText(Path.Combine(root, "a.dll")) == text &&
                   File.ReadAllText(Path.Combine(root, "sub", "b.txt")) == text;
        }

        // A program reading a fresh file (a virus scanner) can refuse the move of a new version into place. A tree
        // Setup built is then copied; a tree that is installed already is only ever renamed, so a held file leaves it
        // whole; and a swap whose new tree cannot move in puts the old one back.
        static void MoveCases(string root)
        {
            var log = new List<string>();
            string built = MakeTree(Path.Combine(root, "built"), "new");
            string target = Path.Combine(root, "versions", "1.0.1");
            Directory.CreateDirectory(Path.GetDirectoryName(target));
            using (new FileStream(Path.Combine(built, "a.dll"), FileMode.Open, FileAccess.Read, FileShare.Read))
                FileUtil.MoveDirectory(built, target, log.Add);
            Check(Holds(target, "new") && Logged(log, "copying it instead"), "a built tree with a file held for reading is copied into place");

            string installed = MakeTree(Path.Combine(root, "installed"), "old");
            using (new FileStream(Path.Combine(installed, "sub", "b.txt"), FileMode.Open, FileAccess.Read, FileShare.Read))
                Expect<IOException>(() => FileUtil.MoveDirectoryInPlace(installed, Path.Combine(root, "aside")),
                    e => e.Message.Contains("In use by") && Holds(installed, "old") && !Directory.Exists(Path.Combine(root, "aside")),
                    "an installed tree with a held file is left whole, and the failure names the program holding it");
            Expect<IOException>(() => FileUtil.MoveDirectory(MakeTree(Path.Combine(root, "other"), "x"), installed, log.Add),
                e => e.Message.Contains("already exists") && Holds(installed, "old"), "a tree is never moved into an existing folder");

            log.Clear();
            string fresh = MakeTree(Path.Combine(root, "fresh"), "new");
            using (new FileStream(Path.Combine(fresh, "a.dll"), FileMode.Open, FileAccess.ReadWrite, FileShare.None))
                Expect<IOException>(() => InstallEngine.Swap(fresh, installed, Path.Combine(root, "installed-old"), log.Add),
                    e => Holds(installed, "old") && !Directory.Exists(Path.Combine(root, "installed-old")),
                    "a swap whose new tree cannot be moved in puts the installed tree back");
            InstallEngine.Swap(fresh, installed, Path.Combine(root, "installed-old"), log.Add);
            Check(Holds(installed, "new") && Holds(Path.Combine(root, "installed-old"), "old"), "a swap keeps the replaced tree aside");
        }
    }
}
