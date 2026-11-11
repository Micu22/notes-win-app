using Microsoft.UI.Xaml;
using TrayNotes_WinUI.Services;

namespace TrayNotes_WinUI;

public partial class App : Application
{
    private Window? _trayHost;
    private TrayService? _tray;

    public App()
    {
        InitializeComponent();
    }

    protected override void OnLaunched(Microsoft.UI.Xaml.LaunchActivatedEventArgs args)
    {
        var desktopNotes = Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory), "Notes");
        if (Directory.Exists(desktopNotes))
            NoteStore.SetNotesDirectory(desktopNotes);

        NoteManager.Instance.LoadFromDisk();

        _trayHost = new MainWindow();
        _trayHost.AppWindow.IsShownInSwitchers = false;
        _trayHost.Activate();
        _trayHost.AppWindow.Hide();

        _tray = new TrayService(_trayHost);
        _tray.NewNoteRequested += () => NoteManager.Instance.CreateNote();
        _tray.ShowAllRequested += () => NoteManager.Instance.OpenBrowser();
        _tray.HideAllRequested += () => NoteManager.Instance.HideAll();
        _tray.ExitRequested += Quit;
        _tray.Add();

        NoteManager.Instance.OpenVisibleNotes();

        if (NoteManager.Instance.Notes.All(n => n.Hidden) || NoteManager.Instance.Notes.Count == 0)
            NoteManager.Instance.CreateNote();
    }

    private void Quit()
    {
        NoteManager.Instance.Shutdown();
        _tray?.Dispose();
        _trayHost?.Close();
        Exit();
    }
}
