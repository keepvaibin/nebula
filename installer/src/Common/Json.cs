using System;
using System.Collections;
using System.Collections.Generic;
using System.IO;
using System.Text;
using System.Web.Script.Serialization;

namespace Nebula
{
    /// <summary>Small JSON helpers over the in-box JavaScriptSerializer.</summary>
    internal static class Json
    {
        private static JavaScriptSerializer Serializer()
        {
            var serializer = new JavaScriptSerializer();
            serializer.MaxJsonLength = int.MaxValue;
            serializer.RecursionLimit = 256;
            return serializer;
        }

        public static Dictionary<string, object> Parse(string text)
        {
            var value = Serializer().DeserializeObject(text) as Dictionary<string, object>;
            if (value == null) throw new InvalidDataException("Expected a JSON object.");
            return value;
        }

        public static object ParseAny(string text) { return Serializer().DeserializeObject(text); }

        public static Dictionary<string, object> Load(string path)
        {
            return Parse(File.ReadAllText(path, Encoding.UTF8));
        }

        public static string Serialize(object value) { return Pretty(Serializer().Serialize(value)); }

        /// <summary>Write JSON through a temporary file and an atomic replace.</summary>
        public static void Save(string path, object value)
        {
            FileUtil.WriteAllTextAtomic(path, Serialize(value) + "\n");
        }

        public static string Str(Dictionary<string, object> map, string key)
        {
            object value;
            return map != null && map.TryGetValue(key, out value) && value != null ? Convert.ToString(value) : null;
        }

        public static long Long(Dictionary<string, object> map, string key)
        {
            object value;
            return map != null && map.TryGetValue(key, out value) && value != null ? Convert.ToInt64(value) : 0;
        }

        public static bool Bool(Dictionary<string, object> map, string key)
        {
            object value;
            return map != null && map.TryGetValue(key, out value) && value is bool && (bool)value;
        }

        public static Dictionary<string, object> Obj(Dictionary<string, object> map, string key)
        {
            object value;
            return map != null && map.TryGetValue(key, out value) ? value as Dictionary<string, object> : null;
        }

        public static IList List(Dictionary<string, object> map, string key)
        {
            object value;
            return map != null && map.TryGetValue(key, out value) ? value as IList : null;
        }

        /// <summary>Indent compact serializer output for readable files.</summary>
        private static string Pretty(string json)
        {
            var output = new StringBuilder(json.Length * 2);
            int indent = 0;
            bool quoted = false;
            for (int i = 0; i < json.Length; i++)
            {
                char c = json[i];
                if (quoted)
                {
                    output.Append(c);
                    if (c == '\\' && i + 1 < json.Length) { output.Append(json[++i]); }
                    else if (c == '"') quoted = false;
                    continue;
                }
                switch (c)
                {
                    case '"': quoted = true; output.Append(c); break;
                    case '{':
                    case '[':
                        output.Append(c);
                        if (i + 1 < json.Length && (json[i + 1] == '}' || json[i + 1] == ']')) { output.Append(json[++i]); break; }
                        output.Append('\n').Append(' ', 2 * ++indent);
                        break;
                    case '}':
                    case ']':
                        output.Append('\n').Append(' ', 2 * --indent).Append(c);
                        break;
                    case ',': output.Append(",\n").Append(' ', 2 * indent); break;
                    case ':': output.Append(": "); break;
                    default: output.Append(c); break;
                }
            }
            return output.ToString();
        }
    }
}
