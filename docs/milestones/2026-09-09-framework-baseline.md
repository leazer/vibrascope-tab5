# TAB5 official framework baseline

Date: 2026-09-09

## Result

- Imported the M5Tab5-UserDemo 0.4 application, TAB5 BSP, Desktop platform, assets, and pinned UI dependencies.
- Preserved the existing `vhm_domain` and `probe_protocol` components and their host tests.
- Kept Desktop as the top-level CMake target and ESP32-P4 under `platforms/tab5`.
- Added an explicit framework contract for Desktop/TAB5 selection and LVGL 9.2.2.
- Added an ESP-IDF component-manager override so the lock file and actual build both resolve the in-tree LVGL 9.2.2 source.

## Verification

- Host contract/domain/protocol executable: PASS with MSVC `/W4 /WX`.
- Desktop: `tools/build-desktop-windows.ps1` linked `build/desktop/app_desktop_build.exe`.
- ESP32-P4: ESP-IDF 5.4.4 clean build linked `platforms/tab5/build-clean/vibranode_tab5.bin`.
- Target configuration reported `LVGL version: 9.2.2`; `project_description.json` resolves it to `dependencies/lvgl`.
- Target binary size: `0x5746b0`; app partition free: `0x48b950` (45%).

The imported factory code emits upstream compiler and Kconfig warnings. They did not block either build and are retained as baseline behavior.

## Next

Implement the complete frozen-protocol control-message codecs with golden-vector and malformed-length tests.
