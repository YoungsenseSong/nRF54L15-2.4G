import argparse
import csv
import re
import sys
from datetime import datetime

import serial
from serial.tools import list_ports


STAT_RE = re.compile(r"\b([A-Za-z_][A-Za-z0-9_]*)=(-?\d+)")

CSV_COLUMNS = [
    "host_time",
    "port",
    "source",
    "frames",
    "samples",
    "bps",
    "lost",
    "dup",
    "bad",
    "seq",
    "first",
    "last",
    "sent",
    "ipc_rx",
    "queued",
    "q_drop",
    "ipc_bad_size",
    "ipc_bad_magic",
    "rf_ok",
    "rf_fail",
    "rf_timeout",
    "rf_err",
    "rf_evt",
    "rf_frames",
    "rf_read_err",
    "attempts",
    "mac_cnt",
    "mac_last_us",
    "mac_min_us",
    "mac_avg_us",
    "mac_max_us",
    "lp_stage",
    "lp_loop",
    "lp_seq",
    "lp_ok",
    "lp_busy",
    "lp_fail",
    "lp_ret",
    "raw",
]


def parse_stat_line(line: str):
    line = line.strip()
    if line.startswith("RX stat "):
        source = "rx"
    elif line.startswith("TX stat "):
        source = "tx"
    else:
        return None

    values = {key: value for key, value in STAT_RE.findall(line)}
    if not values:
        return None

    row = {column: "" for column in CSV_COLUMNS}
    row["host_time"] = datetime.now().isoformat(timespec="milliseconds")
    row["source"] = source
    row["raw"] = line

    for key, value in values.items():
        if key in row:
            row[key] = value

    return row


def print_ports():
    ports = list(list_ports.comports())
    if not ports:
        print("No serial ports found.")
        return

    for item in ports:
        print(f"{item.device}\t{item.description}")


def main():
    parser = argparse.ArgumentParser(
        description="Capture rf_link TX/RX stat lines from UART and save them as CSV."
    )
    parser.add_argument("--port", default="COM7", help="Serial port, for example COM7.")
    parser.add_argument("--baud", type=int, default=115200, help="UART baud rate.")
    parser.add_argument("--output", default="rf_link_stats.csv", help="Output CSV path.")
    parser.add_argument("--append", action="store_true", help="Append to an existing CSV.")
    parser.add_argument("--echo-all", action="store_true", help="Print skipped non-stat lines too.")
    parser.add_argument("--list-ports", action="store_true", help="List serial ports and exit.")
    args = parser.parse_args()

    if args.list_ports:
        print_ports()
        return 0

    mode = "a" if args.append else "w"
    write_header = not args.append

    try:
        ser = serial.Serial(args.port, args.baud, timeout=1)
    except serial.SerialException as exc:
        print(f"Failed to open {args.port}: {exc}", file=sys.stderr)
        return 1

    print(f"Listening on {args.port} @ {args.baud}, saving stats to {args.output}")
    print("Press Ctrl+C to stop.")

    try:
        with ser, open(args.output, mode, newline="", encoding="utf-8") as csv_file:
            writer = csv.DictWriter(csv_file, fieldnames=CSV_COLUMNS)
            if write_header:
                writer.writeheader()
                csv_file.flush()

            while True:
                raw = ser.readline()
                if not raw:
                    continue

                line = raw.decode("utf-8", errors="ignore").strip()
                if not line:
                    continue

                row = parse_stat_line(line)
                if row is None:
                    if args.echo_all:
                        print(f"skip: {line}")
                    continue

                row["port"] = args.port
                writer.writerow(row)
                csv_file.flush()
                print(f"save: {line}")
    except KeyboardInterrupt:
        print("\nStopped.")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
