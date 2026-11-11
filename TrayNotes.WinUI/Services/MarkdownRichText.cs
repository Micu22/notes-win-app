using Microsoft.UI.Text;
using Microsoft.UI.Xaml.Controls;

namespace TrayNotes_WinUI.Services;

/// <summary>Load/save Keep-style markdown (**bold**, - bullets, 1. numbers) into RichEditBox.</summary>
public static class MarkdownRichText
{
    private const char Bullet = '\u2022';

    public static void Load(RichEditBox box, string md, Windows.UI.Color textColor)
    {
        box.Document.SetText(TextSetOptions.None, "");
        var bolds = new List<(int A, int B)>();
        var numbered = new List<(int Cp, int Start)>();
        var display = new System.Text.StringBuilder();

        var lines = md.Replace("\r\n", "\n").Replace('\r', '\n').Split('\n');
        for (var li = 0; li < lines.Length; li++)
        {
            var line = lines[li];
            var body = line;
            var numbering = false;
            var numStart = 1;

            if (body.StartsWith("- ", StringComparison.Ordinal) && !body.StartsWith("- [", StringComparison.Ordinal))
            {
                display.Append(Bullet).Append(' ');
                body = body[2..];
            }
            else
            {
                var dot = body.IndexOf(". ", StringComparison.Ordinal);
                if (dot > 0 && body.AsSpan(0, dot).ToArray().All(char.IsDigit))
                {
                    numbering = true;
                    numStart = int.TryParse(body[..dot], out var n) ? n : 1;
                    body = body[(dot + 2)..];
                }
            }

            var lineCp = display.Length;
            if (numbering) numbered.Add((lineCp, numStart));

            for (var j = 0; j < body.Length;)
            {
                if (j + 1 < body.Length && body[j] == '*' && body[j + 1] == '*')
                {
                    var close = body.IndexOf("**", j + 2, StringComparison.Ordinal);
                    if (close >= 0)
                    {
                        var a = display.Length;
                        display.Append(body, j + 2, close - (j + 2));
                        bolds.Add((a, display.Length));
                        j = close + 2;
                        continue;
                    }
                }
                display.Append(body[j++]);
            }

            if (li < lines.Length - 1) display.Append('\r');
        }

        box.Document.SetText(TextSetOptions.None, display.ToString());
        box.Document.Selection.SetRange(0, display.Length);
        box.Document.Selection.CharacterFormat.ForegroundColor = textColor;
        box.Document.Selection.CharacterFormat.Bold = FormatEffect.Off;
        box.Document.Selection.SetRange(display.Length, display.Length);

        foreach (var (cp, start) in numbered)
        {
            box.Document.Selection.SetRange(cp, cp);
            box.Document.Selection.ParagraphFormat.ListType = MarkerType.Arabic;
            box.Document.Selection.ParagraphFormat.ListStart = start;
        }

        foreach (var (a, b) in bolds)
        {
            if (b <= a) continue;
            box.Document.Selection.SetRange(a, b);
            box.Document.Selection.CharacterFormat.Bold = FormatEffect.On;
        }

        box.Document.Selection.SetRange(display.Length, display.Length);
        box.Document.GetText(TextGetOptions.None, out var _);
    }

    public static void LoadPlainBold(RichEditBox box, string md, Windows.UI.Color textColor)
    {
        var bolds = new List<(int A, int B)>();
        var display = new System.Text.StringBuilder();
        for (var j = 0; j < md.Length;)
        {
            if (j + 1 < md.Length && md[j] == '*' && md[j + 1] == '*')
            {
                var close = md.IndexOf("**", j + 2, StringComparison.Ordinal);
                if (close >= 0)
                {
                    var a = display.Length;
                    display.Append(md, j + 2, close - (j + 2));
                    bolds.Add((a, display.Length));
                    j = close + 2;
                    continue;
                }
            }
            display.Append(md[j++]);
        }

        box.Document.SetText(TextSetOptions.None, display.ToString());
        box.Document.Selection.SetRange(0, display.Length);
        box.Document.Selection.CharacterFormat.ForegroundColor = textColor;
        box.Document.Selection.CharacterFormat.Bold = FormatEffect.Off;

        foreach (var (a, b) in bolds)
        {
            if (b <= a) continue;
            box.Document.Selection.SetRange(a, b);
            box.Document.Selection.CharacterFormat.Bold = FormatEffect.On;
        }
        box.Document.Selection.SetRange(display.Length, display.Length);
    }

    public static string Save(RichEditBox box)
    {
        box.Document.GetText(TextGetOptions.UseCrlf, out var raw);
        raw = raw.Replace("\r\n", "\n").TrimEnd('\0');
        if (raw.EndsWith('\r')) raw = raw.TrimEnd('\r');

        var sb = new System.Text.StringBuilder();
        var lines = raw.Replace('\r', '\n').Split('\n');
        var autoNum = 1;
        var cp = 0;

        for (var li = 0; li < lines.Length; li++)
        {
            var line = lines[li];
            var contentStart = 0;
            var lineCp = cp;

            box.Document.Selection.SetRange(lineCp, lineCp);
            var listType = box.Document.Selection.ParagraphFormat.ListType;

            if (line.StartsWith($"{Bullet} ", StringComparison.Ordinal))
            {
                sb.Append("- ");
                contentStart = 2;
            }
            else if (listType == MarkerType.Arabic)
            {
                sb.Append(autoNum++).Append(". ");
            }
            else
            {
                autoNum = 1;
            }

            AppendStyled(box, line, lineCp + contentStart, contentStart, line.Length, sb);
            if (li < lines.Length - 1) sb.Append('\n');
            cp += line.Length + 1; // +1 for the newline in document
        }

        return sb.ToString();
    }

    public static string SavePlainBold(RichEditBox box)
    {
        box.Document.GetText(TextGetOptions.UseCrlf, out var raw);
        raw = raw.Replace("\r\n", "\n").TrimEnd('\0', '\r', '\n');
        var sb = new System.Text.StringBuilder();
        AppendStyled(box, raw, 0, 0, raw.Length, sb);
        return sb.ToString();
    }

    private static void AppendStyled(RichEditBox box, string line, int docBase, int from, int to,
        System.Text.StringBuilder sb)
    {
        var i = from;
        while (i < to)
        {
            box.Document.Selection.SetRange(docBase + (i - from), docBase + (i - from) + 1);
            var bold = box.Document.Selection.CharacterFormat.Bold == FormatEffect.On;
            var j = i + 1;
            while (j < to)
            {
                box.Document.Selection.SetRange(docBase + (j - from), docBase + (j - from) + 1);
                var b = box.Document.Selection.CharacterFormat.Bold == FormatEffect.On;
                if (b != bold) break;
                j++;
            }
            if (bold) sb.Append("**");
            sb.Append(line, i, j - i);
            if (bold) sb.Append("**");
            i = j;
        }
    }

    public static void ToggleBold(RichEditBox box)
    {
        var sel = box.Document.Selection;
        if (sel.Length == 0)
        {
            var next = sel.CharacterFormat.Bold == FormatEffect.On ? FormatEffect.Off : FormatEffect.On;
            sel.CharacterFormat.Bold = next;
        }
        else
        {
            sel.CharacterFormat.Bold = FormatEffect.Toggle;
        }
    }
}
