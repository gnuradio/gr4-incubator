// bench_DCBlocker.cpp — throughput benchmark for DCBlocker
#include <gnuradio-4.0/basic/DCBlocker.hpp>
#include <chrono>
#include <complex>
#include <cstdio>
#include <cstring>
#include <span>
#include <vector>

static double throughput_mss(double sec, std::size_t n) {
    return static_cast<double>(n) / sec / 1e6;
}

template<typename T>
static void do_not_optimize(const T& v) {
    volatile const void* p = &v;
    (void)p;
}

static bool g_filter_active = false;
static const char* g_filter  = nullptr;

static bool should_run(const char* name) {
    if (!g_filter_active) { return true; }
    return std::strstr(name, g_filter) != nullptr;
}

static void bench_DCBlocker() {
    if (!should_run("DCBlocker")) { return; }
    constexpr std::size_t N = 1u << 20u;
    gr::incubator::basic::DCBlocker<float> blk;
    blk.start();
    std::vector<std::complex<float>> in(N, {1.f, 0.5f});
    std::vector<std::complex<float>> out(N);
    auto t0 = std::chrono::steady_clock::now();
    for (std::size_t i = 0; i < N; ++i) { out[i] = blk.processOne(in[i]); }
    auto t1 = std::chrono::steady_clock::now();
    do_not_optimize(out[N / 2]);
    std::printf("DCBlocker,default,%zu,%.2f\n", N,
                throughput_mss(std::chrono::duration<double>(t1 - t0).count(), N));
}

int main(int argc, char* argv[]) {
    if (argc > 1) { g_filter_active = true; g_filter = argv[1]; }
    std::puts("block,config,N,throughput_MSas");
    bench_DCBlocker();
    return 0;
}
