using System.Runtime.InteropServices;
using Microsoft.UI.Xaml;

namespace TrayNotes_WinUI.Services;

/// <summary>Win32 notify-icon host hooked to a WinUI window HWND.</summary>
public sealed class TrayService : IDisposable
{
    public const uint WmTray = 0x8001;
    private const uint IdNew = 1;
    private const uint IdShow = 2;
    private const uint IdHide = 3;
    private const uint IdExit = 4;

    private readonly IntPtr _hwnd;
    private readonly Native.WndProc _wndProc;
    private readonly IntPtr _oldWndProc;
    private Native.NOTIFYICONDATA _data;
    private bool _added;
    private bool _disposed;

    public event Action? NewNoteRequested;
    public event Action? ShowAllRequested;
    public event Action? HideAllRequested;
    public event Action? ExitRequested;

    public TrayService(Window window)
    {
        _hwnd = WinRT.Interop.WindowNative.GetWindowHandle(window);
        _wndProc = SubclassProc;
        _oldWndProc = Native.SetWindowLongPtr(_hwnd, Native.GWL_WNDPROC,
            Marshal.GetFunctionPointerForDelegate(_wndProc));

        _data = new Native.NOTIFYICONDATA
        {
            cbSize = (uint)Marshal.SizeOf<Native.NOTIFYICONDATA>(),
            hWnd = _hwnd,
            uID = 1,
            uFlags = Native.NIF_MESSAGE | Native.NIF_ICON | Native.NIF_TIP,
            uCallbackMessage = WmTray,
            hIcon = Native.LoadIcon(IntPtr.Zero, Native.IDI_APPLICATION),
            szTip = new char[128],
        };
        "Sticky Notes".AsSpan().CopyTo(_data.szTip);
    }

    public void Add()
    {
        if (_added) return;
        Native.Shell_NotifyIcon(Native.NIM_ADD, ref _data);
        _added = true;
    }

    public void Remove()
    {
        if (!_added) return;
        Native.Shell_NotifyIcon(Native.NIM_DELETE, ref _data);
        _added = false;
    }

    private IntPtr SubclassProc(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam)
    {
        if (msg == WmTray)
        {
            var mouse = (uint)(lParam.ToInt64() & 0xFFFF);
            if (mouse is Native.WM_LBUTTONUP or Native.WM_LBUTTONDBLCLK)
            {
                if (mouse == Native.WM_LBUTTONDBLCLK) NewNoteRequested?.Invoke();
                else ShowAllRequested?.Invoke();
                return IntPtr.Zero;
            }
            if (mouse == Native.WM_RBUTTONUP)
            {
                ShowMenu();
                return IntPtr.Zero;
            }
        }
        return Native.CallWindowProc(_oldWndProc, hWnd, msg, wParam, lParam);
    }

    private void ShowMenu()
    {
        var menu = Native.CreatePopupMenu();
        Native.AppendMenu(menu, Native.MF_STRING, IdNew, "New note");
        Native.AppendMenu(menu, Native.MF_STRING, IdShow, "Show all");
        Native.AppendMenu(menu, Native.MF_STRING, IdHide, "Hide all");
        Native.AppendMenu(menu, Native.MF_STRING, IdExit, "Exit");
        Native.GetCursorPos(out var pt);
        Native.SetForegroundWindow(_hwnd);
        var cmd = Native.TrackPopupMenu(menu,
            Native.TPM_RETURNCMD | Native.TPM_RIGHTBUTTON,
            pt.X, pt.Y, 0, _hwnd, IntPtr.Zero);
        Native.DestroyMenu(menu);
        switch ((uint)cmd)
        {
            case IdNew: NewNoteRequested?.Invoke(); break;
            case IdShow: ShowAllRequested?.Invoke(); break;
            case IdHide: HideAllRequested?.Invoke(); break;
            case IdExit: ExitRequested?.Invoke(); break;
        }
    }

    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;
        Remove();
        if (_oldWndProc != IntPtr.Zero)
            Native.SetWindowLongPtr(_hwnd, Native.GWL_WNDPROC, _oldWndProc);
        GC.KeepAlive(_wndProc);
    }

    private static class Native
    {
        public const int GWL_WNDPROC = -4;
        public const uint NIF_MESSAGE = 0x01, NIF_ICON = 0x02, NIF_TIP = 0x04;
        public const uint NIM_ADD = 0, NIM_DELETE = 2;
        public const uint WM_LBUTTONUP = 0x0202, WM_LBUTTONDBLCLK = 0x0203, WM_RBUTTONUP = 0x0205;
        public const uint MF_STRING = 0;
        public const uint TPM_RETURNCMD = 0x0100, TPM_RIGHTBUTTON = 0x0002;
        public static readonly IntPtr IDI_APPLICATION = (IntPtr)32512;

        public delegate IntPtr WndProc(IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);

        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        public struct NOTIFYICONDATA
        {
            public uint cbSize;
            public IntPtr hWnd;
            public uint uID;
            public uint uFlags;
            public uint uCallbackMessage;
            public IntPtr hIcon;
            [MarshalAs(UnmanagedType.ByValArray, SizeConst = 128)]
            public char[] szTip;
        }

        [StructLayout(LayoutKind.Sequential)]
        public struct POINT { public int X, Y; }

        [DllImport("shell32.dll", CharSet = CharSet.Unicode)]
        public static extern bool Shell_NotifyIcon(uint dwMessage, ref NOTIFYICONDATA lpData);

        [DllImport("user32.dll", CharSet = CharSet.Unicode)]
        public static extern IntPtr LoadIcon(IntPtr hInstance, IntPtr lpIconName);

        [DllImport("user32.dll")]
        public static extern IntPtr SetWindowLongPtr(IntPtr hWnd, int nIndex, IntPtr dwNewLong);

        [DllImport("user32.dll")]
        public static extern IntPtr CallWindowProc(IntPtr lpPrevWndFunc, IntPtr hWnd, uint msg, IntPtr wParam, IntPtr lParam);

        [DllImport("user32.dll")]
        public static extern IntPtr CreatePopupMenu();

        [DllImport("user32.dll", CharSet = CharSet.Unicode)]
        public static extern bool AppendMenu(IntPtr hMenu, uint uFlags, uint uIDNewItem, string lpNewItem);

        [DllImport("user32.dll")]
        public static extern bool DestroyMenu(IntPtr hMenu);

        [DllImport("user32.dll")]
        public static extern bool GetCursorPos(out POINT lpPoint);

        [DllImport("user32.dll")]
        public static extern bool SetForegroundWindow(IntPtr hWnd);

        [DllImport("user32.dll")]
        public static extern uint TrackPopupMenu(IntPtr hMenu, uint uFlags, int x, int y, int nReserved, IntPtr hWnd, IntPtr prcRect);
    }
}
