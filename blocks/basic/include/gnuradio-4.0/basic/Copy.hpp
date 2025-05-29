
#ifndef _GR_BASIC_COPY_HPP
#define _GR_BASIC_COPY_HPP

#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/BlockRegistry.hpp>



namespace gr::basic {

GR_REGISTER_BLOCK(gr::basic::Copy, [ uint8_t, int16_t, int32_t ])
template<typename T>
struct Copy : Block<Copy<T>> {

    using Description = Doc<"@brief Copies from input to output.">;

    PortIn<T> in;
    PortOut<T> out;

    GR_MAKE_REFLECTABLE(Copy, in, out);

    [[nodiscard]] constexpr T processOne(T input) const noexcept { return input; }
};

} // namespace gr::basic

#endif // _GR_BASIC_COPY_HPP