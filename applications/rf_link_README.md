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

The current build is the `v0.5` throughput optimization firmware. The measured
`v0.4` baseline with 1 Mbps ESB + ACK received about 286 to 306 kbps of
effective payload with high sequence loss. This version raises RF headroom and
reduces packet rate; hardware measurement is still required to confirm whether
it reaches the full 800 kbps payload target.

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
TX stat sent=... ipc_rx=... queued=... q_drop=... rf_ok=... rf_fail=... rf_timeout=... attempts=... mac_cnt=... mac_last_us=... mac_min_us=... mac_avg_us=... mac_max_us=...
```

RX prints receiver-side application statistics:

```text
RX stat frames=... samples=... bps=... lost=... dup=... bad=... seq=... first=... last=...
```

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
