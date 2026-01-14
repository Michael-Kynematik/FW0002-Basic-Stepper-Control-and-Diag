# Pin Map Notes

Current pin map (pins command JSON):
{"neopixel_onboard":48,"ir_emitter":8,"beam_input":10,"hx711_sck":12,"hx711_dout":13,"tmc_step":4,"tmc_dir":5,"tmc_en":6,"tmc_diag":7,"tmc_uart_tx":17,"tmc_uart_rx":18}

Reserved/high-risk notes (DevKitC-1 / ESP32-S3):
- USB CDC console: GPIO 19/20 (D-/D+) are reserved.
- Boot/strapping pins: GPIO 0, GPIO 45, GPIO 46 (avoid driving at reset).
- Onboard NeoPixel data pin is GPIO 48.
- Console invariant: USB CDC on known-good COM port; do not change transport without validation.

Reminder:
- Dev boards vary. Verify against the actual board schematic, but keep the USB CDC console invariant.
