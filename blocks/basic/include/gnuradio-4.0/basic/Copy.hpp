#pragma once

#include <gnuradio-4.0/Block.hpp>
#include <gnuradio-4.0/BlockRegistry.hpp>



namespace gr::incubator::basic {

// Copy: identity 1:1 passthrough block.
//
// Forwards every input sample to the output unchanged.  Despite being trivial,
// Copy is useful in several scenarios:
//
//   - Fan-out adapter: connect one source to Copy, then connect Copy's output
//     to multiple downstream consumers (avoids direct multi-connect if the
//     scheduler or port type does not support it natively).
//   - Tag injection point: subclass or wrap Copy to attach tags to samples
//     without modifying the data path.
//   - Chain placeholder: occupy a slot in a graph under development while
//     the real processing block is being written.
//   - Benchmark baseline: measure graph scheduling overhead with zero compute.
//
// Signal chain placement:
//   [any source] → Copy → [any sink]

GR_REGISTER_BLOCK("gr::incubator::basic::Copy", gr::incubator::basic::Copy, ([T]), [ uint8_t, int16_t, int32_t ])

template<typename T>
struct Copy : Block<Copy<T>> {

    using Description = Doc<
        "Identity 1:1 passthrough block. Forwards every input sample to the output unchanged. "
        "Useful as: a fan-out adapter connecting one source to multiple downstream consumers; "
        "a tag injection point to attach tags without modifying the data path; "
        "a chain placeholder while the real processing block is being written; "
        "or a benchmark baseline to measure graph scheduling overhead with zero compute.">;

    PortIn<T> in;
    PortOut<T> out;

    GR_MAKE_REFLECTABLE(Copy, in, out);

    [[nodiscard]] constexpr T processOne(T input) const noexcept { return input; }
};

} // namespace gr::incubator::basic

