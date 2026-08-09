# Fork upstream sync — 2026-08-09

## Purpose

This fork was synchronized with `crosspoint-reader/crosspoint-reader` upstream
`master` at `136cff4b` (CrossPoint 1.5.0). The sync preserves upstream history
and retains the fork's Xteink X4 apps platform.

## Merge record

- Fork merge commit: `fb906eac` — `Merge upstream 1.5.0 and preserve X4 apps`
- Fork GitHub PR merge: `1591fde7` — PR #61
- Strategy: merge upstream into the fork; no rebase, force-push, or write to
  the canonical upstream repository.

## Retained fork features

- An X4-only **Apps** entry on the Home menu.
- Rosary, art gallery, calculator, flashcards, Minesweeper, text tools,
  image viewer, random quotes, and the other bundled SD-card apps.
- 22 app manifests, including the restored Text Editor manifest.
- Confirmed app deletion from the Apps hub with a restricted `/apps/<app>`
  recursive-delete implementation.
- Fork-local OTA releases from `anthonydgallo/crosspoint-reader`.
- A release `apps.zip` containing the top-level `apps/` directory.

## Deliberate design decisions

- The obsolete network App Store was removed. Apps are distributed by copying
  `apps.zip` to the SD card instead.
- The fork's old settings navigation, Lyra/theme customizations, custom app
  icons, rename/move file-browser work, scroll keyboard, and boot branding
  were dropped in favor of upstream behavior.
- The Image Viewer advertises and scans PNG, JPG, and JPEG only; HEIC is not
  supported on-device.
- Apps are shown at runtime only on Xteink X4, while other upstream device
  builds remain available.

## Important upstream capabilities now included

- More robust EPUB rendering, bookmarks, text settings/live preview, next-book
  suggestions, footnote and navigation fixes, and better malformed-book
  handling.
- Arabic/Farsi/Urdu bidirectional shaping, Hebrew and CJK improvements, ruby
  text support, additional hyphenation, and expanded translations.
- SD-card font installation/downloads and improved typography controls.
- Improved OPDS, WebDAV, Wi-Fi, web upload, KOReader sync, and dictionary
  workflows.
- X3/X4 display, sleep, battery, ghosting, touch, OTA safety, and memory
  reliability improvements.

## Memory and safety hardening performed in this fork

- App manifests are size-capped and parsed lazily; app scans are bounded and
  watchdog-cooperative.
- Random Quote uses reservoir sampling, retaining one selected quote rather
  than loading the complete data set into RAM.
- Text Editor limits document size to 8 KB and uses a bounded operation undo
  history instead of full-text snapshots.
- Image discovery is capped at 256 files and yields/reset-watchdogs during
  scans.
- OTA checks both free heap and largest allocatable block before release
  metadata and firmware HTTPS fetches, logs `Free`, `Min Free`, and `MaxAlloc`,
  and understands `1.5.0-fork.N` ordering.

## Validation completed

- `pio run -e default` passed for the X3/X4 ESP32-C3 configuration.
- `pio run -e gh_release` passed for version `1.5.0-fork.1`.
- Host test suite passed: 130/130.
- All 22 manifests and their referenced app files were validated.
- The generated `apps.zip` layout was validated to contain top-level `apps/`.

## Local artifacts created during the merge session

These are intentionally not versioned in the repository:

- `CrossPoint-1.5.0-fork.1-X4.bin` — release firmware build.
- `CrossPoint-1.5.0-fork.1-apps.zip` — SD-card app bundle.
- `Xteink-current-firmware-backup-20260809-150813.bin` — a complete 16 MB,
  read-only backup of the connected device's flash. Treat it as private because
  it may contain device settings and Wi-Fi credentials.

## Follow-up hardware validation

Before a public release, test the build on an Xteink X4 under large-file,
many-app, and weak-Wi-Fi conditions. Capture serial logs containing `Free`,
`Min Free`, and `MaxAlloc` around network and bulk operations.
