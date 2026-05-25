FROM mcr.microsoft.com/devcontainers/base:ubuntu-24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        build-essential \
        ccache \
        clang \
        clang-format \
        clang-tidy \
        cmake \
        curl \
        doxygen \
        g++ \
        gdb \
        git \
        git-lfs \
        graphviz \
        lld \
        lldb \
        ninja-build \
        pkg-config \
        python3 \
        python3-pip \
        valgrind \
    && rm -rf /var/lib/apt/lists/*
