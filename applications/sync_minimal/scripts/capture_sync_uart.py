from __future__ import annotations

import argparse
import json
import re
import threading
import time
from pathlib import Path

import serial


STATUS_RE = re.compile(
    r"SYNC role=(?P<role>\w+) epoch=(?P<epoch>\d+) "
    r"tx_ok=(?P<tx_ok>\d+) tx_fail=(?P<tx_fail>\d+) "
    r"radio_events=(?P<radio_events>\d+) frames_ok=(?P<frames_ok>\d+) "
    r"frames_bad=(?P<frames_bad>\d+) last_epoch=(?P<last_epoch>\d+) "
    r"last_seq=(?P<last_seq>\d+) pulse_events=(?P<pulse_events>\d+)"
)


def read_port(label: str, port: str, seconds: float, output: Path, result: dict) -> None:
    path = output / f"{label}.log"
    started = time.monotonic()
    lines = 0
    last = None
    try:
        with serial.Serial(port, 115200, timeout=0.25) as ser, path.open(
            "w", encoding="utf-8", errors="replace"
        ) as log:
            while time.monotonic() - started < seconds:
                raw = ser.readline()
                if not raw:
                    continue
                text = raw.decode("utf-8", errors="replace").rstrip("\r\n")
                log.write(text + "\n")
                log.flush()
                lines += 1
                match = STATUS_RE.search(text)
                if match:
                    last = {key: int(value) if key not in {"role"} else value
                            for key, value in match.groupdict().items()}
    except Exception as exc:
        result["error"] = f"{type(exc).__name__}: {exc}"
    result.update(lines=lines, last_status=last)


def main() -> int:
    parser = argparse.ArgumentParser(description="Capture two sync-minimal UARTs.")
    parser.add_argument("--rx2", help="RX2 UART, e.g. COM10")
    parser.add_argument("--tx1", help="TX1 UART, e.g. COM4")
    parser.add_argument("--coordinator", help="Coordinator UART, e.g. COM11")
    parser.add_argument("--responders", nargs=2, metavar=("PORT_A", "PORT_B"),
                        help="Two responder UARTs, e.g. COM10 COM4")
    parser.add_argument("--seconds", type=float, default=30.0)
    parser.add_argument("--output-root", default="evidence/sync_minimal_uart")
    args = parser.parse_args()

    if args.coordinator and args.responders:
        ports = [
            ("coordinator", args.coordinator),
            ("responder_a", args.responders[0]),
            ("responder_b", args.responders[1]),
        ]
    elif args.rx2 and args.tx1:
        ports = [("rx2", args.rx2), ("tx1", args.tx1)]
    else:
        parser.error("provide --coordinator/--responders or --rx2/--tx1")

    stamp = time.strftime("%Y%m%d_%H%M%S")
    output = Path(args.output_root) / stamp
    output.mkdir(parents=True, exist_ok=True)
    results = {}
    threads = [
        threading.Thread(
            target=read_port,
            args=(label, port, args.seconds, output, results.setdefault(label, {})),
        )
        for label, port in ports
    ]
    for thread in threads:
        thread.start()
    for thread in threads:
        thread.join()

    statuses = {
        label: results.get(label, {}).get("last_status") or {}
        for label, _ in ports
    }
    coordinator = next(
        (status for status in statuses.values() if status.get("role") == "COORD"),
        {},
    )
    responders = [
        status for status in statuses.values() if status.get("role") == "RESP"
    ]
    responder_ok = len(responders) == 2 and all(
        status.get("frames_ok", 0) > 0
        and status.get("last_epoch") == coordinator.get("epoch")
        and status.get("pulse_events", 0) > 0
        for status in responders
    )
    same_epoch = (
        bool(coordinator)
        and responder_ok
    )
    summary = {
        "ports": {label: port for label, port in ports},
        "captures": results,
        "same_last_epoch": same_epoch,
        "host_timestamp_comparison_valid_for_us_sync": False,
        "passed_protocol_observation": same_epoch,
    }
    (output / "result.json").write_text(
        json.dumps(summary, indent=2, ensure_ascii=True), encoding="utf-8"
    )
    print(json.dumps({"output": str(output), **summary}, ensure_ascii=True))
    return 0 if same_epoch else 1


if __name__ == "__main__":
    raise SystemExit(main())
