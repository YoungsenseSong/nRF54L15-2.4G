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
| `v0.5-rf-throughput-optimization` | ESB 4 Mbps if supported, no-ACK payloads, 96 samples/frame, 1920 us frame period | 50 ksps x 16-bit = 800 kbps payload | Bench reached about 774 kbps before deadline pacing and near 800 kbps after deadline pacing; stable excerpts show zero loss/drop in the active path | Combined optimization plus bring-up closure meets the target closely enough for the current fake-sample stage. |

## Continuous Optimization Table

| ID | Date | Firmware/tag | Parameter change | Goal | Actual bps | RX lost delta | RX bad delta | TX q_drop delta | TX rf_fail delta | MAC latency us | Better than v0.4? | Evidence |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| EXP-000 | 2026-04-19 | `v0.4-50ksps-load-test` | 32 samples/frame, 640 us period, ESB 1 Mbps, ACK on | Establish 50 ksps stress baseline | 286k-306k | High, rising rapidly | 0 in excerpt | High, rising rapidly | Low, about 120 cumulative in excerpt | min 1087, avg 1574, max 11671 | Baseline | `20260419_50ksps_uart_excerpt.txt` |
| EXP-001 | 2026-04-24 | `v0.5-rf-throughput-optimization` working state | 4 Mbps if supported, no-ACK payloads, 96 samples/frame, 1920 us period, 204-byte frame, HP queue 64, ESB FIFO 16, direct LP->IPC send, corrected ICMSG send semantics, PBUF RX 256, LP absolute-deadline pacing | Reach 800 kbps payload by increasing PHY headroom and reducing packet rate | About 774k before deadline pacing, near 800k after deadline pacing | 0 in stable excerpt | 0 in stable excerpt | 0 in stable excerpt | 0 in stable excerpt | stable excerpt about 620/631/722 | Yes | `rf_link_development_log.txt`, `rf_link_experiment_records.md` |
| EXP-002 | TBD | TBD | Force 2 Mbps no-ACK with 96 samples/frame | Compare range/loss against 4 Mbps no-ACK | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| EXP-003 | TBD | TBD | Re-enable ACK or add low-rate batch/control ACK while keeping 96 samples/frame | Check whether reliability improves without returning to high MAC latency | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| EXP-004 | TBD | TBD | Tune samples per frame around 64/96/120 if payload limit and range allow it | Find packet-size tradeoff between airtime efficiency and loss burst size | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| EXP-005 | TBD | TBD | Add GPIO timing probe around TX send and RX payload read | Measure hardware-level timing independent of UART | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |
| EXP-006 | TBD | TBD | Replace LP fake sampler with ADC DMA block source | Validate real acquisition source after RF margin exists | TBD | TBD | TBD | TBD | TBD | TBD | TBD | TBD |

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
ACK enabled cannot drain 76-byte frames every 640 us.

`EXP-001` confirms that the 50 ksps target required both RF-parameter changes
and code-level bring-up closure. The final working state was not only `4 Mbps +
no-ACK + larger frames`; it also depended on correct ICMSG send semantics,
PBUF sizing for the 204-byte frame, and LP absolute-deadline pacing. Later
rows should still isolate one variable at a time and compare against both
EXP-000 and the fixed EXP-001 working state.

Support-tooling note: `v0.6-rx-sample-stream-export` does not change the RF
parameter set in `EXP-001`. It adds an optional RX-side accepted-frame export
path so future ADC integration and downstream processing can be validated with
real received samples instead of statistics only.
