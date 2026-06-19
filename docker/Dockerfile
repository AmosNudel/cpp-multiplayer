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

# Railway TCP proxy uses TCP_PORT / RAILWAY_TCP_APPLICATION_PORT (7777).
# WebSocket listens on Railway's PORT (injected at runtime) or WS_PORT fallback.
ENV TCP_PORT=7777

EXPOSE 7777 8080

CMD ["/app/game_server"]
