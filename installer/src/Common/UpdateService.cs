using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Net;
using System.Net.Sockets;
using System.Runtime.InteropServices;
using System.Threading.Tasks;

namespace Nebula
{
    /// <summary>
    /// When Nebula looks for a newer release, and which one it offers. The launcher starts the check as its process
    /// starts (Begin), in parallel with its own start work; when it reaches the point of starting the game it takes
    /// the check's answer if it arrives within FreshBudgetMilliseconds, else the last answer kept in update-check.json
    /// (Offer). Offline ends the wait at once: beside the request a probe resolves the server's name and opens a
    /// connection to it, and a probe that fails stops the wait (Windows tries a refused connection again for about two
    /// seconds; the probe's socket does not). A check that fails (offline, captive portal, rate limit, malformed
    /// listing) keeps the last answer; within CheckInterval of the last answer no request is made. The launcher waits
    /// for a late answer once the game has started (Finish), so it is kept for the next start.
    /// </summary>
    internal static class UpdateService
    {
        /// <summary>
        /// The longest a start waits for a fresh answer: GitHub's release list answers a healthy connection in about
        /// 150 to 350 ms (TLS set-up and the listing), so 400 ms takes it and costs a slow network at most that.
        /// </summary>
        internal const int FreshBudgetMilliseconds = 400;
        private const int CheckMilliseconds = 15000;
        internal static readonly TimeSpan CheckInterval = TimeSpan.FromHours(1);

        private static readonly object gate = new object();
        private static Task<ReleaseInfo> running;
        // True when the server cannot be reached directly (no network, its name not resolving, its port refusing).
        private static Task<bool> unreachable;

        internal static string CachePath { get { return Path.Combine(NebulaPaths.DataRoot, "update-check.json"); } }

        /// <summary>The check of this process, null when none was started (the service off, or the last answer recent).</summary>
        internal static Task<ReleaseInfo> Running { get { lock (gate) return running; } }

        /// <summary>Starts the check behind the caller unless the last answer is under CheckInterval old. A last answer
        /// dated after now (a clock set wrong) is checked again.</summary>
        public static void Begin()
        {
            lock (gate)
            {
                if (running != null) return;
                ReleaseInfo cached;
                DateTime checkedUtc, now = DateTime.UtcNow;
                if (ReadCache(out cached, out checkedUtc) && checkedUtc <= now && now - checkedUtc < CheckInterval) return;
                // Each on its own thread (LongRunning): both block on the network, and a start's budget must not wait
                // for a pool thread the pool has yet to add.
                running = Task.Factory.StartNew(() =>
                {
                    ReleaseInfo release = Updater.FindNewer(CheckMilliseconds);
                    WriteCache(release, DateTime.UtcNow);
                    return release;
                }, System.Threading.CancellationToken.None, TaskCreationOptions.LongRunning, TaskScheduler.Default);
                string url = Updater.ReleasesUrl;
                unreachable = Task.Factory.StartNew(() => Unreachable(url), System.Threading.CancellationToken.None,
                    TaskCreationOptions.LongRunning, TaskScheduler.Default);
            }
        }

        /// <summary>The release to offer now: the check's answer if it comes within <paramref name="budgetMilliseconds"/>,
        /// else the last kept answer; none when it is not newer than this install, of the other channel, or no newer
        /// than the version the player skipped.</summary>
        public static ReleaseInfo Offer(int budgetMilliseconds, string skipped)
        {
            Task<ReleaseInfo> task;
            Task<bool> probe;
            lock (gate)
            {
                task = running;
                probe = unreachable;
            }
            if (task != null)
            {
                var clock = System.Diagnostics.Stopwatch.StartNew();
                try
                {
                    // The answer, or the probe finding the server unreachable, whichever comes first; a probe that
                    // reaches it leaves the rest of the budget to the answer.
                    if (probe == null) task.Wait(budgetMilliseconds);
                    else if (Task.WaitAny(new Task[] { task, probe }, budgetMilliseconds) == 1 && !probe.Result)
                        task.Wait(Math.Max(0, budgetMilliseconds - (int)clock.ElapsedMilliseconds));
                }
                catch (AggregateException) { }
            }
            ReleaseInfo release;
            DateTime checkedUtc;
            if (task != null && task.Status == TaskStatus.RanToCompletion) release = task.Result;
            else if (!ReadCache(out release, out checkedUtc)) release = null;
            return Offered(release, skipped) ? release : null;
        }

        /// <summary>Waits for this process's check to end, so its answer is kept; for when the game has started.</summary>
        public static void Finish()
        {
            Task<ReleaseInfo> task = Running;
            if (task == null) return;
            try { task.Wait(CheckMilliseconds + 1000); }
            catch (AggregateException) { }
        }

        internal static bool Offered(ReleaseInfo release, string skipped)
        {
            if (release == null || Updater.CompareVersions(release.Version, BuildInfo.Version) <= 0 ||
                Updater.IsBetaVersion(release.Version) != Updater.IsBetaVersion(BuildInfo.Version)) return false;
            return string.IsNullOrEmpty(skipped) || Updater.CompareVersions(release.Version, skipped) > 0;
        }

        /// <summary>Forgets this process's check (tests start each case afresh).</summary>
        internal static void Reset()
        {
            lock (gate)
            {
                running = null;
                unreachable = null;
            }
        }

        // SIO_TCP_INITIAL_RTO with TCP_INITIAL_RTO_PARAMETERS { Rtt = unspecified, MaxSynRetransmissions = none }: a
        // refused connection fails at the first reset instead of being tried again (Windows 8 and later).
        private const int SioTcpInitialRto = unchecked((int)0x98000011);
        private static readonly byte[] NoSynRetransmissions = { 0xFF, 0xFF, 0xFE, 0x00 };

        /// <summary>True when the release list's server cannot be reached directly; false when it can, or when a proxy
        /// stands between (only the request itself can tell then). Only calls that cost no more than their own work
        /// in a process just started: NetworkInterface.GetIsNetworkAvailable and WebRequest.DefaultWebProxy each took
        /// 100 to 400 ms on their first call (the latter loads .NET's configuration and may run proxy detection),
        /// which spent the start's budget before the probe began. Without a network the name lookup fails at once.</summary>
        private static bool Unreachable(string url)
        {
            try
            {
                var uri = new Uri(url);
                if (ProxyConfigured(uri)) return false;
                IPAddress[] addresses;
                try { addresses = Dns.GetHostAddresses(uri.DnsSafeHost); }
                catch (SocketException) { return true; }
                foreach (IPAddress address in addresses)
                {
                    using (var socket = new Socket(address.AddressFamily, SocketType.Stream, ProtocolType.Tcp))
                    {
                        try { socket.IOControl(SioTcpInitialRto, NoSynRetransmissions, null); }
                        catch (SocketException) { }
                        try
                        {
                            socket.Connect(address, uri.Port);
                            return false;
                        }
                        catch (SocketException) { }
                    }
                }
                return true;
            }
            catch (Exception) { return false; }
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct IeProxyConfig
        {
            public bool AutoDetect;
            public IntPtr AutoConfigUrl, Proxy, ProxyBypass;
        }

        [DllImport("winhttp.dll", SetLastError = true)]
        private static extern bool WinHttpGetIEProxyConfigForCurrentUser(ref IeProxyConfig config);

        [DllImport("kernel32.dll")]
        private static extern IntPtr GlobalFree(IntPtr memory);

        // A proxy the user set (a proxy server or a configuration script, in Windows' proxy settings), which the
        // request may take: a direct connection then tells nothing. Automatic detection alone (WPAD, on by default)
        // does not count: on a network that requires it the direct connection fails, the start offers the last
        // answer, and the request's own answer is kept for the next start. A registry read, about 10 ms.
        private static bool ProxyConfigured(Uri uri)
        {
            if (uri.IsLoopback) return false;
            var config = new IeProxyConfig();
            try
            {
                if (!WinHttpGetIEProxyConfigForCurrentUser(ref config)) return false;
                return config.AutoConfigUrl != IntPtr.Zero || config.Proxy != IntPtr.Zero;
            }
            catch (DllNotFoundException) { return false; }
            catch (EntryPointNotFoundException) { return false; }
            finally
            {
                if (config.AutoConfigUrl != IntPtr.Zero) GlobalFree(config.AutoConfigUrl);
                if (config.Proxy != IntPtr.Zero) GlobalFree(config.Proxy);
                if (config.ProxyBypass != IntPtr.Zero) GlobalFree(config.ProxyBypass);
            }
        }

        internal static void WriteCache(ReleaseInfo release, DateTime checkedUtc)
        {
            var values = new Dictionary<string, object> { { "checkedUtc", checkedUtc.ToString("o", CultureInfo.InvariantCulture) } };
            if (release != null)
            {
                values["version"] = release.Version;
                values["tag"] = release.Tag;
                values["notes"] = release.Notes;
                values["setupUrl"] = release.SetupUrl;
                values["signatureUrl"] = release.SignatureUrl;
                values["setupSize"] = release.SetupSize;
                values["prerelease"] = release.Prerelease;
            }
            Json.Save(CachePath, values);
        }

        /// <summary>False without a readable answer; release null when the last check found nothing newer.</summary>
        internal static bool ReadCache(out ReleaseInfo release, out DateTime checkedUtc)
        {
            release = null;
            checkedUtc = DateTime.MinValue;
            try
            {
                if (!File.Exists(CachePath)) return false;
                var values = Json.Load(CachePath);
                if (!DateTime.TryParse(Json.Str(values, "checkedUtc"), CultureInfo.InvariantCulture,
                                       DateTimeStyles.AdjustToUniversal | DateTimeStyles.AssumeUniversal, out checkedUtc)) return false;
                string version = Json.Str(values, "version");
                if (string.IsNullOrEmpty(version)) return true;
                release = new ReleaseInfo
                {
                    Version = version, Tag = Json.Str(values, "tag"), Notes = Json.Str(values, "notes") ?? "",
                    SetupUrl = Json.Str(values, "setupUrl"), SignatureUrl = Json.Str(values, "signatureUrl"),
                    SetupSize = Json.Long(values, "setupSize"), Prerelease = Json.Bool(values, "prerelease")
                };
                if (release.SetupUrl == null || release.SignatureUrl == null) release = null;
                return true;
            }
            catch (Exception) { return false; }
        }
    }
}
