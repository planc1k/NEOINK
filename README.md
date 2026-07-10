# CrossInk Planck

CrossInk Planck is a public firmware fork for the Xteink X3/X4 e-ink reader,
based on [uxjulia/CrossInk](https://github.com/uxjulia/CrossInk), itself a fork
of [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader).

This fork keeps the reading-focused CrossInk experience and adds a lightweight
study and library-management layer: flashcards, richer browser tools, native
Markdown reading, and safer SD-card organization helpers.

> Status: experimental but build-tested. The `tiny` firmware currently builds
> with flash headroom and has been sanity-checked in the simulator smoke test.

## What This Fork Adds

### Flashcards

- Home-screen **Flashcards** app.
- CSV and TSV deck support.
- Browser-side `.apkg` import for simple Anki Basic-style notes.
- Session-based spaced repetition with Again, Good, and Easy ratings.
- SD-backed card loading: the firmware indexes card line offsets and hashes,
  then reads the active card text from SD instead of keeping every card in RAM.
- Web UI deck management, upload, rename, delete, reset, progress import, and
  progress export.
- Web UI stats: total cards, new cards, due cards, reviewed cards, retention
  estimate, rating totals, lapses, sessions, and compact per-card progress.

See [Flashcards](./docs/flashcards.md) for deck format, button controls,
scheduler behavior, limits, and backup details.

### Library Management

- Author/title metadata scan from the web file manager.
- Read, Unread, and Collections folder workflows.
- Bulk rename and bulk move in the web UI.
- Duplicate-candidate detection for likely repeated books.
- "Move to Read" and finished-book organization improvements.
- Cache migration for ordinary rename/move operations so progress is preserved
  when possible.

See [Web Server Guide](./docs/webserver.md) and
[Web Server Endpoints](./docs/webserver-endpoints.md).

### Native Markdown Reading

- `.md` and `.markdown` files open in the text reader.
- Streaming-friendly Markdown cleanup for headings, lists, blockquotes, links,
  images, emphasis, strike, inline code, horizontal rules, and code fences.
- Uses the same lightweight TXT cache/progress path instead of adding a large
  Markdown engine.

See [File Formats](./docs/file-formats.md#text-and-markdown-books).

### Neobrutalist Theme

- Select **Neobrutalist** from **Settings > Display > UI Theme**.
- Uses square high-contrast panels, thick borders, offset shadows, inverted
  selected states, and dither accents tuned for monochrome e-ink.
- Covers headers, tab bars, lists, menus, button hints, side hints, recent-book
  home cards, popups, text fields, and the on-screen keyboard.

### CrossInk Base Features

This fork keeps CrossInk's existing reader improvements:

- ChareInk, Lexend Deca, Bitter, and Inter typography.
- Font build variants for different point-size/headroom tradeoffs.
- Bookmarks, reader control remapping, Bionic Reading, Guide Dots, and Force
  Paragraph Indents.
- Reading stats, finished-book tracking, Read-folder movement, and reading
  stats sync.
- Custom sleep images, favorite sleep image support, and Minimal/Neobrutalist themes.
- EPUB optimization in the browser before upload.
- Web file transfer, WebDAV, Calibre wireless upload, OPDS, and SD-card fonts.

## Device Limits

The reader is built around an ESP32-C3 with limited RAM and flash. This fork is
designed around those constraints instead of trying to behave like a phone app.

- Keep folders below about 200 files. For the smoothest browsing, aim for
  50-100 files per folder.
- Split large libraries into folders by author, series, genre, read/unread
  status, or collection.
- Text-first EPUBs work best. Large scanned books, comics, image-heavy EPUBs,
  and giant omnibus files are more likely to be slow or memory-sensitive.
- EPUBs under roughly 20 MB are the safest target. Files over 50 MB may still
  work, but they are more likely to need browser-side optimization.
- Flashcard decks are intentionally text-first. Media, cloze cards, Anki
  templates, and full Anki scheduling are not stored or rendered on-device.
- The web server has no authentication. Use it on trusted private networks or
  in hotspot mode only while you are actively transferring files.

## Flashcard Decks

Decks live on the SD card at:

```text
/.crosspoint/flashcards/decks/
```

Progress lives at:

```text
/.crosspoint/flashcards/progress/
```

TSV is recommended:

```text
front	back
Capital of Bangladesh?	Dhaka
Largest planet?	Jupiter
```

CSV is also supported:

```csv
front,back
"Capital of Bangladesh?","Dhaka"
"Largest planet?","Jupiter"
```

A sample test deck is included at
[docs/flashcards/test_deck.csv](./docs/flashcards/test_deck.csv).

## Web UI Workflow

1. On the reader, open **File Transfer**.
2. Join Wi-Fi or create a hotspot.
3. Open the displayed URL or scan the QR code.
4. Use **Files** for library organization, EPUB optimization, bulk rename/move,
   metadata scan, duplicate scan, and uploads.
5. Use **Flashcards** for deck upload, APKG conversion, stats, reset, rename,
   delete, and progress backup/restore.
6. Exit File Transfer when finished.

The file manager keeps most dot-prefixed system folders hidden. The flashcard
deck folder is exposed only through the Flashcards page so decks can be managed
without opening the rest of `/.crosspoint`.

## Installation

Download a `firmware-*.bin` release when available, then flash with the web
installer or command line.

For local builds, the tested firmware artifact is:

```text
.pio/build/tiny/firmware-tiny.bin
```

See [Installation](./docs/installation.md) for flashing and revert guidance.

## Build From Source

CrossInk uses PlatformIO.

```sh
pio run -e tiny
```

To flash a connected Xteink X3/X4:

```sh
pio run -e tiny --target upload
```

Other variants are available for different font-size/headroom tradeoffs:

```sh
pio run -e teensy
pio run -e xlarge
```

See [Font Build Variants](./docs/font-build-variants.md) for the full matrix.

## Simulator And Testing

Build the simulator:

```sh
pio run -e simulator
```

Run the simulator smoke test:

```sh
CROSSINK_SIMULATOR_SMOKE_TEST=1 .pio/build/simulator/program
```

Recommended validation before flashing:

```sh
git diff --check
pio run -e simulator
pio run -e tiny
CROSSINK_SIMULATOR_SMOKE_TEST=1 .pio/build/simulator/program
```

See [Simulator](./docs/simulator.md) and
[Testing and Debugging](./docs/contributing/testing-debugging.md).

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
- [Contributing Docs](./docs/contributing/README.md)

## SD Card Data Model

CrossInk stores reusable data on the SD card so the firmware does not need to
rebuild or retain everything in RAM.

Important paths:

```text
/.crosspoint/epub_<hash>/          EPUB metadata, reader settings, and cache
/.crosspoint/flashcards/decks/     User flashcard decks
/.crosspoint/flashcards/progress/  Flashcard scheduler/progress files
/.fonts/ or /fonts/                Optional SD-card font files
/Read                              Finished/read books
/Unread                            Unread books
/Collections/<name>                User-created shelves
```

See [Data Cache](./docs/data-cache.md) for the `.crosspoint` layout and
[File Formats](./docs/file-formats.md) for binary cache details.

## Safety Notes

- Back up the SD card before testing experimental firmware.
- Keep a copy of important flashcard decks and exported progress JSON.
- Do not manually edit files in `/.crosspoint/flashcards/progress/`.
- If a deck shows a memory error, split it into smaller decks or reduce the web
  import card limit.
- If an EPUB is unusually slow, try the browser-side EPUB optimizer before
  copying it to the SD card.
- Exit File Transfer mode when done so the unauthenticated web server is no
  longer reachable.

## Upstream

This fork builds on substantial work from:

- [uxjulia/CrossInk](https://github.com/uxjulia/CrossInk)
- [crosspoint-reader/crosspoint-reader](https://github.com/crosspoint-reader/crosspoint-reader)
- [crosspoint-reader/crosspoint-simulator](https://github.com/crosspoint-reader/crosspoint-simulator)
- [yattsu/biscuit](https://github.com/yattsu/biscuit), used as flashcard design
  reference material
- [bigbag/papyrix-reader](https://github.com/bigbag/papyrix-reader), used as
  Markdown-reader reference material
