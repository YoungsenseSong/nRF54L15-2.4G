import argparse
import csv
import struct
import sys
import time
from datetime import datetime
from pathlib import Path

import serial
from serial.tools import list_ports


DEFAULT_OUTPUT_DIR = Path(r"D:\nRF54L15\NCS-Project\nrf54l15-connectkit\2.4g_results")
DEFAULT_BAUD = 2_000_000
DEFAULT_PORT = "COM7"
DEFAULT_SAMPLE_COLUMNS = 96
STREAM_MAGIC = 0x31535852
STREAM_VERSION = 1
RF_LINK_MAGIC = 0xA55A
HEADER_FORMAT = "<I H B B I I I"
FRAME_FORMAT = "<H H H H I " + ("h " * DEFAULT_SAMPLE_COLUMNS)
HEADER_SIZE = struct.calcsize(HEADER_FORMAT)
FRAME_SIZE = struct.calcsize(FRAME_FORMAT)
RECORD_SIZE = HEADER_SIZE + FRAME_SIZE
STREAM_MAGIC_BYTES = struct.pack("<I", STREAM_MAGIC)

CSV_COLUMNS = [
    "host_time",
    "capture_index",
    "stream_frame_index",
    "stream_drop_total",
    "stream_uptime_ms",
    "rf_magic",
    "rf_seq",
    "rf_sample_count",
    "rf_flags",
    "rf_timestamp_ms",
]
CSV_COLUMNS.extend(f"sample_{index}" for index in range(DEFAULT_SAMPLE_COLUMNS))


def build_output_path(output_dir: Path) -> Path:
    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    return output_dir / f"rx_frames_{stamp}.csv"


def print_ports() -> None:
    ports = list(list_ports.comports())
    if not ports:
        print("No serial ports found.")
        return

    for item in ports:
        print(f"{item.device}\t{item.description}")


def find_next_record(buffer: bytearray) -> tuple[bytes | None, int]:
    discarded = 0

    while True:
        magic_pos = buffer.find(STREAM_MAGIC_BYTES)
        if magic_pos < 0:
            keep = min(len(buffer), len(STREAM_MAGIC_BYTES) - 1)
            discarded += len(buffer) - keep
            if keep > 0:
                del buffer[:-keep]
            else:
                buffer.clear()
            return None, discarded

        if magic_pos > 0:
            discarded += magic_pos
            del buffer[:magic_pos]

        if len(buffer) < HEADER_SIZE:
            return None, discarded

        header = struct.unpack_from(HEADER_FORMAT, buffer, 0)
        _, record_len, version, _, _, _, _ = header

        if record_len != RECORD_SIZE or version != STREAM_VERSION:
            discarded += 1
            del buffer[0]
            continue

        if len(buffer) < record_len:
            return None, discarded

        record = bytes(buffer[:record_len])
        del buffer[:record_len]
        return record, discarded


def record_to_row(record: bytes, capture_index: int) -> dict[str, object]:
    header = struct.unpack_from(HEADER_FORMAT, record, 0)
    frame = struct.unpack_from(FRAME_FORMAT, record, HEADER_SIZE)

    _, _, _, _, stream_frame_index, stream_drop_total, stream_uptime_ms = header
    rf_magic, rf_seq, sample_count, flags, rf_timestamp_ms, *samples = frame

    if rf_magic != RF_LINK_MAGIC:
        raise ValueError(f"Unexpected rf_magic: 0x{rf_magic:04x}")

    if sample_count == 0 or sample_count > DEFAULT_SAMPLE_COLUMNS:
        raise ValueError(f"Unexpected sample_count: {sample_count}")

    row: dict[str, object] = {
        "host_time": datetime.now().isoformat(timespec="milliseconds"),
        "capture_index": capture_index,
        "stream_frame_index": stream_frame_index,
        "stream_drop_total": stream_drop_total,
        "stream_uptime_ms": stream_uptime_ms,
        "rf_magic": rf_magic,
        "rf_seq": rf_seq,
        "rf_sample_count": sample_count,
        "rf_flags": flags,
        "rf_timestamp_ms": rf_timestamp_ms,
    }

    for index in range(DEFAULT_SAMPLE_COLUMNS):
        row[f"sample_{index}"] = samples[index] if index < sample_count else ""

    return row


def main() -> int:
    parser = argparse.ArgumentParser(
        description=(
            "Capture binary rx_sample_stream records from rf_link_rx and save "
            "accepted frames to CSV."
        )
    )
    parser.add_argument("--port", default=DEFAULT_PORT, help="Serial port, for example COM7.")
    parser.add_argument("--baud", type=int, default=DEFAULT_BAUD, help="Serial baud rate.")
    parser.add_argument(
        "--output-dir",
        default=str(DEFAULT_OUTPUT_DIR),
        help="Directory for the generated CSV file.",
    )
    parser.add_argument(
        "--output",
        help="Full output CSV path. Overrides --output-dir when provided.",
    )
    parser.add_argument(
        "--max-frames",
        type=int,
        default=0,
        help="Stop after capturing this many frames. 0 means unlimited.",
    )
    parser.add_argument(
        "--list-ports",
        action="store_true",
        help="List serial ports and exit.",
    )
    args = parser.parse_args()

    if args.list_ports:
        print_ports()
        return 0

    if args.max_frames < 0:
        raise SystemExit("--max-frames must be >= 0")

    if args.output:
        output_path = Path(args.output)
        output_path.parent.mkdir(parents=True, exist_ok=True)
    else:
        output_dir = Path(args.output_dir)
        output_dir.mkdir(parents=True, exist_ok=True)
        output_path = build_output_path(output_dir)

    try:
        ser = serial.Serial(args.port, args.baud, timeout=0.2)
    except serial.SerialException as exc:
        print(f"Failed to open {args.port}: {exc}", file=sys.stderr)
        return 1

    print(f"Listening on {args.port} @ {args.baud}, saving frames to {output_path}")
    print("Press Ctrl+C to stop.")

    buffer = bytearray()
    capture_index = 0
    bad_records = 0
    discarded_bytes = 0
    last_report = time.monotonic()
    last_seq: int | None = None

    try:
        with ser, output_path.open("w", newline="", encoding="utf-8") as csv_file:
            writer = csv.DictWriter(csv_file, fieldnames=CSV_COLUMNS)
            writer.writeheader()

            while True:
                chunk = ser.read(4096)
                if chunk:
                    buffer.extend(chunk)

                while True:
                    record, discarded = find_next_record(buffer)
                    discarded_bytes += discarded
                    if record is None:
                        break

                    try:
                        row = record_to_row(record, capture_index)
                    except ValueError:
                        bad_records += 1
                        continue

                    writer.writerow(row)
                    capture_index += 1
                    last_seq = int(row["rf_seq"])

                    if capture_index % 32 == 0:
                        csv_file.flush()

                    if args.max_frames and capture_index >= args.max_frames:
                        csv_file.flush()
                        print(f"Captured {capture_index} frames. Stopping.")
                        return 0

                now = time.monotonic()
                if now - last_report >= 1.0:
                    csv_file.flush()
                    last_report = now
                    seq_text = "N/A" if last_seq is None else str(last_seq)
                    print(
                        "frames="
                        f"{capture_index} last_seq={seq_text} "
                        f"bad_records={bad_records} discarded_bytes={discarded_bytes}"
                    )
    except KeyboardInterrupt:
        print("\nStopped.")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
