# Wireless synchronization coordinator prototype

This standalone Zephyr application contains the coordinator and responder roles
used in the MC014 minimum synchronization experiment. Build the same source
with `coordinator.conf` for RX1 or `responder.conf` for RX2/TX1.

The coordinator broadcasts an epoch and sequence every 100 ms over ESB channel
40. A responder receives the RADIO ADDRESS event, starts TIMER20 through DPPI,
and toggles P1.09 after the configured delay. P1.09 is an observation output;
check the board schematic and disconnect any conflicting FPGA connection
before attaching an instrument.

## Build

Set `NCS_ROOT` to the local nRF Connect SDK installation. Optionally set
`ZEPHYR_SDK_INSTALL_DIR` or pass `-SdkRoot`.

```powershell
$env:NCS_ROOT = 'D:\path\to\ncs'
.\scripts\build_sync.ps1 -Role coordinator -Board connectkit -Pristine
.\scripts\build_sync.ps1 -Role responder -Board connectkit -Pristine
.\scripts\build_sync.ps1 -Role responder -Board tx1 -Pristine
```

Build output defaults to the system temporary directory. Pass `-BuildDirectory`
to choose another location.

## Capture

```powershell
python .\scripts\capture_sync_uart.py `
  --coordinator COM11 --responders COM10 COM4 --seconds 30
```

## Evidence boundary

The RX1-to-RX2 ten-second window on 2026-10-18 recorded coordinator
`tx_ok=940`, responder `frames_ok=927`, `frames_bad=0`, and `last_seq=940`.
This supports the wireless common-round and local hardware timer prototype.
TX1 did not produce sync-prototype status in the three-board capture, and no
common-instrument edge-difference measurement was completed. Do not infer
simultaneous IIM ADC sampling or microsecond accuracy from UART arrival times.
See `docs/verification.md` for the recorded result and limitations.
