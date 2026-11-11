using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Controls;
using Microsoft.UI.Xaml.Media;
using TrayNotes_WinUI.Models;
using TrayNotes_WinUI.Services;
using Windows.Graphics;

namespace TrayNotes_WinUI;

public sealed partial class BrowserWindow : Window
{
    private readonly Dictionary<string, NoteEditor> _editors = new(StringComparer.OrdinalIgnoreCase);
    private bool _rebuilding;
    private string _lastFilter = "";

    public BrowserWindow()
    {
        InitializeComponent();
        AppWindow.Resize(new SizeInt32(1000, 740));
        Title = "Sticky Notes";

        NoteManager.Instance.NotesChanged += OnNotesChanged;
        Closed += (_, _) =>
        {
            NoteManager.Instance.NotesChanged -= OnNotesChanged;
            foreach (var ed in _editors.Values)
                ed.Flush();
            _editors.Clear();
        };

        Rebuild(forceContentReload: true);
    }

    private void OnNotesChanged()
    {
        DispatcherQueue.TryEnqueue(() =>
        {
            if (_rebuilding) return;
            Rebuild(forceContentReload: false);
        });
    }

    private void Rebuild(bool forceContentReload)
    {
        _rebuilding = true;
        try
        {
            foreach (var ed in _editors.Values)
                ed.Flush();

            var filter = (FilterBox.Text ?? "").Trim().TrimStart('#').ToLowerInvariant();
            var filterChanged = !string.Equals(filter, _lastFilter, StringComparison.Ordinal);
            _lastFilter = filter;

            var notes = NoteManager.Instance.Notes
                .Where(n => MatchesFilter(n, filter))
                .OrderByDescending(n => n.Id)
                .ToList();

            var keep = new HashSet<string>(notes.Select(n => n.Id), StringComparer.OrdinalIgnoreCase);
            foreach (var id in _editors.Keys.Where(id => !keep.Contains(id)).ToList())
            {
                if (_editors.Remove(id, out var dead) && dead.Parent is Border oldCard)
                    NotesHost.Children.Remove(oldCard);
                else if (dead != null)
                    NotesHost.Children.Remove(dead);
            }

            NotesHost.Children.Clear();

            foreach (var note in notes)
            {
                if (!_editors.TryGetValue(note.Id, out var editor))
                {
                    editor = CreateCard(note);
                    _editors[note.Id] = editor;
                }
                else if (forceContentReload || filterChanged)
                {
                    if (!NoteManager.Instance.IsWindowOpen(note.Id))
                        editor.ReloadFromModel();
                    else
                        editor.RefreshExternalOverlay();
                }
                else
                {
                    editor.RefreshExternalOverlay();
                }

                NotesHost.Children.Add(WrapCard(editor, note));
            }
        }
        finally
        {
            _rebuilding = false;
        }
    }

    private static bool MatchesFilter(NoteDocument n, string filter)
    {
        if (string.IsNullOrEmpty(filter)) return true;
        if (n.Id.Contains(filter, StringComparison.OrdinalIgnoreCase)) return true;
        if (n.Tags.Any(t => t.Contains(filter, StringComparison.OrdinalIgnoreCase))) return true;
        if (n.Body.Contains(filter, StringComparison.OrdinalIgnoreCase)) return true;
        if (n.Mode == NoteMode.Checklist &&
            n.Items.Any(i => i.Text.Contains(filter, StringComparison.OrdinalIgnoreCase)))
            return true;
        return false;
    }

    private static Border WrapCard(NoteEditor editor, NoteDocument note)
    {
        if (editor.Parent is Border existing)
            existing.Child = null;
        else if (editor.Parent is Panel panel)
            panel.Children.Remove(editor);

        return new Border
        {
            Margin = new Thickness(8),
            CornerRadius = new CornerRadius(12),
            BorderThickness = new Thickness(1),
            BorderBrush = new SolidColorBrush(Windows.UI.Color.FromArgb(36, 0, 0, 0)),
            Background = note.Palette.DotBrush, // subtle color edge via outer; editor paints paper
            Child = new Border
            {
                CornerRadius = new CornerRadius(11),
                Background = note.Palette.PaperBrush,
                Child = editor,
                Margin = new Thickness(0, 0, 0, 0),
            }
        };
    }

    private NoteEditor CreateCard(NoteDocument note)
    {
        var editor = new NoteEditor
        {
            Width = 284,
            Height = 292,
        };
        editor.Bind(
            note,
            embedded: true,
            closeAction: () =>
            {
                editor.Flush();
                NoteManager.Instance.HideNote(note);
            });
        editor.PopOutRequested += ed =>
        {
            if (ed.Note == null) return;
            ed.Flush();
            NoteManager.Instance.ShowNote(ed.Note);
            ed.RefreshExternalOverlay();
        };
        editor.Changed += ed =>
        {
            if (ed.Note == null) return;
            // Outer border color tip + inner paper
            if (ed.Parent is Border inner && inner.Parent is Border outer)
            {
                outer.Background = ed.Note.Palette.DotBrush;
                inner.Background = ed.Note.Palette.PaperBrush;
            }
        };
        return editor;
    }

    private void FilterBox_TextChanged(AutoSuggestBox sender, AutoSuggestBoxTextChangedEventArgs args)
    {
        if (args.Reason == AutoSuggestionBoxTextChangeReason.UserInput)
            Rebuild(forceContentReload: true);
    }

    private void NewBtn_Click(object sender, RoutedEventArgs e)
    {
        var note = NoteManager.Instance.CreateNote(openWindow: false);
        Rebuild(forceContentReload: true);
        if (_editors.TryGetValue(note.Id, out var ed))
            ed.Focus(FocusState.Programmatic);
    }
}
