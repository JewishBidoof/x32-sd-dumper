FROM debian:trixie-slim

ENV DEBIAN_FRONTEND=noninteractive

# Enable amd64 multiarch so the x86-64 musl toolchain can run under qemu-user
RUN dpkg --add-architecture amd64 \
    && apt-get update \
    && apt-get install -y --no-install-recommends \
        build-essential \
        gcc-arm-linux-gnueabi \
        g++-arm-linux-gnueabi \
        binutils-arm-linux-gnueabi \
        u-boot-tools \
        bc \
        bison \
        flex \
        libssl-dev \
        libncurses-dev \
        gawk \
        cpio \
        fakeroot \
        perl \
        python3 \
        git \
        ca-certificates \
        xz-utils \
        gzip \
        rsync \
        file \
        device-tree-compiler \
        wget \
        diffutils \
        patch \
        libc6:amd64 \
        libstdc++6:amd64 \
    && rm -rf /var/lib/apt/lists/*
