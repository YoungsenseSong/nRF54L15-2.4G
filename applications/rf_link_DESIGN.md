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
  - Prints TX status every 100 successful RF sends or once per idle second.
- `proto.h`
  - Defines the fixed 76-byte `rf_frame`.
  - Defines the common channel, magic value, sample count, and TX period.
- `radio_link.c`
  - Starts the radio clock domain.
  - Configures ESB in PTX mode.
  - Uses 1 Mbps PHY, channel 40, 16-bit CRC, ACK enabled.
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
- `debug_uart.c`
  - Uses `uart_poll_out()` directly on `uart30`.
  - Avoids relying on Zephyr console/printk in dual-core mode.

### TX LP core: `applications/rf_link_tx/flpr_app/src`

- `main.c`
  - Initializes fake sampler, sample buffer, and IPC TX endpoint.
  - Generates one frame per configured period.
  - Sends buffered frames to HP over IPC.
- `adc_sampler.c`
  - Generates deterministic fake 12-bit ramp samples.
  - Fills `rf_frame` with magic, sequence, count, flags, timestamp, and samples.
  - Intended replacement point for real ADC acquisition.
- `sample_buffer.c`
  - Provides a small LP-side frame queue.
  - Drops oldest data under pressure.
- `ipc_tx.c`
  - Opens `ipc0`, registers endpoint, waits for HP bind.
  - Sends full `rf_frame` payloads to HP.

### RX single-core: `applications/rf_link_rx/src`

- `main.c`
  - Starts UART, RX statistics, and ESB PRX.
  - Prints one status line per second.
- `radio_link.c`
  - Starts radio clock.
  - Configures ESB PRX with matching address/channel/PHY.
  - Reads every ESB payload and passes it to `rx_reorder_process_frame()`.
- `rx_reorder.c`
  - Validates frame size, magic, and sample count.
  - Tracks received frames, samples, bytes, sequence loss, duplicates, bad
    frames, and latest first/last sample.
- `debug_uart.c`
  - Uses direct UART polling output.

## Current Performance Finding

The 50 ksps target is configured by setting:

```text
32 samples/frame, one frame every 640 us
```

This equals:

```text
32 / 0.00064 = 50000 samples/s
50000 * 16 = 800000 bit/s
```

Observed logs with ESB 1 Mbps + ACK show:

- RX effective payload rate: about 286 to 306 kbps.
- RX sequence loss increases rapidly.
- TX `q_drop` increases rapidly.
- TX `rf_fail` remains low, so the limiting factor is throughput rather than
  frame corruption.
- TX MAC latency min is about 1087 us and average about 1574 us, which is
  longer than the requested 640 us frame period.

## Next Engineering Options

To reach 50 ksps reliably, the next changes should be evaluated separately:

1. Move ESB PHY from 1 Mbps to 2 Mbps or 4 Mbps if supported on this target.
2. Reduce ACK frequency or use a no-ACK streaming mode for payload data.
3. Increase samples per RF frame if ESB payload limits allow it.
4. Lower UART status print rate under high-rate tests.
5. Add GPIO timing probes for hardware latency measurement.
6. Replace fake sampler with ADC DMA only after the RF path has enough margin.
