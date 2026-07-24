# Layered build image: walkero OS4 PPC toolchain + Python 3.12 as the
# CPython build-python. CPython's configure requires PYTHON_FOR_BUILD
# to match the target major.minor for freezing modules.
FROM walkero/amigagccondocker:os4-gcc11-arm64

# Install Python 3.12 via deadsnakes so we get a proper venv-capable
# interpreter separate from the system 3.14.
RUN apt-get update && \
    apt-get install -y --no-install-recommends software-properties-common && \
    add-apt-repository -y ppa:deadsnakes/ppa && \
    apt-get update && \
    apt-get install -y --no-install-recommends \
        python3.12 python3.12-venv python3.12-dev \
        pkg-config make && \
    apt-get clean && \
    rm -rf /var/lib/apt/lists/*

# Verify.
RUN python3.12 --version
