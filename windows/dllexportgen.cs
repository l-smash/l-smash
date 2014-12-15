using System.IO;
using System.Text.RegularExpressions;

namespace dllexportgen
{
    class dllexportgen
    {
        static void Main(string[] args)
        {
            var sw = new StreamReader(args[0] + "\\lsmash.h");
            var so = new StreamWriter(args[0] + "\\lsmash.def");
            string prevline = "";

            so.WriteLine("EXPORTS");

            while (!sw.EndOfStream)
            {
                var funcregex = new Regex("^\\($");
                var typeregex = new Regex("^DEFINE_(ISOM|QTFF)_CODEC_TYPE\\(");

                string line = sw.ReadLine();

                if (funcregex.IsMatch(line) && prevline != "")
                {
                    /* Export all public API functions. */
                    var sub = new Regex(".+\\s+\\*{0,1}(.+)");
                    string newline = sub.Replace(prevline, "$1");

                    so.Write("    ");
                    so.WriteLine(newline);
                }
                else if (typeregex.IsMatch(line))
                {
                    /* Export all codec types. */
                    var sub = new Regex("^.+\\s+((ISOM|QT|LSMASH)_CODEC_TYPE_.+?),\\s+.+");
                    string newline = sub.Replace(line, "$1");

                    so.Write("    ");
                    so.WriteLine(newline);
                }

                prevline = line;
            }

            sw.Close();

            /* Non-public symbols for the cli apps. */
            so.Write("lsmash_importer_open\n" +
                     "lsmash_importer_get_access_unit\n" +
                     "lsmash_importer_close\n" +
                     "lsmash_importer_get_track_count\n" +
                     "lsmash_importer_get_last_delta\n" +
                     "lsmash_importer_construct_timeline\n" +
                     "lsmash_duplicate_summary\n" +
                     "lsmash_string_from_wchar\n" +
                     "lsmash_win32_fopen");

            so.Flush();
            so.Close();
        }
    }
}
