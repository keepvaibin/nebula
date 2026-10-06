using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;

namespace Nebula
{
    /// <summary>Thrown when the user cancels a running operation.</summary>
    internal sealed class OperationCanceledByUserException : Exception
    {
        public OperationCanceledByUserException() : base("The operation was canceled.") { }
    }

    /// <summary>
    /// Runs a child process inside a kill-on-close job object, so canceling
    /// or closing the setup also stops every compiler it started.
    /// </summary>
    internal sealed class ProcessRunner : IDisposable
    {
        private readonly IntPtr job;

        public ProcessRunner()
        {
            job = CreateJobObjectW(IntPtr.Zero, null);
            if (job == IntPtr.Zero) throw new Win32Exception(Marshal.GetLastWin32Error());
            var info = new JOBOBJECT_EXTENDED_LIMIT_INFORMATION();
            info.BasicLimitInformation.LimitFlags = 0x2000; // JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE
            int length = Marshal.SizeOf(typeof(JOBOBJECT_EXTENDED_LIMIT_INFORMATION));
            IntPtr buffer = Marshal.AllocHGlobal(length);
            try
            {
                Marshal.StructureToPtr(info, buffer, false);
                if (!SetInformationJobObject(job, 9, buffer, (uint)length))
                    throw new Win32Exception(Marshal.GetLastWin32Error());
            }
            finally { Marshal.FreeHGlobal(buffer); }
        }

        /// <summary>
        /// Run a program to completion. Each output line goes to <paramref name="onLine"/>.
        /// Returns the exit code; throws when canceled.
        /// </summary>
        public int Run(string fileName, IList<string> arguments, string workingDirectory,
            IDictionary<string, string> environment, Action<string> onLine, CancellationToken cancel)
        {
            return RunCommandLine(fileName, JoinArguments(arguments), workingDirectory, environment, onLine, cancel);
        }

        /// <summary>Run with a preformatted command line, for programs such as
        /// msiexec that parse their own quoting.</summary>
        public int RunCommandLine(string fileName, string commandLine, string workingDirectory,
            IDictionary<string, string> environment, Action<string> onLine, CancellationToken cancel)
        {
            var start = new ProcessStartInfo(fileName, commandLine);
            start.UseShellExecute = false;
            start.CreateNoWindow = true;
            start.RedirectStandardOutput = true;
            start.RedirectStandardError = true;
            start.StandardOutputEncoding = Encoding.UTF8;
            start.StandardErrorEncoding = Encoding.UTF8;
            start.WorkingDirectory = workingDirectory;
            if (environment != null)
            {
                start.EnvironmentVariables.Clear();
                foreach (var pair in environment) start.EnvironmentVariables[pair.Key] = pair.Value;
            }
            using (var process = new Process())
            {
                process.StartInfo = start;
                DataReceivedEventHandler handler = delegate(object sender, DataReceivedEventArgs e)
                {
                    if (e.Data != null && onLine != null) onLine(e.Data);
                };
                process.OutputDataReceived += handler;
                process.ErrorDataReceived += handler;
                process.Start();
                AssignProcessToJobObject(job, process.Handle);
                process.BeginOutputReadLine();
                process.BeginErrorReadLine();
                while (!process.WaitForExit(200))
                {
                    if (cancel.IsCancellationRequested)
                    {
                        TerminateJobObject(job, 1);
                        process.WaitForExit();
                        throw new OperationCanceledByUserException();
                    }
                }
                process.WaitForExit();
                return process.ExitCode;
            }
        }

        public static string JoinArguments(IList<string> arguments)
        {
            var builder = new StringBuilder();
            foreach (var argument in arguments)
            {
                if (builder.Length > 0) builder.Append(' ');
                builder.Append(Quote(argument));
            }
            return builder.ToString();
        }

        /// <summary>Quote one argument for CommandLineToArgvW-compatible parsing.</summary>
        public static string Quote(string argument)
        {
            if (argument.Length > 0 && argument.IndexOfAny(new[] { ' ', '\t', '"' }) < 0) return argument;
            var builder = new StringBuilder("\"");
            int backslashes = 0;
            foreach (char c in argument)
            {
                if (c == '\\') { backslashes++; continue; }
                if (c == '"') builder.Append('\\', backslashes * 2 + 1);
                else builder.Append('\\', backslashes);
                backslashes = 0;
                builder.Append(c);
            }
            builder.Append('\\', backslashes * 2).Append('"');
            return builder.ToString();
        }

        public void Dispose() { CloseHandle(job); }

        [StructLayout(LayoutKind.Sequential)]
        private struct JOBOBJECT_BASIC_LIMIT_INFORMATION
        {
            public long PerProcessUserTimeLimit;
            public long PerJobUserTimeLimit;
            public uint LimitFlags;
            public UIntPtr MinimumWorkingSetSize;
            public UIntPtr MaximumWorkingSetSize;
            public uint ActiveProcessLimit;
            public UIntPtr Affinity;
            public uint PriorityClass;
            public uint SchedulingClass;
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct IO_COUNTERS
        {
            public ulong ReadOperationCount, WriteOperationCount, OtherOperationCount;
            public ulong ReadTransferCount, WriteTransferCount, OtherTransferCount;
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct JOBOBJECT_EXTENDED_LIMIT_INFORMATION
        {
            public JOBOBJECT_BASIC_LIMIT_INFORMATION BasicLimitInformation;
            public IO_COUNTERS IoInfo;
            public UIntPtr ProcessMemoryLimit;
            public UIntPtr JobMemoryLimit;
            public UIntPtr PeakProcessMemoryUsed;
            public UIntPtr PeakJobMemoryUsed;
        }

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern IntPtr CreateJobObjectW(IntPtr attributes, string name);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool SetInformationJobObject(IntPtr job, int infoClass, IntPtr info, uint length);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool AssignProcessToJobObject(IntPtr job, IntPtr process);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool TerminateJobObject(IntPtr job, uint exitCode);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool CloseHandle(IntPtr handle);
    }
}
