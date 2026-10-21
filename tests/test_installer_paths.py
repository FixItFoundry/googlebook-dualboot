import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class InstallerPathTests(unittest.TestCase):
    def test_relative_input_and_output_paths(self):
        # Stub extraction/module tools, but run the real builder and image packer.
        with tempfile.TemporaryDirectory() as temp:
            work = Path(temp)
            iso = work / 'iso/images/pxeboot'
            iso.mkdir(parents=True)
            (iso / 'vmlinuz').write_bytes(b'kernel')
            (iso / 'initrd.img').write_bytes(b'archive')
            commands = work / 'commands'
            commands.mkdir()
            stubs = {
                'zstd': '#!/bin/sh\ncat\n',
                'cpio': '''#!/bin/sh
case "$1" in
  -idm) cat >/dev/null; mkdir -p usr/lib/modules/path-test; ;;
  *) cat >/dev/null; printf archive ;;
esac
''',
                'modprobe': '#!/bin/sh\nexit 0\n',
                'depmod': '#!/bin/sh\nexit 0\n',
            }
            for name, source in stubs.items():
                p = commands / name
                p.write_text(source)
                p.chmod(0o755)
            env = dict(os.environ, PATH=str(commands) + os.pathsep + os.environ['PATH'])
            result = subprocess.run(['bash', str(ROOT / 'scripts/build-slotb-installer.sh'),
                                     './iso', './out', 'http://localhost:8080'], cwd=work,
                                    env=env, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            for name in ('boot_b.img', 'init_boot_b.img', 'vendor_boot_b.img'):
                self.assertGreater((work / 'out' / name).stat().st_size, 0)


if __name__ == '__main__':
    unittest.main()
