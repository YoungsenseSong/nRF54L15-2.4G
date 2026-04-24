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
- Export UART statistics and optional real RX frame CSV logs for later paper/patent evidence.

## Repository Entry Points

| Path | Purpose |
| --- | --- |
| `applications/rf_link_tx/` | Dual-core transmitter application. |
| `applications/rf_link_tx/flpr_app/` | LP core child image for fake sampling and IPC TX. |
| `applications/rf_link_rx/` | Single-core receiver application. |
| `applications/rf_link_rx/stream.conf` | Optional RX sample-stream capture configuration. |
| `applications/rf_link_README.md` | Application-level README with build, flash, and UART use. |
| `applications/rf_link_DESIGN.md` | Architecture and design notes. |
| `applications/rf_link_development_log.txt` | Short devlog for each development step. |
| `applications/rf_link_experiment_records.md` | Experiment notes and captured UART snippets. |
| `applications/rf_link_experiment_logs/` | Per-experiment raw excerpts and optimization table. |
| `applications/rf_link_parameter_matrix.csv` | Version/parameter comparison table. |
| `applications/rf_link_tag_notes/` | Five-line notes for each project tag. |
| `save_serial_csv.py` | PC-side UART statistics to CSV capture tool. |
| `dump_rx_frames.py` | PC-side binary RX frame capture tool for the optional v0.6 stream build. |
| `export_fake_adc_csv.py` | Host-side exporter for the current TX fake-ADC sample pattern. |

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
  optional rx_sample_stream tap
  UART status output or binary frame stream
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

Raw sample arrays are intentionally not printed on the default statistics UART.
In the optional `v0.6` stream build, accepted frames are exported as binary
records instead of text so the serial port does not become the measurement
bottleneck.

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
| `v0.5-rf-throughput-optimization` | Increase RF headroom for 50 ksps | 4 Mbps/no-ACK/96-sample frame firmware builds completed. The 2026-04-24 intermediate stable excerpt before deadline pacing reached `774144 bps`, and the current working state after deadline pacing reaches about `800256 bps` or about `50016 sps`. |
| `v0.6-rx-sample-stream-export` | Add real RX sample export without changing the default link path | Default RX build remains statistics-only. An optional `stream.conf` build adds accepted-frame binary export on `COM7` at `2000000` baud plus `dump_rx_frames.py` for CSV capture. Both builds pass locally; hardware stream capture is the next bench step. |

The latest measured 50 ksps test (`v0.4`) shows the old
`1 Mbps ESB + ACK + 76-byte frame` configuration is throughput-limited. The
full version-by-version optimization path, code-level fix details, and
zero-throughput debug closure are recorded in:

- `applications/rf_link_development_log.txt`
- `applications/rf_link_experiment_logs/optimization_attempts.md`
- `applications/rf_link_experiment_records.md`

## Common Communication Parameters

The project uses these common engineering quantities:

| Parameter | Definition | Formula | `v0.3` reference | `v0.4` target | `v0.5` target | `v0.5` current measured |
| --- | --- | --- | --- | --- | --- | --- |
| `sample_bits` | Bits per sample | fixed | 16 | 16 | 16 | 16 |
| `samples_per_frame` | Samples in one application frame | fixed by protocol | 32 | 32 | 96 | 96 |
| `frame_payload_bits` | Useful sample bits in one frame | `samples_per_frame * sample_bits` | 512 | 512 | 1536 | 1536 |
| `period_us` | Target frame period | fixed by firmware | 10000 us | 640 us | 1920 us | 1920 us |
| `pps` | Packets per second | `1 / period_s` or `payload_bps / frame_payload_bits` | 100 pps | 1562.5 pps | 520.8 pps | about 521 pps |
| `sample_rate` | Samples per second | `pps * samples_per_frame` | 3200 sps | 50000 sps | 50000 sps | about 50016 sps |
| `payload_bps` | Useful sample bitrate | `sample_rate * sample_bits` or `pps * frame_payload_bits` | 51200 bps | 800000 bps | 800000 bps | about 800256 bps |

Notes:

- The `774144 bps` value is a `2026-04-24` intermediate stable excerpt captured
  before LP absolute-deadline pacing. It corresponds to
  `774144 / 1536 = 504 pps` and `504 * 96 = 48384 sps`.
- The current reported working state after deadline pacing is
  `800256 bps`, which corresponds to `800256 / 1536 = 521 pps` and
  `521 * 96 = 50016 sps`.

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

Build the optional RX sample-stream variant in the same fixed build directory:

```powershell
west build -p always -d build_rf_link_rx_stream -b nrf54l15_connectkit/nrf54l15/cpuapp applications\rf_link_rx -- "-DEXTRA_CONF_FILE=stream.conf"
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

TX numeric parameter meanings:

| UART name | Meaning |
| --- | --- |
| `TX stat sent` | HP side successfully transmitted frames over ESB. |
| `TX stat ipc_rx` | HP side frames received from LP through IPC. |
| `TX stat queued` | IPC frames accepted into the HP TX queue. |
| `TX stat q_drop` | Frames dropped before RF send because the queue path could not keep up. |
| `TX stat ipc_bad_size` | IPC payload size was not the expected `rf_frame` size. |
| `TX stat ipc_bad_magic` | IPC payload failed frame validation. |
| `TX stat rf_ok` | ESB reported TX success. |
| `TX stat rf_fail` | ESB reported TX failure. |
| `TX stat rf_timeout` | HP timed out waiting for TX completion. |
| `TX stat rf_err` | Local TX-side write/setup error before a packet completed. |
| `TX stat attempts` | Attempt count reported for the latest ESB packet. |
| `TX stat mac_cnt` | Number of packets included in MAC latency statistics. |
| `TX stat mac_last_us` | Latest packet MAC latency in microseconds. |
| `TX stat mac_min_us` | Minimum recorded MAC latency in microseconds. |
| `TX stat mac_avg_us` | Average MAC latency in microseconds. |
| `TX stat mac_max_us` | Maximum recorded MAC latency in microseconds. |
| `TX stat lp_stage` | LP state: `0 reset`, `1 boot`, `2 IPC ready`, `3 IPC bound`, `4 run`. |
| `TX stat lp_boots` | Historical LP boot counter stored in shared trace memory. |
| `TX stat lp_fatal` | Historical LP fatal counter stored in shared trace memory. |
| `TX stat lp_fatal_reason` | Last recorded LP fatal reason. |
| `TX stat lp_loop` | LP main loop iterations. |
| `TX stat lp_seq` | Latest LP-generated frame sequence. |
| `TX stat lp_ok` | LP frames successfully sent into IPC. |
| `TX stat lp_busy` | LP IPC send retries due to busy/full conditions. |
| `TX stat lp_fail` | LP IPC send failures after retries. |
| `TX stat lp_ret` | Last LP IPC send return value. |

RX numeric parameter meanings:

| UART name | Meaning |
| --- | --- |
| `RX stat frames` | Frames accepted by RX application logic after basic validation. |
| `RX stat samples` | Total accepted samples. |
| `RX stat bps` | Effective payload bitrate calculated from accepted sample bytes, not air-interface bitrate. |
| `RX stat lost` | Sequence-gap based lost-frame counter. |
| `RX stat dup` | Duplicate or old-sequence frame counter. |
| `RX stat bad` | Invalid frame counter (`bad_magic + bad_size`). |
| `RX stat rf_evt` | Number of ESB RX events seen by the radio callback. |
| `RX stat rf_frames` | Number of payloads read from the ESB RX FIFO. |
| `RX stat rf_read_err` | Unexpected RX event/read-side error count. |
| `RX stat seq` | Latest accepted frame sequence. |
| `RX stat first` | First sample value of the latest accepted frame. |
| `RX stat last` | Last sample value of the latest accepted frame. |

These fields are intended for throughput and stability measurement. Raw sample
arrays are intentionally not printed on UART so the serial port does not become
the measurement bottleneck.

Use the CSV capture tool:

```powershell
python .\save_serial_csv.py --list-ports
python .\save_serial_csv.py --port COM7 --output rx_stats.csv
python .\save_serial_csv.py --port COM11 --output tx_stats.csv
```

`save_serial_csv.py` saves UART application statistics only. It does not save
per-sample payload arrays because the current firmware does not print raw
samples on UART.

If you need the current TX-side fake source pattern without changing firmware,
use:

```powershell
python .\export_fake_adc_csv.py --frames 1000 --output-dir "D:\nRF54L15\NCS-Project\nrf54l15-connectkit\2.4g_results"
```

That command exports the deterministic fake-ADC pattern from
`applications/rf_link_tx/flpr_app/src/adc_sampler.c`, not actual RX-captured
samples.

For real received samples, use the optional `v0.6` RX stream build in the
dedicated `build_rf_link_rx_stream` directory. In this mode RX stops printing
`RX stat ...` text and instead outputs accepted frames as binary records on
`COM7` at `2000000` baud. The default `build_rf_link_rx` directory remains the
statistics build.

If `pyserial` is not installed yet:

```powershell
python -m pip install pyserial
```

Build and flash the RX stream variant:

```powershell
west build -p always -d build_rf_link_rx_stream -b nrf54l15_connectkit/nrf54l15/cpuapp applications\rf_link_rx -- "-DEXTRA_CONF_FILE=stream.conf"
west flash -d build_rf_link_rx_stream
```

Then capture real received frames to CSV:

```powershell
python .\dump_rx_frames.py --port COM7
python .\dump_rx_frames.py --port COM7 --max-frames 1000 --output "D:\nRF54L15\NCS-Project\nrf54l15-connectkit\2.4g_results\rx_frames_1000.csv"
```

`dump_rx_frames.py` saves one CSV row per accepted RF frame with metadata plus
`sample_0` to `sample_95`. The default output directory is
`D:\nRF54L15\NCS-Project\nrf54l15-connectkit\2.4g_results`.

## Milestone Tags

| Tag | Meaning |
| --- | --- |
| `v0.1-rf-link-apps` | Runnable TX/RX RF link applications. |
| `v0.2-rf-link-csv-capture` | UART status CSV capture tool. |
| `v0.3-rf-link-docs-devlog` | README, design notes, devlog, experiment records. |
| `v0.4-50ksps-load-test` | 50 ksps x 16-bit stress test evidence. |
| `v0.5-rf-throughput-optimization` | 4 Mbps/no-ACK/96-sample throughput optimization build. |
| `v0.6-rx-sample-stream-export` | Optional accepted-frame binary export and host CSV dump tool. |

Each tag has a short note under `applications/rf_link_tag_notes/`.

## Next Optimization Experiments

The next work should be recorded as continuous experiments. `v0.5` implements
the first combined optimization build; the remaining work is measurement and
controlled comparison:

1. Measure `v0.5` at fixed distance and record RX bps/lost plus TX q_drop/MAC latency.
2. Validate `v0.6` on hardware and confirm `stream_drop_total` stays low during real CSV capture.
3. Run a controlled 2 Mbps comparison if 4 Mbps is unstable in range tests.
4. Evaluate ACK-on or batch-ACK control frames after no-ACK payload capacity is known.
5. Add GPIO timing probes for hardware latency measurement.
6. Replace fake samples with ADC DMA after RF throughput and v0.6 export path both remain stable.

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
