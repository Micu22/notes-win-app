using System.Globalization;
using System.Text;
using System.Text.RegularExpressions;
using TrayNotes_WinUI.Models;

namespace TrayNotes_WinUI.Services;

public static class NoteStore
{
    // Prefer the existing Desktop\Notes folder (sibling of the WinUI project / next to .md files).
    public static string NotesDirectory { get; private set; } = ResolveNotesDirectory();

    private static string ResolveNotesDirectory()
    {
        var desktopNotes = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory), "Notes");
        if (Directory.Exists(desktopNotes) &&
            (Directory.EnumerateFiles(desktopNotes, "note-*.md").Any() ||
             File.Exists(Path.Combine(desktopNotes, "main.cpp"))))
            return desktopNotes;

        var dir = AppContext.BaseDirectory;
        for (var i = 0; i < 10 && !string.IsNullOrEmpty(dir); i++)
        {
            if (Directory.EnumerateFiles(dir, "note-*.md").Any() ||
                File.Exists(Path.Combine(dir, "TrayNotes.exe")) ||
                File.Exists(Path.Combine(dir, "main.cpp")))
                return dir;
            dir = Path.GetDirectoryName(dir.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar));
        }
        Directory.CreateDirectory(desktopNotes);
        return desktopNotes;
    }

    public static void SetNotesDirectory(string path)
    {
        Directory.CreateDirectory(path);
        NotesDirectory = path;
    }

    public static List<NoteDocument> LoadAll()
    {
        Directory.CreateDirectory(NotesDirectory);
        var notes = new List<NoteDocument>();
        foreach (var path in Directory.EnumerateFiles(NotesDirectory, "note-*.md").OrderBy(p => p))
        {
            try
            {
                var note = LoadOne(path);
                if (note != null) notes.Add(note);
            }
            catch { /* skip corrupt */ }
        }
        return notes;
    }

    public static NoteDocument? LoadOne(string path)
    {
        var raw = File.ReadAllText(path, Encoding.UTF8);
        var lines = raw.Replace("\r\n", "\n").Split('\n');
        int x = 80, y = 80, w = 320, h = 280, color = 0;
        var hidden = false;
        var checklist = false;
        var bodyStart = 0;
        if (lines.Length > 0 && lines[0].Contains("<!-- traynotes", StringComparison.Ordinal))
        {
            ParseMeta(lines[0], ref x, ref y, ref w, ref h, ref hidden, ref checklist, ref color);
            bodyStart = 1;
        }
        var body = string.Join("\n", lines.Skip(bodyStart)).TrimEnd('\n') + (raw.EndsWith('\n') ? "\n" : "");
        if (body.EndsWith("\n") && body.Length > 0) body = body.TrimEnd('\n');

        var id = Path.GetFileNameWithoutExtension(path);
        var note = new NoteDocument
        {
            Id = id,
            Path = path,
            X = x,
            Y = y,
            Width = Math.Max(220, w),
            Height = Math.Max(180, h),
            Hidden = hidden,
            ColorIdx = Math.Clamp(color, 0, NotePalettes.All.Length - 1),
            Mode = checklist || LooksLikeChecklist(body) ? NoteMode.Checklist : NoteMode.Text,
            Body = body,
            Tags = ParseTags(body),
        };
        if (note.Mode == NoteMode.Checklist)
            note.Items = ParseChecklist(body);
        return note;
    }

    public static void Save(NoteDocument note)
    {
        Directory.CreateDirectory(NotesDirectory);
        if (string.IsNullOrEmpty(note.Path))
            note.Path = Path.Combine(NotesDirectory, note.Id + ".md");

        var body = note.Mode == NoteMode.Checklist ? ChecklistToMd(note.Items) : note.Body;
        note.Tags = ParseTags(body);
        var sb = new StringBuilder();
        sb.Append("<!-- traynotes")
          .Append(" x=").Append((int)Math.Round(note.X))
          .Append(" y=").Append((int)Math.Round(note.Y))
          .Append(" w=").Append((int)Math.Round(note.Width))
          .Append(" h=").Append((int)Math.Round(note.Height))
          .Append(" hidden=").Append(note.Hidden ? 1 : 0)
          .Append(" mode=").Append(note.Mode == NoteMode.Checklist ? "check" : "text")
          .Append(" color=").Append(note.ColorIdx)
          .Append(" -->\n");
        sb.Append(body);
        if (!body.EndsWith('\n')) sb.Append('\n');
        File.WriteAllText(note.Path, sb.ToString(), new UTF8Encoding(encoderShouldEmitUTF8Identifier: false));
    }

    public static NoteDocument CreateNew()
    {
        var id = NextId();
        var note = new NoteDocument
        {
            Id = id,
            Path = Path.Combine(NotesDirectory, id + ".md"),
            Body = "#inbox\n",
            Tags = ["inbox"],
            X = 100 + Random.Shared.Next(0, 8) * 28,
            Y = 100 + Random.Shared.Next(0, 8) * 28,
        };
        Save(note);
        return note;
    }

    private static string NextId()
    {
        for (var n = 1; n < 10000; n++)
        {
            var id = $"note-{n:D3}";
            if (!File.Exists(Path.Combine(NotesDirectory, id + ".md")))
                return id;
        }
        return $"note-{Guid.NewGuid():N}"[..12];
    }

    private static void ParseMeta(string line, ref int x, ref int y, ref int w, ref int h,
        ref bool hidden, ref bool checklist, ref int color)
    {
        static int Grab(string s, string key, int fallback)
        {
            var i = s.IndexOf(key, StringComparison.Ordinal);
            if (i < 0) return fallback;
            i += key.Length;
            var j = i;
            while (j < s.Length && (char.IsDigit(s[j]) || s[j] == '-')) j++;
            return int.TryParse(s.AsSpan(i, j - i), NumberStyles.Integer, CultureInfo.InvariantCulture, out var v)
                ? v : fallback;
        }
        x = Grab(line, "x=", x);
        y = Grab(line, "y=", y);
        w = Grab(line, "w=", w);
        h = Grab(line, "h=", h);
        hidden = Grab(line, "hidden=", 0) != 0;
        checklist = line.Contains("mode=check", StringComparison.Ordinal);
        color = Grab(line, "color=", color);
    }

    public static List<string> ParseTags(string md)
    {
        var first = md.Split('\n', 2)[0];
        var tags = new List<string>();
        for (var i = 0; i < first.Length;)
        {
            while (i < first.Length && char.IsWhiteSpace(first[i])) i++;
            if (i >= first.Length || first[i] != '#') break;
            i++;
            var start = i;
            while (i < first.Length && !char.IsWhiteSpace(first[i]) && first[i] != '#') i++;
            if (i > start)
            {
                var t = first[start..i].ToLowerInvariant();
                if (!tags.Contains(t)) tags.Add(t);
            }
        }
        return tags;
    }

    public static bool LooksLikeChecklist(string md)
    {
        var lines = md.Split('\n');
        var checks = 0;
        var nonempty = 0;
        foreach (var line in lines)
        {
            var t = line.TrimEnd('\r');
            if (string.IsNullOrWhiteSpace(t)) continue;
            nonempty++;
            if (Regex.IsMatch(t, @"^- \[[ xX]\]") || t.StartsWith("- []", StringComparison.Ordinal))
                checks++;
        }
        return checks > 0 && checks * 2 >= nonempty;
    }

    public static List<CheckItemModel> ParseChecklist(string md)
    {
        var items = new List<CheckItemModel>();
        foreach (var raw in md.Replace("\r\n", "\n").Split('\n'))
        {
            var line = raw.TrimEnd('\r');
            var m = Regex.Match(line, @"^- \[([xX ])\]\s?(.*)$");
            if (m.Success)
            {
                items.Add(new CheckItemModel
                {
                    Done = m.Groups[1].Value is "x" or "X",
                    Text = m.Groups[2].Value
                });
                continue;
            }
            m = Regex.Match(line, @"^- \[\]\s?(.*)$");
            if (m.Success)
            {
                items.Add(new CheckItemModel { Done = false, Text = m.Groups[1].Value });
                continue;
            }
            // Continuation of previous checklist item (Shift+Enter newlines).
            if (items.Count > 0 && !string.IsNullOrWhiteSpace(line))
            {
                items[^1].Text += "\n" + line;
                continue;
            }
            if (!string.IsNullOrEmpty(line) || items.Count == 0)
                items.Add(new CheckItemModel { Text = line });
        }
        if (items.Count == 0) items.Add(new CheckItemModel());
        return items;
    }

    public static string ChecklistToMd(IEnumerable<CheckItemModel> items)
    {
        var sb = new StringBuilder();
        foreach (var it in items)
        {
            var text = it.Text.Replace("\r\n", "\n").Replace('\r', '\n');
            var lines = text.Split('\n');
            sb.Append(it.Done ? "- [x] " : "- [ ] ");
            sb.Append(lines[0]);
            sb.Append('\n');
            for (var i = 1; i < lines.Length; i++)
            {
                sb.Append(lines[i]);
                sb.Append('\n');
            }
        }
        return sb.ToString().TrimEnd('\n');
    }
}
