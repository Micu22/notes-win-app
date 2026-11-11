using Microsoft.UI.Xaml;
using TrayNotes_WinUI.Services;
using Windows.Graphics;

namespace TrayNotes_WinUI;

/// <summary>Hidden host window for the tray icon message pump.</summary>
public sealed partial class MainWindow : Window
{
    private TrayService? _tray;

    public MainWindow()
    {
        InitializeComponent();

        // Tiny off-screen host — tray owns the UX.
        AppWindow.IsShownInSwitchers = false;
        AppWindow.MoveAndResize(new RectInt32(-32000, -32000, 1, 1));

        _tray = new TrayService(this);
        _tray.NewNoteRequested += () => DispatcherQueue.TryEnqueue(() => NoteManager.Instance.CreateNote());
        _tray.ShowAllRequested += () => DispatcherQueue.TryEnqueue(() => NoteManager.Instance.OpenBrowser());
        _tray.HideAllRequested += () => DispatcherQueue.TryEnqueue(() => NoteManager.Instance.HideAll());
        _tray.ExitRequested += () => DispatcherQueue.TryEnqueue(Quit);
        _tray.Add();

        Closed += (_, _) =>
        {
            _tray?.Dispose();
            _tray = null;
        };
    }

    private void Quit()
    {
        NoteManager.Instance.Shutdown();
        _tray?.Dispose();
        _tray = null;
        Application.Current.Exit();
    }
}
