# Stopped waveform and RMS modal performance

## Reproduction and evidence

User confirms DMA2D version has no visible corruption/ghosting, but RMS popup
remains very slow after stopping acquisition. User reproduced opening/closing
RMS repeatedly and holding it open. Capture: `build/stopped-modal-before-device.log`.

With run=0 and frozen sample counters, the app still spends about 4.3 ms copying
samples, 9.4 ms copying analysis results and 21 ms preparing plots each update.
One modal-heavy five-second window averages ~163 ms per LVGL frame, including
~33 ms software lines; other windows mix opening/closing and idle rendering.
Shared Window animates position, dimensions and opacity. RMS lv_chart is
explicitly refreshed on every poll even when history is unchanged.

## Changes

- Cache plot images by complete data/config/viewport state. New samples, late FFT
  results, segment changes, validity, warnings, axis/cursor/scale/source changes,
  zoom and pan still redraw. Layout recreation explicitly clears the cache.
- Read lightweight runtime metadata first; copy sample histories only when
  serial/generation/validity/rate changes. Otherwise move retained vectors.
- Accept each immutable analysis output once per source, avoiding deep copies
  of the same spectra every stopped poll.
- Replace small and detailed RMS lv_chart instances with cached RGB565 canvases.
  History rasterization occurs only when its values change. Keep the 60-second
  slots, AC RMS values, autoscale and captions.
- ScopeWindow uses a fixed 840x490 opaque short vertical slide, with flat panel
  corners. No resizing or transparency layer animation. Shared launcher Window
  code remains unchanged.
- Suspend plot/pose updates behind the popup; resume using latest state after
  closing. Avoid unchanged LED/record background style writes and pose redraws.

## Validation

Render-cache and RMS raster regression tests first failed before the helper
existed, then passed; full host suite passes. Native and Desktop builds pass.
Independent code review found no important correctness issue. Desktop UI checks:
stop retains data, RMS popup/close displays correctly, stopped zoom and drag
still change the displayed time range without being overwritten by the cache.

COM10 flash hash verified. Current capture: `build/stopped-modal-after-device.log`.
User asked to run 10 seconds, stop, repeat RMS popup/close, then hold it open.
Additional capture: `build/stopped-modal-after-steady-device.log` contains full
retained 4036/8042/8042 sample windows, followed by STOP and RMS modal tests.
User reports it is somewhat faster and asks whether background waveforms stop.
Clarified that background plot display pauses while the popup exists; USB
acquisition and analysis continue when running, and closing catches up to latest.

In steady STOP with RMS held open, sample counters remain unchanged, waveform
preparation is 0 us/update, metadata copy is ~22 us and analysis handling ~8 us;
whole apply ~272 us/update. No image-copy tasks occur in those steady windows.
Only clock/performance-label draws remain: 8-9 render frames per five seconds,
~1.1 ms average. This is idle-modal cost, not modal-opening latency. A mixed
open/close window averages ~23.6 ms/render, peak ~102 ms, so transitions still
have measurable full-area redraw/rotation cost and are not proven 60 FPS.
No panic/brownout/PPA error/fallback, CRC error or probe drop recorded in these
short tests. Main task stack high-water is 220 bytes in the captured live run;
future changes must avoid additional large stack temporaries.

UI FPS is demand-driven: a frozen idle screen now legitimately redraws only
for the clock/performance label and user input. Low stopped-state FPS alone is
not a measure of input latency. No git commit made.
