# Verification

## 1. Environment

- Date: 2026-10-18
- Platform: nRF54L15 Connect Kit RX1/RX2; custom TX1 attempt
- Stack: Nordic Connect SDK / Zephyr / ESB

## 2. Build

| Role | Board | Result | Notes |
|---|---|---|---|
| Coordinator | Connect Kit | Pass | 2026-10-19 clean build; Flash 63,976 B, RAM 12,448 B |
| Responder | Connect Kit | Pass | 2026-10-19 clean build; Flash 62,868 B, RAM 12,424 B |
| Responder | TX1 board | Pass | Image built; no status line observed after programming |

## 3. Board observations

| Date | Test | Observation | Conclusion |
|---|---|---|---|
| 2026-10-18 | RX1 to RX2, 10 s | Coordinator tx_ok=940/tx_fail=0; RX2 frames_ok=927/frames_bad=0; last_seq=940 | Protocol/common-round prototype observed |
| 2026-10-18 | TX1 responder capture | COM4 did not report sync-prototype status; production firmware later restored | TX1 participation not demonstrated |
| 2026-10-18 | P1.09 edge comparison | No common-instrument measurement | Precision remains unverified |

The responder UART reports `pulse_events=0` because TIMER compare is routed
directly through DPPI to GPIOTE; the software ISR counter is not proof of the
hardware pulse.

## 4. Limitations

- UART arrival times do not measure microsecond edge error.
- A common wireless round and local timer start do not prove simultaneous
  IIM ADC sampling.
- HFXO source and ppm must be verified before estimating drift.
