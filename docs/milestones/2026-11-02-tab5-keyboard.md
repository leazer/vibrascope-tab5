# Tab5 Keyboard base integration

## Installed component

The project previously contained USB HID mouse support and an inactive USB
keyboard callback, not the A164 I2C keyboard driver. The official M5Stack
`m5_tab5_keyboard_component` v0.1.0 is now vendored under
`platforms/tab5/components/`, with its license and exact revision in
`UPSTREAM.md`. Its `espressif/i2c_bus` dependency is locked to 1.5.2; legacy I2C
compatibility remains disabled to match the existing IDF 5.4.4 master driver.

References:

- https://docs.m5stack.com/zh_CN/tab5/Tab5_Keyboard
- https://github.com/m5stack/M5Tab5-Keyboard-UserDemo/tree/b4c36227e2f3373e4e1f8cd7063aefa05cbf72bf/components/m5_tab5_keyboard_component
- https://github.com/m5stack/M5Tab5-Keyboard-Internal-FW/tree/539ec2a06d16b7ebc0ebfafa453d456a9dadc534/code/Keyboard_APP

## Current integration

- I2C1 on SDA GPIO0 / SCL GPIO1, 400 kHz, address 0x6D, hardware interrupt GPIO50.
- A background monitor discovers the device, reads firmware/configuration and
  reconnects after repeated communication failures. An absent keyboard is
  nonfatal. Disconnect clears all held keys after three failed 1 s health checks.
- Official Normal mode delivers press/release events into a 5×14 state matrix.
  Callback work is bounded memory updates only; it never accesses LVGL. Per-key
  UART output is suppressed; a summary is printed at most once per second.
- `GetHAL()->getKeyboardState()` returns a thread-safe snapshot: connection,
  firmware, brightness, RGB mode, 70 held-key flags, event count and last event.
- `setKeyboardRgb(index,r,g,b)` sets LED0/LED1 color registers;
  `setKeyboardRgbMode(true)` selects custom colors, `false` restores binding
  mode. `setKeyboardBrightness(0..100)` changes brightness. These control methods
  perform synchronous, bounded I2C transfers; call them from a background task
  if using them for frequent application updates. Startup preserves the existing
  RGB mode/colors and brightness.
- Platform-neutral defaults report no keyboard on desktop. No keyboard shortcut,
  text entry, LVGL focus group or USB/BLE forwarding has been enabled yet.

The BSP's external/Grove I2C is GPIO53/54, also on hardware I2C1. It cannot be
initialized simultaneously on different pins while this keyboard service owns
I2C1. HAL Grove init/scan/deinit entry points guard this conflict. Internal I2C0
and the USB probe transport remain on their existing paths.

## Available follow-up features

The official driver exposes Normal matrix events, HID key codes and Character
strings, Ctrl/Alt modifiers, interrupt/event queue control, two RGB LEDs,
brightness, firmware and address access. The keyboard firmware also handles
Aa/Sym behavior in Character mode.

Useful VibraScope mappings include Run/Stop, recording, layout/plot/axis
selection, cursor movement, numeric parameter entry, filenames and search.
Normal mode also permits custom chords and hold actions. None of these product
mappings is active merely because the component is installed.

USB HID output requires a separate TAB5 USB device/forwarding implementation;
it must not replace the USB Host role currently used by the probe. BLE forwarding
would require additional radio-side integration, currently disabled in this
project. The enum value named BLE in the library is not evidence of a complete
BLE keyboard implementation.

## Verification

- Host regression PASS: first/last matrix positions, simultaneous keys,
  releases, malformed row/column rejection and disconnect/reconnect clearing.
- Desktop and TAB5 builds PASS. TAB5 image 0x312120 bytes, 69% app partition free.
- COM10 flashed at 2500000 bps, hashes verified; console uses115200 bps with
  inactive DTR/RTS.
- `build/keyboard-device.log`: device detected at0x6D, firmware0x01, brightness20,
  RGB binding mode0. User A+S test produced held=2 and subsequent held=0; 32 key
  events observed. No keyboard initialization failure, panic or watchdog.
- Concurrent live acquisition kept full windows (~4036/8043/8043), CRC0,
  queue drops0, receive overruns0. The cumulative gap count was2 at the second
  measurement start and remained2 through the captured typing/run interval;
  this is not a zero-gap start/stop acceptance claim. UI data updates ~10 Hz.
- RGB register stride was verified against the official STM32 source. Physical
  RGB colors/brightness changes and actual hot unplug/replug remain untested.
  No keyboard firmware or persistent I2C address was modified.
