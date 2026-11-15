# Build container for Part B (TF-A, OP-TEE, U-Boot, Linux, Buildroot, QEMU).
#
# Everything that affects the build output is pinned here: the base image (by
# digest) and the cross toolchain (by version and sha256). The same image is
# used locally (bsp/dock) and in CI, so both build the same thing.

FROM debian:trixie-slim@sha256:a72b4433de1cf7d6bae0c4cee74d0547f58601a643b3375f070c51600d63b6b2

ARG DEBIAN_FRONTEND=noninteractive

# Host tools, grouped by who needs them.
RUN apt-get update && apt-get install -y --no-install-recommends \
        # common build tools
        build-essential git make ca-certificates curl xz-utils file \
        bc bison flex cpio rsync unzip wget patch perl gawk \
        # Linux and U-Boot host tools (kernel signing, mkimage, dtc)
        libssl-dev libgnutls28-dev device-tree-compiler \
        # OP-TEE: TA signing and ELF parsing; U-Boot binman
        python3 python3-cryptography python3-pyelftools python3-setuptools \
        python3-dev swig python3-libfdt \
        # Buildroot and optee_client/optee_test (CMake packages)
        cmake pkg-config libncurses-dev \
        # running and testing
        qemu-system-arm expect gdb-multiarch \
    && rm -rf /var/lib/apt/lists/*

# Arm GNU Toolchain, aarch64 GNU/Linux target (glibc). One compiler for every
# component: bare-metal code (TF-A, OP-TEE core, U-Boot, Linux) only needs
# -ffreestanding, and the glibc sysroot is what Buildroot uses for user space.
# Same version as the OP-TEE reference build (build/toolchain.mk, 4.11.0).
ARG TC_VER=14.3.rel1
ARG TC_NAME=arm-gnu-toolchain-${TC_VER}-x86_64-aarch64-none-linux-gnu
ARG TC_SHA256=ddeaff1ea60d4135acba271b0143d9f5add02b68ab9e9be39672d1965c12e82f
RUN curl -fsSL -o /tmp/tc.tar.xz \
        https://developer.arm.com/-/media/Files/downloads/gnu/${TC_VER}/binrel/${TC_NAME}.tar.xz \
    && echo "${TC_SHA256}  /tmp/tc.tar.xz" | sha256sum -c - \
    && mkdir -p /opt/toolchains/aarch64 \
    && tar -xJf /tmp/tc.tar.xz -C /opt/toolchains/aarch64 --strip-components=1 \
    && rm /tmp/tc.tar.xz

ENV PATH=/opt/toolchains/aarch64/bin:$PATH \
    LANG=C.UTF-8 \
    BSP_CONTAINER=1
