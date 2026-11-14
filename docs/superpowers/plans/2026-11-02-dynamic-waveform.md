# Dynamic waveform and stopped inspection

Goal: use real sample positions at low density and preserve extrema per pixel at high density. Inspect the last approximately one second retained by the existing runtime after STOP.

Architecture: pure sample-index viewport and extrema iterator; opaque RGB565 raster remains the rendering path. Per-plot horizontal zoom buttons and stopped drag operate on the same viewport. No interpolation, new acquisition buffer, probe changes or signal processing changes.

User direction: dynamic window rendering plus zoom/pan after stopping acquisition. Implementation choices: 2x horizontal zoom centred on the current viewport, minimum 8 samples, Fit restores the configured time span at the newest data. Only time/gyro plots expose these controls. Drag is stopped-only; active cursor mode takes precedence for cursor positioning. Running follows newest samples and disables retained offsets.

- [x] Pure viewport tests: sparse points retain actual indices; dense impulses survive with chronological extrema; empty/tiny data; zoom/pan boundaries and reversibility.
- [x] Implement viewport and dynamic raster, keeping FFT/waterfall rendering unchanged.
- [x] Integrate per-plot controls, stopped drag, consistent visible-window axes/cursor/autoscale. Preserve existing one-second snapshot and RMS/FFT analysis window.
- [x] Host tests and Desktop/native builds; document measured validation and remaining hardware checks. No automatic commit or flash.

Validation: watched the new host test fail for absent viewport header, then pass after implementation. Final host PASS; Desktop build `build/dynamic-desktop.log` and ESP32-P4 build `build/dynamic-native.log` passed. Desktop visual interaction confirmed STOP retains samples, + changes ~100 ms to ~50 ms, drag shifts waveform and axis together. Independent read-only review found no required fixes. Hardware touch/frame rate not yet measured; USB disconnect retains its existing clearing behaviour.

Ruling: use the retained snapshot mean for acceleration AC baseline across all visible windows, so zooming into part of a cycle does not shift zero. Autoscale and cursor use the visible window; statistics remain independent. Controls apply horizontal zoom only; no new filtering or vertical zoom.
