# Direct LCD rotation experiment

**Goal:** Measure whether rotating directly into the existing DPI framebuffer
reduces TAB5 flush cost without changing the UI or acquisition.

**Architecture:** Keep single LCD scanout framebuffer and synchronous PPA
completion as in the existing baseline. For RGB565 partial-mode 90-degree DSI
flushes, rotate into the correct native framebuffer rectangle and submit that
same framebuffer pointer through the IDF no-copy path. Keep legacy fallback
for other formats/rotations. This experiment does not add tear-free swapping.

**Tech stack:** ESP-IDF 5.4.4, LVGL 9.2, vendored esp_lvgl_port, P4 PPA.

- [x] Check local PPA/cache/flush completion contracts and rectangle bounds.
- [x] Add direct destination path with aligned buffer checks and correct source stride.
- [x] Build native, run relevant host checks, flash COM10 at 2500000.
- [ ] Compare the same dual-plot workload and request visible-artifact feedback.
- [ ] Retain only if no correctness regression; record observed timings and limits.

Constraints: no probe/protocol changes, preserve current no-battery charging
workaround, no automatic git commit. Existing profile counters remain enabled.
All framebuffer ownership remains with the LCD driver. Direct path must not
free that memory or use LVGL's logical landscape width as native stride.

Initial verification: new host geometry test failed before the helper existed,
then passed with the implementation. Native build passed after adding the
explicit cache API include. Independent read-only review found no important
issue in rectangle mapping, stride, cache ownership or synchronous completion.
Flash hash verified. Boot log confirms direct rotation enabled at 720x1280.
The existing staging allocation is retained for other-rotation fallback; this
experiment does not yet reduce allocated PSRAM.

## Second single-variable experiment: identity copies

Phase 1 removed explicit flush waits, but the rotation callback remains costly.
PPA image work is still about 21 ms/frame. The IDF's SRM performance test uses
a 21 MPixel/s lower target on pre-v3 P4: the scale/rotate unit is not equivalent
to a linear framebuffer copier. Try IDF's DMA2D `esp_async_fbcpy` for the
already-eligible opaque RGB565 identity-image tasks only. Keep PPA fills and
direct-LCD rotation unchanged, and retain SRM fallback if copier init fails.

Ruling: use the pinned IDF 5.4.4 private copier interface, with an explicitly
private esp_lcd include directory; do not change main component dependencies.
This adds an SDK-version coupling that must be revisited on IDF upgrades.

- [x] Preserve destination CPU-dirty neighboring rows before invalidation.
- [x] Synchronize source cache, submit DMA, wait for completion before LVGL reuse.
- [x] Build/independent review, flash, measure image-copy delta.
- [ ] User confirmation of visual integrity and perceived response.

Result: image copy drops from approximately 21 ms to 6.5-7.1 ms/frame, errors
and fallbacks zero. Rotation callback peak remains ~45 ms. Overall FPS varies
with large dirty coverage and user switching between layouts; no 60 FPS claim.
See docs/milestones/2026-11-03-direct-lcd-rotation.md for captured limits.
