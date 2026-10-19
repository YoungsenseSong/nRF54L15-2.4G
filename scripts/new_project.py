#!/usr/bin/env python3
"""Create a new IC/FPGA/MCU project from projects/_template."""
from __future__ import annotations
import argparse
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TEMPLATE = ROOT / "projects" / "_template"

def valid_name(value: str) -> bool:
    return bool(re.fullmatch(r"[a-z0-9][a-z0-9_\-]*", value))

def replace_text(text: str, project_name: str, title: str, kind: str) -> str:
    text = text.replace("<project_name>", project_name)
    text = text.replace("<工程名称>", title)
    text = text.replace("type: fpga", f"type: {kind}")
    text = text.replace("YYYY-MM-DD", "TODO")
    return text

def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("path", help="new project path, e.g. projects/my_fpga")
    parser.add_argument("--type", choices=["fpga", "mcu", "embedded", "mixed"], default="fpga")
    parser.add_argument("--name", required=True, help="human-readable project title")
    args = parser.parse_args()

    target = (ROOT / args.path).resolve() if not Path(args.path).is_absolute() else Path(args.path).resolve()
    if not valid_name(target.name):
        raise SystemExit("工程目录名只能使用小写字母、数字、下划线和短横线")
    if target.exists():
        raise SystemExit(f"目标目录已存在：{target}")
    if TEMPLATE.resolve() == target:
        raise SystemExit("不能覆盖模板目录")

    for source in TEMPLATE.rglob("*"):
        if source.is_dir():
            continue
        relative = source.relative_to(TEMPLATE)
        destination = target / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        if source.suffix in {".md", ".yaml"}:
            destination.write_text(replace_text(source.read_text(encoding="utf-8"), target.name, args.name, args.type), encoding="utf-8")
        else:
            destination.write_bytes(source.read_bytes())
    print(f"created {target}")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
