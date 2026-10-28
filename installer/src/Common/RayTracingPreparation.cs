using System;
using System.Collections.Generic;
using System.IO;

namespace Nebula
{
    /// <summary>
    /// Ray tracing's shader preparation, asked for from Setup and the update offer: rt-prepare.request in the data
    /// folder makes the game's next start compile the ray tracing shaders before the title (rt_prepare.h), whatever
    /// the RAY TRACING setting. The game removes it once they are prepared.
    /// </summary>
    internal static class RayTracingPreparation
    {
        public static string RequestPath { get { return Path.Combine(NebulaPaths.DataRoot, "rt-prepare.request"); } }

        /// <summary>
        /// The box is offered on a PC that runs Nebula's ray tracing (as the game recorded for its GPU in adapter.ini,
        /// else when it has an NVIDIA RTX GPU) while ray tracing is off: with it on, the next start prepares anyway.
        /// </summary>
        public static bool Offered()
        {
            var adapter = ReadKeys(Path.Combine(NebulaPaths.DataRoot, "adapter.ini"));
            string value;
            bool capable = adapter != null && adapter.TryGetValue("ray_tracing", out value)
                ? value == "1"
                : DlssPackage.RtxAdapterPresent();
            var config = ReadKeys(Path.Combine(NebulaPaths.DataRoot, "config.ini"));
            bool enabled = config != null && config.TryGetValue("ray_tracing_enabled", out value) && value != "0";
            return capable && !enabled;
        }

        public static void Request()
        {
            try
            {
                Directory.CreateDirectory(NebulaPaths.DataRoot);
                File.WriteAllText(RequestPath, "");
            }
            catch (IOException) { }
            catch (UnauthorizedAccessException) { }
        }

        private static Dictionary<string, string> ReadKeys(string path)
        {
            try
            {
                if (!File.Exists(path)) return null;
                var values = new Dictionary<string, string>();
                foreach (string line in File.ReadAllLines(path))
                {
                    int equals = line.IndexOf('=');
                    if (equals > 0) values[line.Substring(0, equals).Trim()] = line.Substring(equals + 1).Trim();
                }
                return values;
            }
            catch (IOException) { }
            catch (UnauthorizedAccessException) { }
            return null;
        }
    }
}
