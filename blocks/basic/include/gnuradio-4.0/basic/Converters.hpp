#pragma once

#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/BlockRegistry.hpp>

namespace gr::basic {

GR_REGISTER_BLOCK("gr::basic::StreamToPmt", gr::basic::StreamToPmt, ([T]), [ uint8_t, int16_t, int32_t, float, std::complex<float> ]);

template<typename T>
struct StreamToPmt : gr::Block<StreamToPmt<T>, Resampling<1024U, 1UZ, false>> {    
    using Description = Doc<"@brief Converts a stream of samples to uniform vector PMTs of a specified packet size">;

    PortIn<T> in;
    PortOut<pmtv::pmt> out;

    size_t packet_size;

    GR_MAKE_REFLECTABLE(StreamToPmt, in, out, packet_size);

    std::vector<T> _saved_samples;

    void settingsChanged(const property_map& /*old_settings*/, const property_map& new_settings) noexcept {
        if (new_settings.contains("packet_size")) {
            _saved_samples.resize(packet_size);
            this->input_chunk_size = packet_size;
        }
    }

    template<typename TSpanIn, typename TSpanOut>
    gr::work::Status processBulk(const TSpanIn& in, TSpanOut& out) {
        const size_t N = this->input_chunk_size;
        size_t num_chunks = out.size();

        if (in.size() < num_chunks * N) {
            return gr::work::Status::ERROR;
        }

        for (size_t idx = 0; idx < num_chunks; ++idx) {
            auto start = in.begin() + idx * N;
            std::vector<T> chunk(start, start + N);
            out[idx] = pmtv::pmt(std::move(chunk));
        }
        return gr::work::Status::OK;
    }
};



}