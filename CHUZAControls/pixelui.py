"""Reusable dark-jungle gaming-HUD widget toolkit for CHUZA Control.

Built on plain tk.Canvas + place() (not ttk, not pack/grid for HUD
elements) so absolute-position HUD chips can sit at fixed corners over
the camera viewport. Text is native Tkinter font rendering - crisp and
anti-aliased at any size, not a bitmap trick. Animation (button press,
pulses, toasts) is driven by one shared Ticker per app instead of many
independent after() chains, so nothing is individually easy to forget
to cancel on screen teardown.
"""
import time
import tkinter as tk
from tkinter import font as tkfont

from theme import (
    ACCENT, ACCENT_BRIGHT, BG_DEEP, BG_PANEL, CRITICAL, JUNGLE_DARK,
    TEXT_DIM, WARNING, get_background_photo, make_font,
)


class Ticker:
    """One shared ~60fps after() loop. Callbacks are `fn(dt_ms) -> bool`;
    return False to auto-deregister."""

    def __init__(self, widget):
        self._widget = widget
        self._callbacks = {}
        self._next_id = 0
        self._last_time = None
        self._running = False

    def add(self, callback):
        token = self._next_id
        self._next_id += 1
        self._callbacks[token] = callback
        if not self._running:
            self._running = True
            self._last_time = time.monotonic()
            self._widget.after(16, self._tick)
        return token

    def remove(self, token):
        self._callbacks.pop(token, None)

    def _tick(self):
        now = time.monotonic()
        dt_ms = int((now - self._last_time) * 1000)
        self._last_time = now
        dead = []
        for token, cb in list(self._callbacks.items()):
            try:
                keep = cb(dt_ms)
            except Exception:
                keep = False
            if not keep:
                dead.append(token)
        for token in dead:
            self._callbacks.pop(token, None)
        if self._callbacks:
            self._widget.after(16, self._tick)
        else:
            self._running = False


def draw_pixel_border(canvas, x0, y0, x1, y1, thickness=2, color=ACCENT, fill=None):
    """Flat rectangular border built from plain canvas rectangles.
    Returns the created item ids."""
    items = []
    if fill is not None:
        items.append(canvas.create_rectangle(x0, y0, x1, y1, fill=fill, outline=""))
    items.append(canvas.create_rectangle(x0, y0, x1, y0 + thickness, fill=color, outline=""))
    items.append(canvas.create_rectangle(x0, y1 - thickness, x1, y1, fill=color, outline=""))
    items.append(canvas.create_rectangle(x0, y0, x0 + thickness, y1, fill=color, outline=""))
    items.append(canvas.create_rectangle(x1 - thickness, y0, x1, y1, fill=color, outline=""))
    return items


class BaseScreen(tk.Frame):
    """Shared per-screen chrome: a fixed-size Canvas with the jungle
    background pre-drawn on it. Subclasses build content on self.canvas
    via place_widget() (absolute placement) or self.canvas.create_*."""

    def __init__(self, parent, app, width, height):
        super().__init__(parent, width=width, height=height)
        self.pack_propagate(False)
        self.app = app
        self.width = width
        self.height = height
        self.canvas = tk.Canvas(self, width=width, height=height,
                                 highlightthickness=0, bg=BG_DEEP)
        self.canvas.pack(fill="both", expand=True)
        self._bg_photo = get_background_photo(width, height)
        self.canvas.create_image(0, 0, image=self._bg_photo, anchor="nw")
        self.toast = Toast(self)

    def place_widget(self, widget, x, y, anchor="nw"):
        widget.place(x=x, y=y, anchor=anchor)
        # NOT widget.lift()/tkraise(): tk.Canvas hard-aliases both of
        # those to its own item-level tag_raise(tag), which errors with
        # no args. Issue the underlying Tk "raise" window command
        # directly so HUD widgets always paint above the video feed.
        widget.tk.call("raise", widget._w)
        return widget


class PixelButton(tk.Canvas):
    """Flat gaming-HUD button with a short 'shrink inward' press
    animation. Deliberately doesn't shim .config(text=, state=) - use
    .set_text()/.set_enabled() instead."""

    _VARIANTS = {
        "primary": ACCENT,
        "danger": CRITICAL,
    }

    def __init__(self, parent, text, command=None, width=160, height=44,
                 font_size=12, variant="primary", enabled=True):
        super().__init__(parent, width=width, height=height,
                          highlightthickness=0, bg=BG_DEEP, cursor="hand2")
        self._border_color = self._VARIANTS.get(variant, ACCENT)
        self._command = command
        self._text = text
        self._font_size = font_size
        self._enabled = enabled
        self._pressed = False
        self._width = width
        self._height = height
        self._redraw()

        self.bind("<ButtonPress-1>", self._on_press)
        self.bind("<ButtonRelease-1>", self._on_release)
        self.bind("<Leave>", self._on_leave)

    def set_text(self, text):
        self._text = text
        self._redraw()

    def set_command(self, command):
        self._command = command

    def set_enabled(self, enabled):
        self._enabled = enabled
        self.configure(cursor="hand2" if enabled else "arrow")
        self._redraw()

    def _redraw(self):
        self.delete("all")
        inset = 3 if self._pressed else 0
        x0, y0 = inset, inset
        x1, y1 = self._width - 1 - inset, self._height - 1 - inset
        color = self._border_color if self._enabled else TEXT_DIM
        draw_pixel_border(self, x0, y0, x1, y1, thickness=2, color=color, fill=BG_PANEL)
        cx = (x0 + x1) // 2
        cy = (y0 + y1) // 2 + (1 if self._pressed else 0)
        self.create_text(cx, cy, text=self._text, fill=color,
                          font=make_font(self._font_size, bold=True), anchor="center")

    def _on_press(self, event):
        if not self._enabled:
            return
        self._pressed = True
        self._redraw()

    def _on_release(self, event):
        if not self._enabled:
            return
        was_pressed = self._pressed
        self._pressed = False
        self._redraw()
        if was_pressed and 0 <= event.x <= self._width and 0 <= event.y <= self._height and self._command:
            self._command()

    def _on_leave(self, event):
        if self._pressed:
            self._pressed = False
            self._redraw()


class Badge(tk.Canvas):
    """Small single-line bordered badge for a transient message shown
    over other content. Must be placed via BaseScreen.place_widget()
    (not drawn as a bare canvas item) - canvas items always paint below
    sibling child widgets, so a plain create_text badge would silently
    render underneath anything placed on top of it."""

    def __init__(self, parent, width=280, height=32):
        super().__init__(parent, width=width, height=height, highlightthickness=0, bg=BG_DEEP)
        self._width, self._height = width, height

    def set_text(self, text, color=CRITICAL):
        self.delete("all")
        draw_pixel_border(self, 0, 0, self._width - 1, self._height - 1, thickness=2, color=color, fill=BG_PANEL)
        self.create_text(self._width // 2, self._height // 2, text=text, fill=color,
                          font=make_font(11, bold=True), anchor="center")


class HudStatPanel(tk.Canvas):
    """Bordered HUD box: a dim title and one or more label/value lines,
    updated live via set_lines(). Sized generously so full telemetry
    (env/battery/distance/link) reads clearly at a glance."""

    def __init__(self, parent, title, width=230, height=112, lines=None):
        super().__init__(parent, width=width, height=height, highlightthickness=0, bg=BG_DEEP)
        self._width, self._height = width, height
        self._title = title
        self._lines = lines or []  # list of (text, color)
        self._redraw()

    def set_lines(self, lines):
        self._lines = lines
        self._redraw()

    def _redraw(self):
        self.delete("all")
        draw_pixel_border(self, 0, 0, self._width - 1, self._height - 1, thickness=2, color=ACCENT, fill=BG_PANEL)
        self.create_text(10, 8, text=self._title, fill=TEXT_DIM,
                          font=make_font(11, bold=True), anchor="nw")
        y = 30
        for text, color in self._lines:
            self.create_text(10, y, text=text, fill=color or ACCENT,
                              font=make_font(13, mono=True), anchor="nw")
            y += 20


class HealthBar(tk.Canvas):
    """Segmented gaming 'health bar' for battery %. Color shifts
    green -> yellow -> red; blinks (via the shared Ticker) once critical."""

    SEGMENTS = 10
    CRITICAL_PCT = 15
    WARNING_PCT = 35

    def __init__(self, parent, ticker, width=230, height=34, label="BATT"):
        super().__init__(parent, width=width, height=height, highlightthickness=0, bg=BG_DEEP)
        self._ticker = ticker
        self._width, self._height = width, height
        self._label = label
        self._pct = 100
        self._suffix = ""
        self._pulse_token = None
        self._pulse_on = True
        self._elapsed = 0
        self._redraw()

    def set_pct(self, pct, suffix=""):
        self._pct = max(0, min(100, pct))
        self._suffix = suffix
        self._update_pulse_registration()
        self._redraw()

    def _color_for_pct(self):
        if self._pct <= self.CRITICAL_PCT:
            return CRITICAL
        if self._pct <= self.WARNING_PCT:
            return WARNING
        return ACCENT

    def _update_pulse_registration(self):
        critical = self._pct <= self.CRITICAL_PCT
        if critical and self._pulse_token is None:
            self._elapsed = 0
            self._pulse_on = True
            self._pulse_token = self._ticker.add(self._pulse_tick)
        elif not critical and self._pulse_token is not None:
            self._ticker.remove(self._pulse_token)
            self._pulse_token = None
            self._pulse_on = True

    def _pulse_tick(self, dt_ms):
        self._elapsed += dt_ms
        if self._elapsed >= 400:
            self._elapsed = 0
            self._pulse_on = not self._pulse_on
            self._redraw()
        return True  # deregistration is handled explicitly in _update_pulse_registration

    def _redraw(self):
        self.delete("all")
        draw_pixel_border(self, 0, 0, self._width - 1, self._height - 1, thickness=2, color=ACCENT, fill=BG_PANEL)
        color = self._color_for_pct()
        blink_off = self._pct <= self.CRITICAL_PCT and not self._pulse_on
        label_color = BG_PANEL if blink_off else color

        self.create_text(8, 6, text=f"{self._label} {int(self._pct)}%{self._suffix}",
                          fill=label_color, font=make_font(12, bold=True, mono=True), anchor="nw")

        bar_y0, bar_y1 = self._height - 12, self._height - 4
        pad, gap = 6, 2
        inner_w = self._width - 2 * pad
        seg_w = (inner_w - gap * (self.SEGMENTS - 1)) / self.SEGMENTS
        filled = round((self._pct / 100) * self.SEGMENTS)
        for i in range(self.SEGMENTS):
            x0 = pad + i * (seg_w + gap)
            x1 = x0 + seg_w
            seg_color = (BG_PANEL if blink_off else color) if i < filled else JUNGLE_DARK
            self.create_rectangle(x0, bar_y0, x1, bar_y1, fill=seg_color, outline="")


class Toast:
    """Non-modal notification chip: slides up from the bottom, holds,
    slides back out. Single-depth - a new toast replaces whatever is
    currently showing rather than queuing, to avoid over-engineering."""

    _COLORS = {"info": ACCENT, "warning": WARNING, "error": CRITICAL}
    _FONT = make_font(12, bold=True)

    def __init__(self, base_screen):
        self.base = base_screen
        self.canvas = base_screen.canvas
        self._ticker = base_screen.app.ticker
        self._token = None
        self._items = []
        self._measure_font = tkfont.Font(font=self._FONT)

    def show(self, text, kind="info", duration_ms=2400):
        self._clear()
        color = self._COLORS.get(kind, ACCENT)
        pad_x, pad_y = 16, 12
        w = self._measure_font.measure(text) + pad_x * 2
        h = self._measure_font.metrics("linespace") + pad_y * 2
        cx = self.base.width // 2
        rest_y = self.base.height - h - 20
        start_y = self.base.height + 4
        x0, x1 = cx - w // 2, cx + w // 2

        border_items = draw_pixel_border(self.canvas, x0, start_y, x1, start_y + h,
                                          thickness=2, color=color, fill=BG_PANEL)
        text_item = self.canvas.create_text(cx, start_y + h // 2, text=text, fill=color,
                                             font=self._FONT, anchor="center")
        self._items = border_items + [text_item]

        state = {"phase": "in", "elapsed": 0, "y": start_y}

        def tick(dt_ms):
            state["elapsed"] += dt_ms
            if state["phase"] == "in":
                t = min(1.0, state["elapsed"] / 150)
                new_y = start_y + (rest_y - start_y) * t
                self._shift(new_y - state["y"])
                state["y"] = new_y
                if t >= 1.0:
                    state["phase"], state["elapsed"] = "hold", 0
            elif state["phase"] == "hold":
                if state["elapsed"] >= duration_ms:
                    state["phase"], state["elapsed"] = "out", 0
            elif state["phase"] == "out":
                t = min(1.0, state["elapsed"] / 150)
                new_y = rest_y + (start_y - rest_y) * t
                self._shift(new_y - state["y"])
                state["y"] = new_y
                if t >= 1.0:
                    self._clear()
                    return False
            return True

        self._token = self._ticker.add(tick)

    def _shift(self, dy):
        for item in self._items:
            self.canvas.move(item, 0, dy)

    def _clear(self):
        for item in self._items:
            self.canvas.delete(item)
        self._items = []
        if self._token is not None:
            self._ticker.remove(self._token)
            self._token = None


class DPadIndicator(tk.Canvas):
    """Lights up per currently-pressed WASD/space/b key, mirroring
    ControlApp.pressed - replaces the plain 'Left: 0% Right: 0%' text."""

    KEYS_GRID = {"w": (1, 0), "a": (0, 1), "s": (1, 1), "d": (2, 1)}
    ESTOP_KEY = "b"

    def __init__(self, parent, width=190, height=110, cell=30):
        super().__init__(parent, width=width, height=height, highlightthickness=0, bg=BG_DEEP)
        self._width, self._height, self._cell = width, height, cell
        self._pressed = set()
        self._redraw()

    def set_pressed(self, pressed):
        pressed = set(pressed)
        if pressed == self._pressed:
            return
        self._pressed = pressed
        self._redraw()

    def _redraw(self):
        self.delete("all")
        cell, gap = self._cell, 3
        ox, oy = 6, 4
        for key, (col, row) in self.KEYS_GRID.items():
            on = key in self._pressed
            x0 = ox + col * (cell + gap)
            y0 = oy + row * (cell + gap)
            fill = ACCENT if on else BG_PANEL
            border = ACCENT_BRIGHT if on else ACCENT
            draw_pixel_border(self, x0, y0, x0 + cell, y0 + cell, thickness=2, color=border, fill=fill)
            text_color = BG_DEEP if on else TEXT_DIM
            self.create_text(x0 + cell // 2, y0 + cell // 2, text=key.upper(), fill=text_color,
                              font=make_font(11, bold=True), anchor="center")

        badge_y = oy + 2 * (cell + gap) + 6
        badge_h = 24
        badge_w = (self._width - ox * 2 - 4) // 2
        boost_on = "space" in self._pressed
        estop_on = self.ESTOP_KEY in self._pressed

        bx0 = ox
        b_fill = ACCENT if boost_on else BG_PANEL
        b_border = ACCENT_BRIGHT if boost_on else ACCENT
        draw_pixel_border(self, bx0, badge_y, bx0 + badge_w, badge_y + badge_h, thickness=2, color=b_border, fill=b_fill)
        self.create_text(bx0 + badge_w // 2, badge_y + badge_h // 2, text="BOOST",
                          fill=BG_DEEP if boost_on else TEXT_DIM, font=make_font(9, bold=True), anchor="center")

        ex0 = bx0 + badge_w + 4
        e_fill = CRITICAL if estop_on else BG_PANEL
        e_border = ACCENT_BRIGHT if estop_on else CRITICAL
        draw_pixel_border(self, ex0, badge_y, ex0 + badge_w, badge_y + badge_h, thickness=2, color=e_border, fill=e_fill)
        self.create_text(ex0 + badge_w // 2, badge_y + badge_h // 2, text="STOP (B)",
                          fill=BG_DEEP if estop_on else CRITICAL, font=make_font(9, bold=True), anchor="center")
