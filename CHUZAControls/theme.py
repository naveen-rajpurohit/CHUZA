"""Palette and asset loading for CHUZA Control's dark jungle gaming theme.

Text is rendered with Tkinter's native (anti-aliased) font rendering -
crisp at any size - not a bitmap-font trick. Background art is generated
at high resolution with smooth (LANCZOS) scaling, no blocky upscaling.
"""
import ctypes
import os
import sys

from PIL import Image, ImageTk

# --- Palette ---
BG_DEEP = "#0a0f0c"
BG_PANEL = "#111a14"
JUNGLE_DARK = "#12261c"
JUNGLE_MID = "#1d3a2a"
JUNGLE_LIGHT = "#2c5940"
ACCENT = "#3ddc84"
ACCENT_BRIGHT = "#39ff14"
WARNING = "#ffcc00"
CRITICAL = "#ff3b3b"
MOON = "#eef7ea"
STAR = "#cfe8d8"
TEXT_DIM = "#7fae8f"

PALETTE = {
    "BG_DEEP": BG_DEEP,
    "BG_PANEL": BG_PANEL,
    "JUNGLE_DARK": JUNGLE_DARK,
    "JUNGLE_MID": JUNGLE_MID,
    "JUNGLE_LIGHT": JUNGLE_LIGHT,
    "ACCENT": ACCENT,
    "ACCENT_BRIGHT": ACCENT_BRIGHT,
    "WARNING": WARNING,
    "CRITICAL": CRITICAL,
    "MOON": MOON,
    "STAR": STAR,
    "TEXT_DIM": TEXT_DIM,
}

# --- Fonts (native Tk fonts - crisp, anti-aliased) ---
FONT_UI = "Segoe UI"
FONT_MONO = "Consolas"


def make_font(size, bold=False, mono=False):
    family = FONT_MONO if mono else FONT_UI
    return (family, size, "bold") if bold else (family, size)


def hex_to_rgb(h):
    h = h.lstrip("#")
    return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))


def resource_path(*parts):
    """Resolve a bundled, read-only asset path. Works both running from
    source and from a PyInstaller build: under --onefile, bundled data
    is extracted to sys._MEIPASS; under --onedir (and from source), it
    sits next to this file."""
    base = getattr(sys, "_MEIPASS", os.path.dirname(os.path.abspath(__file__)))
    return os.path.join(base, *parts)


def enable_dpi_awareness():
    """Must be called before tk.Tk() is constructed, so Windows doesn't
    bitmap-stretch the window (and blur everything) on HiDPI displays."""
    try:
        ctypes.windll.shcore.SetProcessDpiAwareness(1)
    except Exception:
        try:
            ctypes.windll.user32.SetProcessDPIAware()
        except Exception:
            pass


_bg_cache = {}


def get_background_photo(w, h):
    """Loads assets/bg_jungle.png once and smoothly (LANCZOS) resizes it
    to the requested screen size, cached by size. The source art itself
    is generated at high resolution (see assets_gen.py), so this is a
    minor fit-to-window resize, not an upscale from a tiny bitmap."""
    key = (w, h)
    if key not in _bg_cache:
        img = Image.open(resource_path("assets", "bg_jungle.png")).convert("RGB")
        img = img.resize((w, h), Image.LANCZOS)
        _bg_cache[key] = ImageTk.PhotoImage(img)
    return _bg_cache[key]
