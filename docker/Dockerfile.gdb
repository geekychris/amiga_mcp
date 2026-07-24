# Minimal gdb-multiarch image for debugging QEMU's PPC (sam460ex) target.
#
# We don't have a native ppc-amigaos-gdb (walkero's image doesn't ship
# one), and building one from source is overkill. gdb-multiarch knows
# PowerPC well enough for CPU-level work — registers, memory, break/step,
# and disassembly. It won't understand OS4-specific things like the
# task list, but that's what the bridge is for.
FROM ubuntu:24.04
RUN apt-get update -qq && \
    DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
        gdb-multiarch \
    && apt-get clean && rm -rf /var/lib/apt/lists/*
CMD ["gdb-multiarch"]
