# Stage 1: Build environment with modern GCC and CMake
FROM debian:bookworm AS builder

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    ninja-build \
    git \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src

# Copy CMake configuration and source tree
COPY CMakeLists.txt ./
COPY include/ include/
COPY src/ src/
COPY tools/ tools/
COPY tests/ tests/
COPY benchmarks/ benchmarks/

# Configure and compile in Release mode with aggressive optimizations (-O3, LTO)
RUN cmake -B build -S . -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DORDERBOOK_BUILD_BENCHMARKS=ON \
    -DORDERBOOK_BUILD_TESTS=ON \
    && cmake --build build --target market_replay orderbook_benchmarks -j$(nproc)

# Stage 2: Ultra-minimal runtime image
FROM debian:bookworm-slim AS runtime

RUN apt-get update && apt-get install -y --no-install-recommends \
    libstdc++6 \
    ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy compiled binaries from builder stage
COPY --from=builder /src/build/market_replay /app/market_replay
COPY --from=builder /src/build/benchmarks/orderbook_benchmarks /app/orderbook_benchmarks

# Default entrypoint runs the synthetic market replay latency profiler
ENTRYPOINT ["/app/market_replay"]
CMD ["--ops", "5000000"]
