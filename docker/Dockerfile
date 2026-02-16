FROM ubuntu:25.04 AS base

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update -q && apt-get install --no-install-recommends -qy \
    wget gpg ca-certificates software-properties-common \
    bash file locales build-essential sudo cmake git make ninja-build meson \
    cppzmq-dev libfftw3-dev \
    clang-20 clang-tools-20 libc++-20-dev libc++abi-20-dev gdb lldb-20 gcc-15 g++-15 \
    ccache mold \
    python3-dev pybind11-dev libgtest-dev python3-numpy pkg-config \
    libsoapysdr-dev soapysdr-tools soapysdr-module-all \
    uhd-host libuhd-dev usbutils libusb-dev \
    libasound2-dev libpulse-dev libjack-jackd2-dev jackd2 \
    pulseaudio-utils alsa-utils libasound2-plugins libncurses-dev \
    libopengl-dev libimgui-dev libglfw3-dev \
    libcurl4-openssl-dev \
    libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libx11-dev \
    librtaudio-dev libcli11-dev \
    && rm -rf /var/lib/apt/lists/*

# Install ImPlot (Ubuntu does not package it yet).
# Build it against the system imgui package and install to /usr/local.
ARG IMPLOT_REF=v0.16
RUN git clone --depth 1 --branch ${IMPLOT_REF} https://github.com/epezent/implot.git /tmp/implot \
    && cd /tmp/implot \
    && IMGUI_CFLAGS="$(pkg-config --cflags imgui 2>/dev/null || true)" \
    && IMGUI_LIBS="$(pkg-config --libs imgui 2>/dev/null || echo -limgui)" \
    && c++ ${IMGUI_CFLAGS} -O2 -fPIC -c implot.cpp -o implot.o \
    && c++ ${IMGUI_CFLAGS} -O2 -fPIC -c implot_items.cpp -o implot_items.o \
    && c++ -shared -o libimplot.so implot.o implot_items.o ${IMGUI_LIBS} \
    && install -m 0755 libimplot.so /usr/local/lib/libimplot.so \
    && install -m 0644 implot.h implot_internal.h /usr/local/include/ \
    && ldconfig \
    && rm -rf /tmp/implot

# UHD images + udev rules
ENV UHD_IMAGES_DIR=/usr/share/uhd/images
RUN mkdir -p /etc/udev/rules.d/ \
    && uhd_images_downloader --refetch \
    && cp /usr/libexec/uhd/utils/uhd-usrp.rules /etc/udev/rules.d/

RUN locale-gen en_US.UTF-8 \
    && echo 'LANG="en_US.UTF-8"' > /etc/default/locale \
    && if ! id -u ubuntu >/dev/null 2>&1; then useradd -m -g users ubuntu; fi \
    && echo ubuntu ALL=\(root\) NOPASSWD:ALL > /etc/sudoers.d/ubuntu \
    && chmod 0440 /etc/sudoers.d/ubuntu

RUN dpkg-reconfigure --frontend=noninteractive locales \
    && update-locale LANG=en_US.UTF-8 \
    && update-alternatives --install /usr/bin/c++ c++ /usr/bin/g++-15 110 \
    && update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-15 110 \
      --slave /usr/bin/g++ g++ /usr/bin/g++-15 \
      --slave /usr/bin/gcov gcov /usr/bin/gcov-15


FROM base AS gnuradio4-builder

ARG GNURADIO4_REPO=https://github.com/gnuradio/gnuradio4.git
ARG GNURADIO4_COMMIT=68f96c887fec15eb2a211b491a6a229151d6f997

WORKDIR /opt
RUN git clone ${GNURADIO4_REPO} gnuradio4 \
    && cd gnuradio4 \
    && git fetch --depth 1 origin ${GNURADIO4_COMMIT} \
    && git checkout FETCH_HEAD

WORKDIR /opt/gnuradio4
RUN cmake -S . -B build -G Ninja \
      -DCMAKE_INSTALL_PREFIX=/usr/local \
      -DWARNINGS_AS_ERRORS=OFF \
      -DCMAKE_BUILD_TYPE=RelWithAssert \
      -DGR_ENABLE_BLOCK_REGISTRY=OFF \
      -DENABLE_TESTING=OFF \
      -DENABLE_EXAMPLES=OFF \
    && cmake --build build -j"$(nproc)" \
    && DESTDIR=/opt/gr4-install-root cmake --install build \
    && install -D -m 0755 /opt/gnuradio4/build/core/libgnuradio-blocklib-core.so /opt/gr4-install-root/usr/local/lib/libgnuradio-blocklib-core.so \
    && mkdir -p /opt/gr4-install-root/usr/local/include \
    && find /opt/gnuradio4/blocks -type d -name 'gnuradio-4.0' -exec cp -r {} /opt/gr4-install-root/usr/local/include/ \; \
    && find /opt/gnuradio4/meta -type d -name 'gnuradio-4.0' -exec cp -r {} /opt/gr4-install-root/usr/local/include/ \; \
    && find /opt/gnuradio4/algorithm -type d -name 'gnuradio-4.0' -exec cp -r {} /opt/gr4-install-root/usr/local/include/ \;


FROM base AS ci
COPY --from=gnuradio4-builder /opt/gr4-install-root/usr/local/ /usr/local/
RUN set -eux; \
    for pc in /usr/local/lib/pkgconfig/gnuradio4.pc /usr/local/lib/*/pkgconfig/gnuradio4.pc; do \
      if [ -f "${pc}" ]; then \
        sed -i 's|^prefix=/opt/gr4-install$|prefix=/usr/local|' "${pc}"; \
        sed -i 's|/opt/gr4-install|/usr/local|g' "${pc}"; \
      fi; \
    done; \
    ldconfig

USER ubuntu
WORKDIR /workspaces/gr4-incubator
ENTRYPOINT ["/bin/bash", "-l", "-c"]


FROM ci AS dev
WORKDIR /code
