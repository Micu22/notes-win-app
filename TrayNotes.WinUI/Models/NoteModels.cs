using Microsoft.UI.Xaml.Media;
using Windows.UI;

namespace TrayNotes_WinUI.Models;

public enum NoteMode
{
    Text,
    Checklist
}

public sealed class NotePalette
{
    public required Color Top { get; init; }
    public required Color Bottom { get; init; }
    public required Color Text { get; init; }
    public required Color Dot { get; init; }

    public SolidColorBrush TextBrush => new(Text);
    public SolidColorBrush DotBrush => new(Dot);

    public LinearGradientBrush PaperBrush => new()
    {
        StartPoint = new Windows.Foundation.Point(0, 0),
        EndPoint = new Windows.Foundation.Point(0, 1),
        GradientStops =
        {
            new GradientStop { Color = WithAlpha(Top, 0xEE), Offset = 0 },
            new GradientStop { Color = WithAlpha(Bottom, 0xDD), Offset = 1 },
        }
    };

    public static Color WithAlpha(Color c, byte a) => Color.FromArgb(a, c.R, c.G, c.B);
}

public static class NotePalettes
{
    public static readonly NotePalette[] All =
    [
        new() { Top = Color.FromArgb(255, 255, 242, 170), Bottom = Color.FromArgb(255, 255, 210, 85),  Text = Color.FromArgb(255, 40, 30, 8),   Dot = Color.FromArgb(255, 255, 200, 40) },
        new() { Top = Color.FromArgb(255, 255, 220, 215), Bottom = Color.FromArgb(255, 245, 155, 145), Text = Color.FromArgb(255, 60, 20, 16),  Dot = Color.FromArgb(255, 230, 80, 70) },
        new() { Top = Color.FromArgb(255, 225, 245, 215), Bottom = Color.FromArgb(255, 155, 205, 140), Text = Color.FromArgb(255, 20, 45, 20),  Dot = Color.FromArgb(255, 80, 170, 70) },
        new() { Top = Color.FromArgb(255, 215, 230, 250), Bottom = Color.FromArgb(255, 145, 175, 225), Text = Color.FromArgb(255, 16, 30, 60),  Dot = Color.FromArgb(255, 70, 120, 210) },
        new() { Top = Color.FromArgb(255, 240, 240, 240), Bottom = Color.FromArgb(255, 185, 185, 185), Text = Color.FromArgb(255, 30, 30, 30),  Dot = Color.FromArgb(255, 120, 120, 120) },
    ];

    public static NotePalette Get(int index)
    {
        if (index < 0 || index >= All.Length) index = 0;
        return All[index];
    }
}

public sealed class CheckItemModel
{
    public bool Done { get; set; }
    public string Text { get; set; } = "";
}

public sealed class NoteDocument
{
    public string Id { get; set; } = "";
    public string Path { get; set; } = "";
    public NoteMode Mode { get; set; } = NoteMode.Text;
    public int ColorIdx { get; set; }
    public bool Hidden { get; set; }
    public double X { get; set; }
    public double Y { get; set; }
    public double Width { get; set; } = 320;
    public double Height { get; set; } = 280;
    public string Body { get; set; } = "";
    public List<string> Tags { get; set; } = [];
    public List<CheckItemModel> Items { get; set; } = [];

    public NotePalette Palette => NotePalettes.Get(ColorIdx);
}
