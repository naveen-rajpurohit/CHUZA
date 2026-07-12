// Ported from CHUZAControls/net.py (DEFAULT_CONFIG, REQUIRED_KEYS,
// SETTINGS_SCHEMA, SETTINGS_CHOICES) and CHUZAControls/app.py (driving
// tuning constants). Kept as plain data, no logic, mirroring net.py's
// own separation of protocol/schema from UI.

// Same topic defaults as the desktop app, but broker_port defaults to
// 8884 (MQTT-over-WebSockets) instead of 8883 (raw TLS) - this app has
// no LAN-direct path and only ever speaks MQTT over WSS.
export const DEFAULT_CONFIG = {
  broker_host: "",
  broker_port: 8884,
  username: "",
  password: "",
  topic_status: "robot/status",
  topic_telemetry: "robot/telemetry",
  topic_commands: "robot/commands",
  topic_camera_frame: "robot/camera/frame",
  topic_settings: "robot/settings",
};

export const REQUIRED_KEYS = ["broker_host", "broker_port", "username", "password"];

// (key, label, kind, low, high) - low/high are the numeric input range
// for int/float kinds, unused for bool/choice. Mirrors RobotSettings
// (lib/CHUZASettings on the device) field-for-field.
export const SETTINGS_SCHEMA = {
  Timing: [
    ["angryTimeoutSec", "Angry timeout (sec)", "int", 5, 3600],
    ["tiredHoldSec", "Tired hold (sec)", "int", 1, 600],
    ["menuTimeoutSec", "Menu auto-off (sec)", "int", 2, 120],
    ["timerAlarmSec", "Timer alarm duration (sec)", "int", 1, 120],
  ],
  Threshold: [
    ["lowBattPct", "Low battery (%)", "int", 0, 100],
    ["cliffThresholdMm", "Cliff threshold (mm)", "int", 10, 500],
    ["touchSensitivity", "Touch sensitivity", "float", 1.02, 3.0],
    ["wheelMinPwm", "Wheel min PWM", "int", 0, 255],
    ["wheelMaxPwm", "Wheel max PWM", "int", 0, 255],
    ["wheelRampRate", "Wheel ramp rate (%/s)", "int", 10, 2000],
    ["wheelTrimPct", "Motor trim (L/R balance)", "int", -50, 50],
  ],
  Hardware: [
    ["motorsEnabled", "Motors", "bool", null, null],
    ["oledEnabled", "OLED display", "bool", null, null],
    ["buzzerEnabled", "Buzzer", "bool", null, null],
    ["envSensorEnabled", "Environment sensor (BME280)", "bool", null, null],
    ["distSensorEnabled", "Distance sensor (ToF)", "bool", null, null],
  ],
  Behavior: [["wanderMode", "Wander mode", "choice", null, null]],
};

export const SETTINGS_CHOICES = { wanderMode: ["OFF", "SOFT", "NORMAL"] };

// Driving feel - copied from CHUZAControls/app.py's tuning knobs.
export const BASE_SPEED = 70; // % speed for forward/back, and solo pivot turns
export const BOOST_SPEED = 100; // % speed when boost is held
export const TURN_BIAS = 20; // % differential nudge when turning while moving
export const CONTROL_TICK_MS = 100; // how often targets are recomputed + published

// VL53L0X's own "nothing in range" sentinel, not a real distance.
export const DISTANCE_OUT_OF_RANGE_MM = 8190;
