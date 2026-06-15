FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

# Install GCC 15, CMake, and build essentials
RUN apt-get update && apt-get install -y \
    software-properties-common \
    && add-apt-repository ppa:ubuntu-toolchain-r/test -y \
    && apt-get update && apt-get install -y \
    gcc-15 \
    g++-15 \
    cmake \
    make \
    ninja-build \
    gdb \
    git \
    && rm -rf /var/lib/apt/lists/*

# Set GCC 15 as the system default
RUN update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-15 100 \
    && update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-15 100

WORKDIR /workspace