# Upstream provenance

Official M5Stack component, version 0.1.0, imported from:

- Repository: https://github.com/m5stack/M5Tab5-Keyboard-UserDemo
- Revision: `b4c36227e2f3373e4e1f8cd7063aefa05cbf72bf`
- Path: `components/m5_tab5_keyboard_component`
- License: MIT; copied repository LICENSE and retained source SPDX notices.
- Dependency: `espressif/i2c_bus`, resolved/locked to 1.5.2 with the IDF new
  master driver (`CONFIG_I2C_BUS_BACKWARD_CONFIG=n`).

Local compatibility corrections:

1. Native master transfers use 100 ms timeout instead of waiting indefinitely.
   The upstream two-attempt retry policy is unchanged.
2. `setRGB(index, ...)` uses `index * 4`, matching upstream `getRGB`,
   `setBothRGB`, and the official STM32 firmware's 4-byte LED slots, including
   the reserved byte at 0x63. Verified against internal firmware revision
   `539ec2a06d16b7ebc0ebfafa453d456a9dadc534`,
   `code/Keyboard_APP/Core/User/rgb/user_rgb.c`.

No keyboard firmware, I2C address, USB role or BLE forwarding is changed.
