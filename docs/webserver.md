# Web Server Guide

This guide explains how to use CrossInk's built-in web server for file
transfer, device settings, Wi-Fi/OPDS management, and SD-card font management.

## Overview

The web server is available while the device is in **File Transfer** or
**Calibre Wireless** mode. It can:

- Upload, download, rename, move, and delete files on the SD card
- Scan book metadata and duplicate candidates
- Create folders
- Edit many device settings from a browser
- Manage and import flashcard decks
- Manage saved Wi-Fi networks and OPDS servers
- Upload and delete `.cpfont` SD-card font families
- Accept WebDAV clients and Calibre wireless uploads

The server does not require authentication. Use it only on trusted private
networks or in hotspot mode when you control who is connected.

## Starting File Transfer

1. From the Home screen, select **File Transfer**.
2. Choose one of the available modes:

| Mode | Use when |
|------|----------|
| **Join Network** | You want the reader to join an existing Wi-Fi network. |
| **Calibre Wireless** | You want to receive books from the CrossPoint Calibre plugin workflow. |
| **Create Hotspot** | You want the reader to create its own open Wi-Fi network. |

## Join Network Mode

1. Select **Join Network**.
2. Pick a 2.4 GHz Wi-Fi network from the scan results.
3. Enter the password if prompted.
4. Save credentials if you want the reader to reconnect automatically next time.

After connection, the reader shows:

- The connected SSID
- A QR code for the web URL
- The direct IP URL, for example `http://192.168.1.102/`
- The mDNS fallback URL, usually `http://crosspoint.local/`

Use either URL from a phone, tablet, or computer on the same network.

## Create Hotspot Mode

1. Select **Create Hotspot**.
2. Connect your phone or computer to the open Wi-Fi network:

```text
CrossPoint-Reader
```

3. Open the URL shown on the reader. `http://crosspoint.local/` is preferred
   when supported; the fallback IP is typically `http://192.168.4.1/`.

The reader displays one QR code for joining the hotspot and another QR code for
opening the web interface.

## Calibre Wireless Mode

Calibre Wireless starts the same web server in station mode, then displays setup
instructions and upload progress on the reader. Use this mode with the
CrossPoint Calibre plugin or other clients that speak the documented WebSocket
upload protocol.

For Calibre OPDS browsing, add `/opds` to the catalog URL when configuring an
OPDS server.

## Web Interface

The browser UI has five primary pages.

### Home

The Home page shows firmware status, network mode, IP address, device type,
uptime, and free heap.

### File Manager

The File Manager page can:

- Browse SD-card folders
- Upload files, using WebSocket upload when available and HTTP upload as a fallback
- Optimize EPUB files before upload
- Create folders
- Download files
- Rename files
- Move files into folders, including auto-created Read, Unread, and Collections shelves
- Bulk move and bulk rename selected files
- Scan author/title metadata and duplicate candidates for the current folder tree
- Delete one or more selected files or empty folders

Existing files with the same name are overwritten by uploads. When EPUB files
are overwritten or deleted through the web server, the matching book cache is
cleared so stale metadata is not reused. Rename and move operations migrate the
known EPUB, XTC, TXT, and Markdown cache path where possible so reading progress
survives ordinary library organization.

Library organization uses normal SD-card folders:

- `/Read`
- `/Unread`
- `/Collections/<shelf name>`

Duplicate detection is intentionally lightweight: the scan groups books by
normalized title/filename plus file size. It is meant to find likely duplicates
without hashing every file on the ESP32.

The File Manager keeps most dot-prefixed system folders hidden. The managed
flashcard deck folder is the exception: `/.crosspoint/flashcards/decks/` can be
opened from the Flashcards page so decks can be uploaded without exposing the
rest of `.crosspoint`.

#### EPUB Optimization

When uploading an EPUB, the upload dialog can optimize the file in the browser
before sending it to the reader. This is useful for image-heavy books that are
too large or memory-sensitive for the device.

The default optimization path converts images for e-ink reading, limits them to
the target device size, saves them as JPEG at 85% quality, and applies basic EPUB
repairs such as safer SVG handling. Advanced Mode lets you pick the target
device, JPEG quality, image split or rotation handling, and split overlap.

Optimization changes the EPUB file contents before upload. Note: if you use
hash-based KOReader sync, this will break the syncing because it changes the epub
and therefore the hash. If optimization fails, the uploader falls back to sending
the original file.

### Settings

The Settings page exposes many firmware settings in the browser, including the
reader page refresh cadence and the separate flashcard full-refresh cadence. It
also has cards for:

- Saved Wi-Fi networks
- OPDS servers

Passwords are accepted when adding or editing entries, but saved passwords are
not returned by the API.

### Flashcards

The Flashcards page lists installed decks and shows total, new, due, reviewed,
retention, rating, lapse, session, and compact per-card progress stats. It can
upload `.csv` and `.tsv` decks directly.

It can also import `.apkg` files in the browser. APKG conversion is not done on
the reader itself; the browser extracts the Anki SQLite collection, converts
Basic-style front/back notes to TSV, and uploads the generated deck to:

```text
/.crosspoint/flashcards/decks/
```

See [Flashcards](./flashcards.md) for file format details, APKG limits, and the
on-device study workflow.

Progress can be exported as JSON and imported into another device. Import
matches by deck hash and card hash, then merges only progress records for cards
that already exist in the local deck. It never creates or overwrites deck files.

### Fonts

The Fonts page lists installed SD-card font families and lets you upload
`.cpfont` files. Upload files from one font family at a time. The server validates
the font family name, filename, and `.cpfont` magic bytes before accepting the
upload.

Installed fonts appear in **Settings > Reader > Font Family** after the font
registry refreshes.

## Command Line Use

Power users can use `curl`, WebDAV clients, or WebSocket clients while the web
server is running.

Endpoint details are documented in [webserver-endpoints.md](./webserver-endpoints.md).

## Security Notes

- The HTTP server runs on port 80.
- The WebSocket upload server runs on port 81.
- There is no authentication.
- Anyone on the same network can access the web interface while it is running.
- The server stops when you exit File Transfer or Calibre Wireless mode.
- Hotspot mode creates an open network for connectivity fallback; disconnect when done.

## Tips

1. Use **Create Hotspot** when no trusted network is available.
2. Prefer `crosspoint.local` when available, but keep the displayed IP address as a fallback.
3. Move closer to the router if upload progress stalls in Join Network mode.
4. Upload custom fonts through the Fonts page or copy them to `/.fonts/` or `/fonts/` on the SD card.
5. Exit File Transfer mode when finished to conserve battery.

## Related Documentation

- [User Guide](../USER_GUIDE.md)
- [Flashcards](./flashcards.md)
- [Webserver Endpoints](./webserver-endpoints.md)
- [SD Card Fonts](./sd-card-fonts.md)
- [Troubleshooting](./troubleshooting.md)
