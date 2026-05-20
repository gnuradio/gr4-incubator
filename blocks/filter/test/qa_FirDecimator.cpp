#include <boost/ut.hpp>
#include <gnuradio-4.0/filter/FirDecimator.hpp>

#include <complex>
#include <numbers>
#include <numeric>
#include <stdexcept>
#include <vector>

using namespace boost::ut;

const boost::ut::suite<"FirDecimator"> firDecimatorTests = [] {
    "custom taps decimate complex stream"_test = [] {
        gr::incubator::filter::FirDecimator<std::complex<float>> decimator;
        decimator.decim = 2U;
        decimator.taps  = gr::Tensor<float>{1.F};
        decimator.start();

        const std::vector<std::complex<float>> input{{1.F, 0.F}, {2.F, 0.F}, {3.F, 0.F}, {4.F, 0.F}, {5.F, 0.F}, {6.F, 0.F}};
        std::vector<std::complex<float>>       output(input.size() / 2U);

        expect(decimator.processBulk(input, output) == gr::work::Status::OK);
        expect(approx(output[0].real(), 1.F, 1e-6F));
        expect(approx(output[1].real(), 3.F, 1e-6F));
        expect(approx(output[2].real(), 5.F, 1e-6F));
    };

    "designed lowpass creates taps"_test = [] {
        gr::incubator::filter::FirDecimator<std::complex<float>> decimator;
        decimator.decim            = 5U;
        decimator.sample_rate      = 2000000.F;
        decimator.f_low            = 100000.F;
        decimator.transition_width = 50000.F;
        decimator.attenuation_db   = 60.F;
        decimator.start();

        expect(gt(decimator._taps.size(), 1UZ));
        expect(eq(decimator.input_chunk_size, 5UZ));
    };

    "designed lowpass emits finite nonzero samples"_test = [] {
        gr::incubator::filter::FirDecimator<std::complex<float>> decimator;
        decimator.decim            = 5U;
        decimator.sample_rate      = 2000000.F;
        decimator.f_low            = 120000.F;
        decimator.transition_width = 75000.F;
        decimator.attenuation_db   = 40.F;
        decimator.start();

        std::vector<std::complex<float>> input(500U, {1.F, 0.F});
        std::vector<std::complex<float>> output(input.size() / decimator.decim);

        expect(decimator.processBulk(input, output) == gr::work::Status::OK);
        const auto maxMagnitude = std::ranges::max(output | std::views::transform([](const auto& sample) { return std::abs(sample); }));
        expect(gt(maxMagnitude, 0.1F));
        expect(std::ranges::all_of(output, [](const auto& sample) { return std::isfinite(sample.real()) && std::isfinite(sample.imag()); }));
    };

    "designed lowpass passes passband tone"_test = [] {
        gr::incubator::filter::FirDecimator<std::complex<float>> decimator;
        decimator.decim            = 5U;
        decimator.sample_rate      = 2000000.F;
        decimator.f_low            = 120000.F;
        decimator.transition_width = 75000.F;
        decimator.attenuation_db   = 40.F;
        decimator.start();

        constexpr float toneFrequency = 50000.F;
        std::vector<std::complex<float>> input(2000U);
        for (std::size_t n = 0UZ; n < input.size(); ++n) {
            const float phase = 2.F * std::numbers::pi_v<float> * toneFrequency * static_cast<float>(n) / decimator.sample_rate;
            input[n] = std::polar(1.F, phase);
        }
        std::vector<std::complex<float>> output(input.size() / decimator.decim);

        expect(decimator.processBulk(input, output) == gr::work::Status::OK);

        const auto steady = std::span<const std::complex<float>>(output).subspan(std::min<std::size_t>(100UZ, output.size()));
        const float averageMagnitude = std::transform_reduce(steady.begin(), steady.end(), 0.F, std::plus<>{}, [](const auto& sample) { return std::abs(sample); }) / static_cast<float>(steady.size());
        expect(gt(averageMagnitude, 0.7F));
    };

    "designed lowpass rejects stopband tone"_test = [] {
        gr::incubator::filter::FirDecimator<std::complex<float>> decimator;
        decimator.decim            = 5U;
        decimator.sample_rate      = 2000000.F;
        decimator.f_low            = 120000.F;
        decimator.transition_width = 75000.F;
        decimator.attenuation_db   = 40.F;
        decimator.start();

        constexpr float toneFrequency = 500000.F;
        std::vector<std::complex<float>> input(2000U);
        for (std::size_t n = 0UZ; n < input.size(); ++n) {
            const float phase = 2.F * std::numbers::pi_v<float> * toneFrequency * static_cast<float>(n) / decimator.sample_rate;
            input[n] = std::polar(1.F, phase);
        }
        std::vector<std::complex<float>> output(input.size() / decimator.decim);

        expect(decimator.processBulk(input, output) == gr::work::Status::OK);

        const auto steady = std::span<const std::complex<float>>(output).subspan(std::min<std::size_t>(100UZ, output.size()));
        const float averageMagnitude = std::transform_reduce(steady.begin(), steady.end(), 0.F, std::plus<>{}, [](const auto& sample) { return std::abs(sample); }) / static_cast<float>(steady.size());
        expect(lt(averageMagnitude, 0.1F));
    };

    "runtime sample rate propagation does not redesign taps"_test = [] {
        gr::incubator::filter::FirDecimator<std::complex<float>> decimator;
        decimator.decim            = 5U;
        decimator.sample_rate      = 2000000.F;
        decimator.f_low            = 120000.F;
        decimator.transition_width = 75000.F;
        decimator.attenuation_db   = 40.F;
        decimator.start();

        const auto originalTapCount = decimator._taps.size();
        const auto originalTapSum = std::accumulate(decimator._taps.cbegin(), decimator._taps.cend(), 0.F);
        const auto originalDesignSampleRate = decimator._designSampleRate;

        decimator.sample_rate = 400000.F;
        decimator.settingsChanged({}, gr::property_map{{"sample_rate", gr::pmt::Value(400000.F)}});

        expect(eq(decimator._taps.size(), originalTapCount));
        expect(approx(std::accumulate(decimator._taps.cbegin(), decimator._taps.cend(), 0.F), originalTapSum, 1e-6F));
        expect(eq(decimator._designSampleRate, originalDesignSampleRate));
    };

    "batched sample rate setting redesigns taps"_test = [] {
        gr::incubator::filter::FirDecimator<std::complex<float>> decimator;
        decimator.decim            = 5U;
        decimator.sample_rate      = 2000000.F;
        decimator.f_low            = 120000.F;
        decimator.transition_width = 75000.F;
        decimator.attenuation_db   = 40.F;
        decimator.start();

        const auto originalTapCount = decimator._taps.size();

        decimator.sample_rate = 1000000.F;
        decimator.settingsChanged({}, gr::property_map{
            {"sample_rate", gr::pmt::Value(1000000.F)},
            {"transition_width", gr::pmt::Value(75000.F)},
        });

        expect(eq(decimator._designSampleRate, 1000000.F));
        expect(neq(decimator._taps.size(), originalTapCount));
    };

    "custom taps filter only retained decimated samples"_test = [] {
        gr::incubator::filter::FirDecimator<float> decimator;
        decimator.decim = 2U;
        decimator.taps  = gr::Tensor<float>{0.5F, 0.5F};
        decimator.start();

        const std::vector<float> input{2.F, 4.F, 6.F, 8.F};
        std::vector<float>       output(input.size() / 2U);

        expect(decimator.processBulk(input, output) == gr::work::Status::OK);
        expect(approx(output[0], 1.F, 1e-6F));
        expect(approx(output[1], 5.F, 1e-6F));
    };

    "decimation phase is preserved across bulk calls"_test = [] {
        gr::incubator::filter::FirDecimator<float> decimator;
        decimator.decim = 3U;
        decimator.taps  = gr::Tensor<float>{1.F};
        decimator.start();

        const std::vector<float> first{1.F, 2.F};
        std::vector<float>       firstOut(1U);
        expect(decimator.processBulk(first, firstOut) == gr::work::Status::OK);
        expect(approx(firstOut[0], 1.F, 1e-6F));

        const std::vector<float> second{3.F, 4.F, 5.F, 6.F};
        std::vector<float>       secondOut(2U);
        expect(decimator.processBulk(second, secondOut) == gr::work::Status::OK);
        expect(approx(secondOut[0], 4.F, 1e-6F));
    };

    "runtime tap update clears filter history"_test = [] {
        gr::incubator::filter::FirDecimator<float> decimator;
        decimator.decim = 1U;
        decimator.taps  = gr::Tensor<float>{1.F};
        decimator.start();

        const std::vector<float> first{10.F};
        std::vector<float>       firstOut(1U);
        expect(decimator.processBulk(first, firstOut) == gr::work::Status::OK);
        expect(approx(firstOut[0], 10.F, 1e-6F));

        decimator.taps = gr::Tensor<float>{0.F, 1.F};
        decimator.settingsChanged({}, gr::property_map{{"taps", gr::pmt::Value(true)}});

        const std::vector<float> second{2.F};
        std::vector<float>       secondOut(1U);
        expect(decimator.processBulk(second, secondOut) == gr::work::Status::OK);
        expect(approx(secondOut[0], 0.F, 1e-6F));
    };

    "runtime decimation factor rejects zero"_test = [] {
        gr::incubator::filter::FirDecimator<float> decimator;
        decimator.decim = 2U;
        decimator.taps  = gr::Tensor<float>{1.F};
        decimator.start();

        decimator.decim = 0U;
        expect(throws<std::invalid_argument>([&] { decimator.settingsChanged({}, gr::property_map{{"decim", gr::pmt::Value(0U)}}); }));
    };
};

int main() {}
