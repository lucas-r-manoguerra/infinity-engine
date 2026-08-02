# Infinity Engine - hermetic dev toolchain (ADR-025).
#
# Pinned: Ubuntu 24.04 (noble), GCC 14, Clang 20, CMake 3.28, Ninja, ccache.
# Mirrors the CI clang-20 toolchain (see .github/workflows/ci.yml) so local,
# devcontainer and CI builds match. Ubuntu 24.04 apt does NOT ship clang-20,
# so the LLVM 20 apt repo (apt.llvm.org) is added below.

FROM ubuntu:24.04

ARG DEBIAN_FRONTEND=noninteractive

# X11 backend deps (F3.1): libx11-dev is the native window API on Linux (peer
# of Win32 on Windows, not a vendored dependency, ADR-061); xvfb+xauth let
# tests open a real window headlessly via xvfb-run.
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        ca-certificates \
        ccache \
        cmake \
        git \
        g++-14 \
        libx11-dev \
        ninja-build \
        software-properties-common \
        wget \
        xauth \
        xvfb \
    && rm -rf /var/lib/apt/lists/*

# LLVM 20 for noble (Ubuntu 24.04): not in the distro archive, so add the
# LLVM snapshot repo (same 20.x family CI installs on Linux/macOS).
RUN wget -qO- https://apt.llvm.org/llvm-snapshot.gpg.key \
        | tee /etc/apt/trusted.gpg.d/apt.llvm.org.asc \
    && add-apt-repository "deb http://apt.llvm.org/noble/ llvm-toolchain-noble-20 main" \
    && apt-get update && apt-get install -y --no-install-recommends \
        clang-20 \
        clangd-20 \
        clang-format-20 \
        clang-tidy-20 \
        libclang-rt-20-dev \
    && rm -rf /var/lib/apt/lists/*

# Pin the default tools to the audited versions (ADR-025).
RUN update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-14 100 \
    && update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-14 100 \
    && update-alternatives --install /usr/bin/clang clang /usr/bin/clang-20 100 \
    && update-alternatives --install /usr/bin/clang++ clang++ /usr/bin/clang++-20 100 \
    && update-alternatives --install /usr/bin/clang-format clang-format /usr/bin/clang-format-20 100 \
    && update-alternatives --install /usr/bin/clang-tidy clang-tidy /usr/bin/clang-tidy-20 100

WORKDIR /workspace
