# Specification

## 1. Objectives

| Metric | Target | Method | Current result | Status |
|---|---:|---|---|---|
| Coordinator period | 100 ms | UART/radio counters | Configured and built | ☑ |
| Responder channel | 40 | Config and board log | RX1/RX2 window observed | ☑ |
| Common edge difference | To be specified | Same-instrument capture | Not measured | ☐ |
| IIM sample-time association | Per sample/batch | DRDY/FSYNC correlation | Not measured | ☐ |

## 2. Packet

The prototype sync payload contains magic, version, epoch, sequence, and
configured pulse delay. It is separate from the production sensor payload.

## 3. Signals

| Signal | Direction | Width | Protocol/electrical level | Notes |
|---|---|---:|---|---|
| ESB | Bidirectional radio | - | ESB 2 Mbps, channel 40 | prefix 0x54 |
| P1.09 | Responder output | 1 | GPIO | Instrument test point; schematic check required |
| UART | Board to host | 1 | 115200 baud | Diagnostic status only |

## 4. Timing

Responder uses nominal 1 MHz TIMER20 and a 2000 us compare delay. The measured
cross-board delay and drift remain unspecified until instrumented.

## 5. Acceptance

1. Coordinator and responder build from this checked-in source.
2. Responder status reports the same epoch and increasing sequence.
3. Two responder outputs are captured by one instrument before any precision
   claim.
