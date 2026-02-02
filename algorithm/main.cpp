// src/main.cpp
#include <iostream>
#include <gnuradio-4.0/algorithm/pmt_converter/legacy/pmt_legacy.h>
#include <gnuradio-4.0/algorithm/pmt_converter/pmt_converter.h>

int main() {
    using namespace legacy;
    using namespace gr_compat;

    // Build a test legacy PMT
    auto vec = pmt_t::make_vector({
        pmt_t::make_int(42),
        pmt_t::make_bool(true),
        pmt_t::make_symbol("test-symbol")
    });

    auto dict = pmt_dict{
        {pmt_t::make_symbol("key1"), vec},
        {pmt_t::make_symbol("key2"), pmt_t::make_int(1234)}
    };

    auto legacy_obj = pmt_t::make_dict(dict);

    std::cout << "Legacy PMT: " << legacy_obj << std::endl;

    // Convert to GNU Radio 4 PMT
    gr::pmt::Value gr4_obj = to_new_pmt(*legacy_obj);
    std::cout << "Converted to GNURadio 4 PMT: " << gr4_obj << std::endl;

    // Convert back to legacy
    auto legacy_roundtrip = to_legacy_pmt(gr4_obj);
    std::cout << "Back to Legacy PMT: " << legacy_roundtrip << std::endl;

    // Check round-trip success
    if (*legacy_obj == *legacy_roundtrip) {
        std::cout << "✅ Round-trip conversion successful!" << std::endl;
    } else {
        std::cout << "❌ Round-trip conversion failed!" << std::endl;
    }

    return 0;
}
