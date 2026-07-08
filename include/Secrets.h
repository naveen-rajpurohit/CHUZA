#pragma once

// Fill these in with your own values before flashing.
// Do NOT commit this file with real credentials to a public repo —
// add "include/Secrets.h" to your .gitignore.

#define WIFI_SSID     "iPhone"                                      // wifi name (SSID)
#define WIFI_PASSWORD "bhulgaya"                      

// From your HiveMQ Cloud cluster's "Overview" page
#define MQTT_HOST      "5625cabb1caa437b9086bd6b10d6ac4d.s1.eu.hivemq.cloud"
#define MQTT_PORT      8883

// From HiveMQ Cloud -> Access Management -> Manage Credentials
#define MQTT_USERNAME  "chuzarobo"
#define MQTT_PASSWORD  "chuzarobo"

#define MQTT_CLIENT_ID "your-robot-client-id"

// Local time, for the OLED menu's clock page (synced over NTP once WiFi
// is up - see configTime() in main.cpp). Offset is in seconds from UTC.
#define TIMEZONE_OFFSET_SEC (5 * 3600 + 30 * 60) // UTC+5:30 (India)
#define NTP_SERVER "pool.ntp.org"