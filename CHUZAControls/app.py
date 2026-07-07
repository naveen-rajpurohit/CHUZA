import os
import io
import time
from collections import deque
import tkinter as tk

from PIL import Image, ImageTk  # type: ignore

from net import get_app_dir, try_load_config, save_config, MqttBridge, LocalLink
from theme import enable_dpi_awareness, resource_path
from pixelui import Ticker
from screens.home_screen import HomeScreen
from screens.control_screen import ControlScreen
from screens.login_screen import LoginScreen
from screens.settings_screen import SettingsScreen

APP_TITLE = "CHUZA Control"

# --- Tuning knobs ---
BASE_SPEED = 70          # % speed for w/s, and for solo a/d pivot turns
BOOST_SPEED = 100         # % speed when the boost key (Space) is held
TURN_BIAS = 20           # % differential nudge when turning *while* moving
CONTROL_TICK_MS = 100    # how often we recompute + publish targets
KEY_RELEASE_DEBOUNCE_MS = 50  # swallow OS auto-repeat release/press pairs
FEED_FPS_WINDOW_SEC = 1.0     # rolling window for the client-side FPS KPI


class ControlApp:
    def __init__(self, root):
        self.root = root
        self.config = None
        self.local = None
        self.mqtt = None
        self.root.title(APP_TITLE)
        try:
            self.root.iconbitmap(resource_path("assets", "icon.ico"))
        except Exception as e:
            print("iconbitmap failed:", e)
        self.root.resizable(False, False)
        self._login_geometry = f"{LoginScreen.WIDTH}x{LoginScreen.HEIGHT}"
        self._home_geometry = f"{HomeScreen.WIDTH}x{HomeScreen.HEIGHT}"
        self._control_geometry = f"{ControlScreen.WIDTH}x{ControlScreen.HEIGHT}"
        self._settings_geometry = f"{SettingsScreen.WIDTH}x{SettingsScreen.HEIGHT}"

        self.ticker = Ticker(self.root)

        self.status = "connecting"  # connecting | online | offline
        self.pressed = set()
        self._release_timers = {}
        self.control_active = False

        self.cam_wanted_on = False  # what we last asked for (survives until telemetry confirms)
        self._photo = None          # keep a reference so Tk doesn't GC the image
        self._frame_times = deque()
        self._last_frame_bytes = None  # raw JPEG of the last decoded frame, for Capture Photo
        self._link_mode = "cloud"
        self._telemetry = {}  # last known value per telemetry field - fields can arrive
                               # independently (one sensor's tick at a time), not bundled

        self.settings_cache = {}    # latest robot/settings snapshot, keyed same as SETTINGS_SCHEMA

        self.login = LoginScreen(self.root, self)
        self.home = HomeScreen(self.root, self)
        self.control = ControlScreen(self.root, self)
        self.settings = SettingsScreen(self.root, self)

        config = try_load_config()
        if config is not None:
            self._start_networking(config)
            self.show_home()
        else:
            self.show_login()

        self.root.protocol("WM_DELETE_WINDOW", self._on_close)

    def _start_networking(self, config):
        self.config = config
        self.local = LocalLink(self._on_frame, self._on_local_mode_change)
        self.mqtt = MqttBridge(
            config, self._on_mqtt_status, self._on_telemetry, self._on_frame, self._on_settings
        )

    def on_login_submit(self, fields):
        config = save_config(fields)
        self._start_networking(config)
        self.show_home()

    # ---------- views ----------
    def _switch_to(self, screen, geometry):
        for other in (self.login, self.home, self.control, self.settings):
            if other is not screen:
                other.pack_forget()
        screen.pack(fill="both", expand=True)
        self.root.geometry(geometry)

    def show_login(self):
        self._switch_to(self.login, self._login_geometry)

    def show_home(self):
        self.control_active = False
        self.pressed.clear()
        self._switch_to(self.home, self._home_geometry)
        # make sure the robot doesn't keep moving - or streaming - after we leave this screen
        if self.mqtt is not None:
            self._send_command({"cmd": "stop"})
            self._set_camera(False)
        else:
            self._reset_video()

    def show_control(self):
        self._switch_to(self.control, self._control_geometry)
        self.control_active = True
        self.root.focus_force()
        self._control_tick()

    def show_settings(self):
        self.control_active = False
        self._switch_to(self.settings, self._settings_geometry)
        self.request_settings_refresh()
        self.settings.refresh_from_cache()

    # ---------- mqtt callbacks (run on a background thread — marshal to Tk) ----------
    def _on_mqtt_status(self, payload):
        self.root.after(0, lambda: self._apply_status(payload))

    def _apply_status(self, payload):
        if payload == "online":
            self.status = "online"
            self.home.set_status("online")
            self.home.set_play_enabled(True)
            self.control.set_status("online")
        elif payload == "offline":
            self.status = "offline"
            self.home.set_status("offline")
            self.home.set_play_enabled(False)
            self.control.set_status("offline")
            if self.control_active:
                self.show_home()
                self.home.toast.show("Lost connection to the robot.", kind="error")
        else:  # "connecting" or anything unrecognized
            self.home.set_status("connecting")
            self.home.set_play_enabled(False)
            self.control.set_status("connecting")

    def _on_telemetry(self, data):
        self.root.after(0, lambda: self._apply_telemetry(data))

    _ENV_KEYS = ("tempC", "humidity", "pressureHpa", "altitudeM")
    _BATT_KEYS = ("battPct", "battV")
    _DIST_KEYS = ("distanceMm", "chipTempC")

    def _apply_telemetry(self, data):
        # Each sensor publishes on its own schedule, so a given message may
        # contain only a subset of these fields - track the last known
        # value of each so every HUD panel re-renders with complete data
        # rather than clobbering fields the current message didn't include.
        self._telemetry.update(data)
        t = self._telemetry

        if any(k in data for k in self._ENV_KEYS):
            self.control.set_env(t.get("tempC"), t.get("humidity"), t.get("pressureHpa"), t.get("altitudeM"))
        if any(k in data for k in self._BATT_KEYS):
            self.control.set_battery(t.get("battPct"), t.get("battV"))
        if any(k in data for k in self._DIST_KEYS):
            # 8190 is the VL53L0X's own "nothing in range" sentinel, not a real distance.
            mm = t.get("distanceMm")
            out_of_range = mm is not None and mm >= 8190
            self.control.set_distance(mm, out_of_range, t.get("chipTempC"))
        if "camOverheat" in data:
            self.control.set_overheat(bool(data["camOverheat"]))
        if "camOn" in data:
            # Reflects the device's actual state (e.g. another client toggled
            # it, or a reconnect happened) rather than just our last click.
            self.control.set_camera_label(data["camOn"])
            if not data["camOn"]:
                self._reset_video()
        if "ip" in data:
            # Lets LocalLink start probing for a same-network fast path.
            self.local.set_ip(data["ip"])

    # ---------- settings ----------
    def _on_settings(self, data):
        self.root.after(0, lambda: self._apply_settings(data))

    def _apply_settings(self, data):
        self.settings_cache.update(data)
        # The device echoes back the authoritative (clamped) values after
        # every set_settings, so keep the Settings screen in sync rather
        # than showing stale numbers the user just changed.
        self.settings.refresh_from_cache()

    def request_settings_refresh(self):
        # Always MQTT, never _send_command()'s local-preferring routing:
        # CHUZALocalLink's UDP path calls dispatchRobotCommand() with
        # settings=nullptr (see CHUZACommand.h), so get_settings/
        # set_settings silently no-op over LAN-direct - the device was
        # never meant to receive these outside the cloud connection.
        self.mqtt.publish_command({"cmd": "get_settings"})

    def send_settings(self, fields, persist):
        self.mqtt.publish_command({"cmd": "set_settings", "persist": persist, "settings": fields})

    def open_settings(self):
        self.show_settings()

    # ---------- quick actions ----------
    def set_timer_dialog(self):
        self.control.open_timer_dialog()

    def on_timer_confirmed(self, minutes):
        # Always MQTT - see request_settings_refresh() for why: the UDP
        # path calls dispatchRobotCommand() with face=nullptr, so
        # set_timer silently no-ops over LAN-direct.
        self.mqtt.publish_command({"cmd": "set_timer", "minutes": minutes})
        self.control.toast.show(f"Timer set: {minutes} min")

    def capture_photo(self):
        if self._last_frame_bytes is None:
            self.control.toast.show("No camera frame yet - turn the camera on first.", kind="warning")
            return
        photos_dir = os.path.join(get_app_dir(), "Photos")
        os.makedirs(photos_dir, exist_ok=True)
        filename = f"chuza_{time.strftime('%Y%m%d_%H%M%S')}.jpg"
        with open(os.path.join(photos_dir, filename), "wb") as f:
            f.write(self._last_frame_bytes)
        self.control.toast.show(f"Saved: {filename}")

    # ---------- LAN-direct / cloud switching ----------
    def _send_command(self, payload_dict):
        if not self.local.send_command(payload_dict):
            self.mqtt.publish_command(payload_dict)

    def _on_local_mode_change(self, mode):
        self.root.after(0, lambda: self._apply_link_mode(mode))

    def _apply_link_mode(self, mode):
        self._link_mode = "local" if mode == "local" else "cloud"
        self.control.set_mode(self._link_mode)
        self.control.set_link(self._link_mode, len(self._frame_times))

    # ---------- camera ----------
    def toggle_camera(self):
        self._set_camera(not self.cam_wanted_on)

    def _set_camera(self, on):
        self.cam_wanted_on = on
        self.control.set_camera_label(on)
        self._send_command({"cmd": "cam_on" if on else "cam_off"})
        if not on:
            self._reset_video()

    def _reset_video(self):
        self._frame_times.clear()
        self.control.set_link(self._link_mode, 0)
        self.control.reset_video()
        self._photo = None

    def _on_frame(self, payload):
        self.root.after(0, lambda: self._apply_frame(payload))

    def _apply_frame(self, payload):
        if not self.control_active:
            return  # a frame arrived after we already navigated away
        try:
            image = Image.open(io.BytesIO(payload))
        except Exception:
            return  # partial/corrupt JPEG - just drop it, next frame will be fine

        self._last_frame_bytes = payload
        # Device sends native QVGA (320x240); smoothly upscale to the
        # displayed size for a bigger picture without blocky pixelation.
        image = image.resize((ControlScreen.CAM_DISPLAY_W, ControlScreen.CAM_DISPLAY_H), Image.LANCZOS)
        self._photo = ImageTk.PhotoImage(image)
        self.control.set_frame(self._photo)

        now = time.time()
        self._frame_times.append(now)
        while self._frame_times and now - self._frame_times[0] > FEED_FPS_WINDOW_SEC:
            self._frame_times.popleft()
        self.control.set_link(self._link_mode, len(self._frame_times))

    # ---------- keyboard handling ----------
    def bind_keys(self):
        self.root.bind_all("<KeyPress>", self._on_key_press)
        self.root.bind_all("<KeyRelease>", self._on_key_release)

    @staticmethod
    def _normalize(keysym):
        k = keysym.lower()
        if k in ("w", "a", "s", "d", "space", "b"):
            return k
        return None

    def _on_key_press(self, event):
        key = self._normalize(event.keysym)
        if key is None:
            return
        if key in self._release_timers:
            self.root.after_cancel(self._release_timers.pop(key))
        self.pressed.add(key)

    def _on_key_release(self, event):
        key = self._normalize(event.keysym)
        if key is None:
            return

        def finalize():
            self.pressed.discard(key)
            self._release_timers.pop(key, None)

        self._release_timers[key] = self.root.after(KEY_RELEASE_DEBOUNCE_MS, finalize)

    # ---------- control loop ----------
    def _compute_targets(self):
        boost = "space" in self.pressed
        base = BOOST_SPEED if boost else BASE_SPEED

        w = "w" in self.pressed
        s = "s" in self.pressed
        a = "a" in self.pressed
        d = "d" in self.pressed

        left = right = 0

        if w:
            left += base
            right += base
        if s:
            left -= base
            right -= base

        moving_front = w 
        moving_back = s
        if moving_front:
            # gentle arc while already moving forward
            if d:
                left += TURN_BIAS
                right -= TURN_BIAS
            if a:
                left -= TURN_BIAS
                right += TURN_BIAS

        elif moving_back:
            # gentle arc while already moving back
            if d:
                left -= TURN_BIAS
                right += TURN_BIAS
            if a:
                left += TURN_BIAS
                right -= TURN_BIAS
        else:
            # pure pivot turn in place
            if d:
                left += base
                right -= base
            if a:
                left -= base
                right += base

        left = max(-100, min(100, left))
        right = max(-100, min(100, right))
        return left, right

    def _control_tick(self):
        if not self.control_active:
            return

        self.control.set_dpad(self.pressed)
        if "b" in self.pressed:
            self._send_command({"cmd": "brake"})
            self.control.set_targets(0, 0, braking=True)
        else:
            left, right = self._compute_targets()
            self._send_command({"cmd": "move", "left": left, "right": right})
            self.control.set_targets(left, right)

        self.root.after(CONTROL_TICK_MS, self._control_tick)

    def _on_close(self):
        try:
            self._send_command({"cmd": "brake"})
            self._send_command({"cmd": "cam_off"})
        except Exception:
            pass
        self.root.destroy()


def main():
    enable_dpi_awareness()
    root = tk.Tk()
    app = ControlApp(root)
    app.bind_keys()
    root.mainloop()


if __name__ == "__main__":
    main()
