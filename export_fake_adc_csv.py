import argparse
import csv
from datetime import datetime
from pathlib import Path


DEFAULT_OUTPUT_DIR = Path(r"D:\nRF54L15\NCS-Project\nrf54l15-connectkit\2.4g_results")
DEFAULT_SAMPLES_PER_FRAME = 96
DEFAULT_SAMPLE_WRAP = 0x1000


def build_output_path(output_dir: Path) -> Path:
    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    return output_dir / f"fake_adc_samples_{stamp}.csv"


def main() -> int:
    parser = argparse.ArgumentParser(
        description=(
            "Export the current rf_link fake ADC source data pattern to CSV. "
            "This matches adc_sampler.c in the current TX LP firmware."
        )
    )
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
        "--frames",
        type=int,
        default=1000,
        help="Number of frames to export.",
    )
    parser.add_argument(
        "--samples-per-frame",
        type=int,
        default=DEFAULT_SAMPLES_PER_FRAME,
        help="Samples per frame. Current firmware default is 96.",
    )
    parser.add_argument(
        "--start-seq",
        type=int,
        default=0,
        help="Starting frame sequence number.",
    )
    parser.add_argument(
        "--start-sample",
        type=int,
        default=0,
        help="Starting sample value before wrapping.",
    )
    parser.add_argument(
        "--sample-wrap",
        type=int,
        default=DEFAULT_SAMPLE_WRAP,
        help="Sample wrap modulus. Current firmware uses 4096.",
    )
    args = parser.parse_args()

    if args.frames <= 0:
        raise SystemExit("--frames must be > 0")
    if args.samples_per_frame <= 0:
        raise SystemExit("--samples-per-frame must be > 0")
    if args.sample_wrap <= 0:
        raise SystemExit("--sample-wrap must be > 0")

    if args.output:
        output_path = Path(args.output)
        output_path.parent.mkdir(parents=True, exist_ok=True)
    else:
        output_dir = Path(args.output_dir)
        output_dir.mkdir(parents=True, exist_ok=True)
        output_path = build_output_path(output_dir)

    sample_value = args.start_sample % args.sample_wrap

    with output_path.open("w", newline="", encoding="utf-8") as csv_file:
        writer = csv.writer(csv_file)
        writer.writerow(
            [
                "global_sample_index",
                "frame_seq",
                "sample_index_in_frame",
                "sample_value",
            ]
        )

        global_sample_index = 0
        for frame_offset in range(args.frames):
            frame_seq = (args.start_seq + frame_offset) & 0xFFFF
            for sample_index_in_frame in range(args.samples_per_frame):
                writer.writerow(
                    [
                        global_sample_index,
                        frame_seq,
                        sample_index_in_frame,
                        sample_value,
                    ]
                )
                global_sample_index += 1
                sample_value = (sample_value + 1) % args.sample_wrap

    print(f"Saved {args.frames} frames to {output_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
