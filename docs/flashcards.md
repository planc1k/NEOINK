---
title: Flashcards
nav_order: 9
---

# Flashcards

CrossInk includes a lightweight flashcard study mode for text-first decks. It is
designed for the ESP32-C3's limited RAM and flash: the device studies simple
front/back cards, while heavier conversion work such as APKG parsing happens in
the browser.

## What It Supports

- Deck selection from the Home screen.
- Front/back card review on the device.
- Four review ratings: Again, Hard, Good, and Easy.
- Session-based spaced repetition.
- Configurable e-ink full-refresh cadence, defaulting to every 30 reviewed cards.
- CSV and TSV deck files.
- Browser-side APKG import from the Flashcards web tab.
- Per-deck stats in the web UI: total cards, new cards, and due cards.
- Progress export/import, reset, rename, and delete actions in the web UI.

## What It Does Not Support Yet

- Cloze deletion cards.
- Anki templates and model-specific rendering.
- Images, audio, video, or other media in cards.
- Rich HTML card styling on the device.
- Syncing flashcard progress between devices.
- Calendar-date scheduling. Scheduling is based on study sessions, not wall-clock days.

Those limits are intentional for the first version. They keep the firmware small
and avoid adding a SQLite/APKG parser to the ESP32 build.

## SD Card Layout

Deck files live in:

```text
/.crosspoint/flashcards/decks/
```

Progress files live in:

```text
/.crosspoint/flashcards/progress/
```

The web UI exposes only the deck folder. The rest of `.crosspoint` remains
protected from the file manager and WebDAV so cache, settings, and progress files
are not accidentally edited.

## Deck File Format

TSV is preferred because it is simple to parse on-device:

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

Rules:

- The first row may be a header: `front,back` or `front<TAB>back`.
- Lines starting with `#` are ignored.
- Empty fronts or backs are ignored.
- CSV quotes are supported for comma-containing fields.
- TSV is safer for generated files and APKG imports.

There is a sample deck at:

```text
docs/flashcards/test_deck.csv
```

## Device Controls

From Home, select **Flashcards**.

Deck list:

| Button | Action |
|--------|--------|
| Back | Return home |
| Confirm | Start selected deck |
| Up / Down | Move through decks |

Studying:

| Screen | Back | Confirm | Up / Left | Down / Right |
|--------|------|---------|-----------|--------------|
| Front | Deck list | Flip card | - | - |
| Back | Front | Good | Again | Hard |

On the answer screen, long-press **Confirm** to rate the card **Easy**.

## Scheduling

CrossInk uses a small session-based scheduler:

- New or due cards enter the review queue.
- Again makes the card due next session and lowers ease.
- Hard passes the card, but keeps the next interval shorter than Good.
- Good increases the interval using the card's ease.
- Easy increases ease and schedules farther out.

Because scheduling is session-based, flashcards work even if the device clock is
wrong or Wi-Fi is unavailable.

## Display Refresh

Flashcards use fast e-ink updates while studying and periodically force a
stronger refresh to clear ghosting. The default cadence is every 30 reviewed
cards. Change it from:

```text
Settings > Display > Flashcard Refresh Frequency
```

Available values are 1, 5, 10, 15, and 30 reviewed cards. This setting is
separate from the reader's page refresh frequency.

## Progress Tracking

Per card, CrossInk tracks:

- Card hash.
- Review count.
- Lapse count.
- Ease.
- Interval in sessions.
- Due session.
- Last reviewed session.
- Again, Hard, Good, and Easy counts.
- Last rating.

Per deck, CrossInk tracks:

- Deck hash.
- Total reviews.
- Total Again, Hard, Good, Easy, and lapse counts.
- Current session count.
- Lifetime study sessions.
- Last studied session.

CrossInk does not track answer time, daily streaks, buried cards, suspended
cards, Anki note IDs, or original Anki scheduling data.

## Web Flashcards Tab

Start File Transfer, then open the web UI shown on the device. The
**Flashcards** tab can:

- List installed decks.
- Show deck totals, new cards, due cards, reviewed cards, retention estimate,
  rating totals, lapses, sessions, and a compact per-card progress table.
- Upload CSV or TSV files.
- Convert APKG files in the browser and upload the generated TSV deck.
- Open the managed deck folder in the file manager.
- Export all progress or one deck's progress as JSON.
- Import progress JSON and merge it into matching local decks.
- Reset, rename, or delete decks.

APKG import is intentionally browser-side. The APKG file is a zip containing an
Anki SQLite collection; parsing it on the ESP32 would cost too much firmware
space and RAM.

APKG import behavior:

- Looks for `collection.anki21` or `collection.anki2`.
- Reads the Anki `notes` table.
- Uses field 1 as the card front and field 2 as the card back.
- Strips HTML to plain text.
- Exports up to 300 cards by default.
- Uploads a generated `.tsv` deck.

Progress import behavior:

- Matches decks by deck hash.
- Matches cards by card hash.
- Merges only progress records for cards that exist in the local deck.
- Prefers records with a higher review count, or with the same review count and
  a newer last-reviewed session.
- Does not create, modify, or delete deck files.
- Skips decks whose content hash does not match a local deck.

## Limits

The firmware builds a small in-memory index for deck order/card hashes, then
reads the active card text from SD when it is displayed. It does not keep every
front/back string resident in RAM.

The firmware enforces these limits:

| Limit | Value |
|-------|-------|
| Max deck file size | 512 KB |
| Max indexed cards | 300 |
| Max front text | 512 bytes |
| Max back text | 1024 bytes |
| Heap reserve before/during indexing | 24 KB |
| Minimum largest heap block | 8 KB |

If a deck is too large or memory is low, CrossInk shows an error instead of
continuing into risky allocations.

## Recommended Workflow

1. Start **File Transfer** on the reader.
2. Open the web UI from the QR code or displayed URL.
3. Go to **Flashcards**.
4. Choose a CSV, TSV, or APKG file.
5. Adjust the deck name or card limit if needed.
6. Select **Convert & Upload**.
7. Exit File Transfer.
8. Open **Flashcards** from Home and study the deck.

## Safety Notes

- Keep decks text-first.
- Prefer TSV for generated decks.
- Keep individual cards short.
- Split very large Anki decks before import.
- Do not edit files in `/.crosspoint/flashcards/progress/` manually.
- Keep a backup of important decks on your computer.
