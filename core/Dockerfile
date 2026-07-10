FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    git \
    ca-certificates \
    pkg-config \
    libgit2-dev \
    python3 \
    python3-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace

COPY . .

RUN cmake -S . -B build -DCOOPER_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release && \
    cmake --build build -j

CMD ["ctest", "--test-dir", "build", "--output-on-failure"]
