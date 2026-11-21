# Software bill of materials and licences (2026-11-20)

Every component in the release tarball and in the build, with its licence. Update this file when a dependency
changes (CONTRIBUTING.md rule 4).

| Component | Version | Used for | Licence | Shipped how | Notes |
|---|---|---|---|---|---|
| corasip | 0.1.0 | the app | GPL-3.0-or-later (proposed D5, licence review) | binary | this repository |
| PJSIP (pjproject) | 2.17 (commit 5a457451) | SIP and media engine | GPL-2.0-or-later, or a commercial licence | static, in the binary | the GPL-3.0 licence of corasip covers the combined work |
| Qt | 6.4.2 (QtBase, QtSvg, QtOpenGL) | user interface | LGPL-3.0 | shared libraries in lib/ | replaceable |
| OpenSSL | 3.0.13 | TLS and SRTP | Apache-2.0 | shared libraries in lib/ | built from the upstream source, checksum verified |
| Opus | system (libopus 1.x) | audio codec | BSD-3-Clause | shared library in lib/ | |
| SQLite | system (3.3x) | call history and contacts | public domain | shared library in lib/ | |
| ICU | 56 (from Qt's bundle) | text handling in Qt | ICU License | shared libraries in lib/ | |
| ALSA (libasound) | system | audio devices | LGPL-2.1 | not shipped; needed on the desktop | |
| libglvnd (GL, EGL) | system | graphics dispatch | MIT-like (libglvnd) | not shipped; needed on the desktop | |
| libxcb and X11 libraries | system | X11 window system | MIT-like (X11) | not shipped; needed on the desktop | |
| GLib | system | Qt's threading helpers | LGPL-2.1 | not shipped; needed on the desktop | |
| Generated audio assets | ours | ringtone and key tones | corasip (GPL-3.0-or-later) | in the binary | made by assets/make_*.py |
| corasip icon | ours | application icon | corasip (GPL-3.0-or-later) | in the binary and tarball | assets/corasip.svg |

## Removed

- OpenSSL 1.1 was replaced by OpenSSL 3.0.13 on 2026-11-20 (its licence is not compatible with the GPL).

## Open licence questions

- The GPL-3.0-or-later licence for corasip is proposed, not yet confirmed by the owner (D5).
- PJSIP is GPL-2.0-or-later: distributing the static binary means the source must be available with each release.
- The legal review of the whole licence set is the owner's (D35).
