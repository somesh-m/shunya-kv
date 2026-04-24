# -------- Stage 1: Build Seastar --------
FROM ubuntu:24.04 AS seastar-build
ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    git curl python3 python3-pip python3-venv software-properties-common ca-certificates && \
    add-apt-repository -y universe && apt-get update

RUN git clone --recurse-submodules --depth 1 https://github.com/scylladb/seastar.git /opt/seastar && \
    cd /opt/seastar && ./install-dependencies.sh

RUN python3 -m venv /opt/py && /opt/py/bin/pip install --no-cache-dir -U pip setuptools wheel
ENV PATH="/opt/py/bin:${PATH}"

RUN cd /opt/seastar && \
    ./configure.py --mode=release --prefix=/usr/local --enable-dpdk && \
    ninja -C build/release install

# -------- Stage 2: Build ShunyaKV --------
FROM ubuntu:24.04 AS shunyakv-build
ENV DEBIAN_FRONTEND=noninteractive

COPY --from=seastar-build /usr/local/ /usr/local/

RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential cmake ninja-build pkg-config git \
    libboost-all-dev libabsl-dev libc-ares-dev libdpdk-dev \
    libfmt-dev libgnutls28-dev libhwloc-dev libnuma-dev \
    libprotobuf-dev libssl-dev libsctp-dev liburing-dev \
    libyaml-cpp-dev protobuf-compiler \
    liblz4-dev libcryptsetup-dev libxml2-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app
COPY . .
RUN rm -rf build

RUN cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DENABLE_TESTS=OFF && \
    cmake --build build -j$(nproc)

# -------- Stage 3: Final Runtime Image --------
FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive

# 1. Install base dependencies + libfmt-dev (to get the system paths ready)
RUN apt-get update && apt-get install -y --no-install-recommends \
    libatomic1 libboost-program-options1.83.0 libc-ares2 \
    libaio1t64 libgnutls30 libhwloc15 libnuma1 libpciaccess0 \
    libssl3 liburing2 libyaml-cpp0.8 libprotobuf32 libdpdk-dev \
    libfmt-dev libabsl20220623t64 && \
    rm -rf /var/lib/apt/lists/*

# 2. Copy everything from build stages
COPY --from=shunyakv-build /usr/local/ /usr/local/
COPY --from=shunyakv-build /app/build/shunya_store /usr/local/bin/shunya_store

# 3. THE FIX: Force the linker to see the Seastar-built fmt
# We symlink the version Seastar likely built (.9) to the standard path
RUN ln -sf /usr/local/lib/x86_64-linux-gnu/libfmt.so.9* /usr/local/lib/libfmt.so.9 && \
    echo "/usr/local/lib" > /etc/ld.so.conf.d/local.conf && \
    echo "/usr/local/lib/x86_64-linux-gnu" >> /etc/ld.so.conf.d/local.conf && \
    echo "/usr/lib/x86_64-linux-gnu" >> /etc/ld.so.conf.d/local.conf && \
    ldconfig

EXPOSE 60111
ENTRYPOINT ["/usr/local/bin/shunya_store"]
