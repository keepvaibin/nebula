using System;
using System.Collections.Generic;
using System.Web.Script.Serialization;

namespace Nebula.Setup
{
    internal static class BetaChannelTests
    {
        static int checks;
        static object Release(string version, bool draft, string asset, bool signed)
        {
            var assets = new List<object>();
            assets.Add(new Dictionary<string, object>{{"name", asset}, {"browser_download_url", "https://example.invalid/" + version + "/" + asset}});
            if(signed) assets.Add(new Dictionary<string, object>{{"name", asset + ".sig"}, {"browser_download_url", "https://example.invalid/" + version + "/signature"}});
            return new Dictionary<string, object>{{"tag_name", "v" + version}, {"draft", draft}, {"prerelease", version.IndexOf('-') >= 0}, {"assets", assets}};
        }
        static void Expect(string installed, string expected, params object[] releases)
        {
            string json = new JavaScriptSerializer().Serialize(releases);
            var actual = Updater.SelectNewer(json, installed);
            string version = actual == null ? null : actual.Version;
            if(version != expected) throw new Exception(installed + ": expected " + expected + ", got " + version);
            if(actual != null && !actual.SetupUrl.EndsWith(Updater.IsBetaVersion(installed) ? "/Nebula-Beta-Setup.exe" : "/Nebula-Setup.exe")) throw new Exception("Wrong channel asset");
            checks++;
        }
        public static int Main()
        {
            if (InstallIntent.ForPackage(null, "0.1.1-beta.4") != InstallMode.Install ||
                InstallIntent.ForPackage("0.1.1-beta.3", "0.1.1-beta.4") != InstallMode.Update ||
                InstallIntent.ForPackage("0.1.1-beta.4", "0.1.1-beta.4") != InstallMode.Repair ||
                InstallIntent.ForPackage("0.1.0-preview.2", "0.1.1-beta.4") != InstallMode.Update)
                throw new Exception("Downloaded setup did not select fresh install, update or repair correctly.");
            Console.WriteLine("PASS: production install/update/repair selection for fresh, beta and preview installations.");
            var preview = Release("0.1.0-preview.3", false, "Nebula-Setup.exe", true);
            var beta = Release("0.1.1-beta.2", false, "Nebula-Beta-Setup.exe", true);
            var stable = Release("0.2.0", false, "Nebula-Setup.exe", true);
            Expect("0.1.0-preview.2", "0.1.0-preview.3", beta, preview);
            Expect("0.1.0-preview.2", "0.2.0", preview, stable, beta);
            Expect("0.1.1-beta.1", "0.1.1-beta.2", stable, beta, preview);
            Expect("0.1.1-beta.2", null, stable, beta, preview);
            Expect("0.2.0", null, beta, preview, stable);
            // Channel filtering still works if a beta is accidentally aliased to
            // the ordinary name. Publishing such an alias would affect OLD clients.
            Expect("0.1.0-preview.2", null, Release("0.9.0-beta.1", false, "Nebula-Setup.exe", true));
            Expect("0.1.1-beta.1", null, Release("0.1.1-beta.3", false, "Nebula-Setup.exe", true));
            Expect("0.1.1-beta.1", null, Release("0.1.1-beta.3", true, "Nebula-Beta-Setup.exe", true));
            Expect("0.1.1-beta.1", null, Release("0.1.1-beta.3", false, "Nebula-Beta-Setup.exe", false));
            Expect("0.1.1-beta.1", "0.1.1-beta.10", beta, Release("0.1.1-beta.10", false, "Nebula-Beta-Setup.exe", true));
            Expect("0.1.1-beta.1", "0.1.1-beta.10", Release("0.1.1-beta.10", false, "Nebula-Beta-Setup.exe", true), beta);
            Expect("0.1.1-beta.1", null);
            Expect("0.1.0-preview.2", null, Release("0.1.0-preview.9", true, "Nebula-Setup.exe", true));
            Expect("0.1.0-preview.2", null, Release("0.1.0-preview.9", false, "Nebula-Setup.exe", false));
            Console.WriteLine("PASS: " + checks + " actual production updater selection cases; no network, install or game executed.");
            return 0;
        }
    }
}
