# corasip release build image (M6A). Old glibc on purpose: binaries built here run on distributions newer than this.
# Base: ubuntu:20.04 (glibc 2.31). debian:11 was tried first; its security mirror returned 404 for listed packages. Update the base and the Qt version deliberately.
FROM ubuntu:20.04

ENV DEBIAN_FRONTEND=noninteractive \
    LC_ALL=C.UTF-8 \
    TZ=UTC \
    QT_VERSION=6.4.2

# Build tools; PJSIP needs OpenSSL (TLS, SRTP), Opus and ALSA. SQLite is the system library.
COPY install-deps.sh /tmp/install-deps.sh
RUN sh /tmp/install-deps.sh && rm /tmp/install-deps.sh

# OpenSSL 3.0.13 (Apache-2.0, compatible with GPL-3.0), built from the upstream source. The checksum is the one
# OpenSSL publishes at openssl.org/source/openssl-3.0.13.tar.gz.sha256. Replaces the OpenSSL 1.1 system library
# (OpenSSL 1.x licence is not compatible with the GPL) (M6, licence review).
RUN curl -fsSL -o /tmp/openssl.tar.gz https://www.openssl.org/source/openssl-3.0.13.tar.gz \
 && echo "88525753f79d3bec27d2fa7c66aa0b92b3aa9498dafd93d7cfa4b3780cdae313  /tmp/openssl.tar.gz" | sha256sum -c - \
 && tar -C /tmp -xzf /tmp/openssl.tar.gz \
 && cd /tmp/openssl-3.0.13 \
 && ./Configure --prefix=/opt/openssl3 --openssldir=/opt/openssl3/ssl --libdir=lib shared >/dev/null \
 && make -j4 >/dev/null && make install_sw >/dev/null \
 && cd / && rm -rf /tmp/openssl-3.0.13 /tmp/openssl.tar.gz
ENV OPENSSL_PREFIX=/opt/openssl3

# Qt 6.4.2 from the official installer tool, pinned. qtbase carries Widgets, which corasip uses.
RUN python3 -m venv /opt/aqt \
 && /opt/aqt/bin/pip install --no-cache-dir --upgrade pip setuptools wheel \
 && /opt/aqt/bin/pip install --no-cache-dir "aqtinstall==3.1.*" \
 && /opt/aqt/bin/aqt install-qt linux desktop "${QT_VERSION}" gcc_64 -O /opt/Qt \
 && rm -rf /root/.cache/pip
ENV Qt6_DIR=/opt/Qt/6.4.2/gcc_64/lib/cmake/Qt6 \
    PATH=/opt/Qt/6.4.2/gcc_64/bin:$PATH \
    LD_LIBRARY_PATH=/opt/openssl3/lib:/opt/openssl3/lib64:/opt/Qt/6.4.2/gcc_64/lib
