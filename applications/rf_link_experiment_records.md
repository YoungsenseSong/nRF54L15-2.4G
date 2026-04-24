# RF Link Experiment Records

## How To Record Future Experiments

For each run, record:

- Date and operator.
- TX/RX firmware tag.
- Distance and antenna orientation.
- PHY/channel/ACK setting.
- Frame format and sample rate.
- TX UART log or CSV path.
- RX UART log or CSV path.
- Average payload bps, lost delta, bad delta, TX rf_fail delta, TX q_drop delta.
- Notes about power measurement if available.

## Experiment 1 - 50 kbps First Stable Target

- Date: 2026-04-16
- Firmware: pre-tag working state, later captured by rf_link application commits.
- Target: about 50 kbps effective payload.
- Parameters: 32 x 16-bit samples per frame, 10 ms frame period.
- Expected payload: 51.2 kbps.
- Result: Link reached the intended 50 kbps-class goal when RF conditions were acceptable.
- Data path: LP fake samples -> IPC -> HP ESB PTX -> RX ESB PRX -> UART stats.

## Experiment 2 - 50 ksps x 16-bit Stress Target

- Date: 2026-04-19
- Target: 50 ksps x 16 bit = 800 kbps effective payload.
- Parameters: 32 x 16-bit samples per frame, 640 us frame period, ESB 1 Mbps, ACK enabled.
- RX observed payload: about 286 to 306 kbps in the captured snippet.
- TX observed MAC latency: min about 1087 us, average about 1574 us, max about 11671 us.
- Result: Current 1 Mbps ESB + ACK configuration does not meet 800 kbps effective payload.

RX snippet:

```text
RX stat frames=101220 samples=3239040 bps=291328 lost=150976 dup=0 bad=0 seq=60155 first=3936 last=3967
RX stat frames=108754 samples=3480128 bps=293888 lost=162076 dup=0 bad=0 seq=13253 first=2208 last=2239
RX stat frames=112218 samples=3590976 bps=288768 lost=167209 dup=0 bad=0 seq=21850 first=2880 last=2911
```

TX snippet:

```text
TX stat sent=117600 ipc_rx=297485 queued=297485 q_drop=179749 rf_ok=117600 rf_fail=120 rf_timeout=0 attempts=1 mac_cnt=117720 mac_last_us=1099 mac_min_us=1087 mac_avg_us=1574 mac_max_us=11671
TX stat sent=118400 ipc_rx=299549 queued=299549 q_drop=181013 rf_ok=118400 rf_fail=120 rf_timeout=0 attempts=1 mac_cnt=118520 mac_last_us=1089 mac_min_us=1087 mac_avg_us=1574 mac_max_us=11671
```

Interpretation:

- `bad=0` means received frames are well-formed.
- `dup=0` means duplicate detection is not the issue.
- `lost` rising means RX sequence gaps are large.
- `q_drop` rising means HP cannot send as fast as LP produces frames.
- Low `rf_fail` means link quality is not the primary blocker in this sample.
- MAC latency is longer than the requested 640 us frame period.

## Experiment 3 - v0.5 Throughput Optimization Build

- Date: 2026-04-23
- Firmware: `v0.5-rf-throughput-optimization`
- Target: 50 ksps x 16 bit = 800 kbps effective payload.
- Parameters: ESB 4 Mbps on nRF54L15 if supported, no-ACK streaming payloads,
  96 x 16-bit samples per frame, 1920 us frame period, 204-byte frame.
- Queue/runtime changes: HP TX queue depth 64, ESB FIFO size 16, UART status
  period 1000 ms, FLPR direct frame-to-IPC send path.
- Build result: TX dual-core sysbuild and RX single-core build both pass.
- Hardware result: pending UART/CSV capture.

Planned capture commands:

```powershell
python .\save_serial_csv.py --port COM7 --output experiments\20260423_v0.5_rx.csv
python .\save_serial_csv.py --port COM8 --output experiments\20260423_v0.5_tx.csv
```

Record the actual distance, orientation, RX average `bps`, RX `lost` delta, TX
`q_drop` delta, and TX MAC latency fields in
`rf_link_experiment_logs/optimization_attempts.md` after the run.

## Experiment 4 - v0.5 Bring-Up Debug Closure and Near-Target Throughput

- Date: 2026-04-24
- Firmware: `v0.5` working state after IPC, PBUF, diagnostics, and deadline-pacing fixes.
- Target: recover the 50 ksps x 16-bit payload path and close the remaining gap to 800 kbps.
- Initial failure symptoms:
  - TX first showed LP IPC startup but no useful forward progress, while RX stayed at `frames=0 samples=0 bps=0`.
  - After the first IPC fix, TX exposed `HP fatal reason=4` immediately after `ipc init begin`.
- Root causes and fixes:
  - `flpr_app/src/ipc_tx.c` accepted only return `0` as a successful `ipc_service_send()` result. This was fixed so returned byte count is also treated as success and busy/full conditions are retried.
  - The new `204`-byte frame exceeded the default `CONFIG_PBUF_RX_READ_BUF_SIZE=128`, which caused an ICMSG-side assert. Both TX cores now use `CONFIG_PBUF_RX_READ_BUF_SIZE=256`, and `proto.h` asserts that PBUF RX size is not smaller than the wire frame.
  - HP fatal UART output, LP shared trace memory, and extra TX/RX counters were added so the failure could be isolated to IPC or RF quickly.
  - LP pacing was changed from relative `k_sleep(1920 us)` to optional absolute-deadline pacing to remove period drift.
- Stable excerpt before deadline pacing:

```text
TX stat sent=51857 ipc_rx=51857 queued=51857 q_drop=0 ipc_bad_size=0 ipc_bad_magic=0 rf_ok=51857 rf_fail=0 rf_timeout=0 rf_err=0 attempts=1 mac_cnt=51857 mac_last_us=635 mac_min_us=620 mac_avg_us=631 mac_max_us=722 lp_stage=4 lp_boots=10 lp_fatal=4 lp_fatal_reason=0 lp_loop=51857 lp_seq=51856 lp_ok=51857 lp_busy=0 lp_fail=0 lp_ret=0
RX stat frames=63130 samples=6060480 bps=774144 lost=0 dup=614 bad=0 rf_evt=63130 rf_frames=63130 rf_read_err=0 seq=62515 first=800 last=895
```

- Interpretation of the stable excerpt:
  - `sent=ipc_rx=queued=rf_ok` means LP generation, IPC receive, HP queueing, and RF send were all in lockstep.
  - `q_drop=0`, `lost=0`, `bad=0`, and `rf_read_err=0` mean the remaining issue was no longer packet corruption or pipeline collapse.
  - `bps=774144` corresponds to about `504 fps`, which exposed the last timing gap caused by relative pacing.
- Final working-state result:
  - After LP absolute-deadline pacing was enabled, the operator reported the bench state as stable near the `800 kbps` payload target.
  - This closes the original "TX no send info, RX zero" failure chain and leaves the main remaining work as long-duration CSV evidence and real ADC integration.

## CSV Output Locations

Recommended naming:

```text
experiments/YYYYMMDD_tag_distance_rate_tx.csv
experiments/YYYYMMDD_tag_distance_rate_rx.csv
```

Current parser:

```powershell
python .\save_serial_csv.py --port COM7 --output experiments\20260419_v0.4_rx.csv
python .\save_serial_csv.py --port COM8 --output experiments\20260419_v0.4_tx.csv
```

No raw sample dump is required on UART for the current statistics workflow.

## Experiment 5 - v0.6 Optional RX Sample Stream Export

- Date: 2026-04-24
- Firmware: `v0.6-rx-sample-stream-export`
- Goal: export accepted RX frames as real sample data for host CSV capture and later downstream processing.
- RF parameters: unchanged from the current `v0.5` working state.
- RX export parameters:
  - `stream.conf` enables `CONFIG_RF_LINK_RX_SAMPLE_STREAM=y`
  - RX switches `uart30` to `2000000` baud
  - RX stops printing `RX stat ...` text and outputs fixed-size binary records
- Build result:
  - default `build_rf_link_rx` still passes locally
  - stream-enabled `build_rf_link_rx_stream_verify` also passes locally
- Host capture command:

```powershell
python .\dump_rx_frames.py --port COM7
python .\dump_rx_frames.py --port COM7 --max-frames 1000 --output "D:\nRF54L15\NCS-Project\nrf54l15-connectkit\2.4g_results\rx_frames_1000.csv"
```

- Output format:
  - one CSV row per accepted RF frame
  - metadata fields plus `sample_0` to `sample_95`
  - `stream_drop_total` is included so UART-side export pressure can be checked separately from RF loss
- Hardware result: pending bench verification.
