#pragma once

#include <algorithm>
#include <complex>
#include <cctype>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>
#include <vector>

#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/BlockRegistry.hpp>
#include <gnuradio-4.0/meta/utils.hpp>

#include <SoapySDR/Device.hpp>
#include <SoapySDR/Formats.h>

namespace gr::soapysdr {

GR_REGISTER_BLOCK("Soapy RX", gr::soapysdr::SoapyRx, ([T]), [ std::complex<float>, float, int16_t, uint8_t ])

template<typename T>
struct SoapyRx : Block<SoapyRx<T>> {
    using Description = Doc<"SoapySDR RX source block (single-channel)">;
    using Base = Block<SoapyRx<T>>;
    using Base::Base;

    PortOut<T> out;

    std::string device;
    std::string device_args;
    float       sample_rate = 1'000'000.0f;
    gr::Size_t  channel = 0U;
    double      center_frequency = 100'000'000.0;
    double      bandwidth = 0.0;
    double      gain = 0.0;
    std::string antenna;

    std::uint32_t max_chunk_size = 8192U;
    std::uint32_t stream_timeout_us = 1'000U;
    gr::Size_t    max_overflow_count = 10U;

    GR_MAKE_REFLECTABLE(SoapyRx, out, device, device_args, sample_rate, channel, center_frequency, bandwidth, gain, antenna, max_chunk_size, stream_timeout_us, max_overflow_count);

    SoapyRx() = default;

    void start() {
        openDevice();
    }

    void stop() {
        closeDevice();
    }

    [[nodiscard]] gr::work::Status processBulk(OutputSpanLike auto& output) {
        if (!_dev || !_stream) {
            openDevice();
        }

        const std::size_t max_samples = std::min<std::size_t>(output.size(), max_chunk_size);
        if (max_samples == 0) {
            output.publish(0UZ);
            return gr::work::Status::OK;
        }

        int       flags = 0;
        long long time_ns = 0;
        void* buffers[1] = {output.data()};
        int   ret = _dev->readStream(_stream, buffers, max_samples, flags, time_ns, stream_timeout_us);

        if (ret > 0) {
            output.publish(static_cast<std::size_t>(ret));
            _overflow_count = 0U;
            return gr::work::Status::OK;
        }

        if (ret == 0 || ret == SOAPY_SDR_TIMEOUT) {
            output.publish(0UZ);
            return gr::work::Status::OK;
        }

        if (ret == SOAPY_SDR_OVERFLOW) {
            if (max_overflow_count > 0 && ++_overflow_count > max_overflow_count) {
                throw gr::exception(std::format("SoapySDR overflow exceeded max_overflow_count={} for device '{}'", max_overflow_count, device));
            }
            output.publish(0UZ);
            return gr::work::Status::OK;
        }

        throw gr::exception(std::format("SoapySDR readStream error {} ({})", ret, SoapySDR_errToStr(ret)));
    }

private:
    SoapySDR::Device* _dev = nullptr;
    SoapySDR::Stream* _stream = nullptr;
    gr::Size_t        _overflow_count = 0U;

    static constexpr const char* soapyFormat() {
        if constexpr (std::is_same_v<T, std::complex<float>>) {
            return SOAPY_SDR_CF32;
        } else if constexpr (std::is_same_v<T, float>) {
            return SOAPY_SDR_F32;
        } else if constexpr (std::is_same_v<T, int16_t>) {
            return SOAPY_SDR_S16;
        } else if constexpr (std::is_same_v<T, uint8_t>) {
            return SOAPY_SDR_U8;
        } else {
            static_assert(!gr::meta::always_false<T>, "unsupported SoapyRx type");
        }
    }

    static std::string_view trim(std::string_view s) {
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) {
            s.remove_prefix(1);
        }
        while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) {
            s.remove_suffix(1);
        }
        return s;
    }

    static SoapySDR::Kwargs parseArgs(std::string_view args) {
        SoapySDR::Kwargs kwargs;
        std::size_t pos = 0;
        while (pos < args.size()) {
            std::size_t next = args.find(',', pos);
            if (next == std::string_view::npos) {
                next = args.size();
            }
            auto token = trim(args.substr(pos, next - pos));
            if (!token.empty()) {
                const std::size_t eq = token.find('=');
                if (eq == std::string_view::npos) {
                    kwargs[std::string(token)] = "";
                } else {
                    auto key = trim(token.substr(0, eq));
                    auto val = trim(token.substr(eq + 1));
                    if (!key.empty()) {
                        kwargs[std::string(key)] = std::string(val);
                    }
                }
            }
            pos = next + 1;
        }
        return kwargs;
    }

    void openDevice() {
        if (_dev) {
            return;
        }

        SoapySDR::Kwargs kwargs = parseArgs(device_args);
        if (!device.empty()) {
            kwargs["driver"] = device;
        }

        _dev = SoapySDR::Device::make(kwargs);
        if (!_dev) {
            throw gr::exception("SoapySDR Device::make failed");
        }

        _dev->setSampleRate(SOAPY_SDR_RX, static_cast<std::size_t>(channel), sample_rate);
        if (bandwidth > 0.0) {
            _dev->setBandwidth(SOAPY_SDR_RX, static_cast<std::size_t>(channel), bandwidth);
        }
        _dev->setFrequency(SOAPY_SDR_RX, static_cast<std::size_t>(channel), center_frequency);
        if (gain != 0.0) {
            _dev->setGain(SOAPY_SDR_RX, static_cast<std::size_t>(channel), gain);
        }
        if (!antenna.empty()) {
            _dev->setAntenna(SOAPY_SDR_RX, static_cast<std::size_t>(channel), antenna);
        }

        std::vector<std::size_t> channels{static_cast<std::size_t>(channel)};
        _stream = _dev->setupStream(SOAPY_SDR_RX, soapyFormat(), channels);
        if (!_stream) {
            SoapySDR::Device::unmake(_dev);
            _dev = nullptr;
            throw gr::exception("SoapySDR setupStream failed");
        }

        _dev->activateStream(_stream);
    }

    void closeDevice() {
        if (_dev && _stream) {
            _dev->deactivateStream(_stream, 0, 0);
            _dev->closeStream(_stream);
            _stream = nullptr;
        }
        if (_dev) {
            SoapySDR::Device::unmake(_dev);
            _dev = nullptr;
        }
    }
};

} // namespace gr::soapysdr
