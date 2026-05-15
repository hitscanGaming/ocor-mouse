FROM debian:bookworm-slim

RUN apt-get update && apt-get install -y --no-install-recommends \
        ca-certificates \
        curl \
        xz-utils \
        make \
        python3 \
        python3-pip \
        git \
    && rm -rf /var/lib/apt/lists/*

# xPack RISC-V Embedded GCC (works for CH32V; alternative is WCH's own riscv-none-elf-gcc fork)
ARG TOOLCHAIN_VERSION=14.2.0-3
RUN curl -L -o /tmp/riscv-toolchain.tar.gz \
        "https://github.com/xpack-dev-tools/riscv-none-elf-gcc-xpack/releases/download/v${TOOLCHAIN_VERSION}/xpack-riscv-none-elf-gcc-${TOOLCHAIN_VERSION}-linux-x64.tar.gz" \
    && mkdir -p /opt/riscv-toolchain \
    && tar -xzf /tmp/riscv-toolchain.tar.gz -C /opt/riscv-toolchain --strip-components=1 \
    && rm /tmp/riscv-toolchain.tar.gz

ENV PATH=/opt/riscv-toolchain/bin:$PATH

# sanity check the prefix the Makefile uses
RUN riscv-none-elf-gcc --version
