# RF Link Optimization Attempt Table

This file records each throughput optimization attempt after the first runnable
private 2.4 GHz link. Keep every test as one row, even when the result is not
better than the previous version. This gives a clear research trail from
working link, to bottleneck identification, to validated optimization.

## Baseline Summary

| Baseline | Parameters | Target | Actual result | Main conclusion |
| --- | --- | --- | --- | --- |
| `v0.3-rf-link-docs-devlog` baseline notes | ESB 1 Mbps, ACK on, 32 samples/frame, 10 ms frame period | About 51.2 kbps payload | 50 kbps-class link was usable in short-range tests | Good first-pass functional baseline. |
| `v0.4-50ksps-load-test` | ESB 1 Mbps, ACK on, 32 samples/frame, 640 us frame period | 50 ksps x 16-bit = 800 kbps payload | RX about 286-306 kbps, high `lost`, high TX `q_drop` | Current MAC timing is slower than the production period. |

## Continuous Optimization Table

| ID | Date | Firmware/tag | Parameter change | Goal | Actual bps | RX lost delta | RX bad delta | TX q_drop delta | TX rf_fail delta | MAC latency us | Better than v0.4? | Evidence |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| EXP-000 | 2026-04-19 | `v0.4-50ksps-load-test` | 32 samples/frame, 640 us period, ESB 1 Mbps, ACK on | Establish 50 ksps stress baseline | 286k-306k | High, rising rapidly | 0 in excerpt | High, rising rapidly | Low, about 120 cumulative in excerpt | min 1087, avg 1574, max 11671 | Baseline | `20260419_50ksps_uart_excerpt.txt` |
| EXP-001 | TBD | TBD | Switch ESB PHY to 2 Mbps, keep ACK on and 32 samples/frame | Reduce MAC latency below 640 us or raise RX bps | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| EXP-002 | TBD | TBD | Switch ESB PHY to 4 Mbps if supported, keep ACK on | Test physical-layer headroom | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| EXP-003 | TBD | TBD | Keep 1 Mbps PHY but evaluate no-ACK or reduced ACK strategy | Reduce ACK turnaround overhead | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| EXP-004 | TBD | TBD | Increase samples per frame if ESB payload limit allows it | Reduce packets per second for 50 ksps payload | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| EXP-005 | TBD | TBD | Lower TX/RX UART status print frequency during high-rate tests | Remove UART status output as a runtime disturbance | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| EXP-006 | TBD | TBD | Add GPIO timing probe around TX send and RX payload read | Measure hardware-level one-way timing | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| EXP-007 | TBD | TBD | Replace LP fake sampler with ADC DMA block source | Validate real acquisition source after RF margin exists | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |

## Required Notes For Each New Experiment

For each future row, record:

- Exact code change or git commit hash.
- TX and RX firmware tags.
- Distance, antenna direction, and test environment.
- RX `bps`, `lost`, `bad`, `seq` trend.
- TX `q_drop`, `rf_fail`, `rf_timeout`, `attempts`.
- TX `mac_min_us`, `mac_avg_us`, `mac_max_us`.
- Whether the result improves over EXP-000.

## Current Interpretation

`EXP-000` proves that simply increasing the LP production rate to 50 ksps is not
enough. The TX queue drops frames because ESB transmission with 1 Mbps PHY and
ACK enabled cannot drain 76-byte frames every 640 us. The next useful experiment
should change one RF parameter at a time and compare it against this table.
