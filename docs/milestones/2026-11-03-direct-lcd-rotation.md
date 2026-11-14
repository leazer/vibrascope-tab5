# TAB5 display path experiments

## Phase 1: direct rotation into scanout framebuffer

Implemented in the vendored LVGL 9 port. RGB565, partial mode, 90-degree DSI
flushes now rotate directly into the LCD driver's borrowed 720x1280 buffer.
IDF draw_bitmap receives that same buffer and takes its no-copy branch. PPA
completion is blocking, and the original on_color_trans_done callback releases
the LVGL buffer. The original single scanout buffer is retained, so this is not
a tear-free implementation. Other rotations/formats retain the old path.

Host geometry tests, native build and independent source review passed. Flash
hash verified; boot log confirms direct path enabled. Captures:
`build/direct-rotation-device.log`, `build/direct-rotation-steady-device.log`.

Observed: explicit flush waits fall from about 2-4 ms/frame to below 0.1 ms/frame.
Rotation callback still has approximately 45 ms maximum. Image-copy tasks remain
approximately 21 ms/frame. Overall workload varied (user tried plot types and
triple layout), so no controlled headline FPS gain is established. No panic,
PPA error, CRC error or acquisition drop occurred in these short captures.
Visual artifact confirmation remains pending.

## Phase 2: identity image copies via DMA2D

PPA SRM was being used even for opaque unscaled/unrotated RGB565 images.
Experiment: use IDF 5.4.4 esp_lcd's private esp_async_fbcpy interface for those
already-eligible tasks. Keep PPA fills, direct LCD rotation and eligibility
unchanged. Write back CPU-dirty destination rows before invalidation, write back
source rows, and wait for DMA completion before allowing LVGL buffer reuse.
Copier allocation failure retains the PPA path; transaction/cache failure uses
the existing software fallback before any copy is outstanding.

The SDK-private header is included privately by main CMake; no PRIV_REQUIRES
was added. This pins the integration to the audited IDF version and should be
reviewed during SDK upgrades. Independent source review found no important issue.
Host tests, Desktop and native builds pass. Flash hash verified; boot log
confirms `RGB565 image copy: DMA2D`. Capture: `build/dma-copy-device.log`.

Observed identity image copy time drops from approximately 21.3 ms/frame to
6.5-7.1 ms/frame, with DMA image counter increasing and errors/fallbacks zero.
Explicit flush waits remain below 0.1 ms/frame. Rotation callback peak remains
approximately 45 ms. User changed two/three-plot layouts and plot types during
the capture, so there is no controlled overall FPS claim. Stable dual-plot
windows observed 37 frames / 5 seconds and 42 frames / 5 seconds with LVGL
render means 90.9 ms and 73.4 ms respectively. Triple-plot windows were roughly
32-33 frames / 5 seconds, depending on plot type. No panic/brownout/CRC errors,
probe drops or PPA fallback/errors were recorded in this short run. These are
not long-duration reliability results. User later reported no visible image
corruption or ghosting; tearing was not explicitly assessed in that response.

The board is left on phase 1 + phase 2 for user inspection. No git commit made.
Next bottleneck: repeated large dirty coverage, plus the SRM rotation itself.
Changing framebuffers alone did not eliminate that work; any further design
must account for partial-update consistency and VSYNC ownership.
