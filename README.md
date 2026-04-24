# nRF54L15 Private 2.4G Wireless Data Link

This repository is a private 2.4 GHz wireless data-link project based on the
nRF54L15 Connect Kit. The current implementation focuses on a point-to-point
16-bit sample transport path using Nordic ESB, with a dual-core transmitter and
a single-core receiver.

The original Makerdiary nRF54L15 Connect Kit repository is used as the hardware
and board-support base. This repository entrypoint documents the RF link
application, experiment records, and current performance findings.

## Project Scope

- Build a private 2.4 GHz wireless link on nRF54L15.
- Use `cpuapp` as the high-performance TX control core.
- Use `cpuflpr` as the lightweight TX sampling/data-generation core.
- Exchange TX-side data between LP and HP cores through `ipc_service` + `icmsg`.
- Send framed 16-bit sample data over ESB.
- Receive data on a simple single-core RX application.
- Export UART statistics and CSV logs for later paper/patent evidence.

## Repository Entry Points

| Path | Purpose |
| --- | --- |
| `applications/rf_link_tx/` | Dual-core transmitter application. |
| `applications/rf_link_tx/flpr_app/` | LP core child image for fake sampling and IPC TX. |
| `applications/rf_link_rx/` | Single-core receiver application. |
| `applications/rf_link_README.md` | Application-level README with build, flash, and UART use. |
| `applications/rf_link_DESIGN.md` | Architecture and design notes. |
| `applications/rf_link_development_log.txt` | Short devlog for each development step. |
| `applications/rf_link_experiment_records.md` | Experiment notes and captured UART snippets. |
| `applications/rf_link_experiment_logs/` | Per-experiment raw excerpts and optimization table. |
| `applications/rf_link_parameter_matrix.csv` | Version/parameter comparison table. |
| `applications/rf_link_tag_notes/` | Five-line notes for each project tag. |
| `save_serial_csv.py` | PC-side UART statistics to CSV capture tool. |

## Current Architecture

```text
TX LP core cpuflpr
  fake ADC/sample generator
  frame buffer
  IPC sender

        |
        v

TX HP core cpuapp
  IPC receiver
  TX queue
  ESB PTX radio sender
  MAC latency statistics
  UART status output

        |
        v

RX cpuapp
  ESB PRX radio receiver
  frame validation
  sequence/loss statistics
  UART status output
```

## Current Frame Format

The current RF payload is a fixed 204-byte application frame:

```c
struct rf_frame {
    uint16_t magic;          /* 0xA55A */
    uint16_t seq;            /* wraps at 65535 */
    uint16_t sample_count;   /* 96 */
    uint16_t flags;
    uint32_t timestamp_ms;
    int16_t samples[96];
} __packed;
```

Raw sample arrays are intentionally not printed on UART. UART output is limited
to application statistics and latency fields so that the serial port does not
become the measurement bottleneck.

## Current Radio Parameters

| Item | Current value |
| --- | --- |
| Radio backend | Nordic ESB |
| TX mode | PTX |
| RX mode | PRX |
| PHY | 4 Mbps on nRF54L15 when supported, otherwise 2 Mbps fallback |
| Channel | 40 |
| ACK | No-ACK streaming payloads |
| Application frame | 204 bytes |
| Samples per frame | 96 x 16-bit |
| TX period | 1920 us |
| UART status period | 1000 ms |
| Current stress target | 50 ksps x 16-bit = 800 kbps payload |

## Current Performance Status

Two key operating points have been recorded:

| Version | Target | Result |
| --- | --- | --- |
| `v0.3-rf-link-docs-devlog` baseline notes | 16-bit 50 kbps-class link | 32 samples every 10 ms, about 51.2 kbps payload target. |
| `v0.4-50ksps-load-test` | 50 ksps x 16-bit, 800 kbps payload | RX observed about 286 to 306 kbps with high sequence loss and TX queue drops. |
| `v0.5-rf-throughput-optimization` | Increase RF headroom for 50 ksps | 4 Mbps/no-ACK/96-sample frame firmware builds completed. After IPC/PBUF fixes the bench reached about 774 kbps with zero-drop excerpts, and after LP absolute-deadline pacing the current working state is reported stable near the 800 kbps payload target. |

The latest measured 50 ksps test (`v0.4`) shows the old
`1 Mbps ESB + ACK + 76-byte frame` configuration is throughput-limited. The
evidence is recorded in:

- `applications/rf_link_experiment_logs/20260419_50ksps_uart_excerpt.txt`
- `applications/rf_link_experiment_logs/optimization_attempts.md`
- `applications/rf_link_experiment_records.md`

## Throughput Optimization Summary

The 50 ksps x 16-bit bring-up was solved by a bundle of changes rather than one
parameter tweak:

- Raise RF headroom: move from `1 Mbps + ACK` to `4 Mbps preferred + no-ACK`
  ESB streaming.
- Reduce packet rate: increase each application frame from `32` to `96`
  samples, which lowers the required packet rate from `1562.5 pps` to
  `520.8 pps` for the same `800 kbps` payload target.
- Reduce software backpressure: increase HP TX queue depth and ESB FIFO sizing,
  remove the inactive LP software queue from the hot path, and send frames
  directly from LP generation to IPC.
- Fix IPC semantics: `applications/rf_link_tx/flpr_app/src/ipc_tx.c` now
  treats both return `0` and returned byte count as successful
  `ipc_service_send()` results, and retries `-ENOMEM`, `-EAGAIN`, `-EBUSY`,
  and `-ENOBUFS`.
- Match ICMSG buffering to the 204-byte wire frame: both TX cores set
  `CONFIG_PBUF_RX_READ_BUF_SIZE=256`, and `proto.h` asserts that the PBUF RX
  buffer is not smaller than the wire frame.
- Remove pacing drift: LP pacing can use absolute-deadline timing through
  `RF_LINK_LP_USE_ABSOLUTE_PACING`, so per-frame processing time does not
  accumulate into the next frame period.

## Zero-Throughput Debug Closure

The hardest bring-up issue after the `v0.5` parameter change was a false
"link dead" state:

- TX first showed LP IPC bound, but `ipc_rx=0` and RX stayed at
  `frames=0 samples=0 bps=0`.
- After the first IPC-side fix, TX exposed `HP fatal reason=4` during
  `ipc_bridge_init()`.

The debug and fix chain was:

- Add HP fatal UART output, LP shared trace memory, and extra TX/RX status
  fields (`lp_*`, `ipc_bad_*`, `rf_evt/rf_frames/rf_read_err`) so the failure
  could be isolated to LP generation, HP IPC receive, or RF delivery.
- Fix `applications/rf_link_tx/flpr_app/src/ipc_tx.c`, where LP previously
  treated only return `0` as success. On Zephyr ICMSG, success may return the
  sent byte count, so frames were being generated but not handled correctly.
- Raise `CONFIG_PBUF_RX_READ_BUF_SIZE` from the default `128` to `256` on both
  TX cores. The new 204-byte frame was tripping an ICMSG-side assert and
  causing `K_ERR_KERNEL_OOPS` (`HP fatal reason=4`) as soon as endpoint
  traffic started.
- After IPC became stable, close the remaining throughput gap by switching LP
  pacing from relative `k_sleep(1920 us)` to absolute-deadline pacing.

## Build

From the NCS project root:

```powershell
cd D:\nRF54L15\NCS-Project
.\.venv\Scripts\Activate.ps1
cd nrf54l15-connectkit
```

Build TX dual-core application:

```powershell
west build -p always --sysbuild -d build_rf_link_tx -b nrf54l15_connectkit/nrf54l15/cpuapp applications\rf_link_tx
```

Build RX single-core application:

```powershell
west build -p always -d build_rf_link_rx -b nrf54l15_connectkit/nrf54l15/cpuapp applications\rf_link_rx
```

## Flash

Flash TX HP core:

```powershell
west flash -d build_rf_link_tx --domain rf_link_tx
```

Flash TX LP core:

```powershell
pyocd load -t nrf54l build_rf_link_tx\flpr_app\zephyr\zephyr.hex
```

Flash RX:

```powershell
west flash -d build_rf_link_rx
```

## UART Statistics

TX prints:

```text
TX stat sent=... ipc_rx=... queued=... q_drop=... ipc_bad_size=... ipc_bad_magic=... rf_ok=... rf_fail=... rf_timeout=... rf_err=... attempts=... mac_cnt=... mac_last_us=... mac_min_us=... mac_avg_us=... mac_max_us=... lp_stage=... lp_boots=... lp_fatal=... lp_fatal_reason=... lp_loop=... lp_seq=... lp_ok=... lp_busy=... lp_fail=... lp_ret=...
```

RX prints:

```text
RX stat frames=... samples=... bps=... lost=... dup=... bad=... rf_evt=... rf_frames=... rf_read_err=... seq=... first=... last=...
```

TX field meanings:

| Field | Meaning |
| --- | --- |
| `sent` | HP side successfully transmitted frames over ESB. |
| `ipc_rx` | HP side frames received from LP through IPC. |
| `queued` | IPC frames accepted into the HP TX queue. |
| `q_drop` | Frames dropped before RF send because the queue path could not keep up. |
| `ipc_bad_size` | IPC payload size was not the expected `rf_frame` size. |
| `ipc_bad_magic` | IPC payload failed frame magic or `sample_count` validation. |
| `rf_ok` | ESB reported TX success. |
| `rf_fail` | ESB reported TX failure. |
| `rf_timeout` | HP timed out waiting for TX completion. |
| `rf_err` | Local TX-side write/setup error before a packet completed. |
| `attempts` | Attempt count reported for the latest ESB packet. |
| `mac_cnt` | Number of packets included in MAC latency statistics. |
| `mac_last_us` | Latest packet MAC latency in microseconds. |
| `mac_min_us`, `mac_avg_us`, `mac_max_us` | Minimum, average, and maximum MAC latency. |
| `lp_stage` | LP state: `0 reset`, `1 boot`, `2 IPC ready`, `3 IPC bound`, `4 run`. |
| `lp_boots` | Historical LP boot counter stored in shared trace memory. |
| `lp_fatal` | Historical LP fatal counter stored in shared trace memory. |
| `lp_fatal_reason` | Last recorded LP fatal reason. |
| `lp_loop` | LP main loop iterations. |
| `lp_seq` | Latest LP-generated frame sequence. |
| `lp_ok` | LP frames successfully sent into IPC. |
| `lp_busy` | LP IPC send retries due to busy/full conditions. |
| `lp_fail` | LP IPC send failures after retries. |
| `lp_ret` | Last LP IPC send return value. |

RX field meanings:

| Field | Meaning |
| --- | --- |
| `frames` | Frames accepted by RX application logic after basic validation. |
| `samples` | Total accepted samples. |
| `bps` | Effective payload bit rate calculated from accepted sample bytes, not air-interface bitrate. |
| `lost` | Sequence-gap based lost-frame counter. |
| `dup` | Duplicate or old-sequence frame counter. |
| `bad` | Invalid frame counter (`bad_magic + bad_size`). |
| `rf_evt` | Number of ESB RX events seen by the radio callback. |
| `rf_frames` | Number of payloads read from the ESB RX FIFO. |
| `rf_read_err` | Unexpected RX event/read-side error count. |
| `seq` | Latest accepted frame sequence. |
| `first` | First sample value of the latest accepted frame. |
| `last` | Last sample value of the latest accepted frame. |

These fields are intended for throughput and stability measurement. Raw sample
arrays are intentionally not printed on UART so the serial port does not become
the measurement bottleneck.

Use the CSV capture tool:

```powershell
python .\save_serial_csv.py --list-ports
python .\save_serial_csv.py --port COM7 --output rx_stats.csv
python .\save_serial_csv.py --port COM8 --output tx_stats.csv
```

## Milestone Tags

| Tag | Meaning |
| --- | --- |
| `v0.1-rf-link-apps` | Runnable TX/RX RF link applications. |
| `v0.2-rf-link-csv-capture` | UART status CSV capture tool. |
| `v0.3-rf-link-docs-devlog` | README, design notes, devlog, experiment records. |
| `v0.4-50ksps-load-test` | 50 ksps x 16-bit stress test evidence. |
| `v0.5-rf-throughput-optimization` | 4 Mbps/no-ACK/96-sample throughput optimization build. |

Each tag has a short note under `applications/rf_link_tag_notes/`.

## Next Optimization Experiments

The next work should be recorded as continuous experiments. `v0.5` implements
the first combined optimization build; the remaining work is measurement and
controlled comparison:

1. Measure `v0.5` at fixed distance and record RX bps/lost plus TX q_drop/MAC latency.
2. Run a controlled 2 Mbps comparison if 4 Mbps is unstable in range tests.
3. Evaluate ACK-on or batch-ACK control frames after no-ACK payload capacity is known.
4. Add GPIO timing probes for hardware latency measurement.
5. Replace fake samples with ADC DMA after RF throughput has enough margin.

The tracking table is:

```text
applications/rf_link_experiment_logs/optimization_attempts.md
```

## Hardware Base

This project is based on Makerdiary's nRF54L15 Connect Kit board support and
development environment. For board hardware documentation, see:

- https://github.com/makerdiary/nrf54l15-connectkit
- https://wiki.makerdiary.com/nrf54l15-connectkit/

## License

This repository inherits the original project license files where applicable.
New project documentation and application code should be reviewed together with
the existing `LICENSE` and `LICENSE-NORDIC` files.
