# nRF54L15 Private 2.4G RF Link Applications

This application set builds a point-to-point private 2.4 GHz data link on the
nRF54L15 Connect Kit.

## Applications

- `rf_link_tx`: transmitter, built as a dual-core sysbuild application.
- `rf_link_rx`: receiver, built as a single-core `cpuapp` application.
- `save_serial_csv.py`: PC-side serial parser that stores TX/RX status lines as
  CSV without requiring raw sample data on the UART.

## Current Architecture

Transmitter:

```text
cpuflpr LP core -> fake ADC sample frames -> ipc_service/icmsg -> cpuapp HP core
cpuapp HP core -> TX queue -> ESB PTX -> private 2.4 GHz radio
```

Receiver:

```text
ESB PRX -> frame validation -> sequence/loss statistics -> UART status output
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

The current build is the `v0.5` throughput optimization firmware plus the
2026-04-24 bring-up fixes. The measured `v0.4` baseline with 1 Mbps ESB + ACK
received about 286 to 306 kbps of effective payload with high sequence loss.
The current working state uses 4 Mbps preferred, no-ACK, 96-sample frames,
204-byte wire payloads, corrected ICMSG handling, and LP absolute-deadline
pacing. Bench operation is now reported stable near the 800 kbps payload
target.

## Throughput Closure Summary

The final 50 ksps x 16-bit bring-up depended on both parameter tuning and
code-level fixes:

- RF headroom increase: 4 Mbps preferred, no-ACK streaming payloads.
- Packet-rate reduction: 96 samples per frame instead of 32.
- Queue-path cleanup: LP sends directly to IPC, HP queue depth increased.
- IPC fix: `flpr_app/src/ipc_tx.c` accepts returned byte count as success and
  retries `-EBUSY` and `-ENOBUFS`.
- ICMSG fix: both TX cores set `CONFIG_PBUF_RX_READ_BUF_SIZE=256` to match the
  204-byte frame.
- Pacing fix: LP can use absolute-deadline timing through
  `RF_LINK_LP_USE_ABSOLUTE_PACING`.

The main "TX silent / RX zero" debug path was:

- TX first showed LP IPC bound but `ipc_rx=0`, while RX stayed at zero.
- Added HP fatal UART, LP shared trace memory, and extra TX/RX counters.
- Fixed LP IPC send success handling.
- Resolved `HP fatal reason=4` by increasing ICMSG PBUF RX buffer size for the
  new 204-byte frame.
- Closed the remaining throughput gap with absolute-deadline pacing.

## Build

From the repository root:

```powershell
cd D:\nRF54L15\NCS-Project
.\.venv\Scripts\Activate.ps1
cd nrf54l15-connectkit
west build -p always --sysbuild -d build_rf_link_tx -b nrf54l15_connectkit/nrf54l15/cpuapp applications\rf_link_tx
west build -p always -d build_rf_link_rx -b nrf54l15_connectkit/nrf54l15/cpuapp applications\rf_link_rx
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

TX fields:

| Field | Meaning |
| --- | --- |
| `sent` | HP-side frames successfully transmitted over ESB. |
| `ipc_rx` | Frames received by HP from LP through ICMSG. |
| `queued` | Frames accepted into the HP TX queue. |
| `q_drop` | Frames dropped in the queueing path before RF send. |
| `ipc_bad_size` | IPC payload length mismatch. |
| `ipc_bad_magic` | IPC payload failed frame validation. |
| `rf_ok`, `rf_fail`, `rf_timeout`, `rf_err` | RF success, RF failure, TX wait timeout, and local RF write/setup errors. |
| `attempts` | Latest ESB attempt count. |
| `mac_cnt`, `mac_last_us`, `mac_min_us`, `mac_avg_us`, `mac_max_us` | MAC latency statistics. |
| `lp_stage` | LP execution stage: reset, boot, IPC ready, IPC bound, run. |
| `lp_boots`, `lp_fatal`, `lp_fatal_reason` | Historical LP boot/fatal counters and last fatal reason. |
| `lp_loop`, `lp_seq` | LP loop count and latest generated sequence. |
| `lp_ok`, `lp_busy`, `lp_fail`, `lp_ret` | LP IPC send success, retry, failure, and last return code. |

RX fields:

| Field | Meaning |
| --- | --- |
| `frames`, `samples` | Accepted application frames and accepted samples. |
| `bps` | Effective payload bitrate computed from accepted sample bytes. |
| `lost` | Sequence-gap based lost-frame count. |
| `dup` | Duplicate or old-sequence frame count. |
| `bad` | Invalid frame count. |
| `rf_evt`, `rf_frames`, `rf_read_err` | RF callback events, payloads read from RX FIFO, and read-side errors. |
| `seq`, `first`, `last` | Latest accepted frame sequence and boundary sample values. |

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
python .\save_serial_csv.py --port COM8 --output tx_stats.csv
```

Use two terminals if TX and RX should be captured at the same time.
