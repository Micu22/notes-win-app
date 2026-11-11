using Microsoft.UI;
using Microsoft.UI.Composition.SystemBackdrops;
using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Media;
using TrayNotes_WinUI.Models;
using TrayNotes_WinUI.Services;
using Windows.Graphics;
using Windows.UI;
using WinRT;

namespace TrayNotes_WinUI;

public sealed partial class NoteWindow : Window
{
    private readonly NoteDocument _note;
    private DesktopAcrylicController? _acrylic;
    private SystemBackdropConfiguration? _backdropConfig;
    private bool _closing;

    public NoteDocument Note => _note;

    public NoteWindow(NoteDocument note)
    {
        _note = note;
        InitializeComponent();

        ExtendsContentIntoTitleBar = true;
        SetTitleBar(Editor.DragRegion);

        Editor.Bind(
            note,
            embedded: false,
            geometryProvider: () =>
            {
                var p = AppWindow.Position;
                var s = AppWindow.Size;
                return (p.X, p.Y, s.Width, s.Height);
            },
            closeAction: () =>
            {
                Editor.Flush();
                NoteManager.Instance.HideNote(_note);
            });

        ApplyPresenter();
        ApplyPosition();
        TrySetTintedAcrylic();
        UpdateAcrylicTint();

        Activated += NoteWindow_Activated;
        Closed += NoteWindow_Closed;
        AppWindow.Changed += AppWindow_Changed;
        Editor.Changed += _ => UpdateAcrylicTint();
    }

    private void ApplyPresenter()
    {
        if (AppWindow.Presenter is OverlappedPresenter op)
        {
            op.IsMinimizable = false;
            op.IsMaximizable = false;
            op.SetBorderAndTitleBar(true, false);
        }
        AppWindow.IsShownInSwitchers = true;
        AppWindow.Title = _note.Tags.FirstOrDefault() is { } t ? $"#{t}" : "Sticky";
    }

    private void ApplyPosition()
    {
        var w = Math.Max(220, (int)_note.Width);
        var h = Math.Max(180, (int)_note.Height);
        AppWindow.MoveAndResize(new RectInt32((int)_note.X, (int)_note.Y, w, h));
    }

    private void TrySetTintedAcrylic()
    {
        if (!DesktopAcrylicController.IsSupported()) return;
        _backdropConfig = new SystemBackdropConfiguration
        {
            IsInputActive = true,
            Theme = SystemBackdropTheme.Light
        };
        _acrylic = new DesktopAcrylicController();
        UpdateAcrylicTint();
        _acrylic.AddSystemBackdropTarget(this.As<Microsoft.UI.Composition.ICompositionSupportsSystemBackdrop>());
        _acrylic.SetSystemBackdropConfiguration(_backdropConfig);
        SystemBackdrop = null;
        RootGrid.Background = new SolidColorBrush(Colors.Transparent);
    }

    private void UpdateAcrylicTint()
    {
        if (_acrylic == null) return;
        var pal = _note.Palette;
        _acrylic.TintColor = pal.Top;
        _acrylic.FallbackColor = pal.Bottom;
        _acrylic.TintOpacity = 0.55f;
        _acrylic.LuminosityOpacity = 0.85f;
        AppWindow.Title = _note.Tags.FirstOrDefault() is { } t ? $"#{t}" : "Sticky";
    }

    public void FlushAndClose()
    {
        Editor.Flush();
        _closing = true;
        Close();
    }

    public void ReloadEditor() => Editor.ReloadFromModel();

    private void AppWindow_Changed(AppWindow sender, AppWindowChangedEventArgs args)
    {
        if (args.DidPositionChange || args.DidSizeChange)
            Editor.Flush(forceBody: false);
    }

    private void NoteWindow_Activated(object sender, WindowActivatedEventArgs args)
    {
        if (_backdropConfig != null)
            _backdropConfig.IsInputActive = args.WindowActivationState != WindowActivationState.Deactivated;
    }

    private void NoteWindow_Closed(object sender, WindowEventArgs args)
    {
        if (!_closing && !_note.Hidden)
            Editor.Flush();
        _acrylic?.Dispose();
        _acrylic = null;
    }
}
