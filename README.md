# corasip

A small, reliable SIP softphone for Linux, with the calling features people expect from a desktop phone: accounts,
dialing, answering, hold, transfer, contacts, call history, and TLS/SRTP where the server supports it.

**Status:** version 0.1.0, pre-release. Tested on Ubuntu 22.04 and 24.04, Debian 12, Fedora, Arch, openSUSE
Tumbleweed, Rocky Linux 9 and Linux Mint 22.1 (install and launch tests; see `tests/install-matrix.md`).

**Licence:** GNU General Public License v3.0 or later. See `LICENSE` and `NOTICE`.

## Install

Download the tarball and its `.sha256` file for your release. Check the checksum, unpack, and run:

```
sha256sum -c corasip-0.1.0-linux-x86_64.tar.gz.sha256
tar xzf corasip-0.1.0-linux-x86_64.tar.gz
corasip-0.1.0-linux-x86_64/bin/corasip
```

Or install for your user with the installer (it checks the checksum and refuses to run as root):

```
sh packaging/install.sh --from corasip-0.1.0-linux-x86_64.tar.gz
```

Remove it with `sh packaging/uninstall.sh`. Your call history and contacts are kept on purpose.

## Build from source

The release is built in `ci/release-image` (Ubuntu 20.04 base, Qt 6.4.2, OpenSSL 3.0.13). Run:

```
sh ci/release-image/build-release.sh <repo-dir> <out-dir>
```

## Not for emergency calls

corasip is not an emergency-calling service. Do not rely on it to reach emergency services.

## Warranty

This software is provided "as is", without warranty of any kind, express or implied, including the warranties of
merchantability, fitness for a particular purpose and non-infringement. See sections 15 and 16 of the licence. The
authors are not liable for any claim, damage or loss arising from the use of this software.

## The reference softphone

corasip is an independent program. It is not affiliated with, endorsed by, or derived from the reference softphone. The reference softphone is
named here only to describe compatibility ("works like the reference softphone"), and its name, logo and screenshots are not used
in the product.

## Reporting problems

See `SECURITY.md` for security issues. Other bugs and questions: open an issue in the repository.
