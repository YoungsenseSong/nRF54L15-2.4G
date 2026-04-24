# nRF54L15 Private 2.4G RF Link Applications

This application set builds a point-to-point private 2.4 GHz data link on the
nRF54L15 Connect Kit.

## Applications

- `rf_link_tx`: transmitter, built as a dual-core sysbuild application.
- `rf_link_rx`: receiver, built as a single-core `cpuapp` application.
- `rf_link_rx/stream.conf`: optional RX sample-stream export configuration.
- `save_serial_csv.py`: PC-side serial parser that stores TX/RX status lines as
  CSV without requiring raw sample data on the UART.
- `dump_rx_frames.py`: PC-side binary RX frame capture tool for the optional
  v0.6 stream build.
- `export_fake_adc_csv.py`: host-side exporter for the current TX fake ADC
  source pattern.

## Current Architecture

Transmitter:

```text
cpuflpr LP core -> fake ADC sample frames -> ipc_service/icmsg -> cpuapp HP core
cpuapp HP core -> TX queue -> ESB PTX -> private 2.4 GHz radio
```

Receiver:

```text
ESB PRX -> frame validation -> sequence/loss statistics -> optional rx_sample_stream tap -> UART status output or binary frame stream
```

## Frame Format

The first-stage radio frame is fixed at 204 bytes:

```c
struct rf_frame {
    uint16_t magic;          /* 0xA55A */
    uint16_t seq;            /* packet sequence, wraps at 65535 */
    uint16_t sample_count;   /* fixed to 96 in this version */
    uint16_t flags;          /* test-data flag for now */
    uint32_t timestamp_ms;   /* LP-side uptime timestamp */
    int16_t samples[96];     /* 96 x 16-bit samples */
} __packed;
```

Raw sample arrays are intentionally not printed on the default statistics UART.
In the optional `v0.6` stream build, accepted frames are exported as binary
records instead of text so the serial port does not become the measurement
bottleneck.

## Current Link Parameters

- ESB mode: PTX/PRX
- PHY: 4 Mbps on nRF54L15 when supported, otherwise 2 Mbps fallback
- Channel: 40
- ACK: no-ACK streaming payloads
- Payload: 204-byte application frame
- Sample frame: 96 x 16-bit samples
- Current TX sample target: 50 ksps x 16 bit, generated as one frame every
  1920 us.
- UART status period: 1000 ms.

The default RX build still matches the `v0.5` throughput optimization firmware
plus the 2026-04-24 bring-up fixes. The measured `v0.4` baseline with 1 Mbps
ESB + ACK received about 286 to 306 kbps of effective payload with high
sequence loss. The current working state uses 4 Mbps preferred, no-ACK,
96-sample frames, 204-byte wire payloads, corrected ICMSG handling, and LP
absolute-deadline pacing. Bench operation is now reported stable near the
800 kbps payload target. `v0.6` adds an optional RX sample-stream export mode
without changing the default statistics-only behavior.

## Common Communication Parameters

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

From the repository root:

```powershell
cd D:\nRF54L15\NCS-Project
.\.venv\Scripts\Activate.ps1
cd nrf54l15-connectkit
west build -p always --sysbuild -d build_rf_link_tx -b nrf54l15_connectkit/nrf54l15/cpuapp applications\rf_link_tx
west build -p always -d build_rf_link_rx -b nrf54l15_connectkit/nrf54l15/cpuapp applications\rf_link_rx
```

Build the optional RX sample-stream variant:

```powershell
west build -p always -d build_rf_link_rx_stream -b nrf54l15_connectkit/nrf54l15/cpuapp applications\rf_link_rx -- "-DEXTRA_CONF_FILE=stream.conf"
```

## Flash

```powershell
west flash -d build_rf_link_tx --domain rf_link_tx
pyocd load -t nrf54l build_rf_link_tx\flpr_app\zephyr\zephyr.hex
west flash -d build_rf_link_rx
```

## Runtime Status Lines

TX prints HP-side pipeline and MAC latency statistics:

```text
TX stat sent=... ipc_rx=... queued=... q_drop=... ipc_bad_size=... ipc_bad_magic=... rf_ok=... rf_fail=... rf_timeout=... rf_err=... attempts=... mac_cnt=... mac_last_us=... mac_min_us=... mac_avg_us=... mac_max_us=... lp_stage=... lp_boots=... lp_fatal=... lp_fatal_reason=... lp_loop=... lp_seq=... lp_ok=... lp_busy=... lp_fail=... lp_ret=...
```

RX prints receiver-side application statistics:

```text
RX stat frames=... samples=... bps=... lost=... dup=... bad=... rf_evt=... rf_frames=... rf_read_err=... seq=... first=... last=...
```

TX numeric parameter meanings:

| UART name | Meaning |
| --- | --- |
| `TX stat sent` | HP-side frames successfully transmitted over ESB. |
| `TX stat ipc_rx` | Frames received by HP from LP through ICMSG. |
| `TX stat queued` | Frames accepted into the HP TX queue. |
| `TX stat q_drop` | Frames dropped in the queueing path before RF send. |
| `TX stat ipc_bad_size` | IPC payload length mismatch. |
| `TX stat ipc_bad_magic` | IPC payload failed frame validation. |
| `TX stat rf_ok` | RF success count. |
| `TX stat rf_fail` | RF failure count. |
| `TX stat rf_timeout` | TX wait timeout count. |
| `TX stat rf_err` | Local RF write/setup error count. |
| `TX stat attempts` | Latest ESB attempt count. |
| `TX stat mac_cnt` | Number of packets included in MAC latency statistics. |
| `TX stat mac_last_us` | Latest packet MAC latency in microseconds. |
| `TX stat mac_min_us` | Minimum MAC latency in microseconds. |
| `TX stat mac_avg_us` | Average MAC latency in microseconds. |
| `TX stat mac_max_us` | Maximum MAC latency in microseconds. |
| `TX stat lp_stage` | LP execution stage: reset, boot, IPC ready, IPC bound, run. |
| `TX stat lp_boots` | Historical LP boot counter. |
| `TX stat lp_fatal` | Historical LP fatal counter. |
| `TX stat lp_fatal_reason` | Last recorded LP fatal reason. |
| `TX stat lp_loop` | LP loop count. |
| `TX stat lp_seq` | Latest LP-generated sequence. |
| `TX stat lp_ok` | LP IPC send success count. |
| `TX stat lp_busy` | LP IPC retry count due to busy/full conditions. |
| `TX stat lp_fail` | LP IPC failure count after retries. |
| `TX stat lp_ret` | Last LP IPC return value. |

RX numeric parameter meanings:

| UART name | Meaning |
| --- | --- |
| `RX stat frames` | Accepted application frames. |
| `RX stat samples` | Accepted samples. |
| `RX stat bps` | Effective payload bitrate from accepted sample bytes. |
| `RX stat lost` | Sequence-gap based lost-frame count. |
| `RX stat dup` | Duplicate or old-sequence frame count. |
| `RX stat bad` | Invalid frame count. |
| `RX stat rf_evt` | ESB RX event count. |
| `RX stat rf_frames` | Payloads read from the ESB RX FIFO. |
| `RX stat rf_read_err` | Read-side error count. |
| `RX stat seq` | Latest accepted frame sequence. |
| `RX stat first` | First sample value of the latest accepted frame. |
| `RX stat last` | Last sample value of the latest accepted frame. |

If LP absolute-deadline pacing should be disabled for comparison, change
`RF_LINK_LP_USE_ABSOLUTE_PACING` in `applications/rf_link_tx/src/proto.h`
from `1u` to `0u` and rebuild `build_rf_link_tx`.

Raw sample arrays are intentionally not printed. This keeps UART output focused
on application statistics and avoids consuming UART bandwidth with measurement
payloads.

## CSV Capture

```powershell
python .\save_serial_csv.py --list-ports
python .\save_serial_csv.py --port COM7 --output rx_stats.csv
python .\save_serial_csv.py --port COM11 --output tx_stats.csv
```

Use two terminals if TX and RX should be captured at the same time.

`save_serial_csv.py` saves UART application statistics only. It does not save
per-sample payload arrays because the current firmware does not print raw
samples on UART.

If you need the current TX-side fake source pattern without changing firmware,
use:

```powershell
python .\export_fake_adc_csv.py --frames 1000 --output-dir "D:\nRF54L15\NCS-Project\nrf54l15-connectkit\2.4g_results"
```

That command exports the deterministic fake-ADC pattern from
`rf_link_tx/flpr_app/src/adc_sampler.c`, not actual RX-captured samples.

For real received samples, build RX with `stream.conf` into the dedicated
`build_rf_link_rx_stream` directory. In this mode RX no longer prints
`RX stat ...` text. It outputs accepted frames as binary records on `COM7` at
`2000000` baud. The default `build_rf_link_rx` directory remains the normal
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

Capture real received frames to CSV:

```powershell
python .\dump_rx_frames.py --port COM7
python .\dump_rx_frames.py --port COM7 --max-frames 1000 --output "D:\nRF54L15\NCS-Project\nrf54l15-connectkit\2.4g_results\rx_frames_1000.csv"
```

`dump_rx_frames.py` writes one CSV row per accepted RF frame with metadata plus
`sample_0` to `sample_95`. The default output directory is
`D:\nRF54L15\NCS-Project\nrf54l15-connectkit\2.4g_results`.
