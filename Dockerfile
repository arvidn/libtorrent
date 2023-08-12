FROM ubuntu:22.04

RUN apt update && apt install libssl-dev libstdc++-11-dev -y && apt-get clean

COPY libtorrent-rasterbar.so.1.2.18 /lib/x86_64-linux-gnu/libtorrent-rasterbar.so.10
COPY simple_client /

ENTRYPOINT ["/simple_client"]

