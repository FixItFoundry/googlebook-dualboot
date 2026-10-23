#!/usr/bin/env python3
"""Exercise the Android validator with the same image fixtures as host recovery."""
from pathlib import Path
import subprocess
from test_recovery import RecoveryTests, ROOT

fixture = RecoveryTests()
fixture.setUp()
try:
    classes = fixture.work / 'classes'
    classes.mkdir()
    srcs = [
        str(ROOT / 'android-restore-app/src/com/fixitfoundry/googlebookrestore/BootImages.java'),
        str(ROOT / 'tests/BootImagesTest.java'),
    ]
    ecj = Path('/tmp/android-sdk-cache/ecj.jar')
    if subprocess.run(['javac', '-d', str(classes), *srcs], capture_output=True).returncode != 0 and ecj.exists():
        subprocess.run(['java', '-jar', str(ecj), '-1.8', '-d', str(classes), *srcs], check=True)
    subprocess.run(['java', '-cp', str(classes), 'com.fixitfoundry.googlebookrestore.BootImagesTest',
                    str(fixture.backup)], check=True)
finally:
    fixture.doCleanups()
