# EPaperOS

A touch-driven e-paper firmware for the **LilyGo T5 4.7" S3 Pro / Pro Lite** —
an EPUB reader and an Obsidian task tracker in one device. Build it as the full
"OS" (reader + tasks + WiFi) or as a pure, offline EPUB reader.

> Forked from [juicecultus/diy-esp32s3-epub-reader](https://github.com/juicecultus/diy-esp32s3-epub-reader)
> (the ESP32-S3 port), which is itself a fork of the original
> [atomic14/diy-esp32-epub-reader](https://github.com/atomic14/diy-esp32-epub-reader).

EPaperOS narrows the focus to one board family and adds a touch UI, a settings
screen, an RTC-backed status-bar clock, and a second "app" — an Obsidian task
tracker that works online (sync to a vault over WiFi) or fully offline (a
markdown file on the SD card).

---

## Target hardware

This firmware is dedicated to the **LilyGo T5 4.7" S3 Pro** and **Pro Lite**
(the variants with frontlight + touch). Other boards from the upstream projects
are not maintained here.

- **MCU**: ESP32-S3 (16 MB flash, octal PSRAM)
- **Display**: 4.7" 960×540 e-paper, driven by epdiy (in-repo board def for the
  LilyGo S3), rendered in inverted portrait (logical page 540×960)
- **Touch**: GT911 (SDA 39 / SCL 40 / INT 3 / RST 9)
- **Storage**: SD card mounted at `/fs`; EPUBs under `/fs/Books`
- **Clock**: PCF8563 RTC (shared touch I²C bus)
- **Battery**: read from the BQ25896 PMIC over I²C (no analog VBAT divider)

## Features

**Reader**
- EPUB parsing + pagination with FreeType (TTF) text
- SD library with cover thumbnails, bookmarks (resume where you left off),
  table of contents, adjustable font size and margins
- Touch UI: tap zones, gestures, on-screen keyboard
- Status bar with RTC clock + battery; deep-sleep cover/screensaver

**Tasks** (full build only)
- A second app alongside the reader, reachable from the home menu
- Renders Obsidian "Tasks plugin" checkbox lines; toggle, add, filter by day
- **Two backends, chosen automatically:**
  - **Online** — if Obsidian credentials are configured, syncs one vault note
    over WiFi (Local REST API, or WebDAV fallback)
  - **Offline** — if no credentials, reads/writes `/fs/Tasks.md` on the SD card
- Hard either/or by configuration; no local↔remote merge

## Build modes

Two PlatformIO environments select what ships. The Tasks app + WiFi stack are
gated behind the `EPAPEROS_TASKS` flag (see `src/CMakeLists.txt`).

| Environment | Includes | Boots to |
|---|---|---|
| `lilygo_t5s3_idf` *(default)* | Reader + Settings + Obsidian Tasks + WiFi | Home menu (Books / Tasks / Settings) |
| `lilygo_t5s3_reader` | Reader + Settings only — **no Tasks, no WiFi** | Straight into the library |

The reader-only build excludes `src/tasks/` entirely and drops the WiFi/HTTP
components — smaller binary, no radio, no credentials in the image. Its clock
comes from the RTC alone (no NTP).

## Build & flash

Uses [PlatformIO](https://platformio.org/) (VSCode extension recommended). The
project uses **git submodules** — clone recursively:

```
git clone --recursive https://github.com/NipunBhalla/EPaperOS.git
# or, if already cloned:
git submodule update --init --recursive
```

Pick an environment in the PlatformIO sidebar and `Upload`, or:

```
pio run -e lilygo_t5s3_idf -t upload      # full EPaperOS
pio run -e lilygo_t5s3_reader -t upload   # pure reader
```

## Configuration & secrets

**No credentials live in source or in git.** Provide them one of two ways:

1. **SD seed file (easiest)** — copy [`settings.ini.example`](settings.ini.example)
   to `settings.ini` at the SD card root and fill it in. On boot the device
   imports it into NVS, then ignores it. Holds WiFi + Obsidian settings.
2. **Compile-time** — copy `src/tasks/TasksConfig.local.example.h` to
   `TasksConfig.local.h` and fill it in (baked into the image).

Both `settings.ini` and `src/tasks/TasksConfig.local.h` are gitignored. The
on-device Settings screen can also edit these values (stored in NVS).

When Obsidian WiFi + URL + note path are all set, Tasks runs **online**;
otherwise it runs **offline** against `/fs/Tasks.md`.

## Fonts

Place a TrueType font at `/fonts/reader.ttf` on the SD card. A prebuilt
`lib_freetype` static library is included for the S3 environments.

## Credits & license

Built on the work of [atomic14](https://github.com/atomic14/diy-esp32-epub-reader)
(original ESP32 EPUB reader) and the
[juicecultus](https://github.com/juicecultus/diy-esp32s3-epub-reader) S3 port.
See [LICENSE](LICENSE). EPUB internals, layout, and rendering are documented in
the upstream README history and the
[atomic14 wiki](https://github.com/atomic14/diy-esp32-epub-reader/wiki).
