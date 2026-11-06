# bootc Image Layer for the Acer Googlebook 14

Builds a [bootc](https://bootc-dev.github.io/bootc/) image carrying the Moonstone hardware
enablement, on top of `ghcr.io/dsmk/bootc-image-laptop`.

Deployment, migration from a package-based Fedora install, and Slot A/B switching are
documented in **[`docs/BOOTC_GOOGLEBOOK.md`](../docs/BOOTC_GOOGLEBOOK.md)**.

## Build

Build from the **repository root** — this layer needs the module sources and the boot menu
that live beside it:

```bash
podman build -f bootc/Containerfile -t ghcr.io/dsmk/bootc-image-googlebook:main .
```

Override the base with `--build-arg BASE_IMAGE=…`.

## Layout

| Path | Purpose |
|---|---|
| `Containerfile` | Two stages: compile the out-of-tree modules against the base image's own kernel, then assemble the image |
| `install-googlebook-layer.sh` | Build-time assembly, run inside the image — the only moment `/usr` is writable |
| `configs/` | Copied to `/` as mode `0644` (dracut, modprobe, modules-load, hwdb, systemd units, ALSA UCM, tmpfiles) |
| `files/` | Copied to `/` as mode `0755` (`googlebook-slotb-sync`, `googlebook-gpu-init`) |

## Why a builder stage

The runtime image has no `gcc` or `make`, and its `/usr/lib/modules/<kver>/build` symlink
points at a `/usr/src/kernels/…` that is not installed. Stage 1 adds the exact
`kernel-devel-<kver>` for the base image's `kernel-core` and builds:

- `coreboot-mods/` → `coreboot_table.ko`, `framebuffer-coreboot.ko`
- `sensor-mods/` → `cros_ec_ishtp.ko`
- `audio-mods/` → `snd-soc-sdw-utils.ko`, `snd-soc-sof-sdw.ko`
- `googleboot-menu/` → the boot menu binary

Stage 2 copies the results in, runs `depmod`, and **fails the build** if any `vermagic`
does not match the image kernel or if `coreboot_table` is not resolvable afterwards.

That check is the point of the whole arrangement. On the package-based path,
`googlebook-pack-slotb` builds these modules at pack time through a fully silenced branch
(`>/dev/null 2>&1 || true`), so a missing `kernel-devel` produces a Slot B image that boots
to a black screen with nothing in the logs. Here the kernel is fixed for the life of the
image, the modules are built against it, and a mismatch cannot be published.

## CI

[`.github/workflows/googlebook-image.yml`](../.github/workflows/googlebook-image.yml)
rebuilds and publishes `ghcr.io/<owner>/bootc-image-googlebook` on pushes to `main` that
touch anything this image is built from — `bootc/`, `bin/`, the three `*-mods/` trees,
`googleboot-menu/`, `docs/`, `.containerignore` — and builds without publishing on pull
requests. amd64 only: the Googlebook is x86_64 and the modules and boot menu are compiled
for that kernel, so there is no matrix and no manifest job.

`files/usr/local/bin/smoke-googlebook.sh` is picked up automatically by the base image's
`smoketests-all.sh`, which globs `/usr/local/bin/smoke-*.sh`. It re-checks the module
`vermagic`, name resolution through `modprobe`, the firmware, the hwdb entry, the units and
the toolkit entry points.

That overlaps the build-time assertions on purpose. Those only cover this layer's own
build, while the image tracks a base that moves underneath it — a base rebuild that bumps
the kernel without a rebuild here leaves modules that silently refuse to load. Verified by
swapping in a `6.19.10-300` module and confirming the suite exits non-zero:

```
FAIL vermagic coreboot_table.ko: '6.19.10-300.fc44.x86_64' != '7.2.11-200.fc44.x86_64'
```

A base-image rebuild in the other repository does **not** trigger this workflow. Use
`workflow_dispatch`, or wire a `repository_dispatch` from the base repo.

## Notes

- **`keyd` is not packaged in Fedora 44** (only the unrelated `keydb`). `/etc/keyd/cros.conf`
  still ships for a COPR install; on Fedora the Vivaldi top row is handled by
  `61-googlebook-moonstone.hwdb` plus the `EVIOCSKEYCODE` pass in `googlebook-gpu-init`.
- **The hwdb index is regenerated in `/usr`, and the base image's `/etc/udev/hwdb.bin` is
  removed.** udev prefers the `/etc` copy, so leaving it in place would shadow the
  Moonstone keymap entirely. The build asserts that the entry resolves afterwards.
- **Testing the packer inside `podman run` will fail at dracut** with
  `Invalid tmpdir '/var/tmp'` and `dracut-install: ERROR: installing '/root'`. That is a
  container artifact of the empty `/var` a bootc image must have; `systemd-tmpfiles`
  recreates `/var/tmp` and `/var/roothome` on a real boot. Verify on the machine, not in a
  container.
