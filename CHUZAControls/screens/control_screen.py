"""The driving screen: camera viewport with game-HUD telemetry arranged
in a sidebar beside it (never overlapping the feed itself), plus the
D-pad key-press indicator below."""
import tkinter as tk

from PIL import Image, ImageTk

from pixelui import BaseScreen, Badge, DPadIndicator, HealthBar, HudStatPanel, PixelButton, draw_pixel_border
from theme import ACCENT, BG_DEEP, BG_PANEL, CRITICAL, TEXT_DIM, make_font

# Device sends native QVGA (320x240) JPEG frames; displayed at 2x with a
# smooth (LANCZOS) resize for a bigger, still-clean picture - no NEAREST
# blockiness, and no loss beyond the source camera's own resolution.
CAM_W, CAM_H = 320, 240
CAM_DISPLAY_W, CAM_DISPLAY_H = 640, 480


class ControlScreen(BaseScreen):
    WIDTH = 1000
    HEIGHT = 810
    CAM_DISPLAY_W = CAM_DISPLAY_W
    CAM_DISPLAY_H = CAM_DISPLAY_H

    def __init__(self, parent, app):
        super().__init__(parent, app, self.WIDTH, self.HEIGHT)

        # ---- top bar ----
        self.back_btn = PixelButton(self.canvas, "< BACK", command=app.show_home,
                                     width=120, height=44, font_size=13)
        self.place_widget(self.back_btn, 16, 16)

        self._status_item = self.canvas.create_text(
            self.width - 16, 16, text="", font=make_font(13, bold=True), anchor="ne",
        )
        self._mode_item = self.canvas.create_text(
            self.width - 16, 40, text="", font=make_font(12), anchor="ne",
        )
        self.set_status("connecting")
        self.set_mode("cloud")

        # ---- action row ----
        btn_w, btn_h, gap = 150, 44, 10
        actions = [
            ("SETTINGS", app.open_settings),
            ("TIMER", app.set_timer_dialog),
            ("CAPTURE", app.capture_photo),
        ]
        x = 16
        for label, cmd in actions:
            self.place_widget(PixelButton(self.canvas, label, command=cmd,
                                           width=btn_w, height=btn_h, font_size=12), x, 72)
            x += btn_w + gap

        self.cam_btn = PixelButton(self.canvas, "CAM: OFF", command=app.toggle_camera,
                                    width=btn_w, height=btn_h, font_size=12)
        self.place_widget(self.cam_btn, x, 72)

        # ---- camera viewport (left) - nothing is ever placed on top of it ----
        self.video_x = 16
        self.video_y = 170
        draw_pixel_border(
            self.canvas,
            self.video_x - 3, self.video_y - 3,
            self.video_x + CAM_DISPLAY_W + 3, self.video_y + CAM_DISPLAY_H + 3,
            thickness=3, color=ACCENT,
        )
        self._placeholder_photo = ImageTk.PhotoImage(
            Image.new("RGB", (CAM_DISPLAY_W, CAM_DISPLAY_H), "#0d130f")
        )
        self.video_label = tk.Label(self.canvas, image=self._placeholder_photo, bd=0, highlightthickness=0)
        self.place_widget(self.video_label, self.video_x, self.video_y)

        # Overheat alert lives in the gap above the video, not on it.
        self.overheat_badge = Badge(self.canvas, width=380, height=36)
        self.overheat_badge.set_text("! OVERHEAT - CAMERA OFF")
        self._overheat_shown = False

        # ---- telemetry sidebar (right) ----
        sidebar_x = self.video_x + CAM_DISPLAY_W + 24
        panel_w = self.WIDTH - sidebar_x - 16
        y = self.video_y

        self.env_panel = HudStatPanel(self.canvas, "ENVIRONMENT", width=panel_w, height=108)
        self.place_widget(self.env_panel, sidebar_x, y)
        y += 108 + 18

        self.battery_bar = HealthBar(self.canvas, app.ticker, width=panel_w, height=64)
        self.place_widget(self.battery_bar, sidebar_x, y)
        y += 64 + 18

        self.distance_panel = HudStatPanel(self.canvas, "DISTANCE / CHIP TEMP", width=panel_w, height=108)
        self.place_widget(self.distance_panel, sidebar_x, y)
        y += 108 + 18

        self.link_panel = HudStatPanel(self.canvas, "LINK", width=panel_w, height=94)
        self.place_widget(self.link_panel, sidebar_x, y)

        # ---- below the feed: D-pad + numeric target readout ----
        dpad_y = self.video_y + CAM_DISPLAY_H + 16
        self.dpad = DPadIndicator(self.canvas, width=210, height=120)
        self.place_widget(self.dpad, self.video_x, dpad_y)

        self._target_item = self.canvas.create_text(
            self.video_x + 230, dpad_y + 20, text="", font=make_font(15, bold=True, mono=True), anchor="nw",
        )
        self.set_targets(0, 0)

        self._timer_overlay = None

    # ---------- set timer ----------
    def open_timer_dialog(self):
        if self._timer_overlay is not None:
            return
        w, h = 320, 240
        overlay = tk.Frame(self.canvas, bg=BG_PANEL, width=w, height=h,
                            highlightthickness=2, highlightbackground=ACCENT)
        overlay.pack_propagate(False)

        tk.Label(overlay, text="SET TIMER", bg=BG_PANEL, fg=ACCENT, font=make_font(16, bold=True)).pack(
            pady=(20, 12)
        )

        minutes_var = tk.IntVar(value=5)
        tk.Spinbox(
            overlay, from_=1, to=60, textvariable=minutes_var, width=5, justify="center",
            bg=BG_DEEP, fg=ACCENT, insertbackground=ACCENT, relief="flat",
            highlightthickness=1, highlightbackground=ACCENT, font=make_font(20, mono=True),
        ).pack()
        tk.Label(overlay, text="minutes", bg=BG_PANEL, fg=TEXT_DIM, font=make_font(11)).pack(pady=(4, 14))

        btn_row = tk.Frame(overlay, bg=BG_PANEL)
        btn_row.pack()

        def confirm():
            minutes = minutes_var.get()
            self.close_timer_dialog()
            self.app.on_timer_confirmed(minutes)

        PixelButton(btn_row, "SET", command=confirm, width=110, height=44, font_size=12).pack(
            side="left", padx=6
        )
        PixelButton(btn_row, "CANCEL", command=self.close_timer_dialog, width=110, height=44, font_size=12).pack(
            side="left", padx=6
        )

        self._timer_overlay = overlay
        self.place_widget(overlay, (self.WIDTH - w) // 2, (self.HEIGHT - h) // 2)

    def close_timer_dialog(self):
        if self._timer_overlay is not None:
            self._timer_overlay.destroy()
            self._timer_overlay = None

    # ---------- top bar / status ----------
    def set_status(self, status):
        color = {"online": ACCENT, "offline": CRITICAL}.get(status, ACCENT)
        text = {"online": "ONLINE", "offline": "OFFLINE", "connecting": "CONNECTING"}.get(status, status.upper())
        self.canvas.itemconfigure(self._status_item, text=f"● {text}", fill=color)

    def set_mode(self, mode):
        self.canvas.itemconfigure(self._mode_item, text=f"MODE: {mode.upper()}", fill=ACCENT)

    # ---------- camera ----------
    def set_camera_label(self, on):
        self.cam_btn.set_text(f"CAM: {'ON' if on else 'OFF'}")

    def set_frame(self, photo):
        self.video_label.config(image=photo)

    def reset_video(self):
        self.video_label.config(image=self._placeholder_photo)

    # ---------- HUD (complete telemetry) ----------
    def set_env(self, temp_c, humidity, pressure_hpa, altitude_m):
        line1 = f"{temp_c:.1f}°C" if temp_c is not None else "--"
        if humidity is not None:
            line1 += f"   {humidity:.0f}% RH"
        line2_parts = []
        if pressure_hpa is not None:
            line2_parts.append(f"{pressure_hpa:.0f} hPa")
        if altitude_m is not None:
            line2_parts.append(f"{altitude_m:.0f} m")
        line2 = "   ".join(line2_parts) if line2_parts else "--"
        self.env_panel.set_lines([(line1, ACCENT), (line2, ACCENT)])

    def set_battery(self, pct, volts=None):
        suffix = f"  ({volts:.2f}V)" if volts is not None else ""
        self.battery_bar.set_pct(pct if pct is not None else 0, suffix=suffix)

    def set_distance(self, mm, out_of_range, chip_temp_c=None):
        if mm is None:
            line1 = "--"
        elif out_of_range:
            line1 = "Out of range"
        else:
            line1 = f"{mm} mm"
        line2 = f"Chip: {chip_temp_c:.1f}°C" if chip_temp_c is not None else "Chip: --"
        self.distance_panel.set_lines([(line1, ACCENT), (line2, TEXT_DIM)])

    def set_link(self, mode, fps):
        self.link_panel.set_lines([(mode.upper(), ACCENT), (f"{fps} fps", ACCENT)])

    def set_overheat(self, active):
        if active and not self._overheat_shown:
            badge_x = self.video_x + (CAM_DISPLAY_W - 380) // 2
            badge_y = self.video_y - 44  # in the gap above the video, never on it
            self.place_widget(self.overheat_badge, badge_x, badge_y)
            self._overheat_shown = True
        elif not active and self._overheat_shown:
            self.overheat_badge.place_forget()
            self._overheat_shown = False

    def set_dpad(self, pressed):
        self.dpad.set_pressed(pressed)

    def set_targets(self, left, right, braking=False):
        text = "BRAKE   BRAKE" if braking else f"L: {left:>4}%   R: {right:>4}%"
        self.canvas.itemconfigure(self._target_item, text=text)
