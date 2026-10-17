# Cópia de project8-sw tools/switch/Dockerfile (3393f26): devkitA64 + libnx + portlibs, clang 19 e glslang.
FROM devkitpro/devkita64:latest
RUN apt-get update && apt-get install -y --no-install-recommends clang-19 glslang-tools \
    && rm -rf /var/lib/apt/lists/*
