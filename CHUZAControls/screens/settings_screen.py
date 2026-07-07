"""In-app settings screen, replacing the old SettingsWindow popup.
Mirrors RobotSettings (lib/CHUZASettings on the device) via SETTINGS_SCHEMA;
also hosts "Change Connection", reusing the Login screen's ConnectionForm."""
import subprocess
import sys
import tkinter as tk

from net import DEFAULT_CONFIG, SETTINGS_CHOICES, SETTINGS_SCHEMA, save_config
from pixelui import BaseScreen, PixelButton
from screens.control_screen import ControlScreen
from screens.login_screen import ConnectionForm
from theme import ACCENT, BG_DEEP, BG_PANEL, TEXT_DIM, make_font

FIELD_FONT = make_font(13, mono=True)
LABEL_FONT = make_font(12)


class SettingsScreen(BaseScreen):
    # Deliberately identical to ControlScreen's size (Settings is only ever
    # opened from Control) so switching between them never resizes the
    # window - it should read as the same window, not a popup/new tab.
    WIDTH = ControlScreen.WIDTH
    HEIGHT = ControlScreen.HEIGHT

    def __init__(self, parent, app):
        super().__init__(parent, app, self.WIDTH, self.HEIGHT)
        self.app = app
        self.vars = {}  # key -> (tk variable, kind, widget)
        self._connection_overlay = None

        self.back_btn = PixelButton(self.canvas, "< BACK", command=app.show_control,
                                     width=120, height=44, font_size=13)
        self.place_widget(self.back_btn, 16, 16)

        self.canvas.create_text(self.width // 2, 38, text="SETTINGS", fill=ACCENT,
                                 font=make_font(20, bold=True), anchor="center")

        tabs_container = tk.Frame(self.canvas, bg=BG_DEEP, width=self.WIDTH - 32, height=44)
        self.place_widget(tabs_container, 16, 76)

        content_container = tk.Frame(self.canvas, bg=BG_DEEP, width=self.WIDTH - 32, height=470)
        content_container.pack_propagate(False)
        self.place_widget(content_container, 16, 132)

        self.section_frames = {}
        x = 0
        for section, fields in SETTINGS_SCHEMA.items():
            PixelButton(
                tabs_container, section.upper(), command=lambda s=section: self._show_section(s),
                width=150, height=40, font_size=11,
            ).place(x=x, y=0)
            x += 158

            frame = tk.Frame(content_container, bg=BG_DEEP)
            self._build_section(frame, fields)
            self.section_frames[section] = frame

        self._current_section = None

        btn_row = tk.Frame(self.canvas, bg=BG_DEEP)
        self.place_widget(btn_row, 16, self.HEIGHT - 160)
        PixelButton(btn_row, "SAVE SESSION", command=self._save_session, width=470, height=48, font_size=13).pack(
            side="left"
        )
        PixelButton(btn_row, "SAVE DEFAULT", command=self._save_default, width=470, height=48, font_size=13).pack(
            side="left", padx=(20, 0)
        )

        self.change_conn_btn = PixelButton(
            self.canvas, "CHANGE CONNECTION...", command=self._open_change_connection,
            width=400, height=44, font_size=12,
        )
        self.place_widget(self.change_conn_btn, 16, self.HEIGHT - 96)

        self.status_var = tk.StringVar(value="")
        status_label = tk.Label(self.canvas, textvariable=self.status_var, bg=BG_DEEP, fg=ACCENT, font=LABEL_FONT)
        self.place_widget(status_label, 16, self.HEIGHT - 40)

        self._show_section(next(iter(SETTINGS_SCHEMA)))

    def _build_section(self, frame, fields):
        for key, label, kind, low, high in fields:
            row = tk.Frame(frame, bg=BG_DEEP)
            row.pack(fill="x", pady=6)
            tk.Label(
                row, text=label, bg=BG_DEEP, fg=TEXT_DIM, font=LABEL_FONT, width=26,
                anchor="w", wraplength=280, justify="left",
            ).pack(side="left")

            widget = None  # non-None only for kinds whose displayed text can't
                           # just follow the Tk variable via textvariable binding
            if kind == "bool":
                # A plain tk.Checkbutton's indicator is drawn by Windows'
                # native theme engine, which ignores selectcolor/bg on this
                # platform - it silently stays visually "unchecked" even
                # once the variable is True. A PixelButton toggle sidesteps
                # that entirely since we render its own text ourselves.
                var = tk.BooleanVar()
                toggle_btn = PixelButton(row, "OFF", width=100, height=36, font_size=11)

                def _toggle(var=var, btn=toggle_btn):
                    var.set(not var.get())
                    btn.set_text("ON" if var.get() else "OFF")

                toggle_btn.set_command(_toggle)
                toggle_btn.pack(side="left")
                widget = toggle_btn
            elif kind == "choice":
                choices = SETTINGS_CHOICES[key]
                var = tk.StringVar(value=choices[0])
                cycle_btn = PixelButton(row, choices[0], width=130, height=36, font_size=11)

                def _cycle(var=var, choices=choices, btn=cycle_btn):
                    idx = (choices.index(var.get()) + 1) % len(choices)
                    var.set(choices[idx])
                    btn.set_text(choices[idx])

                cycle_btn.set_command(_cycle)
                cycle_btn.pack(side="left")
                widget = cycle_btn
            elif kind == "float":
                var = tk.DoubleVar()
                tk.Spinbox(
                    row, textvariable=var, from_=low, to=high, increment=0.01, width=8, format="%.2f",
                    bg=BG_PANEL, fg=ACCENT, insertbackground=ACCENT, relief="flat",
                    highlightthickness=1, highlightbackground=ACCENT, font=FIELD_FONT,
                ).pack(side="left")
            else:  # int
                var = tk.IntVar()
                tk.Spinbox(
                    row, textvariable=var, from_=low, to=high, width=8,
                    bg=BG_PANEL, fg=ACCENT, insertbackground=ACCENT, relief="flat",
                    highlightthickness=1, highlightbackground=ACCENT, font=FIELD_FONT,
                ).pack(side="left")

            self.vars[key] = (var, kind, widget)

    def _show_section(self, section):
        if self._current_section == section:
            return
        if self._current_section is not None:
            self.section_frames[self._current_section].pack_forget()
        self.section_frames[section].pack(fill="both", expand=True)
        self._current_section = section

    def refresh_from_cache(self):
        cache = self.app.settings_cache
        for key, (var, kind, widget) in self.vars.items():
            if key not in cache:
                continue
            value = cache[key]
            if kind == "choice":
                labels = SETTINGS_CHOICES[key]
                label = labels[value] if isinstance(value, int) else value
                var.set(label)
                if widget is not None:
                    widget.set_text(label)
            elif kind == "bool":
                var.set(bool(value))
                if widget is not None:
                    widget.set_text("ON" if value else "OFF")
            else:
                var.set(value)
        if cache:
            self.status_var.set("")

    def _collect(self):
        out = {}
        for key, (var, kind, widget) in self.vars.items():
            if kind == "choice":
                out[key] = SETTINGS_CHOICES[key].index(var.get())
            elif kind == "bool":
                out[key] = bool(var.get())
            elif kind == "float":
                out[key] = float(var.get())
            else:
                out[key] = int(var.get())
        return out

    def _save_session(self):
        self.app.send_settings(self._collect(), persist=False)
        self.status_var.set("Applied for this session (lost on next reboot).")

    def _save_default(self):
        self.app.send_settings(self._collect(), persist=True)
        self.status_var.set("Saved as the new boot default.")

    # ---------- change connection ----------
    def _open_change_connection(self):
        if self._connection_overlay is not None:
            return
        overlay = tk.Frame(self.canvas, bg=BG_DEEP, width=self.width, height=self.height)
        overlay.pack_propagate(False)

        ConnectionForm(
            overlay, initial_values=self.app.config or dict(DEFAULT_CONFIG),
            on_save=self._on_change_connection_save, save_label="SAVE & RESTART",
        ).pack(fill="both", expand=True, pady=(24, 0))
        PixelButton(overlay, "CANCEL", command=self._close_change_connection, width=160, height=40, font_size=12).pack(
            pady=(8, 16)
        )

        self._connection_overlay = overlay
        self.place_widget(overlay, 0, 0)

    def _close_change_connection(self):
        if self._connection_overlay is not None:
            self._connection_overlay.destroy()
            self._connection_overlay = None

    def _on_change_connection_save(self, fields):
        save_config(fields)
        self._close_change_connection()
        self.toast.show("Saved. Restarting to apply...", duration_ms=1400)
        self.after(1400, self._restart_app)

    def _restart_app(self):
        subprocess.Popen([sys.executable] + sys.argv)
        self.app.root.destroy()
