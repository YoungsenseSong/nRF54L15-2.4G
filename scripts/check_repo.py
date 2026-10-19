#!/usr/bin/env python3
"""Check that every concrete project has the expected documentation entry points."""
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
REQUIRED = [
    "README.md",
    "project.yaml",
    "docs/design.md",
    "docs/spec.md",
    "docs/getting-started.md",
    "docs/verification.md",
    "docs/changes",
]
errors = []
for project in sorted((ROOT / "projects").iterdir()):
    if not project.is_dir() or project.name.startswith("_"):
        continue
    for item in REQUIRED:
        if not (project / item).exists():
            errors.append(f"{project.relative_to(ROOT)} 缺少 {item}")
if errors:
    print("仓库检查失败：")
    print("\n".join(f"- {e}" for e in errors))
    sys.exit(1)
print("仓库结构检查通过")
