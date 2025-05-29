FROM ubuntu:25.04
LABEL authors="akrimm"

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update -q 

RUN apt-get install --no-install-recommends -qy wget gpg ca-certificates software-properties-common bash file locales build-essential sudo cmake git make ninja-build clang-18 clang-tools-18 clang-20 clang-tools-20 libc++-20-dev libc++abi-20-dev gdb lldb-20 gcc-14 g++-14 gcc-15 g++-15 ccache mold \
    && locale-gen en_US.UTF-8 && echo 'LANG="en_US.UTF-8"'>/etc/default/locale \
    # && useradd -m -g users user \
    # && echo user ALL=\(root\) NOPASSWD:ALL > /etc/sudoers.d/user && chmod 0440 /etc/sudoers.d/user \
    && dpkg-reconfigure --frontend=noninteractive locales && update-locale LANG=en_US.UTF-8 \
    && update-alternatives --install /usr/bin/c++ c++ /usr/bin/clang++-18 110 \
    && update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-14 110 --slave /usr/bin/g++ g++ /usr/bin/g++-14 --slave /usr/bin/gcov gcov /usr/bin/gcov-14

# Clone and build PMTV
RUN apt-get install --no-install-recommends -qy meson python3-dev pybind11-dev
WORKDIR /opt
RUN git clone https://github.com/gnuradio/pmt.git
WORKDIR /opt/pmt
RUN meson setup build -Denable_python=false -Denable_testing=false  && cd build && ninja && ninja install

# Clone and build GNURADIO4
WORKDIR /opt
RUN git clone https://github.com/gnuradio/gnuradio4.git
WORKDIR /opt/gnuradio4
RUN mkdir build && cd build && \
    cmake -G Ninja -DCMAKE_BUILD_TYPE=RelWithAssert -DGR_ENABLE_BLOCK_REGISTRY=OFF -DENABLE_TESTING=OFF -DENABLE_EXAMPLES=OFF .. && \
    ninja && ninja install

# This is a complete hack until GR4.0 repo installs to right places
RUN mkdir /usr/local/include/gnuradio-4.0 \
    && cp -r /opt/gnuradio4/core/include/gnuradio-4.0 /usr/include/ \
    && cp -r /opt/gnuradio4/meta/include/gnuradio-4.0 /usr/local/include/ \
    && cp /opt/gnuradio4/third_party/magic_enum/*.hpp /usr/local/include/ \
    && cp /opt/gnuradio4/build/core/include/gnuradio-4.0/config.hpp /usr/local/include/gnuradio-4.0/

# Add additional dependencies for this OOT module
RUN apt-get install --no-install-recommends -qy cppzmq-dev

# USER user
# ENTRYPOINT ["/bin/bash", "-l", "-c"]


