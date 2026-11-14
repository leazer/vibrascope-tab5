# TAB5 sustained acquisition reboot: brownout evidence

User reported automatic reboot after roughly ten minutes of acquisition. This report concerns the TAB5 hardware, not the Windows simulator.

## Captured evidence

User attachment `a2295541-a236-433c-ae2e-3c0a8c3dcaf4/已粘贴的文本.txt`:

- Lines 633–639 immediately before reboot: connected/run=1; batches=34320/65936; rejected=9/113 with the unchanged startup TIME_UNCERTAIN quality 0x40; CRC/gaps/drops=0; rx overruns/timeouts/retries=0; complete sample windows 4036/8041/8041; PPA fallback/errors=0.
- Line 640: `E BOD: Brownout detector was triggered`.
- Lines 645–647: ROM reports SW_SYS_RESET, saved CPU0 PC resolves to `rtc_brownout_isr_handler`, ESP-IDF `components/esp_system/port/brownout.c:72`.
- CPU1 was interrupted in CRC parsing. This is the interrupted execution location, not evidence that CRC code caused the reset.
- The IDF brownout ISR explicitly records ESP_RST_BROWNOUT, logs this message and invokes `esp_rom_software_reset_system()`. Thus the ROM's SW_SYS_RESET label is consistent with brownout handling.
- No panic/assert/watchdog/heap-corruption error appears around this reset. This supports a brownout-triggered reset for this captured incident; it does not prove absence of every other long-run issue.

Non-resetting COM10 capture after the user released the port succeeded: `build/reboot-followup-device.log` (55 seconds). Device connected, idle RUN=0, no new reset during that interval. This is not a sustained-acquisition acceptance run.

## Diagnosis and remaining isolation

Immediate reset trigger is the brownout detector. The supply path responsible is not yet established. Need user power topology (computer/charger, cable/hub, battery fitted, probe USB-A supply) and a controlled sustained-acquisition comparison using a known adequate supply. Do not disable brownout protection, lower its threshold, or change sensor/protocol processing to mask this event.

Existing HAL enables charging and quick-charge control at boot; these settings are inherited defaults, not evidence of causation. INA226 boot bus voltage alone must not be treated as the ESP32-P4 supply measurement without confirming the schematic measurement node. Fast supply dips may also be missed by averaged telemetry.

References: [ESP-IDF P4 fatal errors](https://docs.espressif.com/projects/esp-idf/en/v5.4.2/esp32p4/api-guides/fatal-errors.html), [TAB5 official power notes](https://docs.m5stack.com/en/core/Tab5).

No firmware changes or downloads were performed for this diagnosis. Dynamic waveform changes from the preceding task remain separate and uncommitted.
