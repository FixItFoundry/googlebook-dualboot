# Local Regression Checks

Run from the repository root. None of these checks writes a block device or talks to a laptop.

```sh
python3 -m unittest discover -s tests -v
./tests/check-menu.sh
```

The Python tests generate Android v4 images with the bundled packer and run the restore script against mocked Fastboot commands. They check the Linux marker beyond 64 KiB, unknown or Android Slot B, malformed backups, unsupported reads, and flash failures. The C test compiles the actual menu parser and checks empty USB/Windows fields and a populated Linux field.

To exercise the phone's pure Java image validator as well, install a JDK and run:

```sh
python3 tests/check-android.py
```

The APK can be rebuilt with `./android-restore-app/build-apk.sh` on macOS. The build script uses the cached Android SDK and signing key. These tests do not validate Android document-provider permissions on a real phone, USB/TCP hardware transport, OTA recovery, or kernel modules.
