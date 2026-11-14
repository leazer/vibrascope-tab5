# USB connected standby attitude

**Goal:** After TAB5 automatically binds a USB probe, display live orientation
without starting vibration measurement. Stopping measurement retains the last
waveforms but orientation continues. Raw samples must not enter histories or
recording in standby.

**Contract:** Reuse current HELLO/session, FETCH, ATTITUDE and FETCH_DONE formats.
The probe already accepts FETCH in standby; allow the host client to issue it.
Probe standby service produces attitude without exposing raw stream traffic.
No unsolicited frames before HELLO. No new wire fields or commands. Keep
current attitude quaternion units/timing semantics; GUI may refresh at 20 Hz.
Measurement 4k+8k and calibration behavior must remain compatible.

**Task A (probe repo):** implement independent standby orientation lifecycle,
pause/restore correctly across HELLO/START/STOP/USB disconnect and calibration.
Prefer existing sensor/acquisition/attitude facilities, keep ADXL off if feasible.
No silent raw queue filling/dropping. Preserve STOP draining and stream boundaries,
configuration readback and measurement statistics. Tests first; run probe suites,
native firmware build and image verification, preserving 64 KiB flash/SRAM limits.

**Task B (TAB5 repo):** test standby FETCH in ProbeClient; pump standby pose
without admitting raw samples to measurement history. Pose staleness detection
must apply in standby too. Pose canvas must follow probe while stopped, but
existing waveform caching/stop inspection remain intact. Host/Desktop/native
builds and code review before flashing. Backwards compatible with old probes
that return an empty standby FETCH window.

**Validation:** connect idle -> rotate -> measure -> stop -> rotate, waveform
and record counters remain frozen in standby; unplug clears stale pose; reconnect
recovers automatically; 4k+8k stream delivery regression. Actual probe flashing
needs SWD/J-Link; COM10 is TAB5 only. Do not alter calibration records.

## Preflight interface scan

| Tasks | Shared interface / consistency | Result |
| --- | --- | --- |
| A / B | Existing standby FETCH and ATTITUDE wire formats | Same format, no new command |
| A | Standby IMU lifecycle vs measurement STOP drain | Must test before firmware acceptance |
| B | Live pose vs stopped waveforms | Separate paths; raw sample admission remains measurement-only |

Both nested repositories have unrelated dirty state. Edit only assigned source
and tests, preserve IDE project changes, no commits/push or destructive reset.
Ledger: implementation/review/hardware results appended here as work progresses.

## Completion ledger

- Task A complete: ICM-only standby200Hz, frozen raw status and empty raw queues,
  latest-only pose, safe pending START/save, cancel on HELLO/disconnect.
- Task B complete: standby FETCH1024B/50ms, raw filtering before sequence/sink,
  STOP drain unchanged, unconditional500ms pose expiry, connected UI50ms.
- Host/Desktop/native checks pass; probe software suites plus4buildmodes pass.
- Task reviews clean; requested additional lifecycle tests added and passing;
  final cross-side integration review clean.
- Both devices downloaded and verified; calibration4KiB byte-identical.
- User confirms live pose after STOP; board logs prove frozen raw counters and
  changing pose, and automatic pose recovery following probe reset in run=0.
- No commits. Detailed evidence and remaining physical limits recorded in
  docs/milestones/2026-11-03-idle-probe-attitude.md.
