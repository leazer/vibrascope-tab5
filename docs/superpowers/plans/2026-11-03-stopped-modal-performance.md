# Stopped waveform and RMS modal performance fix

**Goal:** Avoid repeating stopped-state rasterization/copies and remove expensive
RMS chart software lines during modal transitions, keeping actual samples and
inspection controls intact.

**Evidence:** stopped-modal-before-device.log: run=0 yet waveform preparation
~21 ms/update, analysis-result copying ~9.4 ms/update, modal render averages
~163 ms with software lines ~33 ms/frame. Existing Window resizes from 180x100
to 840x490 while moving and fading, and refreshes RMS lv_chart every poll.

- [x] Test render cache invalidation and RMS raster guard/endpoint behavior.
- [x] Cache plots by all data/configuration/viewport state; preserve stopped zoom.
- [x] Fetch lightweight runtime metadata, copy samples only on revision change;
  accept each immutable analysis result once per source.
- [x] Render RMS trends as cached RGB565 canvases; update only when history changes.
- [x] Use fixed-size opaque short slide for ScopeWindow only; leave launcher untouched.
- [x] Pause plot updates behind modal, avoid unchanged background styles/pose writes.
- [x] Host/Desktop/native builds, independent review, COM10 flash.
- [x] Board stopped-modal timing and user response (somewhat faster).

Measured steady STOP/modal: plot preparation 0 us, metadata ~22 us, result
handling ~8 us, apply ~272 us. Idle clock-only draws average ~1.1 ms; this is
not opening-frame time. Mixed transition window peak remains ~102 ms.

No protocol/probe changes, no git commit. Continue current display DMA2D/direct
rotation paths and no-battery charging workaround. All timings are wall time;
final visible smoothness requires board feedback.
