# nRF54L15 Private 2.4G Wireless Data Link

基于 nRF54L15 Connect Kit 的私有 2.4 GHz 无线数据链路。当前成熟路径由
IIM-42352 三轴 MEMS、双核 TX 和单核 RX 组成：FLPR 连续采集，累计 4096 个
`int16_t` 样本后通过共享内存 + ICMsg 通知 CPUAPP；CPUAPP 从 System ON idle
唤醒并沿用既有 Nordic ESB 链路发送。

This repository implements an IIM-42352-to-private-2.4-GHz data path on the
nRF54L15 Connect Kit. FLPR acquires MEMS data continuously, publishes full
4096-sample shared-memory batches through ICMsg, and wakes CPUAPP to transmit
the data through the existing Nordic ESB link.

The Makerdiary nRF54L15 Connect Kit repository remains the board-support base.

## Project scope

- IIM-42352 Packet 1 FIFO acquisition on FLPR through SPIM00.
- CPUAPP System ON idle while no full batch is available.
- Two cache-line-aligned 4096-sample shared SRAM slots.
- Bidirectional ICMsg ready/release ownership protocol with CRC32 validation.
- Existing 204-byte ESB frame, 4 Mbps preferred PHY, channel 40, and no-ACK
  streaming mode.
- Single-core RX statistics and optional accepted-frame binary export.
- Optional V2 software-preintegration RX pipeline for logical synchronization
  and a CRC-protected ZYNQ transport contract.
- Host tools for status capture, raw frame capture, and batch verification.

## Repository entry points

| Path | Purpose |
| --- | --- |
| `applications/rf_link_tx/` | Dual-core MEMS transmitter. |
| `applications/rf_link_tx/flpr_app/` | FLPR IIM-42352 acquisition image. |
| `applications/rf_link_rx/` | Single-core ESB receiver. |
| `applications/rf_link_README.md` | Build, flash, runtime, and capture guide. |
| `applications/rf_link_DESIGN.md` | Current architecture and ownership protocol. |
| `applications/rf_link_INTEGRATION_REPORT.md` | Merge audit, conflict report, and hardware test plan. |
| `applications/rf_link_ROADMAP.md` | V1 two-board acceptance test and V2-V6 upgrade roadmap. |
| `applications/rf_link_FUTURE_INTEGRATION.md` | V2 software preintegration, queue, transport, pin candidates, and acceptance plan. |
| `applications/rf_link_development_log.txt` | Historical development log. |
| `applications/rf_link_experiment_records.md` | Historical bench evidence. |
| `applications/rf_link_experiment_logs/` | Raw experiment excerpts and optimization matrix. |
| `dump_rx_frames.py` | RX binary UART stream to CSV. |
| `verify_mems_batches.py` | Verify 4096-sample batch boundaries and sequence. |
| `save_serial_csv.py` | TX/RX text status capture. |

## Current architecture

```text
IIM-42352, 4 kHz per axis
  -> FLPR SPIM00 + FIFO watermark
  -> shared SRAM ping-pong slots, 4096 int16 samples
  -> CRC32 + ICMsg ready descriptor
  -> VEVIF interrupt wakes CPUAPP from System ON idle
  -> 42 x 96-sample frames + 1 x 64-sample frame
  -> ESB PTX, channel 40, 4 Mbps preferred, no-ACK
  -> ESB PRX
  -> frame validation, loss statistics, optional binary UART export
```

4096 means 4096 scalar `int16_t` values in XYZ-interleaved order. At 4 kHz per
axis the aggregate rate is 12 ksps and the useful average payload is
192 kbit/s. A batch is acquired approximately every 341.3 ms.

## RF frame format

The main repository's fixed 204-byte wire protocol is retained:

```c
struct rf_frame {
    uint16_t magic;          /* 0xA55A */
    uint16_t seq;            /* wraps at 65535 */
    uint16_t sample_count;   /* 96, final batch frame is 64 */
    uint16_t flags;          /* MEMS / batch-start / batch-end */
    uint32_t timestamp_ms;
    int16_t samples[96];
} __packed;
```

TX and RX now include one common protocol header so the frame definition cannot
silently drift between applications.

## Current parameters

| Item | Current value |
| --- | --- |
| Sensor | IIM-42352 |
| Sensor mode | Low-noise, +/-16 g, 4 kHz/axis |
| SPI | Mode 3, 8 MHz |
| Batch | 4096 x `int16_t`, two shared slots |
| Aggregate rate | 12 ksps / 192 kbit/s useful payload |
| Radio | Nordic ESB PTX/PRX |
| PHY | 4 Mbps preferred, 2 Mbps compile-time fallback |
| Channel | 40 |
| ACK | No-ACK streaming |
| RF frame | 204 bytes, up to 96 samples |

## Build

Builds do not require attached hardware.

```powershell
cd D:\nRF54L15\NCS-Project
.\.venv\Scripts\Activate.ps1
cd nrf54l15-connectkit

west build -p always --sysbuild -d build_rf_link_tx_mems `
  -b nrf54l15_connectkit/nrf54l15/cpuapp applications\rf_link_tx

west build -p always -d build_rf_link_rx_mems `
  -b nrf54l15_connectkit/nrf54l15/cpuapp applications\rf_link_rx
```

Optional RX binary stream build:

```powershell
west build -p always -d build_rf_link_rx_stream `
  -b nrf54l15_connectkit/nrf54l15/cpuapp applications\rf_link_rx `
  -- "-DEXTRA_CONF_FILE=stream.conf"
```

V2 software-preintegration RX build, with software SYNC and diagnostic
auto-commit transport:

```powershell
west build -p always -d build_rf_link_rx_future `
  -b nrf54l15_connectkit/nrf54l15/cpuapp applications\rf_link_rx `
  -- "-DEXTRA_CONF_FILE=future.conf"
```

The V2 preview preserves the current 204-byte V1 air frame. It adds receiver-side
32-bit sequence extension, explicit missing ranges, a 64-record static queue,
logical synchronization epochs, and a 248-byte CRC-protected FPGA record. Real
SPIS, DRDY, and GPIOTE-DPPI-TIMER SYNC capture remain disabled until the
receiver adapter schematic is frozen.

The current V1 requires only one TX and one RX for hardware acceptance. Its
two-board test matrix and the staged V2-V6 plan are defined in
[`applications/rf_link_ROADMAP.md`](applications/rf_link_ROADMAP.md).

## Flash

TX requires both CPUAPP and FLPR images:

```powershell
pyocd list
pyocd load -u <TX_ID> -t nrf54l build_rf_link_tx_mems\rf_link_tx\zephyr\zephyr.hex
pyocd load -u <TX_ID> -t nrf54l build_rf_link_tx_mems\flpr_app\zephyr\zephyr.hex
pyocd load -u <RX_ID> -t nrf54l build_rf_link_rx_mems\rf_link_rx\zephyr\zephyr.hex
```

## Hardware validation

The TX and RX builds pass locally with NCS 3.1.0. Hardware was not connected
during the integration, so sensor wiring, WHO_AM_I, FIFO interrupt capture,
measured power, sustained RF loss, and long-duration double-buffer behavior
remain to be verified.

Use the acceptance procedure in
[`applications/rf_link_INTEGRATION_REPORT.md`](applications/rf_link_INTEGRATION_REPORT.md).
The short version is:

1. Confirm SPIM00 wiring and the `P0.02` INT1/board-LED conflict.
2. Flash both TX images and the RX image.
3. Confirm each TX batch adds 4096 `samples_ok` and 43 `frames_ok`.
4. Require zero shared-memory CRC/header errors and zero FIFO overflows.
5. Check RX long-term average near 12 ksps / 192 kbit/s.
6. Capture at least 220 RX frames and run:

```powershell
python .\verify_mems_batches.py .\2.4g_results\mems_batch_test.csv
```

## Historical milestones

The previous fake-sample RF stress path is preserved in history and experiment
records:

| Tag | Meaning |
| --- | --- |
| `v0.1-rf-link-apps` | Runnable TX/RX RF link applications. |
| `v0.2-rf-link-csv-capture` | UART statistics CSV capture. |
| `v0.3-rf-link-docs-devlog` | Architecture and experiment documentation. |
| `v0.4-50ksps-load-test` | 50 ksps load-test evidence. |
| `v0.5-rf-throughput-optimization` | 4 Mbps/no-ACK/96-sample optimization. |
| `v0.6-rx-sample-stream-export` | Optional accepted-frame binary export. |

The prior 50 ksps fake-ramp path demonstrated approximately 800256 bit/s in
the recorded working state. That is historical RF headroom evidence, not the
current IIM-42352 sample rate. See the development log and optimization matrix
for the exact conditions.

## Hardware base and license

- [Makerdiary nRF54L15 Connect Kit](https://github.com/makerdiary/nrf54l15-connectkit)
- [Makerdiary board documentation](https://wiki.makerdiary.com/nrf54l15-connectkit/)

This repository inherits the original license files where applicable. Review
`LICENSE` and `LICENSE-NORDIC` with the application source.
