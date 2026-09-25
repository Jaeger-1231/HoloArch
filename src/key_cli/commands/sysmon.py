"""Replace the Python dispatcher with the native JSON/JSONL sampler."""

from __future__ import annotations

import os
from pathlib import Path
import shutil
import sys


def _sampler() -> str | None:
    configured = os.environ.get("KEY_CLI_SYSMON", "").strip()
    if configured:
        candidates = [configured]
    else:
        checkout = Path(__file__).resolve().parents[3]
        candidates = []
        invocation = Path(sys.argv[0])
        if invocation.is_absolute():
            candidates.append(str(invocation.parent / "key-sysmon"))
        candidates.append(str(checkout / "native/build/bin/key-sysmon"))
        found = shutil.which("key-sysmon")
        if found:
            candidates.append(found)
    for candidate in candidates:
        path = Path(candidate)
        if path.is_file() and os.access(path, os.X_OK):
            return str(path)
    return None


def run(args) -> int:
    sampler = _sampler()
    if sampler is None:
        print(
            "key sysmon: native sampler is unavailable; build or install key-sysmon",
            file=sys.stderr,
        )
        return 127
    try:
        os.execv(sampler, [sampler, *args.arguments])
    except OSError as error:
        print(f"key sysmon: {error}", file=sys.stderr)
        return 127
