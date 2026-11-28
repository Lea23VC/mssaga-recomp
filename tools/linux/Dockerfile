# The Linux release's build machine (build-linux.sh): Ubuntu 22.04, glibc 2.35, so the program runs on any distro
# from 2022 on (Ubuntu 22.04+, Debian 12+, Fedora 36+, Arch, the Steam Deck). The headers are SDL3's: it opens X11,
# Wayland, PipeWire, PulseAudio, ALSA and the gamepads at run time with what the player's system has.
FROM ubuntu:22.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    gcc g++ make cmake ninja-build pkg-config python3 ca-certificates xz-utils patchelf \
    libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxss-dev libxfixes-dev libxtst-dev \
    libxkbcommon-dev libwayland-dev wayland-protocols libdecor-0-dev libegl-dev libdrm-dev libgbm-dev \
    libpulse-dev libasound2-dev libpipewire-0.3-dev libudev-dev libdbus-1-dev libibus-1.0-dev \
    && rm -rf /var/lib/apt/lists/*
