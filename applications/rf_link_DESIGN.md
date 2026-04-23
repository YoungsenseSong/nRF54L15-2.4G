# RF Link Design Notes

## Goals

1. Build a private 2.4 GHz point-to-point link on nRF54L15.
2. Keep the TX side compatible with the planned dual-core acquisition design.
3. Keep RX simple and single-core for first-stage link validation.
4. Export useful engineering statistics without printing raw samples.

## Design Split

### TX HP core: `applications/rf_link_tx/src`

- `main.c`
  - Initializes debug UART, TX queue, ESB radio, and IPC bridge.
  - Pulls validated frames from `tx_queue`.
  - Sends each frame through `radio_link_send_frame()`.
  - Prints TX status once per configured status period.
- `proto.h`
  - Defines the fixed 204-byte `rf_frame`.
  - Defines the common channel, magic value, sample count, TX period, status
    period, and no-ACK streaming flag.
- `radio_link.c`
  - Starts the radio clock domain.
  - Configures ESB in PTX mode.
  - Uses 4 Mbps PHY on nRF54L15 when available, with a 2 Mbps fallback.
  - Uses channel 40, 16-bit CRC, selective ACK support, and no-ACK payloads.
  - Measures MAC send latency from `esb_write_payload()` to the ESB completion
    event or timeout.
  - Tracks min/avg/max/last latency in microseconds.
- `ipc_bridge.c`
  - Opens `ipc0` using `ipc_service` and `icmsg`.
  - Receives LP frames, validates size and magic, and submits valid frames to
    the TX queue.
- `tx_queue.c`
  - Provides a fixed-depth message queue for RF frames.
  - Drops the oldest frame when the queue is full so the link favors newer data.
  - Uses a larger HP queue for the high-rate 50 ksps test path.
- `debug_uart.c`
  - Uses `uart_poll_out()` directly on `uart30`.
  - Avoids relying on Zephyr console/printk in dual-core mode.

### TX LP core: `applications/rf_link_tx/flpr_app/src`

- `main.c`
  - Initializes fake sampler and IPC TX endpoint.
  - Generates one 96-sample frame per 1920 us period.
  - Sends each generated frame directly to HP over IPC to avoid an extra LP
    software queue in the high-rate path.
- `adc_sampler.c`
  - Generates deterministic fake 12-bit ramp samples.
  - Fills `rf_frame` with magic, sequence, count, flags, timestamp, and samples.
  - Intended replacement point for real ADC acquisition.
- `sample_buffer.c`
  - Kept as a local helper for future producer/consumer experiments.
  - Not built into the current high-rate FLPR image.
- `ipc_tx.c`
  - Opens `ipc0`, registers endpoint, waits for HP bind.
  - Sends full `rf_frame` payloads to HP.

### RX single-core: `applications/rf_link_rx/src`

- `main.c`
  - Starts UART, RX statistics, and ESB PRX.
  - Prints one status line per second.
- `radio_link.c`
  - Starts radio clock.
  - Configures ESB PRX with matching address/channel/PHY and no-ACK payload
    behavior.
  - Reads every ESB payload and passes it to `rx_reorder_process_frame()`.
- `rx_reorder.c`
  - Validates frame size, magic, and sample count.
  - Tracks received frames, samples, bytes, sequence loss, duplicates, bad
    frames, and latest first/last sample.
- `debug_uart.c`
  - Uses direct UART polling output.

## Current Performance Finding

The current 50 ksps optimization build is configured by setting:

```text
96 samples/frame, one frame every 1920 us
```

This equals:

```text
96 / 0.00192 = 50000 samples/s
50000 * 16 = 800000 bit/s
```

Measured `v0.4` logs with ESB 1 Mbps + ACK showed:

- RX effective payload rate: about 286 to 306 kbps.
- RX sequence loss increases rapidly.
- TX `q_drop` increases rapidly.
- TX `rf_fail` remains low, so the limiting factor is throughput rather than
  frame corruption.
- TX MAC latency min is about 1087 us and average about 1574 us, which is
  longer than the requested 640 us frame period.

The `v0.5` build changes the bottleneck assumptions:

- PHY is raised to 4 Mbps on nRF54L15.
- Payload data uses ESB no-ACK to remove ACK turnaround from the streaming path.
- Frame size is raised to 204 bytes so the packet rate drops from 1562.5 pps to
  about 520.8 pps for the same 50 ksps payload.
- HP TX queue is raised from 16 to 64 frames.
- UART status printing is time-based at 1000 ms, not packet-count based.
- FLPR sends generated frames directly over IPC with a 200 us IPC send timeout.

The expected full-rate RX payload is still 800 kbps. Whether `lost` and `q_drop`
remain acceptable must be measured on hardware and recorded as EXP-001.

## Next Engineering Options

To reach 50 ksps reliably, the next changes should be evaluated after the
`v0.5` hardware run:

1. Compare 4 Mbps no-ACK with 2 Mbps no-ACK for range and loss.
2. Add a low-rate control/heartbeat frame if batch ACK is needed.
3. Tune frame sample count if 204-byte payloads prove too fragile over distance.
4. Add GPIO timing probes for hardware latency measurement.
5. Replace fake sampler with ADC DMA only after the RF path has enough margin.
