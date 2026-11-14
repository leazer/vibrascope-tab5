# Tab5 Keyboard base integration

Scope: install the official A164 keyboard component, connect its hardware events
and status/RGB controls to the HAL, and verify on the installed hardware. Product
shortcuts and USB/BLE forwarding are follow-up choices, not enabled by this work.

## Implementation

- [x] Vendor the official component and MIT license, record the exact upstream
  revision and any compatibility changes.
- [x] Use the IDF new I2C master driver, I2C1 on SDA0/SCL1 with INT50. The current
  BSP's external/Grove bus uses pins53/54 on the same controller; guard those
  entry points while the keyboard owns I2C1. Internal I2C0 and USB remain intact.
- [x] Add a platform-neutral 5x14 pressed-key state, bounds checking, event
  sequence and disconnect clearing; cover release/multi-key/reconnect behavior
  with host regression before implementation.
- [x] Add an independent hardware-event/health service and HAL snapshots plus
  explicit RGB/brightness controls. Callbacks never touch LVGL or execute I2C in
  the GUI thread. Keep absent/disconnected hardware nonfatal and retry discovery.
- [x] Verify host, desktop and native builds, flash COM10 at2500000, inspect
  connection/version/key events and ensure probe/UI operation still works.
- [x] Document installed capabilities versus available future mappings, upstream
  API/protocol differences and physical verification limits.

## Result

Official component pinned to b4c36227e2f3373e4e1f8cd7063aefa05cbf72bf; host and both builds passed. Device at0x6D/fw0x01 accepted press/release and simultaneous A+S. Background acquisition remained active with full windows. See `docs/milestones/2026-11-02-tab5-keyboard.md` for measured counts and untested RGB/hotplug limits. No application shortcut was added.
