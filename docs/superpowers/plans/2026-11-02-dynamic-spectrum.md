# Dynamic FFT display

User approved the proposed FFT display changes: dynamic frequency-bin mapping, stopped frequency zoom/pan, optional dB amplitude. Follow the existing RGB565 raster and independent per-plot viewport; leave FFT calculation/calibration, time waveform, waterfall and probe protocol unchanged.

- [x] Test sparse original bin positions, dense disjoint peak groups (boundary peak emitted once), endpoints, empty ranges and finite dB conversion.
- [x] Implement dynamic spectrum iterator and raster at real bin positions; eliminate fixed 256-point path for FFT.
- [x] Enable stopped +/-/Fit and drag for spectrum; update axes, visible autoscale and cursor against the same frequency window. Separate kind/source changes reset viewport. Fit = DC through Nyquist. Add third amplitude option dB re 1g, 80 dB visible dynamic range, reference marked on axis.
- [x] Host regression, Desktop and TAB5 build; review. No automatic flash (supply brownout is a separate diagnosed issue).

Validation: host tests passed after red/green coverage; Desktop and native builds passed (build/spectrum-desktop.log, build/spectrum-native.log). Desktop interaction verified stopped FFT zoom/pan and dB. Review found a grid-change window issue; SpectrumGrid::sync and regression fixed it, reviewer confirmed closed. No flash; hardware frame cost remains unmeasured.
