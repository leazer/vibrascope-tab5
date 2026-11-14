# TAB5 periodic speaker noise investigation

## Report

User hears a low-frequency noise approximately every 16.52 seconds after boot.
Supply is computer USB-C, with no battery installed. This is a recurring noise,
not a single pop at startup.

## Evidence and limits

- Startup MP3: 44.1 kHz, stereo, 0.905170 seconds (ffprobe).
- Earlier boot logs show playback completes around boot +6 seconds; no matching
  16-second app audio timer found.
- I2S default DMA configuration is 6 descriptors x 240 frames: about 30 ms at
  48 kHz or 32.65 ms at 44.1 kHz, not a 16.52-second ring period.
- BSP initializes SPK_EN high and leaves the external amplifier enabled.
  Player completion mutes the ES8388; it does not disable SPK_EN.
- HAL enables charging and QC even without a battery. Charger-related supply
  disturbances are a hypothesis, not an established cause or documented
  16.52-second IC behavior.
- The initial 45-second serial capture contained ongoing acquisition, not a
  boot/reproduction. No conclusion about the acoustic event follows from it.

## Single-variable hardware comparison

Temporary change in `platforms/tab5/main/hal/hal_esp32.cpp`: charging enable
changed from true to false. QC and audio code remain unchanged. A warning log
identifies this diagnostic configuration. Native build and COM10 flash passed.
User reported that the noise disappeared with charging disabled.

The original `setChargeEnable(true)` was restored, rebuilt and flashed for the
reverse comparison. QC/audio remained unchanged. User confirmed the original
periodic noise returned. Thus charging-enabled -> disabled -> enabled produced
noise -> no noise -> noise on the same no-battery USB-C setup.

Charging enable is causally associated with the reported noise in this setup.
The exact analog coupling path and the IC's internal timing have not been
measured, so a specific 16.52-second charger mechanism is not established.

Final interim configuration: charging disabled again, QC/audio unchanged, to
restore quiet operation for the user's current no-battery setup. This also
disables battery charging until explicitly enabled again. No automatic battery
presence detection has been added. The configuration change remains uncommitted.

This is an interim workaround, not a final battery-aware charging policy. Future
work should identify a reliable battery-presence signal and validate charging
with a battery installed. Do not infer battery presence from the earlier ~1.8 V
INA226 reading without verifying the schematic and measurement behavior.
