import sys
import os
import json
import time
import threading
import tkinter as tk
from tkinter import ttk, messagebox
import paho.mqtt.client as mqtt # type: ignore

APP_TITLE = "CHUZA Control"

# --- Tuning knobs ---
BASE_SPEED = 70          # % speed for w/s, and for solo a/d pivot turns
BOOST_SPEED = 100         # % speed when the boost key (Space) is held
TURN_BIAS = 20           # % differential nudge when turning *while* moving
CONTROL_TICK_MS = 100    # how often we recompute + publish targets
KEY_RELEASE_DEBOUNCE_MS = 50  # swallow OS auto-repeat release/press pairs


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

    def __init__(self, config, on_status, on_telemetry):
        self.cfg = config
        self.on_status_cb = on_status
        self.on_telemetry_cb = on_telemetry

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

    def publish_command(self, payload_dict):
        self.client.publish(self.cfg["topic_commands"], json.dumps(payload_dict), qos=0)


class ControlApp:
    def __init__(self, root, config):
        self.root = root
        self.config = config
        self.root.title(APP_TITLE)
        self.root.geometry("420x340")
        self.root.resizable(False, False)

        self.status = "connecting"  # connecting | online | offline
        self.pressed = set()
        self._release_timers = {}
        self.control_active = False

        self._build_home_view()
        self._build_control_view()
        self._show_home()

        self.mqtt = MqttBridge(config, self._on_mqtt_status, self._on_telemetry)

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
        ttk.Button(top, text="< Back", command=self._show_home).pack(side="left")
        self.control_status_var = tk.StringVar(value="● Online")
        ttk.Label(top, textvariable=self.control_status_var).pack(side="right")

        ttk.Label(
            self.control,
            text="WASD to move   ·   Space = boost   ·   X = E-STOP",
            font=("Segoe UI", 9),
        ).pack(pady=(12, 4))

        self.target_var = tk.StringVar(value="Left: 0%   Right: 0%")
        ttk.Label(
            self.control, textvariable=self.target_var, font=("Consolas", 11)
        ).pack(pady=(4, 16))

        telem = ttk.LabelFrame(self.control, text="Telemetry", padding=12)
        telem.pack(fill="x")

        self.temp_var = tk.StringVar(value="Temp: -- °C")
        self.hum_var = tk.StringVar(value="Humidity: -- %")
        self.pres_var = tk.StringVar(value="Pressure: -- hPa")
        self.alt_var = tk.StringVar(value="Altitude: -- m")

        for var in (self.temp_var, self.hum_var, self.pres_var, self.alt_var):
            ttk.Label(telem, textvariable=var, font=("Consolas", 10)).pack(anchor="w")

    def _show_home(self):
        self.control_active = False
        self.pressed.clear()
        self.control.pack_forget()
        self.home.pack(fill="both", expand=True)
        # make sure the robot doesn't keep moving after we leave this screen
        if hasattr(self, "mqtt"):
            self.mqtt.publish_command({"cmd": "stop"})

    def _show_control(self):
        self.home.pack_forget()
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
            self.mqtt.publish_command({"cmd": "brake"})
            self.target_var.set("Left: BRAKE   Right: BRAKE")
        else:
            left, right = self._compute_targets()
            self.mqtt.publish_command({"cmd": "move", "left": left, "right": right})
            self.target_var.set(f"Left: {left}%   Right: {right}%")

        self.root.after(CONTROL_TICK_MS, self._control_tick)

    def _on_close(self):
        try:
            self.mqtt.publish_command({"cmd": "brake"})
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
