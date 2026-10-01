"""Exercise capability policy with fake administrator tools in a temporary root."""

import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class CapabilityContracts(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.binary = self.root / "usr/lib/key-cli/key-cpu-power"
        self.binary.parent.mkdir(parents=True)
        self.binary.write_text("fixture")
        self.binary.chmod(0o755)
        commands = self.root / "commands"
        commands.mkdir()
        self.state = self.root / "caps"
        self.log = self.root / "calls"
        program = """#!/usr/bin/env python3
import os,sys
from pathlib import Path
name=Path(sys.argv[0]).name
state=Path(os.environ['CAP_STATE'])
if name=='id': print('0')
elif name=='stat': print('0' if '%u' in sys.argv else '755')
elif name=='pacman': print(os.environ.get('CAP_OWNER','key-cli'))
elif name=='getcap':
    if state.exists() and state.read_text():print(sys.argv[-1]+' '+state.read_text())
elif name=='setcap':
    with open(os.environ['CAP_LOG'],'a') as f:f.write(' '.join(sys.argv[1:])+'\\n')
    state.write_text('' if sys.argv[1]=='-r' else sys.argv[1])
"""
        for name in ("id", "stat", "pacman", "getcap", "setcap"):
            target = commands / name
            target.write_text(program)
            target.chmod(0o755)
        self.environment = {
            **os.environ,
            "PATH": str(commands) + ":" + os.environ["PATH"],
            "CAP_STATE": str(self.state),
            "CAP_LOG": str(self.log),
        }

    def call(self, operation):
        return subprocess.run(
            [
                "bash",
                str(ROOT / "packaging/arch/key-cli-cpu-power-access.sh"),
                operation,
                "--root",
                str(self.root),
            ],
            env=self.environment,
            text=True,
            capture_output=True,
        )

    def test_install_upgrade_and_revoke(self):
        self.assertEqual(self.call("enable").returncode, 0)
        self.assertEqual(self.state.read_text(), "cap_dac_read_search=ep")
        self.assertEqual(self.call("enable").returncode, 0)
        self.assertEqual(len(self.log.read_text().splitlines()), 1)
        self.state.unlink()  # A package upgrade replaces the inode and drops its xattr.
        self.assertEqual(self.call("enable").returncode, 0)
        self.assertEqual(len(self.log.read_text().splitlines()), 2)
        self.assertEqual(self.call("disable").returncode, 0)
        self.assertEqual(self.state.read_text(), "")

    def test_unknown_capabilities_are_preserved(self):
        self.state.write_text("cap_net_admin=ep")
        for operation in ("enable", "disable"):
            self.assertNotEqual(self.call(operation).returncode, 0)
        self.assertEqual(self.state.read_text(), "cap_net_admin=ep")
        self.assertFalse(self.log.exists())

    def test_foreign_binary_and_symlink_are_rejected(self):
        self.environment["CAP_OWNER"] = "another-package"
        self.assertNotEqual(self.call("enable").returncode, 0)
        self.environment["CAP_OWNER"] = "key-cli"
        self.binary.unlink()
        self.binary.symlink_to("/usr/bin/true")
        self.assertNotEqual(self.call("enable").returncode, 0)
        self.assertFalse(self.log.exists())


if __name__ == "__main__":
    unittest.main()
