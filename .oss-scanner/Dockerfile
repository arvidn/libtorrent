# syntax=docker/dockerfile:1
#
# Build from the repository root:
#
#   docker build -f .oss-scanner/Dockerfile .

FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

# Boost must be >= 1.75 (the Jamfile refuses to build otherwise, and that's
# also the floor for Boost.JSON, used by webtorrent, which defaults to on).
# jammy's boost (1.74) doesn't clear that bar; noble's (1.83) does.
#
# cmake is needed to build webtorrent's vendored usrsctp/libjuice.
# g++-14 is used via user-config.jam below, ahead of noble's default g++-13.
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        g++-14 \
        git \
        ca-certificates \
        cmake \
        libboost-dev \
        libboost-system-dev \
        libboost-json-dev \
        libboost-tools-dev \
        libssl-dev \
    && rm -rf /var/lib/apt/lists/*

RUN echo "using gcc : 14 : g++-14 ;" > "$HOME/user-config.jam"

WORKDIR /src

# Only what the build needs, not the whole (often huge) working tree.
# --parents keeps each pattern's directory prefix instead of flattening
# everything into one folder (e.g. test/*.cpp -> ./test/*.cpp, not ./*.cpp).
COPY Jamfile Jamroot.jam project-config.jam .gitmodules ./
COPY .git ./.git
COPY include ./include
COPY src ./src
COPY --parents \
     test/*.cpp test/*.hpp test/Jamfile \
     test/*.gz test/*.py test/*.xml test/*.txt \
     test/mutable_test_torrents test/ssl test/test_torrents \
     ./
COPY --parents simulation/*.cpp simulation/*.hpp simulation/Jamfile ./

# try_signal is always linked; the rest (e.g. libdatachannel) are pulled in
# too so other build configurations work without a rebuild of this image.
RUN git submodule update --init --recursive

RUN b2
RUN (cd simulation; b2)
RUN (cd test; b2 testing.execute=off)
