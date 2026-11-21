# Install matrix (M6E)

Test: `tests/install-matrix/check-in-container.sh` (release tarball unpacks, libraries resolve, the app launches
offscreen for 6 s) followed by `tests/install-matrix/roundtrip.sh` (installer refuses root without `--allow-root`,
installs with the SHA-256 check, the installed app launches, uninstall removes the installer's files and keeps the
user's history by design). Tarball: `corasip-0.1.0-linux-x86_64.tar.gz` (30 MB), built in
`ci/release-image` on Ubuntu 20.04 (glibc 2.31).

| Distribution | Container | Libraries | Launch | Install round trip | Notes |
|---|---|---|---|---|---|
| Ubuntu 22.04 | ubuntu:22.04 | resolve | PASS | PASS | |
| Ubuntu 24.04 | ubuntu:24.04 | resolve | PASS | PASS | package is libasound2t64 |
| Debian 12 | debian:12 | resolve | PASS | PASS | |
| Fedora (45) | fedora:latest | resolve | PASS | PASS | needs libX11, xcb-util-wm |
| Arch Linux | archlinux:latest | resolve | PASS | PASS | needs libx11, xcb-util-wm |
| openSUSE Tumbleweed | opensuse/tumbleweed | resolve | PASS | PASS | needs libX11-6, libgthread-2_0-0 |
| Rocky Linux 9 (RHEL family) | rockylinux:9 (laptop) | resolve | PASS | PASS | run on the laptop: the synapseos virtual CPU lacks x86-64-v2 |
| Mint 22.1 (owner's desktop, native) | native | resolve | PASS | PASS | built on the desktop from a clean copy of the committed source (2026-11-20) |
| Ubuntu 26.04 (laptop, native) | native | resolve | PASS | PASS | the laptop built its own tarball from the same recipe |

Result: 9 of 9 rows pass (2026-11-20). Most rows used the unstripped tarball; Ubuntu 22.04 and Fedora were rerun on the stripped build; Mint 22.1 was built and tested on the stripped build.

Still to do on the owner's screen (not container tests): the tray icon, the hotkey, and autostart at login.

Not covered by the containers: the on-screen window, the tray icon, the `sip:` link and autostart. Those need a
desktop session (the owner's machines).

Not in this version: the release signature (the signing key is an owner decision). The
installer checks the SHA-256 only.

## Rebuild with OpenSSL 3 (2026-11-20)

The release now bundles OpenSSL 3.0.13 (Apache-2.0) instead of OpenSSL 1.1. The Mint 22.1 desktop was rebuilt and
passes the check and the round trip on this build. The other rows above were run on the earlier OpenSSL 1.1 build
and must be rerun on this build before the release.

## Matrix on the OpenSSL 3 build (2026-11-20, laptop)

Built on the Ubuntu 26.04 laptop from the committed source (OpenSSL 3.0.13, Qt 6.4.2, stripped; tarball 27.5 MB).
All nine rows pass: native Ubuntu 26.04, and containers for Ubuntu 22.04, Ubuntu 24.04, Debian 12, Fedora, Arch,
openSUSE Tumbleweed and Rocky Linux 9. Mint 22.1 passed on the desktop on this build earlier (2026-11-20).
The synapseos build could not finish (its network stalled on the OpenSSL download), so synapseos was not used.

