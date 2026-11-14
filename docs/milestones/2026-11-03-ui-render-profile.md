# TAB5 dual-plot render profile

Measured on the board on 2026-11-03, probe running, layout=2, time + spectrum,
no modal. User confirmed approximately 30 seconds of operation. Native/Desktop
builds passed and COM10 flash hash verified. Capture: local build artifact
`build/render-profile-device.log` (55 seconds).

## Representative stable five-second window

35 updates / 35 rendered frames:

| Stage | Wall time per update/frame |
| --- | ---: |
| Snapshot copy, app side | 5.157 ms/update |
| Submit/collect/copy analysis results, app side | 16.868 ms/update |
| Metrics preparation | 0.480 ms/update |
| Pose preparation in main update | 0.424 ms/update |
| Time plot preparation/rasterization | 8.202 ms/update |
| Spectrum preparation/rasterization | 9.153 ms/update |
| Whole LVGL render interval | 96.960 ms/frame |
| Flush callback (rotation + bitmap submission) | 35.277 ms/frame |
| Explicit flush wait | 2.368 ms/frame |
| PPA RGB565 image tasks | 21.279 ms/frame |
| PPA fill tasks | 5.872 ms/frame |
| Software fill | 9.532 ms/frame |
| Software border | 3.026 ms/frame |
| Software label | 8.309 ms/frame |
| Software image | 0.717 ms/frame |
| Software line | 0.721 ms/frame |

The LVGL render interval already includes flush callbacks and waits. Do not add
these to the render interval again. Explicit pre-SRM destination cache sync is
1.075 ms/frame and is already included in the PPA image total. Additional cache
maintenance inside the IDF PPA driver is not separately measured. The software
dispatch wrapper includes synchronous dispatch overhead; all measured times are
wall time and can include preemption by USB/DSP tasks, not isolated CPU cycles.
Background analysis worker cost is roughly 31-33 ms/job and overlaps rendering;
the app-side analysis value above is not the FFT execution time.

Across subsequent windows, render average varies approximately 75-108 ms. PPA
image stays approximately 21 ms/frame, whereas flush and fill/label work vary.
Flushed area ranges from 460,167 to 792,748 pixels/frame in those windows:
roughly 50-86% of the 1280x720 screen. PPA errors/fallback remain zero. Rejected
evaluation count is not a count of failed GPU operations.

## Interpretation and next experiments

1. Largest measured chunk: flush callback plus PPA image work. The portrait
   panel uses a rotated landscape UI, and current canvas data passes through
   composition, rotation and LCD submission. Exact rotation vs submission time
   inside flush is still unmeasured.
2. Dynamic canvas redraw plus broad dirty areas drives repeated background,
   labels and borders. Both visible plots invalidate their full canvas on every
   update. A specific dirty-rectangle overflow is not yet proven.
3. App-side plot preparation costs about 17-19 ms combined. Metrics and the
   small pose raster are not the primary preparation cost, although LVGL
   composition of their affected areas is measured separately.
4. Prioritize reducing dirty coverage and redundant canvas/label updates, then
   measure rotation/submission separately and evaluate fewer image passes.
   Check analysis submission/result copies as a separate app-side bottleneck.

This change adds integer-only, five-second diagnostic counters; it does not
change the rendering strategy. Software timing wraps local LVGL 9.2's existing
single synchronous software draw unit (ID 1, LV_USE_OS=0) without vendor edits.
Other configurations skip that wrapper. Charging remains disabled for the
already-verified no-battery speaker-noise workaround.
