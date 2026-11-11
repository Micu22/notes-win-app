using Microsoft.UI;
using Microsoft.UI.Input;
using Microsoft.UI.Text;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Input;
using Microsoft.UI.Xaml.Media;
using Microsoft.UI.Xaml.Shapes;
using TrayNotes_WinUI.Models;
using TrayNotes_WinUI.Services;
using Windows.System;
using Windows.UI;

namespace TrayNotes_WinUI;

public sealed partial class NoteEditor : UserControl
{
    private NoteDocument? _note;
    private readonly DispatcherTimer _saveTimer;
    private bool _suppress;
    private bool _embedded;
    private bool _updateGeometryOnSave;
    private Func<(int X, int Y, int W, int H)>? _geometryProvider;
    private Action? _closeAction;

    public NoteDocument? Note => _note;
    public FrameworkElement DragRegion => TitleBar;

    public event Action<NoteEditor>? PopOutRequested;
    public event Action<NoteEditor>? Changed;

    public NoteEditor()
    {
        InitializeComponent();
        _saveTimer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(450) };
        _saveTimer.Tick += (_, _) =>
        {
            _saveTimer.Stop();
            FlushSave(forceBody: false);
        };
        Unloaded += (_, _) =>
        {
            _saveTimer.Stop();
            FlushSave(forceBody: true);
        };
    }

    /// <param name="embedded">Gallery card: pop-out button, rounded paper, no window geometry save.</param>
    public void Bind(
        NoteDocument note,
        bool embedded,
        Func<(int X, int Y, int W, int H)>? geometryProvider = null,
        Action? closeAction = null)
    {
        _note = note;
        _embedded = embedded;
        _updateGeometryOnSave = !embedded;
        _geometryProvider = geometryProvider;
        _closeAction = closeAction ?? (() => NoteManager.Instance.HideNote(note));

        PopOutBtn.Visibility = embedded ? Visibility.Visible : Visibility.Collapsed;
        PaperOverlay.CornerRadius = embedded ? new CornerRadius(10) : new CornerRadius(0);
        RootGrid.CornerRadius = embedded ? new CornerRadius(10) : new CornerRadius(0);

        StyleTransparentEditors();
        ApplyTheme();
        BuildColorDots();
        LoadContent();
        RefreshExternalOverlay();
    }

    public void ReloadFromModel()
    {
        if (_note == null) return;
        ApplyTheme();
        BuildColorDots();
        LoadContent();
        RefreshExternalOverlay();
    }

    public void RefreshExternalOverlay()
    {
        if (_note == null || !_embedded)
        {
            ExternalEditOverlay.Visibility = Visibility.Collapsed;
            return;
        }
        var open = NoteManager.Instance.IsWindowOpen(_note.Id);
        ExternalEditOverlay.Visibility = open ? Visibility.Visible : Visibility.Collapsed;
        EditorHost.IsHitTestVisible = !open;
        BodyEdit.IsEnabled = !open;
        ModeBtn.IsEnabled = !open;
        foreach (var child in ColorDots.Children)
            if (child is UIElement u) u.IsHitTestVisible = !open;
    }

    public void Flush(bool forceBody = true) => FlushSave(forceBody);

    private void ApplyTheme()
    {
        if (_note == null) return;
        var pal = _note.Palette;
        PaperOverlay.Background = pal.PaperBrush;
        if (_embedded)
            RootGrid.Background = new SolidColorBrush(Colors.Transparent);
        TitleLabel.Foreground = pal.TextBrush;
        ModeBtn.Foreground = pal.TextBrush;
        ModeBtn.BorderBrush = new SolidColorBrush(Color.FromArgb(80, pal.Text.R, pal.Text.G, pal.Text.B));
        CloseBtn.Foreground = pal.TextBrush;
        PopOutBtn.Foreground = pal.TextBrush;
        BodyEdit.Foreground = pal.TextBrush;
        TitleLabel.Text = _note.Tags.Count > 0
            ? "#" + _note.Tags[0] + (_note.Tags.Count > 1 ? " …" : "")
            : (_note.Mode == NoteMode.Checklist ? "Checklist" : "Sticky");
        ModeBtn.Content = _note.Mode == NoteMode.Checklist ? "Checklist" : "Text";
    }

    private void StyleTransparentEditors()
    {
        void Clear(FrameworkElement el)
        {
            el.Resources["TextControlBackground"] = new SolidColorBrush(Colors.Transparent);
            el.Resources["TextControlBackgroundPointerOver"] = new SolidColorBrush(Colors.Transparent);
            el.Resources["TextControlBackgroundFocused"] = new SolidColorBrush(Colors.Transparent);
            el.Resources["TextControlBackgroundDisabled"] = new SolidColorBrush(Colors.Transparent);
            el.Resources["TextControlBorderBrush"] = new SolidColorBrush(Colors.Transparent);
            el.Resources["TextControlBorderBrushPointerOver"] = new SolidColorBrush(Colors.Transparent);
            el.Resources["TextControlBorderBrushFocused"] = new SolidColorBrush(Colors.Transparent);
            el.Resources["TextControlElevationBorderFocusedBrush"] = new SolidColorBrush(Colors.Transparent);
        }
        Clear(BodyEdit);
        BodyEdit.Background = new SolidColorBrush(Colors.Transparent);
        BodyEdit.BorderThickness = new Thickness(0);
    }

    private void BuildColorDots()
    {
        if (_note == null) return;
        ColorDots.Children.Clear();
        for (var i = 0; i < NotePalettes.All.Length; i++)
        {
            var idx = i;
            var pal = NotePalettes.All[i];
            var ellipse = new Ellipse
            {
                Width = 16,
                Height = 16,
                Fill = pal.DotBrush,
                Stroke = idx == _note.ColorIdx
                    ? pal.TextBrush
                    : new SolidColorBrush(Color.FromArgb(60, 0, 0, 0)),
                StrokeThickness = idx == _note.ColorIdx ? 2 : 1,
            };
            var hit = new Border
            {
                Width = 22,
                Height = 22,
                Background = new SolidColorBrush(Colors.Transparent),
                Child = ellipse,
                VerticalAlignment = VerticalAlignment.Center,
            };
            hit.PointerPressed += (_, e) =>
            {
                e.Handled = true;
                if (_note == null) return;
                _note.ColorIdx = idx;
                BuildColorDots();
                ApplyTheme();
                ScheduleSave();
                Changed?.Invoke(this);
            };
            ColorDots.Children.Add(hit);
        }
    }

    private void LoadContent()
    {
        if (_note == null) return;
        _suppress = true;
        if (_note.Mode == NoteMode.Checklist)
        {
            BodyEdit.Visibility = Visibility.Collapsed;
            CheckScroll.Visibility = Visibility.Visible;
            RebuildChecklist();
        }
        else
        {
            CheckScroll.Visibility = Visibility.Collapsed;
            BodyEdit.Visibility = Visibility.Visible;
            MarkdownRichText.Load(BodyEdit, _note.Body, _note.Palette.Text);
        }
        _suppress = false;
    }

    private void RebuildChecklist()
    {
        if (_note == null) return;
        CheckList.Children.Clear();
        if (_note.Items.Count == 0)
            _note.Items.Add(new CheckItemModel());

        foreach (var item in _note.Items)
        {
            var row = new Grid { Margin = new Thickness(4, 2, 4, 2) };
            row.ColumnDefinitions.Add(new ColumnDefinition { Width = GridLength.Auto });
            row.ColumnDefinitions.Add(new ColumnDefinition { Width = new GridLength(1, GridUnitType.Star) });

            var check = new CheckBox
            {
                IsChecked = item.Done,
                MinWidth = 0,
                MinHeight = 0,
                Padding = new Thickness(0),
                Margin = new Thickness(0, 4, 6, 0),
                VerticalAlignment = VerticalAlignment.Top,
            };
            check.Checked += (_, _) => { item.Done = true; ScheduleSave(); };
            check.Unchecked += (_, _) => { item.Done = false; ScheduleSave(); };

            var edit = new RichEditBox
            {
                BorderThickness = new Thickness(0),
                Background = new SolidColorBrush(Colors.Transparent),
                AcceptsReturn = true,
                TextWrapping = TextWrapping.Wrap,
                MinHeight = 28,
                Tag = item,
                Foreground = _note.Palette.TextBrush,
            };
            StyleRowEdit(edit);
            MarkdownRichText.LoadPlainBold(edit, item.Text, _note.Palette.Text);
            edit.TextChanged += ChecklistEdit_TextChanged;
            edit.PreviewKeyDown += ChecklistEdit_PreviewKeyDown;
            edit.LostFocus += Editor_LostFocus;

            Grid.SetColumn(check, 0);
            Grid.SetColumn(edit, 1);
            row.Children.Add(check);
            row.Children.Add(edit);
            CheckList.Children.Add(row);
        }
    }

    private void StyleRowEdit(RichEditBox edit)
    {
        edit.Resources["TextControlBackground"] = new SolidColorBrush(Colors.Transparent);
        edit.Resources["TextControlBackgroundPointerOver"] = new SolidColorBrush(Colors.Transparent);
        edit.Resources["TextControlBackgroundFocused"] = new SolidColorBrush(Colors.Transparent);
        edit.Resources["TextControlBorderBrush"] = new SolidColorBrush(Colors.Transparent);
        edit.Resources["TextControlBorderBrushFocused"] = new SolidColorBrush(Colors.Transparent);
        edit.Resources["TextControlElevationBorderFocusedBrush"] = new SolidColorBrush(Colors.Transparent);
    }

    private void ChecklistEdit_TextChanged(object sender, RoutedEventArgs e)
    {
        if (_suppress || sender is not RichEditBox edit || edit.Tag is not CheckItemModel item) return;
        edit.Document.GetText(TextGetOptions.UseCrlf, out var t);
        item.Text = t.TrimEnd('\0', '\r', '\n');
        ScheduleSave();
    }

    private void ChecklistEdit_PreviewKeyDown(object sender, KeyRoutedEventArgs e)
    {
        if (_note == null || sender is not RichEditBox edit || edit.Tag is not CheckItemModel item) return;

        if (e.Key == VirtualKey.B && IsCtrlDown())
        {
            MarkdownRichText.ToggleBold(edit);
            e.Handled = true;
            ScheduleSave();
            return;
        }

        if (e.Key == VirtualKey.Back)
        {
            edit.Document.GetText(TextGetOptions.None, out var t);
            t = t.TrimEnd('\0', '\r', '\n');
            var sel = edit.Document.Selection;
            if (t.Length == 0 && sel.StartPosition == 0 && sel.EndPosition == 0 && _note.Items.Count > 1)
            {
                e.Handled = true;
                var idx = _note.Items.IndexOf(item);
                if (idx < 0) return;
                _note.Items.RemoveAt(idx);
                RebuildChecklist();
                FocusChecklistRow(Math.Max(0, idx - 1));
                ScheduleSave();
            }
            return;
        }

        if (e.Key != VirtualKey.Enter) return;

        if (IsShiftDown())
        {
            e.Handled = true;
            edit.Document.Selection.TypeText("\r");
            ScheduleSave();
            return;
        }

        e.Handled = true;
        item.Text = MarkdownRichText.SavePlainBold(edit);
        var idx2 = _note.Items.IndexOf(item);
        if (idx2 < 0) idx2 = _note.Items.Count - 1;
        _note.Items.Insert(idx2 + 1, new CheckItemModel());
        RebuildChecklist();
        FocusChecklistRow(idx2 + 1);
        ScheduleSave();
    }

    private void FocusChecklistRow(int index)
    {
        if (index < 0 || index >= CheckList.Children.Count) return;
        if (CheckList.Children[index] is not Grid row) return;
        foreach (var child in row.Children)
        {
            if (child is RichEditBox box)
            {
                box.Focus(FocusState.Programmatic);
                box.Document.Selection.SetRange(int.MaxValue, int.MaxValue);
                break;
            }
        }
    }

    private void BodyEdit_TextChanged(object sender, RoutedEventArgs e)
    {
        if (_suppress) return;
        ScheduleSave();
        TryConvertToChecklist();
    }

    private void TryConvertToChecklist()
    {
        if (_note == null) return;
        BodyEdit.Document.GetText(TextGetOptions.UseCrlf, out var plain);
        plain = plain.Replace("\r\n", "\n").Replace('\r', '\n');
        var caret = BodyEdit.Document.Selection.StartPosition;
        if (caret < 0 || caret > plain.Length) return;
        var lineStart = plain.LastIndexOf('\n', Math.Max(0, caret - 1)) + 1;
        if (lineStart < 0) lineStart = 0;
        var prefix = plain[lineStart..Math.Min(caret, plain.Length)];
        if (!prefix.EndsWith("[]", StringComparison.Ordinal)) return;

        _suppress = true;
        var body = MarkdownRichText.Save(BodyEdit).Replace("[]", "", StringComparison.Ordinal);
        _note.Body = body;
        _note.Mode = NoteMode.Checklist;
        _note.Items = string.IsNullOrWhiteSpace(body)
            ? [new CheckItemModel()]
            : body.Split('\n').Select(l => new CheckItemModel
            {
                Text = l.StartsWith("- [", StringComparison.Ordinal)
                    ? NoteStore.ParseChecklist(l).FirstOrDefault()?.Text ?? l
                    : l.TrimStart('-', ' ')
            }).ToList();
        if (_note.Items.Count == 0) _note.Items.Add(new CheckItemModel());
        ApplyTheme();
        LoadContent();
        _suppress = false;
        ScheduleSave();
    }

    private void BodyEdit_PreviewKeyDown(object sender, KeyRoutedEventArgs e)
    {
        if (e.Key == VirtualKey.B && IsCtrlDown())
        {
            MarkdownRichText.ToggleBold(BodyEdit);
            e.Handled = true;
            ScheduleSave();
        }
    }

    private static bool IsCtrlDown() =>
        InputKeyboardSource.GetKeyStateForCurrentThread(VirtualKey.Control)
            .HasFlag(Windows.UI.Core.CoreVirtualKeyStates.Down);

    private static bool IsShiftDown() =>
        InputKeyboardSource.GetKeyStateForCurrentThread(VirtualKey.Shift)
            .HasFlag(Windows.UI.Core.CoreVirtualKeyStates.Down);

    private void ModeBtn_Click(object sender, RoutedEventArgs e)
    {
        if (_note == null) return;
        FlushSave(forceBody: true);
        if (_note.Mode == NoteMode.Text)
        {
            _note.Body = MarkdownRichText.Save(BodyEdit);
            _note.Mode = NoteMode.Checklist;
            _note.Items = NoteStore.LooksLikeChecklist(_note.Body)
                ? NoteStore.ParseChecklist(_note.Body)
                : _note.Body.Split('\n').Select(l => new CheckItemModel { Text = l }).ToList();
            if (_note.Items.Count == 0) _note.Items.Add(new CheckItemModel());
        }
        else
        {
            SyncChecklistFromUi();
            _note.Body = NoteStore.ChecklistToMd(_note.Items);
            _note.Mode = NoteMode.Text;
        }
        ApplyTheme();
        LoadContent();
        ScheduleSave();
        Changed?.Invoke(this);
    }

    private void SyncChecklistFromUi()
    {
        if (_note == null) return;
        for (var i = 0; i < CheckList.Children.Count && i < _note.Items.Count; i++)
        {
            if (CheckList.Children[i] is not Grid row) continue;
            foreach (var child in row.Children)
            {
                if (child is RichEditBox box)
                {
                    _note.Items[i].Text = MarkdownRichText.SavePlainBold(box);
                    break;
                }
            }
        }
    }

    private void CloseBtn_Click(object sender, RoutedEventArgs e)
    {
        FlushSave(forceBody: true);
        _closeAction?.Invoke();
    }

    private void PopOutBtn_Click(object sender, RoutedEventArgs e)
    {
        FlushSave(forceBody: true);
        PopOutRequested?.Invoke(this);
    }

    private void FocusWindowBtn_Click(object sender, RoutedEventArgs e)
    {
        if (_note != null)
            NoteManager.Instance.ShowNote(_note);
    }

    private void ScheduleSave()
    {
        _saveTimer.Stop();
        _saveTimer.Start();
    }

    private void FlushSave(bool forceBody)
    {
        if (_note == null) return;
        try
        {
            if (_updateGeometryOnSave && _geometryProvider != null)
            {
                var (x, y, w, h) = _geometryProvider();
                _note.X = x;
                _note.Y = y;
                _note.Width = w;
                _note.Height = h;
            }

            if (_note.Mode == NoteMode.Checklist)
            {
                if (forceBody) SyncChecklistFromUi();
            }
            else if (forceBody || BodyEdit.FocusState == FocusState.Unfocused)
            {
                _note.Body = MarkdownRichText.Save(BodyEdit);
                _note.Tags = NoteStore.ParseTags(_note.Body);
                TitleLabel.Text = _note.Tags.Count > 0
                    ? "#" + _note.Tags[0] + (_note.Tags.Count > 1 ? " …" : "")
                    : "Sticky";
            }

            NoteManager.Instance.Persist(_note);
            Changed?.Invoke(this);
        }
        catch { /* ignore IO */ }
    }

    private void Editor_LostFocus(object sender, RoutedEventArgs e) => FlushSave(forceBody: true);
}
