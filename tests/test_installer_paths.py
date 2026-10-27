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
                'modprobe': '#!/bin/sh\nexit 1\n',
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
            self.assertIn(b'inst.stage2=http://localhost:8080', (work / 'out/vendor_boot_b.img').read_bytes())

            # Verify Fedora Live (LiveOS/squashfs.img) and Ubuntu Casper Live (casper/vmlinuz + /lib/modules)
            (work / 'iso/LiveOS').mkdir(parents=True)
            (work / 'iso/LiveOS/squashfs.img').write_bytes(b'squash')
            (work / 'iso/EFI/BOOT').mkdir(parents=True)
            (work / 'iso/EFI/BOOT/grub.cfg').write_text('linux /images/pxeboot/vmlinuz root=live:CDLABEL=Fedora-WS-Live-44 rd.live.image\n')
            res_live = subprocess.run(['bash', str(ROOT / 'scripts/build-slotb-installer.sh'),
                                       './iso', './out-live', 'usb'], cwd=work,
                                      env=env, capture_output=True, text=True)
            self.assertEqual(res_live.returncode, 0, res_live.stdout + res_live.stderr)
            self.assertIn(b'root=live:CDLABEL=Fedora-WS-Live-44 rd.live.image',
                          (work / 'out-live/vendor_boot_b.img').read_bytes())

            casper = work / 'ubuntu/casper'
            casper.mkdir(parents=True)
            (casper / 'vmlinuz').write_bytes(b'kernel')
            (casper / 'initrd.zst').write_bytes(b'archive')
            (commands / 'cpio').write_text('''#!/bin/sh
case "$1" in
  -idm) cat >/dev/null; mkdir -p lib/modules/6.19.10-300.fc44.x86_64; ;;
  *) cat >/dev/null; printf archive ;;
esac
''')
            res_ubu = subprocess.run(['bash', str(ROOT / 'scripts/build-slotb-installer.sh'),
                                      './ubuntu', './out-ubu', 'usb'], cwd=work,
                                     env=env, capture_output=True, text=True)
            self.assertEqual(res_ubu.returncode, 0, res_ubu.stdout + res_ubu.stderr)
            self.assertIn(b'boot=casper', (work / 'out-ubu/vendor_boot_b.img').read_bytes())

    def test_shrink_userdata_script_guards_and_flow(self):
        with tempfile.TemporaryDirectory() as temp:
            work = Path(temp)
            commands = work / 'commands'
            commands.mkdir()
            log = work / 'calls.log'
            stubs = {
                'sgdisk': f'''#!/bin/sh
echo "sgdisk $*" >> "{log}"
case "$1" in
  -p)
    cat <<'EOF'
Number  Start (sector)    End (sector)  Size       Code  Name
   1        39845888      1000215182   457.9 GiB   8300  userdata
   3           69632        39845887   19.0 GiB    8300  super
EOF
    ;;
  -i)
    cat <<'EOF'
Partition GUID code: 0FC63DAF-8483-4772-8E79-3D69D8477DE4 (Linux filesystem)
Partition unique GUID: 11111111-2222-3333-4444-555555555555
First sector: 39845888 (at 19.0 GiB)
Last sector: 1000215182 (at 476.9 GiB)
Partition size: 960369295 sectors (457.9 GiB)
Attribute flags: 0000000000000000
Partition name: 'userdata'
EOF
    ;;
esac
''',
                'lsblk': '''#!/bin/sh
case "$*" in
  *"-lnno TYPE"*) echo "loop" ;;
  *"-rno PATH,TYPE"*) printf "/dev/null disk\\n/dev/null part\\n" | sed '2s|/dev/null|/dev/null_p1|' ;;
  *"-ndo FSTYPE"*) echo "ext4" ;;
  *"-bdno SIZE"*) echo "491709079040" ;;
  *) echo "nvme0n1p1 214748364800 ext4 userdata" ;;
esac
''',
                'findmnt': '#!/bin/sh\nexit 1\n',
                'mount': f'#!/bin/sh\necho "mount $*" >> "{log}"\nexit 0\n',
                'umount': f'#!/bin/sh\necho "umount $*" >> "{log}"\nexit 0\n',
                'e2fsck': f'#!/bin/sh\necho "e2fsck $*" >> "{log}"\nexit 1\n',
                'dumpe2fs': '#!/bin/sh\nprintf "Block size: 4096\\nFree blocks: 100000000\\n"\n',
                'resize2fs': f'#!/bin/sh\necho "resize2fs $*" >> "{log}"\n',
                'partprobe': '#!/bin/sh\nexit 0\n',
                'udevadm': '#!/bin/sh\nexit 0\n',
            }
            for name, source in stubs.items():
                p = commands / name
                p.write_text(source)
                p.chmod(0o755)
            persist_dir = work / 'persist'
            persist_dir.mkdir()
            script = (ROOT / 'scripts/shrink-googlebook-userdata.sh').read_text()
            # Replace root/block node checks in temp copy to test logic without root
            script = script.replace('if (( EUID != 0 )); then', 'if false; then')
            script = script.replace('|| ! -b "$DISK"', '')
            script = script.replace('[[ -b "$PART" ]]', 'true')
            script = script.replace('PERSIST_MNT=$(mktemp -d)', f'PERSIST_MNT="{persist_dir}"')
            test_script = work / 'shrink.sh'
            test_script.write_text(script)
            test_script.chmod(0o755)
            env = dict(os.environ, PATH=str(commands) + os.pathsep + os.environ['PATH'])
            result = subprocess.run(['bash', str(test_script), '/dev/nvme0n1', '200'],
                                    cwd=work, env=env, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            calls = log.read_text()
            self.assertLess(calls.index('umount '), calls.index('e2fsck '))
            self.assertIn('sgdisk --backup=', calls)
            self.assertTrue((persist_dir / '.googlebook-gpt-backup/userdata-original.txt').is_file())
            self.assertIn('resize2fs /dev/null_p1 199G', calls)
            self.assertIn('sgdisk --delete=1 --new=1:39845888:+200G', calls)


if __name__ == '__main__':
    unittest.main()
