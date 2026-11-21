# corasip roadmap

This is the public plan for corasip. Items are grouped by priority, not by date, and can change.
Feedback and bug reports are welcome through the issue tracker.

## First release: 0.1.0 (Linux, x86_64)

- Calling: register with UDP, TCP or TLS; dial, answer, reject, hang up, hold, mute, DTMF, transfer,
  and several calls at once.
- Contacts with notes, call history, and CSV import and export.
- Audio device selection; codecs Opus, G.722 and G.711; SRTP over TLS; ICE and STUN.
- Tray icon, start at login, `sip:` and `tel:` links, and a global hotkey on X11.
- A call trace panel that shows the SIP messages of a call as a ladder diagram.
- A signed tarball and a one-line installer, with checksum and signature checks.

Known limits in 0.1.0:
- Not for emergency calls. See the disclaimer shown at first start.
- The global hotkey is off under Wayland.
- Tested on a limited set of Linux distributions; see `tests/install-matrix.md`.

## Next

- Wider install testing across Linux distributions.
- Reliability work: a long-running soak test, network change handling, and a fuzz run on SIP parsing.
- Screen reader checks for all main screens.
- An AppImage build alongside the tarball.

## Later

- Skins and themes.
- Trace panel version 2: flow across several calls, export to pcap, and search in the call history.
- Video calls.
- Chat messages.
- A macOS version, after the Linux version is in wide use.

## Not planned

- Emergency calling.
- Telemetry, accounts, or any cloud service run by the project.
