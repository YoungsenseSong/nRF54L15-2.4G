# Design

## 1. Purpose

Demonstrate a common wireless round followed by a responder-local hardware
timer start. This is a synchronization building block, not a complete sensor
sampling synchronization implementation.

## 2. Architecture

```text
RX1 coordinator --ESB channel 40--> RX2/TX1 responder
                                       |
                         RADIO ADDRESS -> DPPI -> TIMER20
                                       |
                          TIMER compare -> GPIOTE P1.09
```

The coordinator and responder share one source file. Kconfig selects the role;
the build configuration selects the role overlay and target board.

## 3. Modules

| Module | Responsibility | Input | Output |
|---|---|---|---|
| ESB coordinator | Broadcast epoch and sequence | TIMER/software cadence | ESB packet |
| ESB responder | Validate received sync frames | RADIO RX | epoch/sequence status |
| DPPI/TIMER/GPIOTE | Schedule a local hardware event | RADIO ADDRESS | P1.09 pulse |
| UART capture script | Save status text | UART | log and JSON summary |

## 4. Decisions

- The prototype uses a separate 16-byte sync frame and channel 40/prefix 0x54.
- The production 204-byte sensor frame is unchanged.
- Host UART timestamps are not used for edge-accuracy claims.
- The GPIO output may conflict with the reviewed RX2-to-FPGA SYNC connection.

## 5. Timing and reset

The responder uses TIMER20 at a configured 1 MHz rate with a nominal 2000 us
compare delay. Fixed RADIO-to-TIMER/GPIOTE latency and cross-board clock
difference require a common oscilloscope or logic analyzer measurement.

## 6. Resource and power

The recorded build used approximately 62-64 KiB Flash and 12.4 KiB RAM. These
are build artifacts for this prototype, not runtime power measurements.

## 7. Open items

- [ ] Verify HFXO source and tolerance on both responder boards.
- [ ] Demonstrate the TX1 responder role.
- [ ] Measure both P1.09 edges using one instrument.
- [ ] Relate the timer event to IIM DRDY/FSYNC and sample numbering.
