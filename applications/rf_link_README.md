# nRF54L15 Private 2.4G RF Link Applications

This application set implements an IIM-42352-to-private-2.4-GHz data path on
the nRF54L15 Connect Kit. The transmitter is a dual-core sysbuild application;
the receiver is a single-core CPUAPP application.

For the merge audit, conflict decisions, shared-memory map, and complete
hardware test procedure, see
[`rf_link_INTEGRATION_REPORT.md`](rf_link_INTEGRATION_REPORT.md).

## Applications and tools

- `rf_link_tx`: dual-core MEMS transmitter.
- `rf_link_tx/flpr_app`: FLPR IIM-42352 acquisition image.
- `rf_link_rx`: single-core ESB receiver and statistics image.
- `rf_link_rx/stream.conf`: optional binary accepted-frame UART export.
- `rf_link_rx/future.conf`: V2 software-preintegration receiver queue, logical synchronization,
  and FPGA transport abstraction without binding unknown hardware pins.
- `rf_link_rx/future_no_transport.conf`: V2-preview receive/sync regression
  build with the debug and SPIS transports disabled.
- `rf_link_FUTURE_INTEGRATION.md`: implemented boundaries, Connect Kit pin
  candidates, counters, and hardware acceptance plan.
- `rf_link_ROADMAP.md`: V1 two-board test matrix and V2-V6 development gates.
- `dump_rx_frames.py`: capture the RX binary stream to CSV.
- `verify_mems_batches.py`: verify sequence continuity and 4096-sample batch
  boundaries in a captured CSV.
- `save_serial_csv.py`: capture the low-rate TX/RX status lines.

## Current architecture

```text
IIM-42352 (4 kHz x 3 axes)
  -> FLPR SPIM00/FIFO acquisition
  -> two shared-SRAM slots, 4096 int16 samples per slot
  -> CRC32 + ICMsg batch-ready descriptor
  -> CPUAPP wakes from System ON idle
  -> 43 fixed-wire-size RF frames
  -> ESB PTX, channel 40, 4 Mbps preferred, no-ACK
  -> ESB PRX receiver
  -> validation, sequence/loss statistics, optional binary UART export
```

CPUAPP blocks on the batch message queue after startup. A VEVIF mailbox event
from ICMsg wakes it when FLPR publishes a full slot. This is System ON idle,
not System OFF. CPUAPP returns a batch-release descriptor after transmission so
FLPR cannot overwrite a slot still in use.

## MEMS batch and RF frame formats

One shared-memory batch contains exactly 4096 scalar `int16_t` values in XYZ
interleaved order:

```text
X0, Y0, Z0, X1, Y1, Z1, ...
```

At a 4 kHz per-axis ODR this is 12 ksps aggregate and 192 kbit/s of useful
sample payload. Each batch takes about 341.3 ms to acquire.

The on-air protocol retains the main repository's fixed 204-byte frame:

```c
struct rf_frame {
    uint16_t magic;          /* 0xA55A */
    uint16_t seq;            /* wraps at 65535 */
    uint16_t sample_count;   /* 96, or 64 for the final batch frame */
    uint16_t flags;          /* MEMS / batch-start / batch-end */
    uint32_t timestamp_ms;
    int16_t samples[96];
} __packed;
```

One batch becomes 42 frames of 96 samples plus one frame of 64 samples. The
final frame is still 204 bytes on air; unused sample fields are zero-filled.

## Current link parameters

- ESB mode: PTX/PRX.
- PHY: 4 Mbps on nRF54L15 when supported, 2 Mbps compile-time fallback.
- Channel: 40.
- CRC: 16 bit.
- Payload mode: no-ACK streaming.
- RF wire payload: 204 bytes.
- MEMS ODR: 4 kHz per axis, +/-16 g, low-noise mode.
- Aggregate sample rate: 12 ksps.
- Useful average sample bitrate: 192 kbit/s.
- Batch size: 4096 `int16_t` samples.
- UART status period: approximately 1 second while batches are processed.

The earlier 50 ksps fake-ramp firmware remains documented in the experiment
records and milestone tags. It is no longer the default TX acquisition source.

## Build

From the repository root:

```powershell
cd D:\nRF54L15\NCS-Project
.\.venv\Scripts\Activate.ps1
cd nrf54l15-connectkit

west build -p always --sysbuild -d build_rf_link_tx_mems `
  -b nrf54l15_connectkit/nrf54l15/cpuapp applications\rf_link_tx

west build -p always -d build_rf_link_rx_mems `
  -b nrf54l15_connectkit/nrf54l15/cpuapp applications\rf_link_rx
```

Build the optional RX binary stream:

```powershell
west build -p always -d build_rf_link_rx_stream `
  -b nrf54l15_connectkit/nrf54l15/cpuapp applications\rf_link_rx `
  -- "-DEXTRA_CONF_FILE=stream.conf"
```

Build the V2 software-preintegration RX without ZYNQ or a physical SYNC input:

```powershell
west build -p always -d build_rf_link_rx_future `
  -b nrf54l15_connectkit/nrf54l15/cpuapp applications\rf_link_rx `
  -- "-DEXTRA_CONF_FILE=future.conf"
```

The default and stream configurations keep their established behavior. The V2
preview moves validation out of the ESB IRQ, extends the 16-bit RF sequence to 32
bits, represents missing intervals explicitly, and passes accepted frames
through a 64-record static queue. Its software SYNC path aligns the first valid
`BATCH_START` after a capture to logical sample index zero. This is logical
alignment, not proof of simultaneous remote MEMS sampling.

The current Future backend validates the 248-byte FPGA record and auto-commits
it without dumping payload bytes to UART. The command-oriented SPIS engine is
present, but real SPIS/DRDY/SYNC capture is held off until a receiver adapter
schematic confirms the wiring. See `rf_link_FUTURE_INTEGRATION.md`.

These builds do not require attached hardware.

## Flash

TX requires both images:

```powershell
pyocd load -u <TX_ID> -t nrf54l build_rf_link_tx_mems\rf_link_tx\zephyr\zephyr.hex
pyocd load -u <TX_ID> -t nrf54l build_rf_link_tx_mems\flpr_app\zephyr\zephyr.hex
pyocd load -u <RX_ID> -t nrf54l build_rf_link_rx_mems\rf_link_rx\zephyr\zephyr.hex
```

Use `pyocd list` to obtain `<TX_ID>` and `<RX_ID>`.

## Runtime status

TX prints batch, IPC, RF, and LP acquisition health:

```text
TX stat batches_rx=... batches_ok=... batch_bad_hdr=... batches_fail=...
batch_bad_crc=... frames_try=... frames_ok=... frames_fail=...
samples_ok=... ipc_bad=... ipc_drop=... release_err=... rf_ok=...
rf_fail=... rf_timeout=... mac_avg_us=... lp_stage=... lp_batches=...
lp_samples=... lp_irq=... lp_poll=... lp_malformed=... lp_io_err=...
lp_fifo_ovf=... lp_slot_wait=... lp_fatal=... lp_fatal_reason=...
```

The most important error indicators are:

- `batch_bad_hdr` / `batch_bad_crc`: shared-memory handoff failure.
- `ipc_drop` / `release_err`: batch ownership protocol failure.
- `lp_io_err`: SPI or sensor initialization/runtime error.
- `lp_fifo_ovf`: MEMS FIFO overwrote unread data.
- `lp_slot_wait`: both shared slots were still owned by CPUAPP.
- `lp_poll`: GPIO interrupt was not observed and the 100 ms polling fallback
  recovered the FIFO.
- `frames_fail`, `rf_fail`, `rf_timeout`: radio transmission failure.

RX retains the existing status format:

```text
RX stat frames=... samples=... bps=... lost=... dup=... bad=...
rf_evt=... rf_frames=... rf_read_err=... seq=... first=... last=...
```

The V2 preview prints separate `RADIO`, `REORDER`, `SYNC`, `QUEUE`,
`SPI_TRANSPORT`, and `CONTROL` lines so a fault is attributable to one layer.

The long-term RX average should be close to 12000 samples/s and 192000 bit/s.
The one-second `bps` value can vary because TX emits one burst per full batch.

## Capture and batch verification

With the RX stream firmware flashed:

```powershell
python .\dump_rx_frames.py --port COM7 --max-frames 220 `
  --output .\2.4g_results\mems_batch_test.csv

python .\verify_mems_batches.py .\2.4g_results\mems_batch_test.csv
```

A successful verification reports zero bad/incomplete batches and zero
sequence gaps. One complete batch is 43 RF frames and exactly 4096 samples.

For low-rate diagnostics instead of raw frames:

```powershell
python .\save_serial_csv.py --port <TX_COM> --output tx_mems_stats.csv
python .\save_serial_csv.py --port <RX_COM> --output rx_mems_stats.csv
```
