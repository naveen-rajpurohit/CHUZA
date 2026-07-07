"""Networking/protocol layer for CHUZA Control.

Deliberately zero tkinter imports: this is the physical boundary that
keeps the MQTT/LAN-direct protocol logic reusable by a future non-desktop
(e.g. iOS) client, rather than just a mental convention. All screen/view
code lives in app.py and screens/ instead.
"""
import json
import os
import re
import socket
import sys
import threading
import time

import paho.mqtt.client as mqtt  # type: ignore
import requests  # type: ignore

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
        ("wheelTrimPct", "Motor trim (L/R balance)", "int", -50, 50),
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


DEFAULT_CONFIG = {
    "broker_host": "",
    "broker_port": 8883,
    "username": "",
    "password": "",
    "topic_status": "robot/status",
    "topic_telemetry": "robot/telemetry",
    "topic_commands": "robot/commands",
    "topic_camera_frame": "robot/camera/frame",
    "topic_settings": "robot/settings",
}

# Fields the Login/Connect screen requires before it'll let you through.
REQUIRED_KEYS = ("broker_host", "broker_port", "username", "password")


def _config_path():
    return os.path.join(get_app_dir(), "config.json")


def try_load_config():
    """Returns the saved config dict, or None if config.json is missing,
    unparsable, missing a required field, or still holds a leftover
    placeholder value - in every one of those cases the caller should
    show the Login screen instead of crashing."""
    path = _config_path()
    if not os.path.exists(path):
        return None
    try:
        with open(path) as f:
            data = json.load(f)
    except (OSError, json.JSONDecodeError):
        return None
    for key in REQUIRED_KEYS:
        value = data.get(key)
        if not value or (isinstance(value, str) and "REPLACE_ME" in value):
            return None
    merged = dict(DEFAULT_CONFIG)
    merged.update(data)
    return merged


def save_config(fields):
    """Merges fields over the defaults and writes config.json next to
    the app, so the Login/Settings screens never need to know the full
    schema - just the fields the user actually edited."""
    merged = dict(DEFAULT_CONFIG)
    merged.update(fields)
    with open(_config_path(), "w") as f:
        json.dump(merged, f, indent=2)
    return merged


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
            except Exception:
                # _consume_stream reads resp.raw directly (see its own
                # docstring for why), which bypasses requests' exception
                # wrapping - a dropped connection can surface as a raw
                # urllib3/socket error instead of RequestException. Catch
                # broadly here since this is already a best-effort loop
                # with its own retry/backoff below.
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
