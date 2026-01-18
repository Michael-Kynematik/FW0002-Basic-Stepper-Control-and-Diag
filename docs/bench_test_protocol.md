# Bench Test Protocol (FW0002)

A) Purpose
This protocol defines a repeatable UART CLI checklist to confirm the boot canary and diagnostic command surface behave consistently before and after cleanup/refactors, aligned to docs/firmware_contract.md.

B) Setup / Preconditions
- Required hardware: ESP32-S3 target with TMC2209 stepper driver and motor connected for motion tests.
- Optional hardware (skippable if not present): onboard neopixel LED, IR emitter/sensor break-beam, load cell + ADC.
- Connect UART console and open a terminal that can send CLI commands and display single-line JSON responses.

C) Smoke Test (2-3 minutes)
1) Boot-time canary (acceptancetest) gating
   - Command: Observe on boot.
   - PASS: By default, no acceptancetest JSON prints at boot.
   - If `CONFIG_FW_BOOT_ACCEPTANCETEST_ON_BOOT=y`: Motor spins ~1s forward then ~1s reverse and a single-line JSON record prints with keys:
     `overall`, `ifcnt_start`, `ifcnt_end`, `cs31`, `cs2`, `microsteps`, `stealthchop`, `errors`.
2) Command list
   - Command: `help`
   - PASS: Plain text list of commands.
3) Version JSON
   - Command: `version`
   - PASS: Single-line JSON with keys `fw_version`, `fw_build`.
4) Snapshot JSON shape
   - Command: `snapshot`
   - PASS: Single-line JSON with top-level keys:
     `uptime_ms`, `heap_free_bytes`, `heap_min_free_bytes`, `reset_reason`, `fw_version`, `fw_build`,
     `schema_version`, `device_id`, `hw_rev`, `board_safe`, `scale`, `motor`.
5) Motor status baseline
   - Command: `motor status`
   - PASS: Single-line JSON with keys:
     `state`, `enabled`, `step_hz`, `dir`, `fault_code`, `fault_reason`.

D) Full Bench Test (10-15 minutes)
1) Command index sanity
   - Command: `help`
   - PASS: Plain text list of commands including:
     `help`, `uptime`, `reboot`, `snapshot`, `version`, `id`, `pins`, `safe`,
     `neopixel`, `ir_emitter`, `ir_sensor`, `scale`, `motor`, `selftest`, `events`, `remote`.
2) Uptime format
   - Command: `uptime`
   - PASS: Plain text line formatted as `uptime_ms=<value>`.
3) Version JSON
   - Command: `version`
   - PASS: Single-line JSON with keys `fw_version`, `fw_build`.
4) Device ID JSON
   - Command: `id`
   - PASS: Single-line JSON with key `device_id` (non-empty string).
5) Pins JSON
   - Command: `pins`
   - PASS: Single-line JSON with keys:
     `neopixel_onboard`, `ir_emitter`, `ir_sensor_input`, `loadcell_adc_sck`, `loadcell_adc_dout`,
     `stepper_driver_step`, `stepper_driver_dir`, `stepper_driver_en`, `stepper_driver_diag`,
     `stepper_driver_uart_tx`, `stepper_driver_uart_rx`.
6) Motor driver acceptancetest (manual)
    - Command: `motor driver acceptancetest`
   - PASS: Explicitly runs the acceptancetest (boot is gated) and prints a single-line JSON record with keys:
     `overall`, `ifcnt_start`, `ifcnt_end`, `cs31`, `cs2`, `microsteps`, `stealthchop`, `errors`.
7) Motor status after test
   - Command: `motor status`
   - PASS: Single-line JSON with keys:
     `state`, `enabled`, `step_hz`, `dir`, `fault_code`, `fault_reason`.
8) Driver status JSON
   - Command: `motor driver status`
   - PASS: Single-line JSON with keys:
     `ifcnt`, `gstat`, `drv_status`, `microsteps`, `run_current_cmd`, `hold_current_cmd`,
     `hold_delay_cmd`, `stst`, `cs_actual`, `stealthchop`.
9) Events tail/clear
   - Command: `events tail 5`
   - PASS: Up to 5 single-line JSON records, each with keys:
     `id`, `ts_ms`, `type`, `subsystem`, `code`, `reason`.
   - Command: `events clear`
   - PASS: `OK`.
10) Remote control surface
    - Command: `remote list`
    - PASS: Single-line JSON with key `actions` containing an array of strings.
    - Command: `remote unlock_status`
    - PASS: Single-line JSON with keys `unlocked`, `expires_in_s`.
    - Command: `remote exec safe`
    - PASS: `OK`.
    - Command: `snapshot`
    - PASS: `board_safe` is `true` in the JSON.
11) Safe state toggle
    - Command: `safe`
    - PASS: `OK` (same behavior as `remote exec safe`).
    - Command: `snapshot`
    - PASS: `board_safe` is `true` in the JSON.
    - Command: `motor enable` (and/or `motor start`)
    - PASS: No unexpected behavior change compared to current behavior. If the current firmware blocks motor actions while safe, expect an error/blocked response; if it allows motor actions today, note this as [CHANGE_WITH_CARE] for future behavior changes.
12) Optional: Neopixel (skip if not present)
    - Command: `neopixel status`
    - PASS: Single-line JSON with keys `mode`, `rgb` (array of 3 ints), `brightness`.
    - Command: `neopixel ready`
    - PASS: `OK` and LED shows ready indication if hardware is present.
13) Optional: IR emitter/sensor (skip if not present)
    - Command: `ir_emitter on`
    - PASS: `OK`.
    - Command: `ir_emitter status`
    - PASS: Single-line JSON with key `ir_emitter_on` set to `true`.
    - Command: `ir_sensor status`
    - PASS: Single-line JSON with key `ir_sensor_state` (0/1 depending on beam).
    - Command: `ir_emitter off`
    - PASS: `OK`.
14) Optional: Load cell (skip if not present)
    - Command: `scale status`
    - PASS: Single-line JSON with keys `raw`, `grams`, `tare_offset_raw`, `scale_factor`, `calibrated`.
    - Command: `scale read`
    - PASS: Single-line JSON with keys `raw`, `grams`, `samples`, `calibrated`.
