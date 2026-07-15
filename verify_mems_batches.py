#!/usr/bin/env python3
"""Verify 4096-sample MEMS batches in dump_rx_frames.py CSV output."""

from __future__ import annotations

import argparse
import csv
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, Mapping

FRAME_FLAG_MEMS = 1 << 1
FRAME_FLAG_BATCH_START = 1 << 2
FRAME_FLAG_BATCH_END = 1 << 3
EXPECTED_BATCH_SAMPLES = 4096
EXPECTED_BATCH_FRAMES = 43


@dataclass
class VerificationResult:
    rows: int = 0
    batches: int = 0
    bad_batches: int = 0
    incomplete_batches: int = 0
    non_mems_frames: int = 0
    sequence_gaps: int = 0

    @property
    def ok(self) -> bool:
        return (
            self.batches > 0
            and self.bad_batches == 0
            and self.incomplete_batches == 0
            and self.non_mems_frames == 0
            and self.sequence_gaps == 0
        )


def verify_rows(rows: Iterable[Mapping[str, str]]) -> VerificationResult:
    result = VerificationResult()
    in_batch = False
    batch_samples = 0
    batch_frames = 0
    previous_seq: int | None = None

    for row in rows:
        result.rows += 1
        flags = int(row["rf_flags"], 0)
        sequence = int(row["rf_seq"], 0)
        sample_count = int(row["rf_sample_count"], 0)

        if previous_seq is not None and sequence != ((previous_seq + 1) & 0xFFFF):
            result.sequence_gaps += 1
        previous_seq = sequence

        if not flags & FRAME_FLAG_MEMS:
            result.non_mems_frames += 1
            continue

        if flags & FRAME_FLAG_BATCH_START:
            if in_batch:
                result.incomplete_batches += 1
            in_batch = True
            batch_samples = 0
            batch_frames = 0

        if not in_batch:
            result.incomplete_batches += 1
            continue

        batch_samples += sample_count
        batch_frames += 1

        if flags & FRAME_FLAG_BATCH_END:
            result.batches += 1
            if (
                batch_samples != EXPECTED_BATCH_SAMPLES
                or batch_frames != EXPECTED_BATCH_FRAMES
            ):
                result.bad_batches += 1
            in_batch = False

    if in_batch:
        result.incomplete_batches += 1

    return result


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Verify RF sequence and 4096-sample MEMS batch boundaries."
    )
    parser.add_argument("csv_path", type=Path, help="CSV created by dump_rx_frames.py")
    args = parser.parse_args()

    with args.csv_path.open(newline="", encoding="utf-8") as csv_file:
        result = verify_rows(csv.DictReader(csv_file))

    print(
        f"rows={result.rows} batches={result.batches} "
        f"bad_batches={result.bad_batches} "
        f"incomplete_batches={result.incomplete_batches} "
        f"non_mems_frames={result.non_mems_frames} "
        f"sequence_gaps={result.sequence_gaps}"
    )
    return 0 if result.ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
