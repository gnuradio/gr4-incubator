FROM ubuntu:25.04
LABEL authors="akrimm"

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update -q  && apt-get install --no-install-recommends -qy wget gpg ca-certificates software-properties-common \
    bash file locales build-essential sudo cmake git make ninja-build cppzmq-dev libfftw3-dev meson ninja-build \
    clang-18 clang-tools-18 clang-20 clang-tools-20 libc++-20-dev libc++abi-20-dev gdb lldb-20 gcc-14 g++-14 gcc-15 g++-15 \
    ccache mold \
    python3-dev pybind11-dev libgtest-dev python3-numpy pkg-config \
    && rm -rf /var/lib/apt/lists/*


RUN locale-gen en_US.UTF-8 && echo 'LANG="en_US.UTF-8"'>/etc/default/locale \
    && useradd -m -g users user \
    && echo user ALL=\(root\) NOPASSWD:ALL > /etc/sudoers.d/user && chmod 0440 /etc/sudoers.d/user 

RUN dpkg-reconfigure --frontend=noninteractive locales && update-locale LANG=en_US.UTF-8 \
    && update-alternatives --install /usr/bin/c++ c++ /usr/bin/g++-14 110 \
    && update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-14 110 --slave /usr/bin/g++ g++ /usr/bin/g++-14 --slave /usr/bin/gcov gcov /usr/bin/gcov-14


# # Clone and build PMTV
# RUN apt-get install --no-install-recommends -qy meson python3-dev pybind11-dev
# WORKDIR /opt
# RUN git clone https://github.com/gnuradio/pmt.git
# WORKDIR /opt/pmt
# RUN meson setup build -Denable_python=false -Denable_testing=false  && cd build && ninja && ninja install

# Clone and build GNURADIO4
WORKDIR /opt
RUN git clone https://github.com/gnuradio/gnuradio4.git
WORKDIR /opt/gnuradio4
RUN mkdir build && cd build && \
    #cmake -G Ninja -DCMAKE_BUILD_TYPE=RelWithAssert -DGR_ENABLE_BLOCK_REGISTRY=OFF -DENABLE_TESTING=OFF -DENABLE_EXAMPLES=OFF .. && \
    cmake -G Ninja -DCMAKE_BUILD_TYPE=RelWithAssert -DGR_ENABLE_BLOCK_REGISTRY=ON -DENABLE_TESTING=OFF -DENABLE_EXAMPLES=OFF .. && \
    ninja -j8 && ninja install && cp /opt/gnuradio4/build/core/libgnuradio-blocklib-core.so /usr/local/lib/ \
    && find /opt/gnuradio4/blocks -type d -name 'gnuradio-4.0' -exec cp -r {} /usr/local/include/ \;


# Copy your source tree (assume it lives next to Dockerfile)
COPY . /opt/gr4-incubator

# Define build argument with default (empty means don't build by default)
ARG BUILD_ACTIVE_PROJECT=
ENV BUILD_ACTIVE_PROJECT=${BUILD_ACTIVE_PROJECT}

# Conditionally build and install if set
RUN if [ "$BUILD_ACTIVE_PROJECT" = "1" ]; then \
      cd /opt/gr4-incubator && meson setup build --libdir=/usr/local/lib && cd build && ninja && ninja install && rm -rf /opt/gr4-incubator; \
    else \
      echo "Skipping active project build/install"; \
    fi

RUN 

RUN echo "ubuntu ALL=(ALL) NOPASSWD:ALL" >> /etc/sudoers.d/ubuntu
USER ubuntu
WORKDIR /code
ENTRYPOINT ["/bin/bash", "-l", "-c"]


