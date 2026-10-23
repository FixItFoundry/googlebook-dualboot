import os
import re
import subprocess
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCAN = os.path.join(ROOT, "googleboot-menu", "bin", "googleboot-scan")


class ScanDistroLabelTests(unittest.TestCase):
    """The menu card label comes from the initramfs's /etc/initrd-release (written by dracut)."""

    def label(self, release_text):
        block = re.search(r'^OS_NAME="Fedora".*?^fi$', open(SCAN).read(), re.S | re.M).group(0)
        with tempfile.TemporaryDirectory() as d:
            rel = os.path.join(d, "initrd-release")
            if release_text is not None:
                open(rel, "w").write(release_text)
            script = block.replace("/etc/initrd-release", rel) + '\nprintf "%s|%s" "$OS_NAME" "$OS_ID"'
            return subprocess.run(["bash", "-c", script], capture_output=True, text=True, check=True).stdout

    def test_ubuntu(self):
        self.assertEqual(self.label('NAME="Ubuntu"\nID=ubuntu\n'), "Ubuntu|ubuntu")

    def test_fedora(self):
        self.assertEqual(self.label('NAME="Fedora Linux"\nID=fedora\n'), "Fedora Linux|fedora")

    def test_missing_file_defaults_to_fedora(self):
        self.assertEqual(self.label(None), "Fedora|fedora")


if __name__ == "__main__":
    unittest.main()
