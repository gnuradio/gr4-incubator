// bench_MovingAverage.cpp — throughput benchmark for MovingAverage
#include <gnuradio-4.0/basic/MovingAverage.hpp>
#include <chrono>
#include <cstdio>
#include <span>
#include <vector>

static double throughput_mss(double seconds, std::size_t n) {
    return static_cast<double>(n) / seconds / 1e6;
}

template<typename T>
static void do_not_optimize(const T& v) {
    volatile const void* p = &v;
    (void)p;
}

static void bench_MovingAverage() {
    constexpr std::size_t N = 1u << 20u;
    std::vector<std::complex<float>> in(N, {1.f, 0.f});
    std::vector<std::complex<float>> out(N);
    for (uint32_t w : {4u, 16u, 64u, 256u}) {
        gr::incubator::basic::MovingAverage<float> blk;
        blk.window_size = w;
        blk.start();
        auto t0 = std::chrono::steady_clock::now();
        for (std::size_t i = 0; i < N; ++i) { out[i] = blk.processOne(in[i]); }
        auto t1 = std::chrono::steady_clock::now();
        do_not_optimize(out[N / 2]);
        std::printf("MovingAverage,window=%u,%zu,%.2f\n", w, N,
                    throughput_mss(std::chrono::duration<double>(t1 - t0).count(), N));
    }
}

int main() {
    std::puts("block,config,N,throughput_MSas");
    bench_MovingAverage();
    return 0;
}
