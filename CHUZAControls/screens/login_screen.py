"""The connect/credentials screen shown when no valid config.json exists
yet - replaces the old flow of hand-editing a placeholder JSON file and
relaunching. Also reused by the in-app Settings screen's "Change
Connection" entry via ConnectionForm.
"""
import tkinter as tk

from PIL import Image, ImageTk

from net import DEFAULT_CONFIG
from pixelui import BaseScreen, PixelButton
from theme import ACCENT, BG_DEEP, BG_PANEL, CRITICAL, TEXT_DIM, make_font, resource_path

FIELD_FONT = make_font(14, mono=True)
LABEL_FONT = make_font(11)


class ConnectionForm(tk.Frame):
    """Broker host/port/username/password fields, with the rarely-
    changed topic overrides tucked behind an Advanced disclosure.
    Plain tk widgets (not ttk/canvas) so real text entry stays fully
    usable - only their colors/fonts are themed."""

    TOPIC_FIELDS = [
        ("topic_status", "Status topic"),
        ("topic_telemetry", "Telemetry topic"),
        ("topic_commands", "Commands topic"),
        ("topic_camera_frame", "Camera frame topic"),
        ("topic_settings", "Settings topic"),
    ]

    def __init__(self, parent, initial_values, on_save, save_label="CONNECT"):
        super().__init__(parent, bg=BG_DEEP)
        self.on_save = on_save
        self.vars = {}

        self._add_field("broker_host", "Broker host", initial_values)
        self._add_field("broker_port", "Broker port", initial_values)
        self._add_field("username", "Username", initial_values)
        self._add_field("password", "Password", initial_values, show="*")

        self._advanced_shown = False
        self.advanced_btn = PixelButton(self, "ADVANCED >", command=self._toggle_advanced,
                                         width=200, height=36, font_size=11)
        self.advanced_btn.pack(pady=(10, 10))

        # The topic overrides are scrollable at a fixed, modest height
        # rather than expanding the window - laptop-sized screens can't
        # always fit a login form tall enough for 4 fields + 5 more topic
        # fields all expanded at once, and a non-resizable window can't
        # be grown to compensate the way a browser page could.
        self.advanced_frame = tk.Frame(self, bg=BG_DEEP)
        adv_canvas = tk.Canvas(self.advanced_frame, bg=BG_DEEP, height=170, highlightthickness=0)
        adv_scroll = tk.Scrollbar(self.advanced_frame, orient="vertical", command=adv_canvas.yview)
        adv_inner = tk.Frame(adv_canvas, bg=BG_DEEP)

        adv_window = adv_canvas.create_window((0, 0), window=adv_inner, anchor="nw")
        adv_inner.bind("<Configure>", lambda e: adv_canvas.configure(scrollregion=adv_canvas.bbox("all")))
        adv_canvas.bind("<Configure>", lambda e: adv_canvas.itemconfigure(adv_window, width=e.width))
        adv_canvas.configure(yscrollcommand=adv_scroll.set)

        def _on_mousewheel(event):
            adv_canvas.yview_scroll(int(-1 * (event.delta / 120)), "units")

        adv_canvas.bind("<Enter>", lambda e: adv_canvas.bind_all("<MouseWheel>", _on_mousewheel))
        adv_canvas.bind("<Leave>", lambda e: adv_canvas.unbind_all("<MouseWheel>"))

        adv_canvas.pack(side="left", fill="both", expand=True)
        adv_scroll.pack(side="right", fill="y")

        for key, label in self.TOPIC_FIELDS:
            self._add_field(key, label, initial_values, parent_frame=adv_inner)

        self.error_var = tk.StringVar(value="")
        self.error_label = tk.Label(
            self, textvariable=self.error_var, bg=BG_DEEP, fg=CRITICAL, font=LABEL_FONT, wraplength=440,
        )
        self.error_label.pack(pady=(6, 0))

        self.save_btn = PixelButton(self, save_label, command=self._submit, width=240, height=52, font_size=15)
        self.save_btn.pack(pady=(16, 6))

    def _add_field(self, key, label, initial_values, show=None, parent_frame=None):
        frame = parent_frame if parent_frame is not None else self
        row = tk.Frame(frame, bg=BG_DEEP)
        row.pack(fill="x", pady=5, padx=40)
        tk.Label(row, text=label, bg=BG_DEEP, fg=TEXT_DIM, font=LABEL_FONT, anchor="w").pack(fill="x")
        var = tk.StringVar(value=str(initial_values.get(key, "")))
        entry = tk.Entry(
            row, textvariable=var, font=FIELD_FONT, bg=BG_PANEL, fg=ACCENT,
            insertbackground=ACCENT, relief="flat",
            highlightthickness=2, highlightbackground=ACCENT, highlightcolor=ACCENT,
            show=show,
        )
        entry.pack(fill="x", ipady=6)
        self.vars[key] = var

    def _toggle_advanced(self):
        self._advanced_shown = not self._advanced_shown
        if self._advanced_shown:
            # pack(before=...) is required here, not just pack(): this frame
            # is packed on demand (long after error_label/save_btn were
            # already packed during __init__), and pack() with no position
            # hint would otherwise always append it after those, below the
            # Connect button, regardless of where it's defined in the code.
            self.advanced_frame.pack(fill="x", before=self.error_label)
            self.advanced_btn.set_text("ADVANCED v")
        else:
            self.advanced_frame.pack_forget()
            self.advanced_btn.set_text("ADVANCED >")

    def _collect(self):
        return {key: var.get().strip() for key, var in self.vars.items()}

    def _submit(self):
        fields = self._collect()
        if not fields.get("broker_host") or not fields.get("username") or not fields.get("password"):
            self.error_var.set("Host, username, and password are required.")
            return
        try:
            fields["broker_port"] = int(fields.get("broker_port") or 8883)
        except ValueError:
            self.error_var.set("Port must be a number.")
            return
        self.error_var.set("")
        self.on_save(fields)


class LoginScreen(BaseScreen):
    WIDTH = 560
    HEIGHT = 820

    def __init__(self, parent, app):
        super().__init__(parent, app, self.WIDTH, self.HEIGHT)

        logo_img = Image.open(resource_path("assets", "logo.png"))
        self._logo_photo = ImageTk.PhotoImage(logo_img)
        self.canvas.create_image(self.width // 2, 90, image=self._logo_photo, anchor="center")

        self.canvas.create_text(
            self.width // 2, 160, text="CONNECT TO YOUR ROBOT",
            fill=ACCENT, font=make_font(13, bold=True), anchor="center",
        )

        self.form = ConnectionForm(
            self.canvas,
            initial_values=DEFAULT_CONFIG,
            on_save=app.on_login_submit,
            save_label="CONNECT",
        )
        self.place_widget(self.form, 0, 190)
