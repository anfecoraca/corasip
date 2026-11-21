# Install matrix

Tests, run in order in each distribution:
1. `tests/install-matrix/check-in-container.sh`: the tarball unpacks, every library resolves, and the app starts
   with Qt's offscreen display and is still running after 6 seconds.
2. `tests/install-matrix/roundtrip.sh`: the installer refuses to run as root without `--allow-root`, refuses a damaged
   tarball, installs the signed release (checksum and signature checked against the pinned key), the installed app
   launches for 6 seconds, and uninstall removes the installed files. The user's history is kept by design.

Run it with `tests/install-matrix/run-matrix.sh <staging dir> <repo dir> [image ...]`. The staging dir holds
`tarball.tar.gz`, its `.sha256` and `.sha256.asc`, and a copy of `packaging/`.

## Release 0.1.2 (signed tarball, x86_64)

Tarball: `corasip-0.1.2-linux-x86_64.tar.gz`, built in `ci/release-image` (Ubuntu 20.04 base, glibc 2.31 build;
the binary needs glibc 2.29 at most). Tested on 2026-11-20.

| Distribution | Container | Libraries | Launch | Install round trip |
|---|---|---|---|---|
| Ubuntu 22.04 | ubuntu:22.04 | resolve | PASS | PASS |
| Ubuntu 24.04 | ubuntu:24.04 | resolve | PASS | PASS |
| Debian 12 | debian:12 | resolve | PASS | PASS |
| Fedora | fedora:latest | resolve | PASS | PASS |
| Arch Linux | archlinux:latest | resolve | PASS | PASS |
| openSUSE Tumbleweed | opensuse/tumbleweed | resolve | PASS | PASS |
| Rocky Linux 9 (RHEL family) | rockylinux:9 | resolve | PASS | PASS |
| Ubuntu 26.04 | native (owner's laptop, Wayland session) | 0.1.2: PASS | 0.1.2: PASS (terminal and desktop icon, no Qt error) | 0.1.2: install (with desktop icon) and uninstall PASS |
| Linux Mint 22.1 | native (owner's desktop) | not yet run | not yet run | not yet run |

Container rows: 7 of 7 pass on 0.1.2. The Ubuntu 26.04 laptop row passes on 0.1.2 (manual run, Wayland session). Linux Mint 22.1 is still to be run on the owner's desktop. The tray icon, the global hotkey
on X11 and autostart at login passed on Linux Mint 22.1 on earlier builds (tray and hotkey on 2026-11-19, autostart on
2026-11-20). Containers cannot test them, and they have not been re-run on 0.1.2.

## Earlier builds

Before 0.1.0 the matrix was run on earlier builds of the same code (OpenSSL 1.1, then OpenSSL 3.0.13). Those runs
found and fixed missing library names in the install lists for several distributions. Their results are not repeated
here because the newest tarball is the one to trust.
