# Multi-stage build: compile the headless server, ship a minimal runtime image.
FROM ubuntu:24.04 AS builder

RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY . .

RUN cmake -S . -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DBUILD_SERVER=ON \
    -DBUILD_CLIENT=OFF \
    && cmake --build build

FROM ubuntu:24.04 AS runtime

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY --from=builder /app/build/game_server /app/game_server

# Railway injects PORT (e.g. 8080). TCP proxy forwards to TCP_PORT.
ENV TCP_PORT=7777

EXPOSE 7777

CMD ["/app/game_server"]
