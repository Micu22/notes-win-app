"""Minimal sticky-notes shell using Edge WebView2 + HTML UI."""

from __future__ import annotations

import ctypes
import json
import re
import sys
import threading
import time
from ctypes import wintypes
from pathlib import Path

import webview
from PIL import Image, ImageDraw

APP_DIR = Path(__file__).resolve().parent
WEB_DIR = APP_DIR / "web"
NOTES_DIR = APP_DIR.parent
BROWSER_HTML = WEB_DIR / "index.html"
STICKY_HTML = WEB_DIR / "sticky.html"

META_RE = re.compile(
    r"<!--\s*traynotes\s+"
    r"x=(?P<x>-?\d+)\s+y=(?P<y>-?\d+)\s+"
    r"w=(?P<w>\d+)\s+h=(?P<h>\d+)\s+"
    r"hidden=(?P<hidden>[01])\s+"
    r"mode=(?P<mode>\w+)\s*-->",
    re.I,
)

user32 = ctypes.windll.user32
WM_NCHITTEST = 0x0084
HTCLIENT = 1
HTCAPTION = 2
HTLEFT = 10
HTRIGHT = 11
HTTOP = 12
HTTOPLEFT = 13
HTTOPRIGHT = 14
HTBOTTOM = 15
HTBOTTOMLEFT = 16
HTBOTTOMRIGHT = 17
GWLP_WNDPROC = -4
GWL_STYLE = -16
WS_THICKFRAME = 0x00040000
WS_BORDER = 0x00800000
TITLE_H = 30
EDGE = 6
BTN_ZONE = 72  # right side reserved for min/close circles


WNDPROC = ctypes.WINFUNCTYPE(
    ctypes.c_longlong, wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM
)


class POINT(ctypes.Structure):
    _fields_ = [("x", ctypes.c_long), ("y", ctypes.c_long)]


class RECT(ctypes.Structure):
    _fields_ = [
        ("left", ctypes.c_long),
        ("top", ctypes.c_long),
        ("right", ctypes.c_long),
        ("bottom", ctypes.c_long),
    ]


# Keep WndProc callbacks alive
_wndproc_refs: dict[int, object] = {}


def utf8(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def write_utf8(path: Path, text: str) -> None:
    path.write_text(text, encoding="utf-8", newline="\n")


def parse_tags(body: str) -> list[str]:
    first = (body.split("\n", 1)[0] if body else "")
    tags: list[str] = []
    i = 0
    while i < len(first):
        while i < len(first) and first[i].isspace():
            i += 1
        if i >= len(first) or first[i] != "#":
            break
        i += 1
        start = i
        while i < len(first) and not first[i].isspace() and first[i] != "#":
            i += 1
        if i > start:
            t = first[start:i].lower()
            if t not in tags:
                tags.append(t)
    return tags


def looks_like_checklist(body: str) -> bool:
    lines = [ln for ln in body.splitlines() if ln.strip()]
    if not lines:
        return False
    checks = sum(
        1
        for ln in lines
        if re.match(r"- \[[ xX]?\]", ln) or ln.startswith("- []")
    )
    return checks > 0 and checks * 2 >= len(lines)


def next_note_id() -> str:
    n = 1
    while True:
        name = f"note-{n:03d}"
        if not (NOTES_DIR / f"{name}.md").exists():
            return name
        n += 1


def make_tray_icon() -> Image.Image:
    s = 64
    img = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    d.rounded_rectangle(
        (4, 4, s - 4, s - 4),
        radius=8,
        fill=(255, 214, 74),
        outline=(168, 124, 12),
        width=2,
    )
    for y in (22, 34, 46):
        d.line((16, y, s - 16 if y < 46 else s - 24, y), fill=(92, 64, 12), width=3)
    return img


def place_on_screen(x: int, y: int, w: int, h: int, index: int = 0) -> tuple[int, int, int, int]:
    w = max(220, w or 300)
    h = max(180, h or 240)
    if x <= -10000 or y <= -10000 or (x == 0 and y == 0):
        x = 80 + index * 28
        y = 80 + index * 28
    return x, y, w, h


def hwnd_of(win: webview.Window) -> int:
    native = getattr(win, "native", None)
    if native is None:
        return 0
    handle = getattr(native, "Handle", None)
    if handle is None:
        return 0
    try:
        return int(handle.ToInt32())
    except Exception:
        try:
            return int(handle.ToInt64())
        except Exception:
            return 0


def enable_frameless_chrome(win: webview.Window) -> None:
    """Native drag (caption) + edge resize for a frameless WinForms window."""
    hwnd = hwnd_of(win)
    if not hwnd:
        return

    # Allow sizing even with FormBorderStyle.None
    style = user32.GetWindowLongW(hwnd, GWL_STYLE)
    user32.SetWindowLongW(hwnd, GWL_STYLE, style | WS_THICKFRAME | WS_BORDER)

    is64 = ctypes.sizeof(ctypes.c_void_p) == 8
    if is64:
        get_long = user32.GetWindowLongPtrW
        set_long = user32.SetWindowLongPtrW
        get_long.restype = ctypes.c_void_p
        get_long.argtypes = [wintypes.HWND, ctypes.c_int]
        set_long.restype = ctypes.c_void_p
        set_long.argtypes = [wintypes.HWND, ctypes.c_int, ctypes.c_void_p]
        call_proc = user32.CallWindowProcW
        call_proc.restype = ctypes.c_longlong
        call_proc.argtypes = [
            ctypes.c_void_p,
            wintypes.HWND,
            wintypes.UINT,
            wintypes.WPARAM,
            wintypes.LPARAM,
        ]
        LRESULT = ctypes.c_longlong
    else:
        get_long = user32.GetWindowLongW
        set_long = user32.SetWindowLongW
        call_proc = user32.CallWindowProcW
        LRESULT = ctypes.c_long

    local_wndproc = ctypes.WINFUNCTYPE(
        LRESULT, wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM
    )

    old_proc = get_long(hwnd, GWLP_WNDPROC)

    def py_wndproc(h, msg, wp, lp):
        if msg == WM_NCHITTEST:
            sx = ctypes.c_short(lp & 0xFFFF).value
            sy = ctypes.c_short((lp >> 16) & 0xFFFF).value
            pt = POINT(sx, sy)
            user32.ScreenToClient(h, ctypes.byref(pt))
            rc = RECT()
            user32.GetClientRect(h, ctypes.byref(rc))
            w = rc.right - rc.left
            hh = rc.bottom - rc.top
            x, y = pt.x, pt.y

            left = x < EDGE
            right = x >= w - EDGE
            top = y < EDGE
            bottom = y >= hh - EDGE
            if top and left:
                return HTTOPLEFT
            if top and right:
                return HTTOPRIGHT
            if bottom and left:
                return HTBOTTOMLEFT
            if bottom and right:
                return HTBOTTOMRIGHT
            if left:
                return HTLEFT
            if right:
                return HTRIGHT
            if top:
                return HTTOP
            if bottom:
                return HTBOTTOM
            if y < TITLE_H and x < w - BTN_ZONE:
                return HTCAPTION
            return HTCLIENT
        return call_proc(old_proc, h, msg, wp, lp)

    cb = local_wndproc(py_wndproc)
    _wndproc_refs[hwnd] = (cb, old_proc)
    set_long(hwnd, GWLP_WNDPROC, ctypes.cast(cb, ctypes.c_void_p))


class Api:
    def __init__(self) -> None:
        self._browser: webview.Window | None = None
        self._stickies: dict[str, webview.Window] = {}
        self._quitting = False
        self._closing: set[str] = set()
        self._lock = threading.Lock()

    def set_browser(self, window: webview.Window) -> None:
        self._browser = window

    def notes_dir(self) -> str:
        return str(NOTES_DIR)

    def _read_note(self, note_id: str) -> dict | None:
        path = NOTES_DIR / f"{note_id}.md"
        if not path.exists():
            return None
        raw = utf8(path)
        meta = {
            "x": 80,
            "y": 80,
            "w": 300,
            "h": 240,
            "hidden": False,
            "mode": "text",
        }
        body = raw
        first_nl = raw.find("\n")
        if first_nl != -1:
            m = META_RE.search(raw[:first_nl])
            if m:
                meta = {
                    "x": int(m.group("x")),
                    "y": int(m.group("y")),
                    "w": int(m.group("w")),
                    "h": int(m.group("h")),
                    "hidden": m.group("hidden") == "1",
                    "mode": "check" if m.group("mode") == "check" else "text",
                }
                body = raw[first_nl + 1 :]
        body = body.rstrip("\n")
        if meta["mode"] != "check" and looks_like_checklist(body):
            meta["mode"] = "check"
        return {
            "id": note_id,
            "path": str(path),
            "body": body,
            "tags": parse_tags(body),
            **meta,
        }

    def list_notes(self) -> list[dict]:
        notes: list[dict] = []
        for path in sorted(NOTES_DIR.glob("*.md")):
            note = self._read_note(path.stem)
            if note:
                note["open"] = path.stem in self._stickies
                notes.append(note)
        return notes

    def get_note(self, note_id: str) -> dict | None:
        return self._read_note(note_id)

    def new_note(self) -> dict:
        note_id = next_note_id()
        body = "#inbox\n"
        path = NOTES_DIR / f"{note_id}.md"
        x, y, w, h = place_on_screen(0, 0, 300, 240, len(self._stickies))
        meta_line = f"<!-- traynotes x={x} y={y} w={w} h={h} hidden=0 mode=text -->\n"
        write_utf8(path, meta_line + body)
        note = {
            "id": note_id,
            "path": str(path),
            "body": body.rstrip("\n"),
            "tags": ["inbox"],
            "x": x,
            "y": y,
            "w": w,
            "h": h,
            "hidden": False,
            "mode": "text",
        }
        self.open_note(note_id)
        self._refresh_browser()
        return note

    def save_note(self, payload: dict | str) -> bool:
        data = json.loads(payload) if isinstance(payload, str) else payload
        note_id = data["id"]
        path = NOTES_DIR / f"{note_id}.md"
        body = data.get("body", "")
        body_out = body if not body or body.endswith("\n") else body + "\n"
        mode = data.get("mode", "text")
        hidden = 1 if data.get("hidden") else 0

        x = int(data.get("x", 80))
        y = int(data.get("y", 80))
        w = int(data.get("w", 300))
        h = int(data.get("h", 240))
        win = self._stickies.get(note_id)
        if win is not None:
            try:
                x, y = int(win.x), int(win.y)
                w, h = int(win.width), int(win.height)
            except Exception:
                pass

        meta = f"<!-- traynotes x={x} y={y} w={w} h={h} hidden={hidden} mode={mode} -->\n"
        write_utf8(path, meta + body_out)
        self._refresh_browser()
        return True

    def delete_note(self, note_id: str) -> bool:
        self._destroy_sticky(note_id, save_hidden=False)
        path = NOTES_DIR / f"{note_id}.md"
        if path.exists():
            path.unlink()
        self._refresh_browser()
        return True

    def open_note(self, note_id: str) -> bool:
        with self._lock:
            existing = self._stickies.get(note_id)
            if existing is not None:
                try:
                    existing.show()
                    existing.restore()
                    return True
                except Exception:
                    self._stickies.pop(note_id, None)

            note = self._read_note(note_id)
            if not note:
                return False

            x, y, w, h = place_on_screen(
                note["x"], note["y"], note["w"], note["h"], len(self._stickies)
            )
            self._write_meta_flags(
                note_id, hidden=False, x=x, y=y, w=w, h=h, mode=note["mode"], body=note["body"]
            )

            url = str(STICKY_HTML.resolve()) + f"#{note_id}"
            window = webview.create_window(
                title=note_id,
                url=url,
                js_api=self,
                width=w,
                height=h,
                x=x,
                y=y,
                frameless=True,
                easy_drag=False,
                resizable=True,
                on_top=False,
                background_color="#FFEC96",
                min_size=(220, 180),
                text_select=True,
                shadow=False,
            )
            self._stickies[note_id] = window

            def on_shown():
                try:
                    enable_frameless_chrome(window)
                except Exception as exc:
                    print("enable_frameless_chrome failed:", exc, file=sys.stderr)

            def on_closing():
                if self._quitting:
                    return True
                # Allow destroy to finish; persist hidden state without re-entering destroy.
                if note_id in self._closing:
                    return True
                self._closing.add(note_id)
                try:
                    self._mark_hidden_from_window(note_id, window)
                finally:
                    self._stickies.pop(note_id, None)
                    self._closing.discard(note_id)
                    hwnd = hwnd_of(window)
                    _wndproc_refs.pop(hwnd, None)
                    self._refresh_browser()
                return True

            window.events.shown += on_shown
            window.events.closing += on_closing
        self._refresh_browser()
        return True

    def minimize_note(self, note_id: str) -> bool:
        win = self._stickies.get(note_id)
        if not win:
            return False
        # Marshal to UI thread via a short delay so the JS bridge can finish.
        def later():
            time.sleep(0.02)
            try:
                win.minimize()
            except Exception:
                pass

        threading.Thread(target=later, daemon=True).start()
        return True

    def close_note(self, note_id: str) -> bool:
        """Hide sticky (circle X). Must not destroy inside the JS bridge call."""
        win = self._stickies.get(note_id)

        def later():
            time.sleep(0.05)
            if win is None:
                note = self._read_note(note_id)
                if note:
                    self._write_meta_flags(
                        note_id,
                        hidden=True,
                        x=note["x"],
                        y=note["y"],
                        w=note["w"],
                        h=note["h"],
                        mode=note["mode"],
                        body=note["body"],
                    )
                return
            try:
                win.destroy()
            except Exception:
                self._stickies.pop(note_id, None)

        threading.Thread(target=later, daemon=True).start()
        return True

    def hide_all_notes(self) -> None:
        for note_id in list(self._stickies.keys()):
            self.close_note(note_id)
        if self._browser:
            self._browser.hide()

    def show_browser(self) -> None:
        if self._browser:
            self._browser.show()
            self._browser.restore()

    def show_all_visible(self) -> None:
        self.show_browser()
        for note in self.list_notes():
            if not note.get("hidden"):
                self.open_note(note["id"])

    def quit_app(self) -> None:
        self._quitting = True
        for note_id in list(self._stickies.keys()):
            self._persist_bounds(note_id)
            win = self._stickies.pop(note_id, None)
            if win is not None:
                try:
                    win.destroy()
                except Exception:
                    pass
        if self._browser:
            try:
                self._browser.destroy()
            except Exception:
                pass

    def _write_meta_flags(
        self,
        note_id: str,
        *,
        hidden: bool,
        x: int,
        y: int,
        w: int,
        h: int,
        mode: str,
        body: str,
    ) -> None:
        path = NOTES_DIR / f"{note_id}.md"
        body_out = body if not body or body.endswith("\n") else body + "\n"
        meta = (
            f"<!-- traynotes x={x} y={y} w={w} h={h} "
            f"hidden={1 if hidden else 0} mode={mode} -->\n"
        )
        write_utf8(path, meta + body_out)

    def _persist_bounds(self, note_id: str) -> None:
        note = self._read_note(note_id)
        win = self._stickies.get(note_id)
        if not note or not win:
            return
        try:
            x, y = int(win.x), int(win.y)
            w, h = int(win.width), int(win.height)
        except Exception:
            return
        self._write_meta_flags(
            note_id,
            hidden=note["hidden"],
            x=x,
            y=y,
            w=w,
            h=h,
            mode=note["mode"],
            body=note["body"],
        )

    def _mark_hidden_from_window(self, note_id: str, win: webview.Window) -> None:
        note = self._read_note(note_id)
        if not note:
            return
        try:
            x, y = int(win.x), int(win.y)
            w, h = int(win.width), int(win.height)
        except Exception:
            x, y, w, h = note["x"], note["y"], note["w"], note["h"]
        self._write_meta_flags(
            note_id,
            hidden=True,
            x=x,
            y=y,
            w=w,
            h=h,
            mode=note["mode"],
            body=note["body"],
        )

    def _destroy_sticky(self, note_id: str, save_hidden: bool = True) -> None:
        win = self._stickies.get(note_id)
        if save_hidden and win is not None:
            self._mark_hidden_from_window(note_id, win)
        win = self._stickies.pop(note_id, None)
        if win is not None:
            try:
                win.destroy()
            except Exception:
                pass

    def _refresh_browser(self) -> None:
        if self._browser and not self._quitting:
            try:
                self._browser.evaluate_js(
                    "window.app && window.app.refresh && window.app.refresh()"
                )
            except Exception:
                pass


def start_tray(api: Api) -> None:
    try:
        import pystray
        from pystray import MenuItem as Item
    except ImportError:
        return

    def on_show(icon, item):  # noqa: ARG001
        api.show_all_visible()

    def on_new(icon, item):  # noqa: ARG001
        api.new_note()

    def on_hide(icon, item):  # noqa: ARG001
        api.hide_all_notes()

    def on_exit(icon, item):  # noqa: ARG001
        icon.stop()
        api.quit_app()

    menu = pystray.Menu(
        Item("New note", on_new),
        Item("Show notes…", on_show, default=True),
        Item("Hide all", on_hide),
        pystray.Menu.SEPARATOR,
        Item("Exit", on_exit),
    )
    icon = pystray.Icon("sticky-notes", make_tray_icon(), "Sticky Notes", menu)
    icon.run()


def main() -> int:
    api = Api()
    browser = webview.create_window(
        "All notes",
        url=str(BROWSER_HTML.resolve()),
        js_api=api,
        width=520,
        height=480,
        min_size=(360, 320),
        background_color="#FFFAF0",
        text_select=True,
    )
    api.set_browser(browser)

    def on_browser_closing():
        if api._quitting:
            return True
        browser.hide()
        return False

    browser.events.closing += on_browser_closing

    def on_start():
        threading.Thread(target=start_tray, args=(api,), daemon=True).start()
        notes = api.list_notes()
        visible = [n for n in notes if not n.get("hidden")]
        if not notes:
            api.new_note()
        else:
            for n in visible:
                api.open_note(n["id"])

    webview.start(on_start, gui="edgechromium", http_server=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
