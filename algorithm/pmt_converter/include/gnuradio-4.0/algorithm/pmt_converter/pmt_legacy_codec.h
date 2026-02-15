#pragma once

#include <gnuradio-4.0/Value.hpp>
#include <vector>
#include <cstdint>

namespace legacy_pmt {

/**
 * Serialize a gr::pmt::Value into the legacy GNU Radio PMT binary format.
 * Returns a vector of bytes that can be passed to a ZMQ socket or saved to a file.
 */
std::vector<uint8_t> serialize_to_legacy(const gr::pmt::Value& obj);

/**
 * Deserialize a binary blob (legacy GNU Radio PMT format) into a gr::pmt::Value.
 * Throws std::runtime_error if the data is malformed or unrecognized.
 */
gr::pmt::Value deserialize_from_legacy(const uint8_t* data, size_t size);

} // namespace legacy_pmt
