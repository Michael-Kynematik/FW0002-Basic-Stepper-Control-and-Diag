# Firmware Contract (FW0002)

A) Purpose
This contract captures the current diagnostic console command surface and JSON output shapes as implemented today, so cleanup and refactor work can validate "no loss of functionality" by comparing behavior and schema before and after changes.

B) CLI Command Surface (API Contract)
- `help` — Lists commands or shows usage for one command. Output type: plain text. Stability: [STABLE].
- `uptime` — Prints uptime in milliseconds as `uptime_ms=<value>`. Output type: plain text. Stability: [CHANGE_WITH_CARE].
- `reboot` — Prints `restarting...` then calls restart. Output type: plain text. Stability: [CHANGE_WITH_CARE].
- `snapshot` — Prints one-line JSON system snapshot. Output type: JSON. Stability: [STABLE].
- `version` — Prints firmware version/build JSON. Output type: JSON. Stability: [STABLE].
- `id` — Prints device ID JSON. Output type: JSON. Stability: [CHANGE_WITH_CARE].
- `pins` — Prints pin map JSON. Output type: JSON. Stability: [CHANGE_WITH_CARE].
- `safe` — Applies board safe state and prints `OK`. Output type: plain text. Stability: [CHANGE_WITH_CARE].
- `neopixel` — Controls LED; `status` prints JSON, others print `OK`/`ERR`. Output type: JSON. Stability: [CHANGE_WITH_CARE].
- `ir_emitter` — Controls IR emitter; `status` prints JSON, others print `OK`/`ERR`. Output type: JSON. Stability: [CHANGE_WITH_CARE].
- `ir_sensor` — Reads IR sensor; prints JSON status. Output type: JSON. Stability: [CHANGE_WITH_CARE].
- `scale` — Load cell commands; `read`/`status` print JSON, `tare`/`cal` print `OK`/`ERR`. Output type: JSON. Stability: [CHANGE_WITH_CARE].
- `motor` — Motor controls; `status`/`driver` subcommands print JSON, other actions print `OK`/`ERR`. Output type: JSON. Stability: [STABLE] for enable/disable/dir/speed/start/stop/status/clearfaults and `driver acceptancetest`; [CHANGE_WITH_CARE] for other motor/driver subcommands. Driver subcommands implemented today: `ping` (OK/ERR), `ifcnt` (JSON), `stealthchop on|off` (OK/ERR), `microsteps <1|2|4|8|16|32|64|128|256>` (OK/ERR), `current run <0-31> hold <0-31> [hold_delay <0-15>]` (OK/ERR), `status` (JSON), `clearfaults` (OK/ERR), `acceptancetest` (JSON).
- `selftest` — Verifies required commands and snapshot format; prints `OK` or `ERR ...`. Output type: plain text. Stability: [CHANGE_WITH_CARE].
- `events` — `tail` prints JSON records (one per line), `clear` prints `OK`/`ERR`. Output type: JSON. Stability: [STABLE] for tail/clear.
- `remote` — Lists or executes allowed remote actions; JSON for `list`/`unlock_status` and some `exec` actions, otherwise `OK`/`ERR`. Output type: JSON. Stability: [CHANGE_WITH_CARE] (some actions are stubbed, e.g., `exec reboot` returns OK without rebooting).

C) JSON Output Contracts
- `snapshot`
  - Top-level keys: `uptime_ms`, `heap_free_bytes`, `heap_min_free_bytes`, `reset_reason`, `fw_version`, `fw_build`, `schema_version`, `device_id`, `hw_rev`, `board_safe`, `scale`, `motor`.
  - `scale` object keys: `raw`, `grams`, `tare_offset_raw`, `scale_factor`, `calibrated`.
  - `motor` object keys: `state`, `enabled`, `step_hz`, `dir`, `fault_code`, `fault_reason`.
  - Invariants: single-line JSON on success; if build fails, output is `{"error":"snapshot_format"}`.
- `version`
  - Keys: `fw_version`, `fw_build`.
- `id`
  - Keys: `device_id`.
- `pins`
  - Keys: `neopixel_onboard`, `ir_emitter`, `ir_sensor_input`, `loadcell_adc_sck`, `loadcell_adc_dout`, `stepper_driver_step`, `stepper_driver_dir`, `stepper_driver_en`, `stepper_driver_diag`, `stepper_driver_uart_tx`, `stepper_driver_uart_rx`.
- `neopixel status`
  - Keys: `mode`, `rgb` (array of 3 ints), `brightness`.
- `ir_emitter status`
  - Keys: `ir_emitter_on`.
- `ir_sensor status`
  - Keys: `ir_sensor_state`.
- `scale read`
  - Keys: `raw`, `grams`, `samples`, `calibrated`.
  - Invariants: `grams` is `null` if not calibrated.
- `scale status`
  - Keys: `raw`, `grams`, `tare_offset_raw`, `scale_factor`, `calibrated`.
  - Invariants: `raw`/`grams` may be `null` when data is unavailable or not calibrated.
- `motor status`
  - Keys: `state`, `enabled`, `step_hz`, `dir`, `fault_code`, `fault_reason`.
- `motor driver ifcnt`
  - Keys: `ifcnt`.
- `motor driver status`
  - Keys: `ifcnt`, `gstat`, `drv_status`, `microsteps`, `run_current_cmd`, `hold_current_cmd`, `hold_delay_cmd`, `stst`, `cs_actual`, `stealthchop`.
  - Invariants: some fields may be `null` if UART reads fail; `*_cmd` fields are cached last-commanded values.
- `motor driver acceptancetest`
  - Keys: `overall`, `ifcnt_start`, `ifcnt_end`, `cs31`, `cs2`, `microsteps`, `stealthchop`, `errors`.
  - Invariants: `overall` is `PASS` or `FAIL`; `errors` is a JSON array of strings.
- `events tail`
  - Each line is a JSON record with keys: `id`, `ts_ms`, `type`, `subsystem`, `code`, `reason`.
- `remote list`
  - Keys: `actions` (array of strings; includes a decorated string for `neopixel_set`).
- `remote unlock_status`
  - Keys: `unlocked`, `expires_in_s`.
- `remote exec`
  - Action-specific JSON when provided. Known actions:
    - `snapshot_now` returns the same JSON shape as `snapshot`.
    - `neopixel_status` returns `{"neopixel_on":<bool>}`.
  - Invariants: if no JSON is returned, output is `OK`.

D) Safety / "Safe State" Contract
- "Safe state" means `board_is_safe()` is set to true (reflected in `snapshot` as `board_safe: true`) and `board_safe()` has been invoked.
- `board_safe()` currently calls `motor_disable()` and sets the safe-state flag. It does not yet drive other GPIOs to safe defaults (marked TODO in code).
- `motor_disable()` stops step pulses, sets the step pin low, disables the driver enable pin, and sets the motor state to `disabled`.
- `remote exec safe` invokes the same `board_safe()` behavior as the CLI `safe` command; `snapshot` `board_safe` is the authoritative safe-state indicator.
- Boot-time acceptancetest is gated by `CONFIG_FW_BOOT_ACCEPTANCETEST_ON_BOOT` (default `n`); when enabled, the boot canary runs once at startup and prints the acceptancetest JSON.

E) Change Rules During Cleanup
- Do not rename stable commands.
- Do not change stable JSON key names or schema shape.
- Behavior changes must be isolated and accompanied by test protocol updates.
- Any gated/service-only changes must preserve behavior when enabled.
