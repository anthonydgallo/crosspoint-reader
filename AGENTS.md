# AI Guardrails for CrossPoint (Xteink X4)

This repo targets an ESP32-C3 device with tight RAM limits (~380KB usable). Every feature must prioritize stability over throughput.

## Freeze Prevention Rules (Mandatory)

1. Treat TLS/network operations as high-risk memory events.
- Before each HTTPS call, verify both `ESP.getFreeHeap()` and `ESP.getMaxAllocHeap()` are above safe thresholds.
- If thresholds are not met, fail gracefully (error state + message). Never continue and "hope it works".

2. Release large allocations before starting another heavy step.
- Scope API response strings and `JsonDocument` objects so they are freed before download loops/TLS handshakes.
- Do not keep multiple large temporary buffers alive across phases.

3. Keep long loops watchdog-safe and cooperative.
- In long-running loops: call `yield()` and periodic `esp_task_wdt_reset()`.
- Never run large download/process loops without cooperative scheduling points.

4. Keep UI responsive during blocking work.
- Show state transitions before blocking operations using `requestUpdate(true)`.
- Progress updates during downloads/installs should use immediate updates (throttled as needed).

5. Design bulk features for constrained memory first.
- Bulk actions (e.g., "Install All") must process sequentially, with small cooldown/yield points between items.
- On low memory, stop cleanly and preserve already-completed work.

6. Avoid heap fragmentation patterns.
- Pre-size vectors where possible (`reserve`).
- Avoid repeated growth/reallocation in hot loops.
- Reuse buffers when practical.

## Required Verification for New Features

For any feature that adds network, parsing, file transfer, or bulk processing:

1. Build and run on Xteink X4.
2. Exercise worst-case path (large file, many items, weak Wi-Fi).
3. Confirm no hangs/resets and that low-memory paths fail gracefully.
4. Capture serial logs with memory metrics (`Free`, `Min Free`, `MaxAlloc`) before/during/after operation.

## App Store-Specific Contract

When modifying `src/activities/appstore/AppStoreActivity.cpp`:

- Keep TLS memory guards (`free heap` + `max alloc heap`) before app list fetches, manifest fetches, and file downloads.
- Keep watchdog-safe yielding in per-file and per-app loops.
- Keep immediate UI updates for install/progress states.
- Any regression that can freeze `Install All` is a release blocker.
