FROM ubuntu:24.04

ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        ca-certificates \
        cmake \
        g++-mingw-w64-x86-64 \
        mingw-w64 \
        ninja-build \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /work

COPY docker/mingw64-toolchain.cmake /opt/mingw64-toolchain.cmake

# Default command builds this repository mounted at /work.
ENTRYPOINT ["/bin/bash", "-lc"]
CMD ["cmake -S . -B build-docker -G Ninja -DCMAKE_TOOLCHAIN_FILE=/opt/mingw64-toolchain.cmake -DCMAKE_BUILD_TYPE=Release && cmake --build build-docker --config Release"]
