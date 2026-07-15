# RF Link Design Notes

## Design goals

1. Keep the main repository's proven ESB frame and RX implementation.
2. Run continuous IIM-42352 acquisition on FLPR.
3. Let CPUAPP block in System ON idle until 4096 samples are ready.
4. Transfer bulk data by shared-memory ownership, not by copying 8 KB through
   ICMsg.
5. Make buffer overruns, cache coherency failures, sensor faults, and RF loss
   visible in bounded status counters.

## Core split

### TX FLPR

- Owns SPIM00 and the IIM-42352 INT1 GPIO.
- Configures Packet 1 FIFO at 4 kHz per axis and a 1024-byte watermark.
- Uses a GPIO ISR only to signal a semaphore; all SPI work runs in the FLPR
  main thread.
- Reads the latched FIFO count in the datasheet-required low-byte/high-byte
  order and drains the complete FIFO in one burst.
- Accumulates XYZ-interleaved `int16_t` values into one of two 4096-sample
  shared slots.
- Computes CRC32, applies a memory barrier, and sends a small batch-ready ICMsg
  descriptor.
- Does not reuse the slot until CPUAPP sends the matching batch-release
  descriptor.

### TX CPUAPP

- Initializes ICMsg and blocks indefinitely on the batch message queue.
- Is awakened by the VEVIF mailbox interrupt when FLPR publishes a descriptor.
- Invalidates its data cache for the shared slot and validates slot metadata
  plus CRC32.
- Lazily initializes ESB when the first valid batch arrives.
- Converts a batch into 43 fixed-size RF frames while preserving the main
  repository's channel, address, PHY, CRC, and no-ACK behavior.
- Releases the slot to FLPR even when validation or radio transmission fails,
  preventing permanent buffer ownership leaks.

### RX CPUAPP

- Keeps the existing single-core ESB PRX path.
- Validates fixed wire size, magic, and variable `sample_count` from 1 to 96.
- Uses RF sequence gaps to count lost frames.
- Supports either one-second text statistics or the optional binary frame
  stream used by `dump_rx_frames.py`.

## Shared-memory ownership protocol

Each shared slot has exactly one owner:

```text
free -> FLPR filling -> ready message -> CPUAPP reading/sending
     -> release message -> free
```

The ready/release message contains protocol magic/version, message type, slot
index, batch sequence, sample count, and sample CRC32. A release is accepted by
FLPR only when both slot index and batch sequence match the in-flight slot.
Duplicate or stale releases are rejected and counted.

Two slots allow FLPR to acquire the next batch while CPUAPP transmits the
previous one. If both slots are busy, FLPR blocks instead of overwriting data;
`lp_slot_wait` exposes this condition. The IIM FIFO can then overflow, which is
reported as `lp_fifo_ovf`.

## Cache and memory ordering

- FLPR has no enabled data cache in this build and writes the shared SRAM
  directly.
- FLPR finishes the slot and executes a full data-memory barrier before the
  ready descriptor is sent.
- CPUAPP invalidates the full cache-line-aligned slot after receiving the
  descriptor, then executes a full barrier before reading metadata or samples.
- Slot addresses and sizes are compile-time checked against the devicetree.

## IIM-42352 details

- SPI mode 3, 8 MHz.
- Packet 1, 8 bytes: header, X, Y, Z, temperature.
- Regular-resolution header check: `(header & 0xFC) == 0x40`.
- FIFO count in bytes (`FIFO_COUNT_REC=0`).
- 1024-byte watermark, repeated threshold events enabled.
- Stream-to-FIFO mode.
- Low-noise accelerometer, 4 kHz ODR, +/-16 g.
- 8 us INT1 pulse, required by the datasheet for ODR >= 4 kHz.
- A 100 ms FIFO polling fallback prevents a missed GPIO edge from deadlocking
  acquisition.

## RF protocol compatibility

The RF wire size stays 204 bytes. A MEMS batch only changes these semantics:

- `sample_count` is 96 for normal batch frames and 64 for the final frame.
- `RF_LINK_FRAME_FLAGS_MEMS` identifies real MEMS samples.
- `RF_LINK_FRAME_FLAGS_BATCH_START` marks the first frame.
- `RF_LINK_FRAME_FLAGS_BATCH_END` marks the 43rd frame.
- Frame sequence remains continuous across batch boundaries.

The RX source did not require a radio-path redesign. TX and RX include one
shared protocol header to prevent future frame-definition drift.

## Validation boundary

Both TX images and the RX image build successfully with NCS 3.1.0 without
attached hardware. Hardware validation is still required for pin wiring,
WHO_AM_I, FIFO interrupt capture, sustained radio loss, and measured power.
See `rf_link_INTEGRATION_REPORT.md` for the test procedure and acceptance
criteria.
