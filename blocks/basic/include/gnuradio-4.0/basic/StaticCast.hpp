#pragma once

#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/BlockRegistry.hpp>

namespace gr::incubator::basic {

GR_REGISTER_BLOCK("gr::incubator::basic::StaticCast", gr::incubator::basic::StaticCast, ([T], [U]), [ uint8_t, int16_t, int32_t, float, double ], [ uint8_t, int16_t, int32_t, float, double ])

template<typename TIN, typename TOUT>
struct StaticCast : Block<StaticCast<TIN, TOUT>> {

    using Description = Doc<"Type-converting passthrough: applies static_cast<TOUT> to every input sample. "
                            "Bridges blocks with mismatched port types, e.g. int16_t ADC output to a float processing chain, "
                            "or float intermediate results to uint8_t output. "
                            "No saturation or rounding is applied beyond what static_cast provides.">;

    PortIn<TIN>   in;
    PortOut<TOUT> out;

    GR_MAKE_REFLECTABLE(StaticCast, in, out);

    [[nodiscard]] constexpr TOUT processOne(TIN input) const noexcept { return static_cast<TOUT>(input); }
};

} // namespace gr::incubator::basic
