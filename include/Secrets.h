#pragma once

// Fill these in with your own values before flashing.
// Do NOT commit this file with real credentials to a public repo —
// add "include/Secrets.h" to your .gitignore.

#define WIFI_SSID     "YOUR_WIFI_SSID"                                      // wifi name (SSID)
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"                      

// From your HiveMQ Cloud cluster's "Overview" page
#define MQTT_HOST      "YOUR_CLUSTER_ID.s1.eu.hivemq.cloud"
#define MQTT_PORT      8883

// From HiveMQ Cloud -> Access Management -> Manage Credentials
#define MQTT_USERNAME  "YOUR_MQTT_USERNAME"
#define MQTT_PASSWORD  "YOUR_MQTT_PASSWORD"

#define MQTT_CLIENT_ID "your-robot-client-id"

// Local time, for the OLED menu's clock page (synced over NTP once WiFi
// is up - see configTime() in main.cpp). Offset is in seconds from UTC.
#define TIMEZONE_OFFSET_SEC (5 * 3600 + 30 * 60) // UTC+5:30 (India)
#define NTP_SERVER "pool.ntp.org"