#pragma once

// ===========================================================================
// Track D - Obsidian task tracker config
//
// SECURITY: This file is COMMITTED. Keep every value below a placeholder.
// DO NOT put real WiFi / Obsidian credentials here.
//
// Real credentials come from one of (preferred first):
//   1. NVS at runtime - flash creds without rebuilding. Namespaces:
//        "epaperos" (SettingsStore): wifi_ssid, wifi_pass, obs_url,
//                   obs_token, note_path  -> REST mode (preferred)
//        "tasks"    (legacy Track D):  ssid, password, dav_url, dav_user,
//                   dav_pass             -> WebDAV fallback
//   2. TasksConfig.local.h - an untracked (gitignored) header for anyone who
//      insists on compile-time creds. Copy TasksConfig.local.example.h ->
//      TasksConfig.local.h and fill it in. It is included below before the
//      #ifndef defaults, so its values win. Never commit it.
//
// The #ifndef placeholders below only exist so a fresh clone still compiles.
// ===========================================================================

// Optional untracked local overrides (real creds). Pulled in first so its
// #defines take precedence over the placeholder defaults further down.
#if defined(__has_include)
#  if __has_include("TasksConfig.local.h")
#    include "TasksConfig.local.h"
#  endif
#endif

// ---- WiFi station credentials (fallback) ----
#ifndef TASKS_WIFI_SSID
#define TASKS_WIFI_SSID "your-ssid"
#endif
#ifndef TASKS_WIFI_PASSWORD
#define TASKS_WIFI_PASSWORD "your-password"
#endif

// Seconds to wait for an IP before giving up.
#ifndef TASKS_WIFI_CONNECT_TIMEOUT_S
#define TASKS_WIFI_CONNECT_TIMEOUT_S 15
#endif

// ---- WebDAV endpoint for the single tasks file (fallback) ----
// Full URL to the markdown file, e.g. http://192.168.1.50:8080/Tasks.md
#ifndef TASKS_DAV_URL
#define TASKS_DAV_URL "http://192.168.1.50:8080/Tasks.md"
#endif
// HTTP Basic auth (leave empty strings if the server needs no auth).
#ifndef TASKS_DAV_USER
#define TASKS_DAV_USER ""
#endif
#ifndef TASKS_DAV_PASS
#define TASKS_DAV_PASS ""
#endif

// ---- Local REST API endpoint (D2, preferred over WebDAV) ----
// Base URL of the Obsidian Local REST API plugin, e.g.
// http://192.168.1.50:27123 (HTTP on a trusted LAN avoids self-signed TLS).
// Leave empty to fall back to the WebDAV settings above.
#ifndef TASKS_OBSIDIAN_URL
#define TASKS_OBSIDIAN_URL ""
#endif
// Bearer token from the Local REST API plugin settings.
#ifndef TASKS_OBSIDIAN_TOKEN
#define TASKS_OBSIDIAN_TOKEN ""
#endif
// Vault-relative note path, e.g. Tasks.md or Inbox/Tasks.md.
#ifndef TASKS_NOTE_PATH
#define TASKS_NOTE_PATH "Tasks.md"
#endif

// Max size of the tasks markdown file we will buffer (bytes).
#ifndef TASKS_FILE_MAX_BYTES
#define TASKS_FILE_MAX_BYTES (16 * 1024)
#endif

// NVS namespace used for runtime overrides of the above.
#define TASKS_NVS_NAMESPACE "tasks"
