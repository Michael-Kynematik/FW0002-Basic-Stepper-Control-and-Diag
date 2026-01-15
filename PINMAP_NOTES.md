# Pin Map Notes

Current pin map (pins command JSON):
{"neopixel_onboard":48,"ir_emitter":8,"ir_sensor_input":10,"loadcell_adc_sck":12,"loadcell_adc_dout":13,"stepper_driver_step":4,"stepper_driver_dir":5,"stepper_driver_en":6,"stepper_driver_diag":7,"stepper_driver_uart_tx":17,"stepper_driver_uart_rx":18}

Reserved/high-risk notes (DevKitC-1 / ESP32-S3):
- USB CDC console: GPIO 19/20 (D-/D+) are reserved.
- Boot/strapping pins: GPIO 0, GPIO 45, GPIO 46 (avoid driving at reset).
- Onboard NeoPixel data pin is GPIO 48.
- Console invariant: USB CDC on known-good COM port; do not change transport without validation.

Reminder:
- This file is board-specific; modules should remain hardware-agnostic.
- Dev boards vary. Verify against the actual board schematic, but keep the USB CDC console invariant.
