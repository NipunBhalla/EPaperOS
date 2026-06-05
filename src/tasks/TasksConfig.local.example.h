#pragma once

// ===========================================================================
// TasksConfig.local.h - OPTIONAL untracked compile-time credentials override.
//
// Copy this file to "TasksConfig.local.h" (same directory) and fill in real
// values. TasksConfig.local.h is gitignored and MUST NOT be committed.
//
// Preferred path is still NVS at runtime (see TasksConfig.h). Use this file
// only if you specifically want creds baked into the firmware image.
//
// Any value you do NOT define here falls back to the placeholder in
// TasksConfig.h, so you only need to set what you use.
// ===========================================================================

// ---- WiFi station credentials ----
// #define TASKS_WIFI_SSID     "my-network"
// #define TASKS_WIFI_PASSWORD "my-wifi-password"

// ---- Obsidian Local REST API (preferred transport) ----
// #define TASKS_OBSIDIAN_URL   "http://192.168.0.184:27123"
// #define TASKS_OBSIDIAN_TOKEN "paste-bearer-token-here"
// #define TASKS_NOTE_PATH      "Tasks.md"

// ---- WebDAV fallback (only if not using the REST API above) ----
// #define TASKS_DAV_URL  "http://192.168.0.184:8080/Tasks.md"
// #define TASKS_DAV_USER ""
// #define TASKS_DAV_PASS ""
