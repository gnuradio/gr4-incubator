#pragma once

#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/BlockRegistry.hpp>
#include <gnuradio-4.0/Value.hpp>
#include <gnuradio-4.0/Tensor.hpp>

namespace gr::basic {

template<typename T>
struct StreamToPmt : gr::Block<StreamToPmt<T>, Resampling<>> {    
    using Description = Doc<"@brief Converts a stream of samples to uniform vector PMTs of a specified packet size">;

    PortIn<T> in;
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
        const size_t N = this->input_chunk_size;
        size_t num_chunks = out.size();

        if (in.size() < num_chunks * N) {
            return gr::work::Status::ERROR;
        }

        for (size_t idx = 0; idx < num_chunks; ++idx) {
            auto start = in.begin() + idx * N;
            std::vector<T> chunk(start, start + N);
            gr::Tensor<T> tensor(gr::data_from, std::move(chunk));
            out[idx] = gr::pmt::Value(std::move(tensor));
        }
        return gr::work::Status::OK;
    }
};

}

GR_REGISTER_BLOCK("gr::basic::StreamToPmt", gr::basic::StreamToPmt, ([T]), [ uint8_t, int16_t, int32_t, float, std::complex<float> ]);
