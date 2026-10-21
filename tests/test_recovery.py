#!/usr/bin/env python3
"""Rootless checks: only temporary files and mock device commands are used."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
RESTORE = ROOT / 'bin/googlebook-restore-slotb'


class RecoveryTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.work = Path(self.temp.name)
        self.backup = self.work / 'backup'
        self.backup.mkdir()
        self.ramdisk = self.work / 'ramdisk'
        self.ramdisk.write_bytes(b'R' * 131072)
        self.config = self.work / 'config'
        self.config.write_text('androidboot.googleboot=1\n')
        self.pack('--kernel', str(self.ramdisk), '-o', str(self.backup / 'boot_b.img'))
        self.pack('--ramdisk', str(self.ramdisk), '-o', str(self.backup / 'init_boot_b.img'))
        self.pack('--pagesize', '2048', '--vendor_bootconfig', str(self.config),
                  '--ramdisk_type', 'platform', '--ramdisk_name', '',
                  '--vendor_ramdisk_fragment', str(self.ramdisk),
                  '--vendor_boot', str(self.backup / 'vendor_boot_b.img'))
        self.device = self.work / 'device-vendor.img'
        self.device.write_bytes((self.backup / 'vendor_boot_b.img').read_bytes())
        self.log = self.work / 'commands'
        mock = self.work / 'fastboot'
        mock.write_text('''#!/usr/bin/env python3
import os, pathlib, shutil, sys
args = sys.argv[1:]
with open(os.environ['MOCK_LOG'], 'a') as f: f.write(' '.join(args) + '\\n')
if args[0] == '-s': args = args[2:]
if args[0] == 'fetch':
    if os.environ.get('FETCH_FAIL'): sys.exit(1)
    shutil.copyfile(os.environ['DEVICE_VENDOR'], args[2])
if args[0] == 'getvar':
    print(args[1] + ': ' + os.environ.get('CAPACITY', '0x4000000'), file=sys.stderr)
if args[:2] == ['flash', os.environ.get('FLASH_FAIL')]: sys.exit(1)
''')
        mock.chmod(0o755)
        self.env = dict(os.environ, PATH=str(self.work) + os.pathsep + os.environ['PATH'],
                        MOCK_LOG=str(self.log), DEVICE_VENDOR=str(self.device))

    def pack(self, *args):
        subprocess.run([sys.executable, str(ROOT / 'bin/mkbootimg'), '--header_version', '4', *args],
                       check=True, capture_output=True)

    def restore(self, *flags):
        return subprocess.run(['/bin/sh', str(RESTORE), '--fastboot', str(self.backup), *flags],
                              env=self.env, capture_output=True, text=True)

    def commands(self):
        return self.log.read_text() if self.log.exists() else ''

    def assert_no_writes(self):
        self.assertNotIn('flash ', self.commands())
        self.assertNotIn('--set-active', self.commands())
        self.assertNotIn('reboot', self.commands())

    def test_linux_marker_after_64k_restores_only_b(self):
        data = self.device.read_bytes()
        self.assertGreater(data.index(b'androidboot.googleboot=1'), 65536)
        result = self.restore()
        self.assertEqual(result.returncode, 0, result.stderr)
        flashes = [line.split()[1] for line in self.commands().splitlines() if line.startswith('flash ')]
        self.assertEqual(flashes, ['boot_b', 'init_boot_b', 'vendor_boot_b'])
        self.assertNotIn('current-slot', self.commands())
        self.assertIn('--set-active=b', self.commands())

    def test_android_b_refused_even_with_legacy_override(self):
        self.device.write_bytes(self.device.read_bytes().replace(b'androidboot.googleboot=1', b'androidboot.googleboot=0'))
        result = self.restore('--no-promote', '--reboot')
        self.assertNotEqual(result.returncode, 0)
        self.assert_no_writes()

    def test_fake_marker_in_header_is_not_enough(self):
        data = bytearray(self.device.read_bytes().replace(b'androidboot.googleboot=1', b'androidboot.googleboot=0'))
        data[28:52] = b'androidboot.googleboot=1\n'
        self.device.write_bytes(data)
        self.assertNotEqual(self.restore().returncode, 0)
        self.assert_no_writes()

    def test_truncated_device_image_refused(self):
        self.device.write_bytes(self.device.read_bytes()[:65536])
        self.assertNotEqual(self.restore().returncode, 0)
        self.assert_no_writes()

    def test_invalid_header_refused(self):
        self.device.write_bytes(bytes(2128))
        self.assertNotEqual(self.restore().returncode, 0)
        self.assert_no_writes()

    def test_missing_or_invalid_backups_prevent_device_commands(self):
        (self.backup / 'init_boot_b.img').write_bytes(b'not an image')
        self.assertNotEqual(self.restore().returncode, 0)
        self.assertEqual(self.commands(), '')

    def test_fetch_failure_prevents_writes(self):
        self.env['FETCH_FAIL'] = '1'
        self.assertNotEqual(self.restore().returncode, 0)
        self.assert_no_writes()

    def test_flash_failure_prevents_activation_and_reboot(self):
        self.env['FLASH_FAIL'] = 'init_boot_b'
        self.assertNotEqual(self.restore('--reboot').returncode, 0)
        self.assertNotIn('flash vendor_boot_b', self.commands())
        self.assertNotIn('--set-active', self.commands())
        self.assertNotIn('reboot', self.commands())

    def test_small_partition_prevents_all_writes(self):
        self.env['CAPACITY'] = '0x1000'
        self.assertNotEqual(self.restore().returncode, 0)
        self.assert_no_writes()

    def test_missing_python_prevents_all_device_commands(self):
        self.env['PATH'] = str(self.work)
        self.assertNotEqual(self.restore().returncode, 0)
        self.assertEqual(self.commands(), '')

    def test_automatic_hook_is_refused(self):
        self.assertNotEqual(self.restore('--install-hook').returncode, 0)
        self.assertEqual(self.commands(), '')

    def test_boot_and_init_images_cannot_be_swapped(self):
        (self.backup / 'boot_b.img').write_bytes((self.backup / 'init_boot_b.img').read_bytes())
        self.assertNotEqual(self.restore().returncode, 0)
        self.assertEqual(self.commands(), '')


if __name__ == '__main__':
    unittest.main()
