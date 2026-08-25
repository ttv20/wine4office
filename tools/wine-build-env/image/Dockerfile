FROM ubuntu:24.04@sha256:33ceb71981b602c1a7443a53469e4dba065f7503eab3078a2d7a57a2ab987517

ARG DEBIAN_FRONTEND=noninteractive

RUN dpkg --add-architecture i386 \
    && apt-get update \
    && apt-get install -y --no-install-recommends \
        autoconf \
        bison \
        build-essential \
        ca-certificates \
        ccache \
        curl \
        flex \
        g++-mingw-w64-i686 \
        g++-mingw-w64-x86-64 \
        gcc-mingw-w64-i686 \
        gcc-mingw-w64-x86-64 \
        git \
        libasound2-dev \
        libavcodec-dev \
        libavformat-dev \
        libavutil-dev \
        libcups2-dev \
        libdbus-1-dev \
        libegl1 \
        libfontconfig-dev \
        libfontconfig-dev:i386 \
        libfreetype-dev \
        libfreetype-dev:i386 \
        libgl1-mesa-dev \
        libgnutls28-dev \
        libgphoto2-dev \
        libgstreamer-plugins-base1.0-dev \
        libgstreamer1.0-dev \
        libkrb5-dev \
        libpcap-dev \
        libpcsclite-dev \
        libpipewire-0.3-dev \
        libpulse-dev \
        libpython3-dev \
        libsane-dev \
        libsdl2-dev \
        libswresample-dev \
        libswscale-dev \
        libudev-dev \
        libunwind-dev \
        libusb-1.0-0-dev \
        libv4l-dev \
        libva-dev \
        libvulkan-dev \
        libwayland-dev \
        libx11-dev \
        libxcomposite-dev \
        libxcursor-dev \
        libxext-dev \
        libxfixes-dev \
        libxi-dev \
        libxinerama-dev \
        libxkbcommon-dev \
        libxkbregistry-dev \
        libxrandr-dev \
        libxrender-dev \
        libxxf86vm-dev \
        ocl-icd-opencl-dev \
        pkg-config \
        python3 \
        python3-pip \
        python3-venv \
        samba-dev \
        tar \
        unixodbc-dev \
        wayland-protocols \
        xz-utils \
        zstd \
    && rm -rf /var/lib/apt/lists/*

LABEL org.opencontainers.image.title="Wine4Office builder" \
      org.opencontainers.image.description="Canonical combined-WoW64 Wine4Office build environment" \
      wine4office.build-contract="1"

WORKDIR /work/build
