# ─── Stage 1: Build ───────────────────────────────────────────────────────────
FROM ubuntu:22.04 AS builder

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    libopencv-dev \
    libopencv-core-dev \
    libopencv-imgproc-dev \
    libopencv-imgcodecs-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy source
COPY CMakeLists.txt .
COPY src/       src/
COPY apps/      apps/
COPY include/   include/
COPY third_party/ third_party/
COPY tests/     tests/

# Build only rust_server (skip video/batch which may need extra deps)
RUN cmake -B build -S . \
      -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build --target rust_server -j$(nproc)

# ─── Stage 2: Runtime ─────────────────────────────────────────────────────────
FROM ubuntu:22.04 AS runtime

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    libopencv-core4.5d \
    libopencv-imgproc4.5d \
    libopencv-imgcodecs4.5d \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY --from=builder /app/build/rust_server ./rust_server
COPY web/ ./web/
COPY data/ ./data/

# Render sets PORT env var; default to 8765 for local use
ENV PORT=8765

EXPOSE $PORT

CMD ["sh", "-c", "./rust_server --port ${PORT} --host 0.0.0.0 --web web --data data"]
