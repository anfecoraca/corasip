# corasip Linux build image (M1 step 4).
# Base image pinned by digest. Update the digest deliberately.
FROM ubuntu@sha256:73b999393d07f2bf65a40373778a44dc9a6d54ad7718d29247d8ff9413c4d6a8

ENV DEBIAN_FRONTEND=noninteractive \
    LC_ALL=C.UTF-8 \
    TZ=UTC

# Build tools and the libraries PJSIP needs: OpenSSL (TLS, SRTP), Opus, ALSA.
# apt versions are not pinned yet.
RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      build-essential cmake pkg-config git ca-certificates \
      libopus-dev libssl-dev libasound2-dev \
 && rm -rf /var/lib/apt/lists/*
