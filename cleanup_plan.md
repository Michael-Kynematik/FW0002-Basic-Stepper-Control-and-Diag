# Cleanup Plan (FW0002)

## 1) Scope and Principles
- Production diagnostics = documented CLI surface + JSON schemas in `docs/firmware_contract.md` and `docs/bench_test_protocol.md`, plus minimal boot banner.
- Dev-only bringup = boot-time acceptancetest, raw driver tuning commands, and verbose UART logs that are not required for end users.
- Non-negotiables: keep `docs/firmware_contract.md` and `docs/bench_test_protocol.md` valid; no behavior changes without explicit contract/test updates.

## 2) Current Structure Snapshot
- `main/app_main.c`: boot sequence, init ordering, and boot canary (acceptancetest).
- `main/board.c` + `main/include/board.h`: pin map + safe-state flag; early motor pin safing.
- `main/motor.c` + `main/include/motor.h`: step pulse generation, motor state machine, and motor JSON.
- `main/stepper_driver_uart.c` + `main/include/stepper_driver_uart.h`: TMC2209 UART transport + driver config/status JSON.
- `main/diag_console.c`: CLI parsing, command handlers, JSON printing, and acceptancetest logic.
- `main/snapshot.c` + `main/include/snapshot.h`: snapshot JSON builder and field registry.
- `main/events.c` + `main/include/events.h`: ring-buffer event log and tail/clear.
- `main/remote_actions.c` + `main/include/remote_actions.h`: allowlisted remote actions + unlock gate.
- `main/loadcell_adc.c` + `main/loadcell_scale.c`: load cell sampling + calibration + JSON.
- `main/neopixel.c` + `main/neopixel_strip.c`: LED control + status JSON.
- `main/ir_emitter.c` + `main/ir_sensor.c`: IR I/O + status JSON.
- Motor-critical path files: `main/board.c`, `main/motor.c`, `main/stepper_driver_uart.c`, `main/diag_console.c`, `main/app_main.c`.

## 3) Findings (by category)
### A) Dead/unused code candidates
- `main/loadcell_adc.c`: `loadcell_adc_power_down()` and `loadcell_adc_power_up()` are defined but never called.
- `main/remote_actions.c`: `s_safe_state` is written but never read or reported.
- `main/snapshot.c`: `snapshot_register_field()` is not used outside this file (only defaults are registered today).

### B) Bring-up/debug leftovers to remove or gate
- Boot-time acceptancetest runs unconditionally in `main/app_main.c`.
- `main/stepper_driver_uart.c` has verbose UART logs at INFO level and wiring reference comments mixed with runtime logic.
- `main/motor.c` sets default driver current on init and logs regardless of product mode.
- `main/remote_actions.c` reboot action is a stub (returns OK without reboot).

### C) Risky coupling / unclear boundaries
- `main/diag_console.c` owns acceptancetest logic and directly drives motor + driver settings; this blurs CLI vs motor diagnostics.
- `main/remote_actions.c` "safe" action does not call `board_safe()` (separate safe-state flags).
- JSON escaping/formatting is duplicated between `main/diag_console.c` and `main/snapshot.c`.
- Reset reason formatting is duplicated in `main/app_main.c` and `main/snapshot.c`.

### D) Naming inconsistencies / duplication / layering issues
- Mixed API prefixes in `main/stepper_driver_uart.c` (`stepper_uart_*` vs `stepper_driver_*`) obscure layering.
- `motor driver` CLI subcommands mix transport, driver config, and motor behavior in one handler.
- Event names are stringly-typed and spread across modules without a shared catalog.

### E) TODO/FIXME triage list (top 10)
- `main/board.c`: safe state does not drive all peripheral GPIOs to safe defaults (multiple TODOs).
- `main/remote_actions.c`: reboot action is stubbed (should reboot or be removed/gated).
- `main/remote_actions.c`: "safe" action does not map to `board_safe()` or `board_is_safe()`.
- `main/app_main.c`: boot-time acceptancetest runs every boot (should be gated or configurable).
- `main/diag_console.c`: acceptancetest logic lives inside CLI (consider moving to motor diagnostics module).
- `main/diag_console.c` and `main/snapshot.c`: duplicate JSON string escaping and formatting logic.
- `main/stepper_driver_uart.c`: INFO-level UART logs on every transaction (should be debug-gated).
- `main/stepper_driver_uart.c`: wiring reference block mixes docs with code (should move to docs).
- `main/motor.c`: driver defaults applied on init without a config/profile boundary.
- `main/events.c`: ring buffer size and overflow policy are implicit (define in contract or config).

## 4) Proposed Target Structure (Recommended)
- Recommendation: keep motor code in more than one file (control vs driver), but group it and clarify ownership.
- Target layout (example):
  - `main/board/` (pin map, safe state)
  - `main/motor/`
    - `motor_control.c` (state machine, timer, enable/disable)
    - `motor_diag.c` (acceptancetest logic)
  - `main/drivers/`
    - `tmc2209_uart.c` (UART transport + driver config/status)
  - `main/diag/`
    - `console.c` (CLI parsing + routing)
    - `json_helpers.c` (shared JSON string escaping)
    - `snapshot.c` (snapshot builder)
    - `events.c` (event store)
    - `remote_actions.c` (remote allowlist)
  - `main/peripherals/`
    - `neopixel.c`, `ir_emitter.c`, `ir_sensor.c`, `loadcell_adc.c`, `loadcell_scale.c`

## 5) Execution Plan: Ordered Blocks (Block 4+)
- Block 4: "Module Map Cleanup"
  - Goal: move files into folders and update includes only.
  - Files: `main/*`, `main/include/*`, `main/CMakeLists.txt`.
  - Behavior change: No behavior change.
  - Tests: smoke (CLI help + snapshot).
  - Risk: low.
- Block 5: "Shared JSON Helpers"
  - Goal: centralize JSON escaping/printing used by console + snapshot.
  - Files: `main/diag_console.c`, `main/snapshot.c`, new `main/diag/json_helpers.c/.h`.
  - Behavior change: No behavior change.
  - Tests: smoke (snapshot + events tail output).
  - Risk: low.
- Block 6: "Reset Reason Unification"
  - Goal: single `reset_reason_to_str` shared by boot + snapshot.
  - Files: `main/app_main.c`, `main/snapshot.c`, new `main/diag/reset_reason.c/.h` (or similar).
  - Behavior change: No behavior change.
  - Tests: smoke (boot banner + snapshot reset_reason).
  - Risk: low.
- Block 7: "Remote Safe Alignment"
  - Goal: make `remote safe` call `board_safe()` or remove if not desired.
  - Files: `main/remote_actions.c`, `docs/firmware_contract.md`, `docs/bench_test_protocol.md`.
  - Behavior change: Behavior change with contract update.
  - Tests: full bench (safe + snapshot + motor behavior).
  - Risk: medium.
- Block 8: "Gate Boot Acceptancetest"
  - Goal: guard boot canary behind build flag or runtime setting.
  - Files: `main/app_main.c`, `main/diag_console.c`, `sdkconfig.defaults`, docs.
  - Behavior change: Behavior change with contract update.
  - Tests: full bench (acceptancetest + CLI).
  - Risk: medium.
- Block 9: "Driver Logging Levels"
  - Goal: move UART logs to debug level or compile-time gate.
  - Files: `main/stepper_driver_uart.c`, docs (if behavior visible).
  - Behavior change: No behavior change.
  - Tests: smoke (motor driver ping/status).
  - Risk: low.
- Block 10: "Motor Driver Defaults Configuration"
  - Goal: move default current/microstep setup into a config struct or profile.
  - Files: `main/motor.c`, `main/stepper_driver_uart.c`, new config header.
  - Behavior change: No behavior change if defaults unchanged.
  - Tests: full bench (motor driver status + acceptancetest).
  - Risk: medium.
- Block 11: "Loadcell Power Management"
  - Goal: decide whether to use or remove `loadcell_adc_power_*`.
  - Files: `main/loadcell_adc.c`, `main/loadcell_scale.c`, docs.
  - Behavior change: Depends on chosen path; update contract if behavior changes.
  - Tests: full bench (scale status/read).
  - Risk: medium.
- Block 12: "Event Catalog + Limits"
  - Goal: document event types and tune capacity/overflow policy.
  - Files: `main/events.c`, `main/include/events.h`, docs.
  - Behavior change: No behavior change if limits unchanged.
  - Tests: smoke (events tail/clear).
  - Risk: low.

## 6) Do Not Touch Yet
- Step pulse timing and ISR behavior in `main/motor.c` until motor motion tests are repeatable.
- TMC2209 UART framing and CRC logic in `main/stepper_driver_uart.c` without hardware in the loop.
- Pin map in `main/include/board.h` until hardware revisions are finalized.
- Snapshot schema keys and ordering until contract-driven tests are in place.
- CLI command names and output formats marked [STABLE] in `docs/firmware_contract.md`.
