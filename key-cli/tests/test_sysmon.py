"""Public command forwarding and exit semantics for the native sampler."""

import os
from pathlib import Path
import subprocess
import sys


ROOT = Path(__file__).resolve().parents[1]


def test_sysmon_passes_native_options_and_exit_code(tmp_path):
    sampler = tmp_path / "sampler"
    sampler.write_text('#!/bin/sh\nprintf "%s\\n" "$@"\nexit 7\n')
    sampler.chmod(0o755)
    environment = {**os.environ, "PYTHONPATH": str(ROOT / "src"), "KEY_CLI_SYSMON": str(sampler)}
    result = subprocess.run(
        [sys.executable, "-m", "key_cli", "sysmon", "stream", "--format", "jsonl"],
        cwd=ROOT,
        env=environment,
        capture_output=True,
        text=True,
        check=False,
    )
    assert result.returncode == 7
    assert result.stdout.splitlines() == ["stream", "--format", "jsonl"]


def test_missing_native_sampler_reports_unavailable(tmp_path):
    environment = {
        **os.environ,
        "PYTHONPATH": str(ROOT / "src"),
        "KEY_CLI_SYSMON": str(tmp_path / "missing"),
    }
    result = subprocess.run(
        [sys.executable, "-m", "key_cli", "sysmon", "system", "--format", "json"],
        cwd=ROOT,
        env=environment,
        capture_output=True,
        text=True,
        check=False,
    )
    assert result.returncode == 127
    assert "native sampler is unavailable" in result.stderr
