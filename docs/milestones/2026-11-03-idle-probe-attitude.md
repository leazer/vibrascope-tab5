# USB standby attitude: two-sided implementation and hardware acceptance

## Behavior

After automatic USB HELLO binding, the probe runs only the ICM at 200 SPS for
orientation. The host fetches a latest-pose mailbox every ~50 ms. No new commands
or frame fields; there is no unsolicited transmission before binding. ADXL is
off during standby. Raw acquisition begins only on START; STOP drains its tail
before orientation resumes. Waveform inspection remains frozen while pose moves.

TAB5 allows standby FETCH, ignores standby raw frames before sequence tracking
and sink delivery, and expires pose after 500 ms without an update. Connected UI
polling is 50 ms; cached raw waveforms are not rebuilt by pose-only updates.
Calibration and ordinary 4k+8k acquisition configuration remain unchanged.

## Verification

- Probe time/AGM/acquisition, SPI/validation/FIFO/frame/LED/attitude/calibration/
  USB/protocol/board-check/IRQ regression suites pass; PC host 6 tests pass.
- Four probe firmware modes build with zero errors/warnings. Production image:
  Code62740, RO2636, RW288, ZI53160; SRAM53448/65536 bytes.
- Pending START/save retries, HELLO replacement, disconnect during pending save,
  STOP tail retention and latest-only pose mailbox have regression coverage.
- TAB5 host suite, Desktop build and native build pass. Task reviews plus final
  cross-side integration review found no important actionable issues.
- TAB5 flashed via COM10 at 2500000 bps, hash verified. Probe flashed through
  J-Link SN20721552, -AT32F413CCU7, SWD1MHz, program/verify succeeded.
- HEX SHA256: 9725f3e5f0b1ba1a7212e295a1f2609045546e4766ab8e9c3d0debe61882b3cb.
  HEX range08000000..08010080 excludes the final4KiB calibration sectors.
- Full calibration backup before/after is byte-identical (4096 bytes), SHA256
  ffca1184fc52e41178c8b343b581eb58a6cd1dc5a7c6e0e7404a0ee3218c1751.
  Slot0 v3 flags07 generation3; slot1 v3 flags05 generation2; both CRC-valid.

## Board evidence

Capture `build/idle-pose-both-device.log`: measurement received full ~4035/8043/
8043 sample windows, then STOP; batch counters stay364/1067 while valid pose
angles continue changing. CRC/gaps/drops stay0. User confirms orientation keeps
changing after stopping. Pose preparation ~0.38ms, cached plot calls ~2-3us,
so standby orientation does not restart wave rasterization.

Capture `build/idle-pose-reconnect-device.log`: probe reset yields CDC disconnect
then reconnect. Without any RUN command, run=0 and samples=0/0/0 with valid=1
pose restored automatically. Quaternion relative yaw resets on probe restart,
as expected for the current nonmagnetic orientation pipeline. No panic/brownout
or PPA error observed in these short captures.

The existing200SPS FIR introduces245ms group delay; first valid pose requires
sensor startup/filter warmup (~0.7s). These checks do not establish long-run
stability or exact sample delivery ratio at every rate. No commits or push made.
