# VibraScope dashboard implementation plan

> **For agentic workers:** Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Replace the placeholder measurement page with the user-approved instrument dashboard and connect the existing probe client.

**Architecture:** App lifecycle delegates to an owned View, as in app_launcher. A platform-independent analysis model owns units, bounded history and FFT/statistics; a worker owns ProbeClient and never touches LVGL. Existing ui::Window supplies animated dialogs. Desktop uses explicitly labelled generated data.

**Tech Stack:** C++17, LVGL 9.2, smooth_ui_toolkit, mooncake, frozen probe protocol / USB transport.

**Spec:** User-approved interactive dashboard in this chat, including subsequent RMS popup and plot selector changes.

## Global constraints

- Preserve launcher and the existing uncommitted startup animation changes.
- Main screen: left 23% logo/about, sensor attitude, numeric metrics with clickable RMS history, acquisition controls. Right: status, flexible plots, bottom controls with rightmost single/dual/triple selector.
- Plot types: acceleration time domain, FFT, time-frequency waterfall, gyro angular velocity. RMS history only in a popup.
- Use sensor coordinates; yaw is relative. Display zero is not calibration.
- Product acquisition presets only Slow / 4k+8k. Do not modify probe firmware or frozen protocol.
- Missing, discontinuous or stale data must not be presented as valid measurements. Desktop demo must remain visibly labelled.
- Follow launcher lifecycle/ownership patterns, use existing window/animation helpers, keep callbacks on the LVGL thread.
- Work directly in the user-specified checkout on codex/vibescope-dashboard; no commit, flash or deployment requested.

## Review focus

- Unit conversion, calibration validity flags, gravity/DC removal and FFT peak units.
- Disconnect, segment/config changes and startup time uncertainty must clear invalid analysis windows.
- Switching gyro must not relabel acceleration statistics as gyro.
- UI destruction must release worker, windows and callback-owned objects without dangling LVGL pointers.
- Recording must not block the acquisition pump; failures/drops must be visible.

### Task 1: analysis and history
- [x] Add host checks for sine+DC RMS/peak/frequency, empty input, calibrated units and history discontinuity.
- [x] Implement focused scope_analysis files and plot/layout state.
- [x] Run host regression.

### Task 2: acquisition and recording
- [x] Implement desktop demo / TAB5 worker with Start/Stop, two presets, status, calibration readback and attitude.
- [x] Maintain bounded histories and asynchronous recording; report unavailable media/errors honestly.
- [x] Keep data freshness and quality explicit in the UI snapshot.

### Task 3: dashboard
- [x] Rewrite view in focused files using launcher naming and owned wrappers.
- [x] Add real logo and a reproducible subset font; use existing animated windows for RMS/about/settings.
- [x] Implement plot selection, layout, source, axis, span, scale, cursor, relative attitude reference and trigger controls.

### Task 4: verification
- [x] Host tests, Windows desktop build and TAB5 build.
- [x] Inspect desktop simulator at 1280x720; exercise layout dropdown, plot modes and RMS popup.
- [x] Fresh review; fix material findings and rerun affected checks.
- [x] Report hardware-only validation limits; preserve unrelated changes.

## Execution ledger

- Baseline: host regression PASS. app_vibescope is static placeholders; app_launcher has reusable owned views and ui::Window.
- Decision: rewrite the view, retain protocol/transport. User explicitly delegated rewrite vs modification and approved the visual design.

- Verification: host tests PASS; Windows desktop build PASS; TAB5 build PASS (binary 0x5964d0, 44% partition free).
- Native simulator exercised single/dual/triple layouts, plot selection, cursor movement, animated RMS detail, recording start/stop and posture rendering.
- Recording readback: VNR v2 parsed and exported successfully; ADXL 83,400 + ICM 166,800 samples, no truncated records.
- Review fixes: reconnect resets client/calibration; history advances only with fresh samples; waterfall preserves narrow peaks; displayed span reflects available samples; recording intent and attitude freshness stay synchronized.
- Initial delivery used desktop validation only. Subsequent user-authorized COM10 flashing and live USB acquisition are recorded in the app README and the following ledger.

### 2026-11-02 hardware acceleration follow-up

- [x] Reproduce running-state delays with integer-only UI/worker/render/flush timing; isolate hidden factory app load and move analysis off the LVGL lock.
- [x] Replace large styled chart tasks with bounded RGB565 raster plots; preserve extrema and real sample/time labels.
- [x] Integrate pinned ESP-DSP 1.8.2 P4 FFT; independent DFT, tiny AC over gravity and worker concurrency host regressions pass.
- [x] Integrate limited PPA image/fill backend with cache/bounds validation and software fallback; native hits confirmed, no DMA errors.
- [x] Preserve global image alignment; align only display DMA allocations. Compare label contents before updating. Expand waterfall directly instead of LVGL software scaling.
- [x] Add mode/source normalization after an automated Gyro-to-FFT diagnostic bypass exposed an out-of-bounds access; regression passes.
- [x] Read-only independent review of DSP, PPA/cache, source normalization and shared waterfall canvas found no remaining blocking issue.
- [x] Complete final native mode-switch regression and flash the manual-start production build.
- Final evidence: `build/vibescope-modes2-device.log` completed all five mode phases and ~60 s acquisition, CRC/gap/drop/rx-overrun/panic/font warnings all zero. Data updates ~10 Hz across dual/waterfall/triple layouts. Native/desktop builds and host regressions passed.
- Production: `build/vibescope-hw-production-build.log`, `build/vibescope-hw-production-flash.log` (COM10, 2500000 bps, hashes verified), `build/vibescope-hw-production-device.log` (manual-start idle, PPA active). Production binary contains no `DIAGNOSTIC_PHASE` string; restored source files were explicitly touched and rebuilt.

### Startup dot animation acceptance

- Body rises from below; cyan dot drops separately from the top, landing frame submission triggers the existing chime once.
- Full-screen composition fixed missing stationary artwork but reduced motion smoothness. Replaced it with a bounded white composition and PPA RGB565 body copy; no repeated redraw during final hold.
- Native evidence: `build/vibescope-startup-fast-device.log`: 29 reveal frames / 1 s; 15 drop frames / 496 ms; mean render 15970 us, max 30902 us; PPA fallback/errors and runtime faults zero. Desktop/native builds pass.
- User physically confirmed the latest animation has recovered; retain this build.

### Near-full-scale display acceptance

- Root cause: the probe reports 95% full-scale proximity, including ICM gyro; live monitoring incorrectly rejected the batch as fatal and erased history.
- Monitoring now retains measured samples with explicit range warnings, while CRC/timing/integrity errors still reject. Warning follows the actual history window and asynchronous analysis result. Probe/protocol/strict diagnostic selection unchanged.
- Host regression, native/desktop builds, and read-only review passed. Native warning counters reached 97/101 while sample windows stayed full, rejected remained startup-only, and CRC/gap/drop/overrun were zero.
- User physically confirmed continuous waveform/numeric display with the near-full-scale warning during the same shaking test. Retain current firmware.
