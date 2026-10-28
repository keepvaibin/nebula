using System.Text;
using System.Text.RegularExpressions;

namespace Nebula
{
    /// <summary>
    /// A release's notes as the update prompts show them: GitHub's Markdown (the release body) as plain text. Headings,
    /// quotes, emphasis and code marks are dropped, a link shows its text and its address, and list items keep their
    /// dash; at most <c>limit</c> characters, cut at a line's end where one is near.
    /// </summary>
    internal static class ReleaseNotes
    {
        private static readonly Regex Link = new Regex(@"\[([^\]]+)\]\(([^)\s]+)\)", RegexOptions.Compiled);
        private static readonly Regex Emphasis = new Regex(@"(\*\*|__)(.+?)\1|(?<![\w*])\*(?!\s)(.+?)(?<!\s)\*(?![\w*])", RegexOptions.Compiled);
        private static readonly Regex Code = new Regex(@"`([^`]*)`", RegexOptions.Compiled);

        public static string Plain(string markdown, int limit)
        {
            var text = new StringBuilder();
            bool blank = true;
            foreach (string raw in (markdown ?? "").Replace("\r\n", "\n").Split('\n'))
            {
                string line = raw.TrimEnd();
                // Quotes and headings: their text alone.
                while (line.StartsWith(">", System.StringComparison.Ordinal)) line = line.Substring(1).TrimStart();
                line = Regex.Replace(line, @"^#{1,6}\s+", "");
                // A list item keeps its dash (an asterisk or plus becomes one).
                line = Regex.Replace(line, @"^(\s*)[*+]\s+", "$1- ");
                line = Link.Replace(line, m => m.Groups[1].Value == m.Groups[2].Value ? m.Groups[2].Value : m.Groups[1].Value + " (" + m.Groups[2].Value + ")");
                line = Emphasis.Replace(line, m => m.Groups[2].Success ? m.Groups[2].Value : m.Groups[3].Value);
                line = Code.Replace(line, "$1");
                if (Regex.IsMatch(line, @"^\s*([-*_]\s*){3,}$")) line = "";  // a rule
                if (line.Length == 0)
                {
                    if (!blank) text.Append("\r\n");
                    blank = true;
                    continue;
                }
                text.Append(line).Append("\r\n");
                blank = false;
            }
            string plain = text.ToString().TrimEnd();
            if (plain.Length <= limit) return plain;
            int cut = plain.LastIndexOf("\r\n", limit, System.StringComparison.Ordinal);
            return (cut > limit / 2 ? plain.Substring(0, cut) : plain.Substring(0, limit)).TrimEnd() + "\r\n…";
        }
    }
}
