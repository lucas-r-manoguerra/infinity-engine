# Infinity Engine - hermetic dev toolchain (ADR-025).
#
# Pinned: Ubuntu 24.04, GCC 14, Clang 18, CMake 3.28, Ninja, ccache.
# Mirrors the CI toolchain so local, devcontainer and CI builds match.

FROM ubuntu:24.04

ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        ca-certificates \
        ccache \
        clang-18 \
        clangd-18 \
        clang-format-18 \
        clang-tidy-18 \
        cmake \
        git \
        g++-14 \
        ninja-build \
    && rm -rf /var/lib/apt/lists/*

# Pin the default tools to the audited versions (ADR-025).
RUN update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-14 100 \
    && update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-14 100 \
    && update-alternatives --install /usr/bin/clang clang /usr/bin/clang-18 100 \
    && update-alternatives --install /usr/bin/clang++ clang++ /usr/bin/clang++-18 100 \
    && update-alternatives --install /usr/bin/clang-format clang-format /usr/bin/clang-format-18 100 \
    && update-alternatives --install /usr/bin/clang-tidy clang-tidy /usr/bin/clang-tidy-18 100

WORKDIR /workspace
