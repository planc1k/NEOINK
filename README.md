# NEOINK

NEOINK is an experimental firmware fork for the Xteink X3/X4 e-ink reader. It
keeps the reading-focused base from [uxjulia/CrossInk](https://github.com/uxjulia/CrossInk)
and [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader),
then adds flashcards, richer SD-card library tools, native Markdown reading, and
new INX-inspired themes.

This is still ESP32-C3 firmware, so the project is designed around limited RAM,
limited flash, and safe SD-card-backed workflows instead of phone-style app
behavior.

## Highlights

- Reads **EPUB**, **XTC / XTCH**, **TXT**, and **MD / Markdown** files.
- EPUB reader features include bookmarks, clippings, footnotes, table of
  contents, go-to-percent, per-book settings, reading stats, render-mode
  fallback, and KOReader sync.
- Flashcard app with CSV/TSV decks, browser-side simple APKG conversion,
  session scheduling, stats, progress import/export, and SD-backed card reads.
- Web UI for file transfer, EPUB optimization, bulk rename/move, metadata scan,
  duplicate detection, collections/read-unread workflows, flashcard management,
  settings, and SD-card fonts.
- Sleep screens include blank/light/dark/custom, book cover modes, overlay,
  reading stats, minimal stats, and quick resume modes supported by this fork.
- Reader fonts can be installed from the SD card, avoiding large baked-in font
  additions for custom families.

## New INX-Inspired UI

NEOINK adds three selectable themes under **Settings > Display > UI Theme**:

- **INX**: clean monochrome interface with compact headers, underline tabs,
  refined lists, popups, keyboard, and button hints.
- **INX Flow**: INX styling with a richer recent-books home treatment and three
  visible book slots.
- **INX Neobrutalist**: INX tab navigation with bold bordered panels, hard
  shadows, dither accents, and sharper selected states.

For these themes the home screen uses six tabs: **Recent**, **Library**,
**Flashcards**, **File Transfer**, **Statistics**, and **Settings**. The tabs
call the existing NEOINK/CrossInk activities; INX's internal application
architecture is not imported.

## File Formats

Supported reader formats:

```text
.epub
.xtc
.xtch
.txt
.md
.markdown
```

Markdown uses the lightweight TXT reader path. It cleans up headings, lists,
task lists, blockquotes, links, images, inline emphasis, inline code, and simple
rules while preserving the streaming page index model. It is not a full
CommonMark engine.

## Flashcards

Decks live on the SD card:

```text
/.crosspoint/flashcards/decks/
/.crosspoint/flashcards/progress/
```

TSV is recommended:

```text
front	back
Capital of Bangladesh?	Dhaka
Largest planet?	Jupiter
```

CSV is also supported, and the web UI can convert simple Anki Basic-style APKG
notes in the browser before upload. The device indexes card line offsets and
loads the active card from SD instead of keeping an entire deck in RAM.

See [Flashcards](./docs/flashcards.md) for controls, scheduling, stats, deck
limits, and backup behavior.

## Web UI

Open **File Transfer** on the reader, connect to the displayed URL, then use:

- **Home** for device status.
- **Files** for uploads, EPUB optimization, bulk file tools, metadata scans,
  duplicate checks, collections, read/unread folders, and cache-preserving moves.
- **Flashcards** for deck upload, APKG conversion, deck stats, reset, rename,
  delete, and progress backup/restore.
- **Settings** for firmware settings.
- **Fonts** for SD-card font installation and management.

The web UI uses an offline-safe colorful neobrutalist style. It does not depend
on external fonts or network assets.

## Device Limits

- Keep folders below about 200 files; 50-100 per folder is smoother.
- Text-first EPUBs are safest. Large scanned books, comics, image-heavy EPUBs,
  and huge omnibus files are more likely to be slow or memory-sensitive.
- EPUBs under roughly 20 MB are the safest target. Larger books may work better
  after browser-side optimization.
- Flashcards are text-first. Full Anki scheduling, media cards, cloze templates,
  and arbitrary Anki card templates are not rendered on-device.
- The web server has no authentication. Use it only on trusted networks or in
  temporary hotspot/file-transfer sessions.

## Build

NEOINK uses PlatformIO:

```sh
pio run -e tiny
```

Other firmware variants:

```sh
pio run -e teensy
pio run -e xlarge
```

Build the simulator:

```sh
pio run -e simulator
```

Run a smoke test:

```sh
python3 scripts/run_simulator_smoke_test.py --theme inx
python3 scripts/run_simulator_smoke_test.py --theme inx-flow --no-build
python3 scripts/run_simulator_smoke_test.py --theme inx-neobrutalist --no-build
```

Before flashing, recommended validation is:

```sh
git diff --check
python3 scripts/build_web.py
pio run -e simulator
pio run -e tiny
```

The built `tiny` binary is expected at:

```text
.pio/build/tiny/firmware-tiny.bin
```

## SD Card Data

Important paths:

```text
/.crosspoint/epub_<hash>/          EPUB metadata, reader settings, stats, cache
/.crosspoint/txt_<hash>/           TXT/Markdown progress and page index
/.crosspoint/flashcards/decks/     Flashcard decks
/.crosspoint/flashcards/progress/  Flashcard scheduler/progress files
/.fonts/ or /fonts/                Optional SD-card fonts
/.crossink-stats-backup/           Compatibility path for stats backups
/Read                              Finished/read books
/Unread                            Unread books
/Collections/<name>                User-created shelves
```

Compatibility file names and wire formats intentionally keep existing
CrossInk/CrossPoint identifiers where changing them would break upgrades,
settings, backups, or imports.

## Documentation

- [Installation](./docs/installation.md)
- [Flashcards](./docs/flashcards.md)
- [File Formats](./docs/file-formats.md)
- [Web Server Guide](./docs/webserver.md)
- [Web Server Endpoints](./docs/webserver-endpoints.md)
- [Font Build Variants](./docs/font-build-variants.md)
- [Reader Features](./docs/reader-features.md)
- [Controls](./docs/controls.md)
- [Simulator](./docs/simulator.md)
- [Data Cache](./docs/data-cache.md)
- [Common Issues](./docs/troubleshooting.md)

## Credits

NEOINK builds on substantial work from:

- [uxjulia/CrossInk](https://github.com/uxjulia/CrossInk)
- [crosspoint-reader/crosspoint-reader](https://github.com/crosspoint-reader/crosspoint-reader)
- [crosspoint-reader/crosspoint-simulator](https://github.com/crosspoint-reader/crosspoint-simulator)
- [obijuankenobiii/inx](https://github.com/obijuankenobiii/inx), used as visual
  and navigation inspiration for the INX theme family
