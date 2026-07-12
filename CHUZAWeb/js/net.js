// Cloud-only MQTT bridge for CHUZA Web - a JS port of MqttBridge from
// CHUZAControls/net.py. LocalLink (the LAN-direct UDP/MJPEG path) is
// deliberately not ported: browsers can't open raw UDP sockets, and
// this app always reaches the robot the same way the desktop app does
// whenever it's off the home LAN - MQTT over TLS, just over the
// WebSocket listener (port 8884, path /mqtt) every HiveMQ Cloud
// cluster already exposes alongside its raw TLS listener (8883), so no
// firmware or broker changes are needed.
//
// mqtt.js (vendored at js/lib/mqtt.min.js, loaded as a plain <script>
// before this module, attaching window.mqtt) already handles
// reconnection internally, so unlike net.py there's no manual
// connect-retry thread to write.
export class MqttBridge {
  constructor(config, { onStatus, onTelemetry, onFrame, onSettings }) {
    this.cfg = config;
    this.onStatus = onStatus;
    this.onTelemetry = onTelemetry;
    this.onFrame = onFrame;
    this.onSettings = onSettings;

    const url = `wss://${config.broker_host}:${config.broker_port}/mqtt`;
    this.onStatus("connecting");
    this.client = window.mqtt.connect(url, {
      username: config.username,
      password: config.password,
      clientId: `chuza-web-${Date.now()}`,
      clean: true,
      reconnectPeriod: 5000,
      connectTimeout: 10000,
    });

    this.client.on("connect", () => this._onConnect());
    this.client.on("reconnect", () => this.onStatus("connecting"));
    this.client.on("close", () => this.onStatus("connecting"));
    this.client.on("offline", () => this.onStatus("connecting"));
    this.client.on("message", (topic, payload) => this._onMessage(topic, payload));
  }

  _onConnect() {
    this.client.subscribe([
      this.cfg.topic_status,
      this.cfg.topic_telemetry,
      this.cfg.topic_camera_frame,
      this.cfg.topic_settings,
    ]);
  }

  _onMessage(topic, payload) {
    if (topic === this.cfg.topic_status) {
      this.onStatus(payload.toString());
    } else if (topic === this.cfg.topic_telemetry) {
      try {
        this.onTelemetry(JSON.parse(payload.toString()));
      } catch {
        // partial/corrupt payload - drop it, next message will be fine
      }
    } else if (topic === this.cfg.topic_camera_frame) {
      this.onFrame(payload);
    } else if (topic === this.cfg.topic_settings) {
      try {
        this.onSettings(JSON.parse(payload.toString()));
      } catch {
        // ignore
      }
    }
  }

  publishCommand(cmd) {
    if (!this.client || !this.client.connected) return;
    this.client.publish(this.cfg.topic_commands, JSON.stringify(cmd), { qos: 0 });
  }

  close() {
    this.client?.end(true);
  }
}
