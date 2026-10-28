# SPDX-License-Identifier: GPL-3.0-only
# opentalk-server in a container (Linux). See docs/server-administration/hosting.md.
#
#   docker build -t opentalk-server .
#   docker run -d --name opentalk --restart unless-stopped \
#       -p 10350:10350/tcp -p 10350:10350/udp -v opentalk-data:/data opentalk-server
#
# Everything the server keeps (database, certificate, backups) is in the /data volume.
# Extra options go after the image name, for example `opentalk-server --allow-guests`.

FROM ubuntu:24.04 AS build
RUN apt-get update \
 && apt-get install -y --no-install-recommends cmake ninja-build g++ pkg-config \
      qt6-base-dev libqt6sql6-sqlite libsodium-dev libssl-dev libopus-dev libspeexdsp-dev \
 && rm -rf /var/lib/apt/lists/*
WORKDIR /src
COPY . .
RUN cmake -S . -B /build -G Ninja -DCMAKE_BUILD_TYPE=Release -DOPENTALK_SERVER_ONLY=ON \
 && cmake --build /build \
 && install -D -m 755 /build/src/server/opentalk-server /out/opentalk-server \
 && install -D -m 755 /build/examples/opentalk-ping/opentalk-ping /out/opentalk-ping

FROM ubuntu:24.04
RUN apt-get update \
 && apt-get install -y --no-install-recommends \
      libqt6core6t64 libqt6network6t64 libqt6sql6t64 libqt6sql6-sqlite \
      libsodium23 libssl3t64 libopus0 libspeexdsp1 ca-certificates \
 && rm -rf /var/lib/apt/lists/* \
 && useradd --system --home-dir /data --shell /usr/sbin/nologin opentalk \
 && mkdir -p /data && chown opentalk:opentalk /data
COPY --from=build /out/ /usr/local/bin/
COPY LICENSE /usr/share/doc/opentalk/LICENSE
USER opentalk
VOLUME /data
EXPOSE 10350/tcp 10350/udp
# `docker stop` sends SIGTERM, which the server handles as a graceful shutdown.
ENTRYPOINT ["opentalk-server", "--data-dir", "/data"]
