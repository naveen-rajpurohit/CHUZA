import sys
import os
import io
import json
import re
import socket
import time
import threading
from collections import deque
import tkinter as tk
from tkinter import ttk, messagebox, simpledialog
import paho.mqtt.client as mqtt # type: ignore
import requests # type: ignore
from PIL import Image, ImageTk # type: ignore

APP_TITLE = "CHUZA Control"

# --- Tuning knobs ---
BASE_SPEED = 70          # % speed for w/s, and for solo a/d pivot turns
BOOST_SPEED = 100         # % speed when the boost key (Space) is held
TURN_BIAS = 20           # % differential nudge when turning *while* moving
CONTROL_TICK_MS = 100    # how often we recompute + publish targets
KEY_RELEASE_DEBOUNCE_MS = 50  # swallow OS auto-repeat release/press pairs
FEED_FPS_WINDOW_SEC = 1.0     # rolling window for the client-side FPS KPI

# Device sends QVGA (320x240) frames. Displayed 1:1 for now - the 3x
# nearest-neighbor blow-up made the pixelation too distracting.
CAM_DISPLAY_W, CAM_DISPLAY_H = 320, 240

# --- LAN-direct fallback ---
# Matches CHUZALocalLink on the device: a UDP command listener + local
# MJPEG stream, used automatically instead of MQTT whenever the robot
# turns out to be reachable on the same network - lower latency, much
# higher achievable fps, no cloud bandwidth. Falls back to MQTT the
# moment the probe fails (different network, robot out of range, etc.)
# /stream lives on its own port (81), separate from /ping (80) - the
# device runs them as two independent httpd instances so a long-running
# stream connection can never block the reachability probe.
LOCAL_UDP_PORT = 4210
LOCAL_STREAM_PORT = 81
LOCAL_PROBE_INTERVAL_SEC = 4.0
LOCAL_PROBE_TIMEOUT_SEC = 0.6

# --- Settings screen ---
# Mirrors RobotSettings (lib/CHUZASettings on the device) field-for-field -
# key names here are the exact JSON keys published on robot/settings and
# expected in a set_settings command's "settings" object. (key, label,
# kind, low, high) - low/high are the Spinbox range for int/float kinds,
# unused for bool/choice.
SETTINGS_SCHEMA = {
    "Timing": [
        ("angryTimeoutSec", "Angry timeout (sec)", "int", 5, 3600),
        ("tiredHoldSec", "Tired hold (sec)", "int", 1, 600),
        ("menuTimeoutSec", "Menu auto-off (sec)", "int", 2, 120),
        ("timerAlarmSec", "Timer alarm duration (sec)", "int", 1, 120),
    ],
    "Threshold": [
        ("lowBattPct", "Low battery (%)", "int", 0, 100),
        ("cliffThresholdMm", "Cliff threshold (mm)", "int", 10, 500),
        ("touchSensitivity", "Touch sensitivity", "float", 1.02, 3.0),
        ("wheelMinPwm", "Wheel min PWM", "int", 0, 255),
        ("wheelMaxPwm", "Wheel max PWM", "int", 0, 255),
        ("wheelRampRate", "Wheel ramp rate (%/s)", "int", 10, 2000),
    ],
    "Hardware": [
        ("motorsEnabled", "Motors", "bool", None, None),
        ("oledEnabled", "OLED display", "bool", None, None),
        ("buzzerEnabled", "Buzzer", "bool", None, None),
        ("envSensorEnabled", "Environment sensor (BME280)", "bool", None, None),
        ("distSensorEnabled", "Distance sensor (ToF)", "bool", None, None),
    ],
    "Behavior": [
        ("wanderMode", "Wander mode", "choice", None, None),
    ],
}
SETTINGS_CHOICES = {"wanderMode": ["OFF", "SOFT", "NORMAL"]}


def get_app_dir():
    """Folder the exe (or script) lives in — used to find config.json
    next to it, so credentials can be edited without rebuilding."""
    if getattr(sys, "frozen", False):
        return os.path.dirname(sys.executable)
    return os.path.dirname(os.path.abspath(__file__))


def load_config():
    path = os.path.join(get_app_dir(), "config.json")
    if not os.path.exists(path):
        default = {
            "broker_host": "REPLACE_ME.s1.eu.hivemq.cloud",
            "broker_port": 8883,
            "username": "REPLACE_ME",
            "password": "REPLACE_ME",
            "topic_status": "robot/status",
            "topic_telemetry": "robot/telemetry",
            "topic_commands": "robot/commands",
            "topic_camera_frame": "robot/camera/frame",
            "topic_settings": "robot/settings",
        }
        with open(path, "w") as f:
            json.dump(default, f, indent=2)
        messagebox.showinfo(
            APP_TITLE,
            "Created a config.json next to the app at:\n"
            f"{path}\n\n"
            "Fill in your HiveMQ Cloud details there, then relaunch.",
        )
        sys.exit(0)
    with open(path) as f:
        return json.load(f)


class MqttBridge:
    """Owns the paho-mqtt client. All inbound callbacks run on paho's
    background thread — they only ever call back into the GUI via the
    provided callbacks, which the GUI marshals onto the Tk thread."""

    def __init__(self, config, on_status, on_telemetry, on_frame, on_settings):
        self.cfg = config
        self.on_status_cb = on_status
        self.on_telemetry_cb = on_telemetry
        self.on_frame_cb = on_frame
        self.on_settings_cb = on_settings

        self.client = mqtt.Client(
            client_id=f"chuza-pc-{int(time.time())}", clean_session=True
        )
        self.client.username_pw_set(config["username"], config["password"])
        self.client.tls_set()  # standard TLS; validates via system CA store
        self.client.on_connect = self._on_connect
        self.client.on_disconnect = self._on_disconnect
        self.client.on_message = self._on_message

        threading.Thread(target=self._connect_loop, daemon=True).start()

    def _connect_loop(self):
        while True:
            try:
                self.on_status_cb("connecting")
                self.client.connect(
                    self.cfg["broker_host"], self.cfg["broker_port"], keepalive=30
                )
                self.client.loop_start()
                return
            except Exception as e:
                print("MQTT connect failed, retrying in 5s:", e)
                time.sleep(5)

    def _on_connect(self, client, userdata, flags, rc):
        if rc == 0:
            client.subscribe(self.cfg["topic_status"])
            client.subscribe(self.cfg["topic_telemetry"])
            client.subscribe(self.cfg.get("topic_camera_frame", "robot/camera/frame"))
            client.subscribe(self.cfg.get("topic_settings", "robot/settings"))
        else:
            self.on_status_cb("connecting")

    def _on_disconnect(self, client, userdata, rc):
        self.on_status_cb("connecting")
        if rc != 0:
            threading.Thread(target=self._reconnect_loop, daemon=True).start()

    def _reconnect_loop(self):
        while True:
            time.sleep(5)
            try:
                self.client.reconnect()
                return
            except Exception as e:
                print("Reconnect failed:", e)

    def _on_message(self, client, userdata, msg):
        if msg.topic == self.cfg["topic_status"]:
            self.on_status_cb(msg.payload.decode(errors="ignore"))
        elif msg.topic == self.cfg["topic_telemetry"]:
            try:
                self.on_telemetry_cb(json.loads(msg.payload.decode()))
            except (json.JSONDecodeError, UnicodeDecodeError):
                pass
        elif msg.topic == self.cfg.get("topic_camera_frame", "robot/camera/frame"):
            self.on_frame_cb(msg.payload)
        elif msg.topic == self.cfg.get("topic_settings", "robot/settings"):
            try:
                self.on_settings_cb(json.loads(msg.payload.decode()))
            except (json.JSONDecodeError, UnicodeDecodeError):
                pass

    def publish_command(self, payload_dict):
        self.client.publish(self.cfg["topic_commands"], json.dumps(payload_dict), qos=0)


class LocalLink:
    """LAN-direct control + video path. The device's IP comes from MQTT
    telemetry (set_ip); every few seconds a background thread probes
    that IP's /ping endpoint, and switches to local UDP commands + the
    device's own MJPEG /stream the moment it's reachable, falling back
    the moment it isn't. Runs entirely on background threads - frames
    go through the same on_frame callback MqttBridge uses, marshaled to
    Tk by the caller exactly like the MQTT path already is."""

    def __init__(self, on_frame, on_mode_change):
        self.on_frame_cb = on_frame
        self.on_mode_change_cb = on_mode_change
        self.ip = None
        self.is_local = False
        self._want_stream = False
        self._sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        threading.Thread(target=self._probe_loop, daemon=True).start()

    def set_ip(self, ip):
        self.ip = ip

    def send_command(self, payload_dict):
        """Returns True if the command actually went out over the LAN;
        the caller should fall back to MQTT on False."""
        if not (self.is_local and self.ip):
            return False
        try:
            self._sock.sendto(json.dumps(payload_dict).encode(), (self.ip, LOCAL_UDP_PORT))
            return True
        except OSError:
            return False

    def _probe_loop(self):
        while True:
            time.sleep(LOCAL_PROBE_INTERVAL_SEC)
            ip = self.ip
            if not ip:
                continue
            reachable = self._probe(ip)
            if reachable and not self.is_local:
                self._switch_to_local(ip)
            elif not reachable and self.is_local:
                self._switch_to_cloud()

    @staticmethod
    def _probe(ip):
        try:
            r = requests.get(f"http://{ip}/ping", timeout=LOCAL_PROBE_TIMEOUT_SEC)
            return r.status_code == 200
        except requests.exceptions.RequestException:
            return False

    def _switch_to_local(self, ip):
        self.is_local = True
        self._want_stream = True
        threading.Thread(target=self._stream_loop, args=(ip,), daemon=True).start()
        self.on_mode_change_cb("local")

    def _switch_to_cloud(self):
        self.is_local = False
        self._want_stream = False
        self.on_mode_change_cb("cloud")

    def _stream_loop(self, ip):
        while self._want_stream and self.ip == ip:
            resp = None
            try:
                resp = requests.get(f"http://{ip}:{LOCAL_STREAM_PORT}/stream", stream=True, timeout=5)
                self._consume_stream(resp)
            except requests.exceptions.RequestException:
                pass
            finally:
                # The device's httpd server only has a handful of
                # concurrent connection slots - leaving this open would
                # eventually starve it out (including /ping), so always
                # release it before maybe reconnecting.
                if resp is not None:
                    resp.close()
            if self._want_stream:
                time.sleep(1)  # brief backoff before retrying a dropped stream

    def _consume_stream(self, resp):
        """Manually parses the multipart/x-mixed-replace body from
        CHUZALocalLink's streamHandler: repeating
        "--frame\\r\\nContent-Type: ...\\r\\nContent-Length: N\\r\\n\\r\\n"
        + N raw JPEG bytes. Both ends are code we control, so the format
        is intentionally this simple."""
        buf = b""
        raw = resp.raw
        while self._want_stream:
            chunk = raw.read(4096)
            if not chunk:
                break
            buf += chunk
            while True:
                start = buf.find(b"--frame\r\n")
                if start == -1:
                    if len(buf) > 65536:
                        buf = buf[-4096:]  # boundary never found - drop stale data
                    break
                header_start = start + len(b"--frame\r\n")
                header_end = buf.find(b"\r\n\r\n", header_start)
                if header_end == -1:
                    break  # header incomplete - wait for more data
                header = buf[header_start:header_end].decode(errors="ignore")
                m = re.search(r"Content-Length:\s*(\d+)", header, re.IGNORECASE)
                if not m:
                    buf = buf[header_end + 4:]
                    continue
                length = int(m.group(1))
                body_start = header_end + 4
                if len(buf) < body_start + length:
                    break  # body incomplete - wait for more data
                self.on_frame_cb(buf[body_start:body_start + length])
                buf = buf[body_start + length:]


class SettingsWindow(tk.Toplevel):
    """One tab per SETTINGS_SCHEMA section, each field bound to a Tk
    variable. Values are seeded from ControlApp.settings_cache (the last
    robot/settings snapshot) and refreshed live if a new one arrives
    while this window is open (see ControlApp._apply_settings) - e.g.
    right after Save, so the fields always show what the device actually
    applied/clamped rather than just what was typed."""

    def __init__(self, app):
        super().__init__(app.root)
        self.app = app
        self.title("CHUZA Settings")
        self.geometry("380x480")
        self.vars = {}  # key -> (Tk variable, kind)

        notebook = ttk.Notebook(self)
        notebook.pack(fill="both", expand=True, padx=10, pady=(10, 4))

        for section, fields in SETTINGS_SCHEMA.items():
            tab = ttk.Frame(notebook, padding=12)
            notebook.add(tab, text=section)
            for key, label, kind, low, high in fields:
                row = ttk.Frame(tab)
                row.pack(fill="x", pady=4)
                ttk.Label(row, text=label, width=24, wraplength=170, justify="left").pack(side="left")

                if kind == "bool":
                    var = tk.BooleanVar()
                    ttk.Checkbutton(row, variable=var).pack(side="left")
                elif kind == "choice":
                    var = tk.StringVar()
                    ttk.Combobox(
                        row, textvariable=var, values=SETTINGS_CHOICES[key],
                        state="readonly", width=10,
                    ).pack(side="left")
                elif kind == "float":
                    var = tk.DoubleVar()
                    ttk.Spinbox(
                        row, textvariable=var, from_=low, to=high,
                        increment=0.01, width=10, format="%.2f",
                    ).pack(side="left")
                else:  # int
                    var = tk.IntVar()
                    ttk.Spinbox(row, textvariable=var, from_=low, to=high, width=10).pack(side="left")

                self.vars[key] = (var, kind)

        btn_row = ttk.Frame(self, padding=(10, 4, 10, 4))
        btn_row.pack(fill="x")
        ttk.Button(
            btn_row, text="Save for Current Session", command=self._save_session
        ).pack(side="left", expand=True, fill="x", padx=(0, 4))
        ttk.Button(
            btn_row, text="Save to Default", command=self._save_default
        ).pack(side="left", expand=True, fill="x", padx=(4, 0))

        self.status_var = tk.StringVar(value="Loading current values...")
        ttk.Label(self, textvariable=self.status_var, foreground="#2e7d32").pack(pady=(0, 10))

        self.app.request_settings_refresh()
        self.refresh_from_cache()

    def refresh_from_cache(self):
        cache = self.app.settings_cache
        for key, (var, kind) in self.vars.items():
            if key not in cache:
                continue
            value = cache[key]
            if kind == "choice":
                labels = SETTINGS_CHOICES[key]
                var.set(labels[value] if isinstance(value, int) else value)
            else:
                var.set(value)
        if cache:
            self.status_var.set("")

    def _collect(self):
        out = {}
        for key, (var, kind) in self.vars.items():
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


class ControlApp:
    def __init__(self, root, config):
        self.root = root
        self.config = config
        self.root.title(APP_TITLE)
        self.root.geometry("420x340")
        self.root.resizable(True, True)
        self._home_geometry = "420x340"
        self._control_geometry = f"{CAM_DISPLAY_W + 340}x{CAM_DISPLAY_H + 80}"

        self.status = "connecting"  # connecting | online | offline
        self.pressed = set()
        self._release_timers = {}
        self.control_active = False

        self.cam_wanted_on = False  # what we last asked for (survives until telemetry confirms)
        self._photo = None          # keep a reference so Tk doesn't GC the image
        self._frame_times = deque()
        self._last_frame_bytes = None  # raw JPEG of the last decoded frame, for Capture Photo

        self.settings_cache = {}    # latest robot/settings snapshot, keyed same as SETTINGS_SCHEMA
        self._settings_window = None

        self._build_home_view()
        self._build_control_view()
        self._show_home()

        self.local = LocalLink(self._on_frame, self._on_local_mode_change)
        self.mqtt = MqttBridge(
            config, self._on_mqtt_status, self._on_telemetry, self._on_frame, self._on_settings
        )

        self.root.protocol("WM_DELETE_WINDOW", self._on_close)

    # ---------- views ----------
    def _build_home_view(self):
        self.home = ttk.Frame(self.root, padding=24)
        ttk.Label(self.home, text=APP_TITLE, font=("Segoe UI", 18, "bold")).pack(
            pady=(0, 16)
        )

        self.status_var = tk.StringVar(value="● Connecting...")
        ttk.Label(self.home, textvariable=self.status_var, font=("Segoe UI", 13)).pack(
            pady=(0, 24)
        )

        self.play_btn = ttk.Button(
            self.home, text="PLAY", command=self._show_control, state="disabled"
        )
        self.play_btn.pack(ipadx=20, ipady=10)

    def _build_control_view(self):
        self.control = ttk.Frame(self.root, padding=16)

        top = ttk.Frame(self.control)
        top.pack(fill="x")
        # takefocus=0: ttk buttons activate on <space> when they hold
        # keyboard focus (which they grab on click). Without this, once
        # you'd clicked "Camera" or "Back" once, later pressing Space to
        # boost also re-clicked whichever button last had focus.
        ttk.Button(
            top, text="< Back", command=self._show_home, takefocus=0
        ).pack(side="left")
        self.control_status_var = tk.StringVar(value="● Online")
        ttk.Label(top, textvariable=self.control_status_var).pack(side="right")
        self.mode_var = tk.StringVar(value="Mode: Cloud")
        ttk.Label(top, textvariable=self.mode_var, width=12).pack(side="right", padx=(0, 12))

        # Quick actions that live outside the Settings screen - Settings
        # itself opens its own window; Set Timer/Capture Photo are one-shot
        # actions, not persisted state, so a dialog/toast is enough.
        actions = ttk.Frame(self.control)
        actions.pack(fill="x", pady=(8, 0))
        ttk.Button(actions, text="Settings", command=self._open_settings, takefocus=0).pack(side="left")
        ttk.Button(actions, text="Set Timer", command=self._set_timer_dialog, takefocus=0).pack(
            side="left", padx=(6, 0)
        )
        ttk.Button(actions, text="Capture Photo", command=self._capture_photo, takefocus=0).pack(
            side="left", padx=(6, 0)
        )
        self.capture_status_var = tk.StringVar(value="")
        ttk.Label(actions, textvariable=self.capture_status_var, font=("Segoe UI", 9)).pack(
            side="left", padx=(10, 0)
        )

        body = ttk.Frame(self.control)
        body.pack(fill="both", expand=True, pady=(12, 0))

        # ---- left: camera ----
        video_col = ttk.Frame(body)
        video_col.pack(side="left", fill="both", expand=True)

        cam_row = ttk.Frame(video_col)
        cam_row.pack(fill="x")
        self.cam_btn = ttk.Button(
            cam_row, text="Camera: OFF", command=self._toggle_camera, takefocus=0
        )
        self.cam_btn.pack(side="left")
        self.feed_fps_var = tk.StringVar(value="Feed: -- fps")
        ttk.Label(
            cam_row, textvariable=self.feed_fps_var, font=("Consolas", 10), width=14
        ).pack(side="right")

        # Fixed size placeholder (matches the device's native QVGA frame)
        # so the layout doesn't jump once real frames arrive.
        self._placeholder_photo = ImageTk.PhotoImage(
            Image.new("RGB", (CAM_DISPLAY_W, CAM_DISPLAY_H), "#202020")
        )
        self.video_label = tk.Label(video_col, image=self._placeholder_photo)
        self.video_label.pack(pady=(8, 4))

        # ---- right: controls + telemetry ----
        sidebar = ttk.Frame(body, padding=(20, 0, 0, 0))
        sidebar.pack(side="left", fill="y")

        ttk.Label(
            sidebar,
            text="WASD to move\nSpace = boost\nX = E-STOP",
            font=("Segoe UI", 10),
            justify="left",
        ).pack(anchor="w", pady=(0, 12))

        # Fixed width on every dynamic-text label in the sidebar: without
        # it, e.g. "Left: 0%" -> "Left: -100%" changes the label's natural
        # size, which changes the sidebar's width, which - since the
        # video column next to it is the flexible (expand=True) one -
        # visibly shifted the whole camera feed sideways on every WASD
        # press.
        self.target_var = tk.StringVar(value="Left: 0%   Right: 0%")
        ttk.Label(
            sidebar, textvariable=self.target_var, font=("Consolas", 11), width=28
        ).pack(anchor="w", pady=(0, 16))

        telem = ttk.LabelFrame(sidebar, text="Telemetry", padding=12)
        telem.pack(fill="x")

        self.temp_var = tk.StringVar(value="Temp: -- °C")
        self.hum_var = tk.StringVar(value="Humidity: -- %")
        self.pres_var = tk.StringVar(value="Pressure: -- hPa")
        self.alt_var = tk.StringVar(value="Altitude: -- m")
        self.batt_var = tk.StringVar(value="Battery: -- %")
        self.dist_var = tk.StringVar(value="Distance: -- mm")
        self.chip_temp_var = tk.StringVar(value="Chip: -- °C")

        for var in (self.temp_var, self.hum_var, self.pres_var, self.alt_var, self.batt_var, self.dist_var):
            ttk.Label(telem, textvariable=var, font=("Consolas", 10), width=24).pack(
                anchor="w"
            )

        ttk.Separator(telem, orient="horizontal").pack(fill="x", pady=6)
        ttk.Label(
            telem, textvariable=self.chip_temp_var, font=("Consolas", 10), width=24
        ).pack(anchor="w")
        self.overheat_var = tk.StringVar(value="")
        ttk.Label(
            telem, textvariable=self.overheat_var, foreground="#c0392b",
            font=("Segoe UI", 9, "bold"), width=24, wraplength=200,
        ).pack(anchor="w")

    def _show_home(self):
        self.control_active = False
        self.pressed.clear()
        self.control.pack_forget()
        self.home.pack(fill="both", expand=True)
        self.root.geometry(self._home_geometry)
        # make sure the robot doesn't keep moving - or streaming - after we leave this screen
        if hasattr(self, "mqtt"):
            self._send_command({"cmd": "stop"})
            self._set_camera(False)
        else:
            self._reset_video()

    def _show_control(self):
        self.home.pack_forget()
        self.root.geometry(self._control_geometry)
        self.control.pack(fill="both", expand=True)
        self.control_active = True
        self.root.focus_force()
        self._control_tick()

    # ---------- mqtt callbacks (run on a background thread — marshal to Tk) ----------
    def _on_mqtt_status(self, payload):
        self.root.after(0, lambda: self._apply_status(payload))

    def _apply_status(self, payload):
        if payload == "online":
            self.status = "online"
            self.status_var.set("● Online")
            self.play_btn.config(state="normal")
            self.control_status_var.set("● Online")
        elif payload == "offline":
            self.status = "offline"
            self.status_var.set("● Offline")
            self.play_btn.config(state="disabled")
            self.control_status_var.set("● Offline")
            if self.control_active:
                self._show_home()
                messagebox.showwarning(APP_TITLE, "Lost connection to the robot.")
        else:  # "connecting" or anything unrecognized
            self.status_var.set("● Connecting...")
            self.play_btn.config(state="disabled")

    def _on_telemetry(self, data):
        self.root.after(0, lambda: self._apply_telemetry(data))

    def _apply_telemetry(self, data):
        if "tempC" in data:
            self.temp_var.set(f"Temp: {data['tempC']:.2f} °C")
        if "humidity" in data:
            self.hum_var.set(f"Humidity: {data['humidity']:.2f} %")
        if "pressureHpa" in data:
            self.pres_var.set(f"Pressure: {data['pressureHpa']:.2f} hPa")
        if "altitudeM" in data:
            self.alt_var.set(f"Altitude: {data['altitudeM']:.2f} m")
        if "battPct" in data:
            battV = data.get("battV")
            suffix = f" ({battV:.2f}V)" if battV is not None else ""
            self.batt_var.set(f"Battery: {data['battPct']}%{suffix}")
        if "distanceMm" in data:
            # 8190 is the VL53L0X's own "nothing in range" sentinel, not a real distance.
            if data["distanceMm"] >= 8190:
                self.dist_var.set("Distance: out of range")
            else:
                self.dist_var.set(f"Distance: {data['distanceMm']} mm")
        if "chipTempC" in data:
            self.chip_temp_var.set(f"Chip: {data['chipTempC']:.1f} °C")
        if "camOverheat" in data:
            self.overheat_var.set(
                "⚠ Camera auto-off: chip overheated" if data["camOverheat"] else ""
            )
        if "camOn" in data:
            # Reflects the device's actual state (e.g. another client toggled
            # it, or a reconnect happened) rather than just our last click.
            self.cam_btn.config(text=f"Camera: {'ON' if data['camOn'] else 'OFF'}")
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
        # every set_settings, so keep an open Settings window in sync
        # rather than showing stale numbers the user just changed.
        if self._settings_window is not None and self._settings_window.winfo_exists():
            self._settings_window.refresh_from_cache()

    def request_settings_refresh(self):
        self._send_command({"cmd": "get_settings"})

    def send_settings(self, fields, persist):
        self._send_command({"cmd": "set_settings", "persist": persist, "settings": fields})

    def _open_settings(self):
        if self._settings_window is not None and self._settings_window.winfo_exists():
            self._settings_window.lift()
            self._settings_window.focus_force()
            return
        self._settings_window = SettingsWindow(self)

    # ---------- quick actions ----------
    def _set_timer_dialog(self):
        minutes = simpledialog.askinteger(
            APP_TITLE, "Timer duration, in minutes (1-60):",
            minvalue=1, maxvalue=60, initialvalue=5, parent=self.root,
        )
        if minutes is None:
            return
        self._send_command({"cmd": "set_timer", "minutes": minutes})
        self.capture_status_var.set(f"Timer set: {minutes} min")

    def _capture_photo(self):
        if self._last_frame_bytes is None:
            messagebox.showinfo(APP_TITLE, "No camera frame yet - turn the camera on first.")
            return
        photos_dir = os.path.join(get_app_dir(), "Photos")
        os.makedirs(photos_dir, exist_ok=True)
        filename = f"chuza_{time.strftime('%Y%m%d_%H%M%S')}.jpg"
        with open(os.path.join(photos_dir, filename), "wb") as f:
            f.write(self._last_frame_bytes)
        self.capture_status_var.set(f"Saved: {filename}")

    # ---------- LAN-direct / cloud switching ----------
    def _send_command(self, payload_dict):
        if not self.local.send_command(payload_dict):
            self.mqtt.publish_command(payload_dict)

    def _on_local_mode_change(self, mode):
        self.root.after(0, lambda: self.mode_var.set(f"Mode: {'LAN' if mode == 'local' else 'Cloud'}"))

    # ---------- camera ----------
    def _toggle_camera(self):
        self._set_camera(not self.cam_wanted_on)

    def _set_camera(self, on):
        self.cam_wanted_on = on
        self.cam_btn.config(text=f"Camera: {'ON' if on else 'OFF'}")
        self._send_command({"cmd": "cam_on" if on else "cam_off"})
        if not on:
            self._reset_video()

    def _reset_video(self):
        self._frame_times.clear()
        self.feed_fps_var.set("Feed: -- fps")
        self.video_label.config(image=self._placeholder_photo)
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
        self._photo = ImageTk.PhotoImage(image)
        self.video_label.config(image=self._photo)

        now = time.time()
        self._frame_times.append(now)
        while self._frame_times and now - self._frame_times[0] > FEED_FPS_WINDOW_SEC:
            self._frame_times.popleft()
        self.feed_fps_var.set(f"Feed: {len(self._frame_times)} fps")

    # ---------- keyboard handling ----------
    def bind_keys(self):
        self.root.bind_all("<KeyPress>", self._on_key_press)
        self.root.bind_all("<KeyRelease>", self._on_key_release)

    @staticmethod
    def _normalize(keysym):
        k = keysym.lower()
        if k in ("w", "a", "s", "d", "space", "x"):
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

        if "x" in self.pressed:
            self._send_command({"cmd": "brake"})
            self.target_var.set("Left: BRAKE   Right: BRAKE")
        else:
            left, right = self._compute_targets()
            self._send_command({"cmd": "move", "left": left, "right": right})
            self.target_var.set(f"Left: {left}%   Right: {right}%")

        self.root.after(CONTROL_TICK_MS, self._control_tick)

    def _on_close(self):
        try:
            self._send_command({"cmd": "brake"})
            self._send_command({"cmd": "cam_off"})
        except Exception:
            pass
        self.root.destroy()


def main():
    config = load_config()
    root = tk.Tk()
    app = ControlApp(root, config)
    app.bind_keys()
    root.mainloop()


if __name__ == "__main__":
    main()
