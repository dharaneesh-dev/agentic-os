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
    python3-pip \
    python3-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /workspace

COPY core/ core/
RUN cmake -S core -B core/build -DCOOPER_BUILD_TESTS=OFF -DCMAKE_BUILD_TYPE=Release && \
    cmake --build core/build -j

COPY orchestrator/requirements.txt orchestrator/requirements.txt
RUN python3 -m pip install --break-system-packages --no-cache-dir -r orchestrator/requirements.txt

COPY orchestrator/ orchestrator/
RUN python3 -m pip install --break-system-packages --no-cache-dir --no-deps ./orchestrator

WORKDIR /workspace/orchestrator
ENV PYTHONUNBUFFERED=1

ENTRYPOINT ["python3", "-m", "cooper_orchestrator.cli"]
CMD ["--help"]
