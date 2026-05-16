#pragma once

#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/BlockRegistry.hpp>
#include <gnuradio-4.0/Tensor.hpp>
#include <gnuradio-4.0/Value.hpp>

namespace gr::incubator::basic {

// StreamToPmt: packs a flat sample stream into fixed-size PMT tensor values.
//
// Collects exactly `packet_size` input samples per output item and wraps them
// as a gr::Tensor<T> inside a gr::pmt::Value.  The block uses Resampling<> so
// the scheduler guarantees that input_chunk_size samples are available before
// processBulk is called.
//
// Use cases:
//   - Bridging a streaming DSP graph to a PMT-based message bus or logger
//   - Creating fixed-size analysis windows (FFT, correlation) as PMT messages
//   - Sending blocks of samples over ZeroMQ or other message transports that
//     expect PMT values
//
// Changing `packet_size` at runtime triggers settingsChanged(), which resizes
// the internal buffer and updates input_chunk_size accordingly.
//
// Signal chain placement:
//   [sample source] → StreamToPmt → [PMT consumer / ZmqPushSink]

template<typename T>
struct StreamToPmt : gr::Block<StreamToPmt<T>, Resampling<>> {
    using Description = Doc<
        "Converts a stream of samples to uniform vector PMTs of a specified packet size. "
        "Collects exactly packet_size input samples per output item and wraps them as a "
        "gr::Tensor<T> inside a gr::pmt::Value. Uses Resampling<> so the scheduler guarantees "
        "input_chunk_size samples are available before processBulk is called. "
        "Use cases: bridging a streaming DSP graph to a PMT-based message bus or logger; "
        "creating fixed-size analysis windows (FFT, correlation) as PMT messages; "
        "sending blocks of samples over ZeroMQ or other message transports that expect PMT values. "
        "Changing packet_size at runtime triggers settingsChanged() which resizes the internal buffer.">;

    PortIn<T>               in;
    PortOut<gr::pmt::Value> out;

    gr::Size_t packet_size = 1024;

    GR_MAKE_REFLECTABLE(StreamToPmt, in, out, packet_size);

    std::vector<T> _saved_samples;

    void settingsChanged(const property_map& /*old_settings*/, const property_map& new_settings) noexcept {
        if (new_settings.contains("packet_size")) {
            _saved_samples.resize(packet_size);
            this->input_chunk_size = packet_size;
        }
    }

    template<typename TSpanIn, typename TSpanOut>
    [[nodiscard]] constexpr gr::work::Status processBulk(const TSpanIn& in, TSpanOut& out) noexcept {
        const size_t N          = this->input_chunk_size;
        size_t       num_chunks = out.size();

        if (in.size() < num_chunks * N) {
            return gr::work::Status::ERROR;
        }

        for (size_t idx = 0; idx < num_chunks; ++idx) {
            auto           start = in.begin() + idx * N;
            std::vector<T> chunk(start, start + N);
            gr::Tensor<T>  tensor(gr::data_from, std::move(chunk));
            out[idx] = gr::pmt::Value(std::move(tensor));
        }
        return gr::work::Status::OK;
    }
};

} // namespace gr::incubator::basic

GR_REGISTER_BLOCK("gr::incubator::basic::StreamToPmt", gr::incubator::basic::StreamToPmt, ([T]), [ uint8_t, int16_t, int32_t, float, std::complex<float> ]);
