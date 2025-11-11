#include <boost/ut.hpp>
#include <vector>
#include <complex>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <algorithm>

#include <gnuradio-4.0/polyphase/PfbArbitraryResampler.hpp>

// Include your block header here (or paste the definition before these tests).
// #include <gnuradio-4.0/your_path/PfbArbitraryResampler.hpp>

using namespace boost::ut;
using namespace gr::polyphase;

// -----------------------------------------------------------------------------
// Minimal fake span-likes that your block's processBulk can work with
// -----------------------------------------------------------------------------
template <typename T>
struct FakeInSpan {
    std::vector<T>& buf;
    // track how much this call consumed
    size_t consumed{0};

    size_t size() const noexcept { return buf.size(); }
    const T& operator[](size_t i) const noexcept { return buf[i]; }
    // GR4 API compatibility: consume up to size(); returns ignored value
    auto consume(size_t n) noexcept {
        consumed += std::min(n, buf.size());
        return 0;
    }
};

template <typename T>
struct FakeOutSpan {
    std::vector<T>& buf;     // storage where block writes via operator[]
    size_t capacity;         // how many slots are available this call
    size_t published{0};     // how many outputs were published

    size_t size() const noexcept { return capacity; }
    T& operator[](size_t i) noexcept { return buf[i]; }
    void publish(size_t n) noexcept { published = std::min(n, capacity); }
};

// -----------------------------------------------------------------------------
// Helpers to prep a PFB arbitrary resampler in a predictable ZOH configuration
// -----------------------------------------------------------------------------

// Fill taps with 1 for all phases -> K=1 ZOH behavior (each phase has one tap=1)
template<typename TAPS_T>
static std::vector<TAPS_T> zoh_taps(size_t P) {
    return std::vector<TAPS_T>(P, TAPS_T{1});
}

// Initialize block for a given rate and num_filters (P) with ZOH taps and K=1
template<typename Block>
static void init_block_zoh(Block& blk, double rate, size_t P) {
    blk.rate = rate;
    blk.num_filters = P;
    blk.taps = zoh_taps<typename Block::taps_type>(P); // if you typedef; otherwise use template arg
    // Build polyphase bank (K=1), set harris accumulator
    blk._build_poly(blk.taps, blk.num_filters);
    blk.P_ = blk.num_filters;
    blk.d_inc_ = static_cast<double>(blk.P_) / std::max(1e-12, blk.rate);
    blk.phase_ = 0.0;

    // Optional: ensure history starts primed for K-1 zeros (here K=1 -> nothing to prime)
    // If your HistoryBuffer has an explicit capacity, you can size it here too.
}

// Convenience to push an input chunk and collect outputs in one call
template<typename Block, typename Tin, typename Tout>
static size_t do_call(Block& blk,
                      std::vector<Tin>& invec,
                      std::vector<Tout>& out_storage,
                      size_t out_capacity,
                      size_t& consumed_out)
{
    FakeInSpan<Tin>  inSpan{invec};
    FakeOutSpan<Tout> outSpan{out_storage, out_capacity};

    auto status = blk.processBulk(inSpan, outSpan);
    (void)status;

    consumed_out = inSpan.consumed;
    return outSpan.published;
}

// -----------------------------------------------------------------------------
// TESTS
// -----------------------------------------------------------------------------

// Define your block template alias for brevity (adjust name/namespace as needed):
template<typename T, typename TAPS_T = T>
using PFB = PfbArbitraryResampler<T, TAPS_T>;

const suite PfbArbResamplerTests = [] {

    "rate=1.0, K=1 ZOH, per-sample driving → pass-through"_test = [] {
        PFB<float, float> blk;
        init_block_zoh(blk, /*rate*/ 1.0, /*P*/ 8);

        // Drive one sample per call (streaming-like)
        std::vector<float> outputs(1);
        for (float x : {0.f, 1.f, 2.f, 3.f, 4.f}) {
            std::vector<float> in{ x };
            size_t consumed = 0;
            auto published = do_call(blk, in, outputs, /*out_capacity*/ 1, consumed);

            expect(published == 1_u) << "should publish 1 output at rate=1";
            expect(consumed  == 1_u) << "should consume exactly 1 input at rate=1";
            expect(eq(outputs[0], x)) << "output equals input (ZOH K=1, phase=0 each step)";
        }
    };

    "upsample x2 (rate=2.0), K=1 ZOH → two equal outputs per input"_test = [] {
        PFB<float, float> blk;
        init_block_zoh(blk, /*rate*/ 2.0, /*P*/ 8);

        std::vector<float> out(2);
        for (float x : {2.f, 5.f, -1.f}) {
            std::vector<float> in{ x };
            size_t consumed = 0;
            auto published = do_call(blk, in, out, /*out_capacity*/ 2, consumed);

            // With ZOH and our accumulator, each input produces ~2 outputs per call (adv alternates 0/1)
            expect(published == 2_u) << "should publish 2 outputs for rate=2.0 with out_capacity=2";
            expect(consumed  == 1_u) << "should consume 1 input";

            expect(eq(out[0], x));
            expect(eq(out[1], x));
        }
    };

    "decimate x0.5 (rate=0.5), K=1 ZOH → one output per two inputs, equals 2nd sample"_test = [] {
        PFB<float, float> blk;
        init_block_zoh(blk, /*rate*/ 0.5, /*P*/ 8);

        // Feed two inputs per call; expect one output that reflects the most recent input
        {
            std::vector<float> in{ 10.f, 11.f };
            std::vector<float> out(1);
            size_t consumed = 0;
            auto published = do_call(blk, in, out, /*out_capacity*/ 1, consumed);
            expect(published == 1_u);
            expect(consumed  == 2_u) << "decimation ~2:1 should consume two inputs";
            expect(eq(out[0], 11.f)) << "ZOH with K=1 outputs most-recent sample";
        }
        {
            std::vector<float> in{ 12.f, 13.f };
            std::vector<float> out(1);
            size_t consumed = 0;
            auto published = do_call(blk, in, out, /*out_capacity*/ 1, consumed);
            expect(published == 1_u);
            expect(consumed  == 2_u);
            expect(eq(out[0], 13.f));
        }
    };

    "arbitrary rate ~160/147 (≈1.088), K=1 ZOH → smoke on ramp, counts sane"_test = [] {
        PFB<float, float> blk;
        // approx 48000/44100
        init_block_zoh(blk, /*rate*/ 160.0/147.0, /*P*/ 32);

        // Stream a ramp in chunks; check that published ~ nin*rate (within ±1 per call)
        const std::vector<std::vector<float>> chunks = {
            {0,1,2,3,4,5,6,7},
            {8,9,10,11,12,13,14,15},
            {16,17,18,19},
        };

        for (const auto& chunk : chunks) {
            const size_t nin = chunk.size();
            const size_t exp_nout = static_cast<size_t>(std::floor(nin * (160.0/147.0))) + 1; // allow +1 headroom

            std::vector<float> out(exp_nout, 0.f);
            size_t consumed = 0;
            auto published = do_call(blk, const_cast<std::vector<float>&>(chunk), out, exp_nout, consumed);

            expect(consumed == nin) << "should consume all provided inputs this call";
            // Basic sanity: outputs should be at least floor(nin*rate) and not exceed capacity
            expect(published <= exp_nout);
            expect(published >= static_cast<size_t>(std::floor(nin * (160.0/147.0))));
        }
    };
};

int main() {
    return boost::ut::cfg<boost::ut::override>.run();
}
