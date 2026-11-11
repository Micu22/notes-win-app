using TrayNotes_WinUI.Models;

namespace TrayNotes_WinUI.Services;

public sealed class NoteManager
{
    public static NoteManager Instance { get; } = new();

    private readonly Dictionary<string, TrayNotes_WinUI.NoteWindow> _windows = new(StringComparer.OrdinalIgnoreCase);
    private TrayNotes_WinUI.BrowserWindow? _browser;

    public IReadOnlyList<NoteDocument> Notes { get; private set; } = [];

    public event Action? NotesChanged;

    public void LoadFromDisk()
    {
        Notes = NoteStore.LoadAll();
        NotesChanged?.Invoke();
    }

    public void OpenVisibleNotes()
    {
        foreach (var note in Notes.Where(n => !n.Hidden))
            ShowNote(note);
    }

    public NoteDocument CreateNote(bool openWindow = true)
    {
        var note = NoteStore.CreateNew();
        var list = Notes.ToList();
        list.Add(note);
        Notes = list;
        NotesChanged?.Invoke();
        if (openWindow)
            ShowNote(note);
        return note;
    }

    public bool IsWindowOpen(string noteId) => _windows.ContainsKey(noteId);

    public void ShowNote(NoteDocument note)
    {
        note.Hidden = false;
        NoteStore.Save(note);

        if (_windows.TryGetValue(note.Id, out var existing))
        {
            existing.Activate();
            NotesChanged?.Invoke();
            return;
        }

        var win = new TrayNotes_WinUI.NoteWindow(note);
        _windows[note.Id] = win;
        win.Closed += (_, _) =>
        {
            _windows.Remove(note.Id);
            NotesChanged?.Invoke();
        };
        win.Activate();
        NotesChanged?.Invoke();
    }

    public void HideNote(NoteDocument note)
    {
        note.Hidden = true;
        NoteStore.Save(note);
        if (_windows.TryGetValue(note.Id, out var win))
            win.Close();
        NotesChanged?.Invoke();
    }

    public void ShowAll()
    {
        foreach (var note in Notes)
            ShowNote(note);
    }

    public void HideAll()
    {
        foreach (var id in _windows.Keys.ToList())
        {
            var note = Notes.FirstOrDefault(n => n.Id == id);
            if (note != null) HideNote(note);
        }
    }

    public void OpenBrowser()
    {
        if (_browser != null)
        {
            _browser.Activate();
            return;
        }
        _browser = new TrayNotes_WinUI.BrowserWindow();
        _browser.Closed += (_, _) => _browser = null;
        _browser.Activate();
    }

    public void Persist(NoteDocument note) => NoteStore.Save(note);

    public void Shutdown()
    {
        foreach (var win in _windows.Values.ToList())
        {
            try { win.FlushAndClose(); } catch { /* ignore */ }
        }
        _windows.Clear();
        _browser?.Close();
    }
}
