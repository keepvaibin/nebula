using System.Collections.Generic;
using System.Text;

namespace Nebula
{
    /// <summary>Command lines for the programs Nebula starts.</summary>
    internal static class CommandLine
    {
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
    }
}
