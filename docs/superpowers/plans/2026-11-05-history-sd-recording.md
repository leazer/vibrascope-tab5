# 30-second history and SD recording implementation plan

> Superseded by the user's rollback request on 2026-11-05: the 30-second shared history implementation was removed; current maximum is 2 seconds using the original fixed ring. SD recording fixes and the SD icon remain. See `docs/milestones/2026-11-05-two-second-rollback.md`.

**Goal:** Keep up to 30 seconds of raw waveforms for live display and stopped inspection, fix SD recording, and show SD availability in the header.

**Architecture:** Rate-sized circular histories retain at least 32 seconds of headroom in shared 2048-sample blocks. A low-priority worker freezes immutable block references and publishes requested windows; the acquisition writer clones only referenced blocks it needs to modify. UI and 1-second analysis remain independent. SD mounts once in a background service and exposes cached state. Recording uses collision-safe exclusive 8.3 filenames without formatting or overwriting files.

**Tech Stack:** C++17, LVGL 9, ESP-IDF 5.4.4, FatFS/SDMMC, host regression executable.

User requirements are the current chat: try 30 seconds, repair SD recording creation failure, add SD status to the header. Preserve all existing uncommitted UI changes.

- [x] Add failing history tests for a 30-second 8k stream, wraparound, bounded range copying, immutable snapshots and gap invalidation; add filename compatibility/collision tests.
- [x] Implement heap-backed shared histories and range reads. Add background history snapshots; keep metric/FFT input limited to one second and throttle long snapshots to 2–5 Hz.
- [x] Expose 2/5/10/30-second window choices for time and gyro; retain stopped zoom/pan through the saved history and cache long plots by their own snapshot version.
- [x] Replace fake SD-mounted result with a background mount/health service, cached status and safe recorder coordination. Preserve media, never autoformat. Use short exclusive filenames and report errno on failures.
- [x] Add SD glyph/state to the header and update generated font resources.
- [x] Run host tests, Desktop and ESP-IDF builds; inspect UI; flash COM10 and monitor. User confirmed SD recording and red REC/timer normal; final shared-block backend has host and Desktop verification plus device boot, while multi-hour stress remains unverified.

Review focus: PSRAM peak usage; acquisitions not blocked by long copies; stale generation rejection; recording cannot overwrite a previous file; SD health polling cannot unmount while a recorder owns a file.
