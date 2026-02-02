#include <gnuradio-4.0/algorithm/pmt_converter/pmt_legacy_codec.h>

#include <algorithm>
#include <bit>
#include <complex>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory_resource>
#include <stdexcept>
#include <string>
#include <vector>

using gr::Tensor;
using gr::pmt::Value;

namespace legacy_pmt {

enum class legacy_tag : uint8_t {
    LEGACY_PMT_TRUE = 0x00,
    LEGACY_PMT_FALSE = 0x01,
    LEGACY_PMT_SYMBOL = 0x02,
    LEGACY_PMT_INT32 = 0x03,
    LEGACY_PMT_DOUBLE = 0x04,
    LEGACY_PMT_COMPLEX = 0x05,
    LEGACY_PMT_NULL = 0x06,
    LEGACY_PMT_PAIR = 0x07,
    LEGACY_PMT_VECTOR = 0x08,
    LEGACY_PMT_DICT = 0x09,
    LEGACY_PMT_UNIFORM_VECTOR = 0x0A,
    LEGACY_PMT_UINT64 = 0x0B,
    LEGACY_PMT_TUPLE = 0x0C,
    LEGACY_PMT_INT64 = 0x0D
};

enum class legacy_uniform_type : uint8_t {
    U8 = 0x00,
    S8 = 0x01,
    U16 = 0x02,
    S16 = 0x03,
    U32 = 0x04,
    S32 = 0x05,
    U64 = 0x06,
    S64 = 0x07,
    F32 = 0x08,
    F64 = 0x09,
    C32 = 0x0A,
    C64 = 0x0B,
    UNKNOWN = 0xFF
};

static void write_u8(std::vector<uint8_t>& out, uint8_t v) { out.push_back(v); }

static void write_u16(std::vector<uint8_t>& out, uint32_t v) {
    for (int i = 1; i >= 0; --i)
        out.push_back((v >> (i * 8)) & 0xFF);
}

static void write_u32(std::vector<uint8_t>& out, uint32_t v) {
    for (int i = 3; i >= 0; --i)
        out.push_back((v >> (i * 8)) & 0xFF);
}

static void write_u64(std::vector<uint8_t>& out, uint64_t v) {
    for (int i = 7; i >= 0; --i)
        out.push_back((v >> (i * 8)) & 0xFF);
}

static void write_double(std::vector<uint8_t>& out, double d) {
    uint64_t bits;
    std::memcpy(&bits, &d, sizeof(double));
    write_u64(out, bits);
}

static uint16_t read_u16(const uint8_t*& data, const uint8_t* end) {
    if (data + 2 > end) {
        throw std::runtime_error("Truncated legacy PMT buffer (u16)");
    }
    uint16_t result = (static_cast<uint16_t>(data[0]) << 8) | static_cast<uint16_t>(data[1]);
    data += 2;
    return result;
}

static uint64_t read_u32(const uint8_t*& data, const uint8_t* end) {
    if (data + 4 > end) {
        throw std::runtime_error("Truncated legacy PMT buffer (u32)");
    }
    uint64_t result = 0;
    for (int i = 0; i < 4; ++i) {
        result = (result << 8) | data[i];
    }
    data += 4;
    return result;
}

static uint64_t read_u64(const uint8_t*& data, const uint8_t* end) {
    if (data + 8 > end) {
        throw std::runtime_error("Truncated legacy PMT buffer (u64)");
    }
    uint64_t result = 0;
    for (int i = 0; i < 8; ++i) {
        result = (result << 8) | data[i];
    }
    data += 8;
    return result;
}

static double read_double(const uint8_t*& data, const uint8_t* end) {
    uint64_t bits = read_u64(data, end);
    double d;
    std::memcpy(&d, &bits, sizeof(double));
    return d;
}

template <typename T>
constexpr legacy_uniform_type legacy_uniform_type_for() {
    if constexpr (std::is_same_v<T, uint8_t>) return legacy_uniform_type::U8;
    else if constexpr (std::is_same_v<T, int8_t>) return legacy_uniform_type::S8;
    else if constexpr (std::is_same_v<T, uint16_t>) return legacy_uniform_type::U16;
    else if constexpr (std::is_same_v<T, int16_t>) return legacy_uniform_type::S16;
    else if constexpr (std::is_same_v<T, uint32_t>) return legacy_uniform_type::U32;
    else if constexpr (std::is_same_v<T, int32_t>) return legacy_uniform_type::S32;
    else if constexpr (std::is_same_v<T, uint64_t>) return legacy_uniform_type::U64;
    else if constexpr (std::is_same_v<T, int64_t>) return legacy_uniform_type::S64;
    else if constexpr (std::is_same_v<T, float>) return legacy_uniform_type::F32;
    else if constexpr (std::is_same_v<T, double>) return legacy_uniform_type::F64;
    else if constexpr (std::is_same_v<T, std::complex<float>>) return legacy_uniform_type::C32;
    else if constexpr (std::is_same_v<T, std::complex<double>>) return legacy_uniform_type::C64;
    else return legacy_uniform_type::UNKNOWN;
}

template <typename T>
requires std::is_integral_v<T> && (!std::is_same_v<T, bool>)
T to_big_endian_integral(T value) {
    if constexpr (std::endian::native == std::endian::little) {
        if constexpr (sizeof(T) == 1) {
            return value;
        } else if constexpr (sizeof(T) == 2) {
            return static_cast<T>(((static_cast<uint16_t>(value) & 0xFF00) >> 8) | ((static_cast<uint16_t>(value) & 0x00FF) << 8));
        } else if constexpr (sizeof(T) == 4) {
            return static_cast<T>(((static_cast<uint32_t>(value) & 0xFF000000) >> 24) | ((static_cast<uint32_t>(value) & 0x00FF0000) >> 8) |
                                  ((static_cast<uint32_t>(value) & 0x0000FF00) << 8) | ((static_cast<uint32_t>(value) & 0x000000FF) << 24));
        } else if constexpr (sizeof(T) == 8) {
            uint64_t u64_val = static_cast<uint64_t>(value);
            return static_cast<T>(((u64_val & 0xFF00000000000000ULL) >> 56) | ((u64_val & 0x00FF000000000000ULL) >> 40) |
                                  ((u64_val & 0x0000FF0000000000ULL) >> 24) | ((u64_val & 0x000000FF00000000ULL) >> 8) |
                                  ((u64_val & 0x00000000FF000000ULL) << 8) | ((u64_val & 0x0000000000FF0000ULL) << 24) |
                                  ((u64_val & 0x000000000000FF00ULL) << 40) | ((u64_val & 0x00000000000000FFULL) << 56));
        } else {
            std::vector<uint8_t> bytes(sizeof(T));
            std::memcpy(bytes.data(), &value, sizeof(T));
            std::reverse(bytes.begin(), bytes.end());
            T result;
            std::memcpy(&result, bytes.data(), sizeof(T));
            return result;
        }
    }
    return value;
}

template <typename T>
requires std::is_trivially_copyable_v<T>
void serialize_to_big_endian(const T& value, std::vector<uint8_t>& out) {
    if constexpr (std::is_integral_v<T> && !std::is_same_v<T, bool>) {
        T big_endian_val = to_big_endian_integral(value);
        const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&big_endian_val);
        out.insert(out.end(), bytes, bytes + sizeof(T));
    } else if constexpr (std::is_floating_point_v<T>) {
        if constexpr (sizeof(T) == sizeof(uint32_t)) {
            uint32_t int_repr = std::bit_cast<uint32_t>(value);
            uint32_t big_endian_int = to_big_endian_integral(int_repr);
            const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&big_endian_int);
            out.insert(out.end(), bytes, bytes + sizeof(uint32_t));
        } else if constexpr (sizeof(T) == sizeof(uint64_t)) {
            uint64_t int_repr = std::bit_cast<uint64_t>(value);
            uint64_t big_endian_int = to_big_endian_integral(int_repr);
            const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&big_endian_int);
            out.insert(out.end(), bytes, bytes + sizeof(uint64_t));
        } else {
            const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&value);
            out.insert(out.end(), bytes, bytes + sizeof(T));
        }
    } else if constexpr (std::is_same_v<T, std::complex<float>>) {
        serialize_to_big_endian(value.real(), out);
        serialize_to_big_endian(value.imag(), out);
    } else if constexpr (std::is_same_v<T, std::complex<double>>) {
        serialize_to_big_endian(value.real(), out);
        serialize_to_big_endian(value.imag(), out);
    } else {
        const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&value);
        out.insert(out.end(), bytes, bytes + sizeof(T));
    }
}

template <typename T>
std::vector<uint8_t> serialize_uniform_vector(const std::vector<T>& vec) {
    std::vector<uint8_t> out;

    write_u8(out, static_cast<uint8_t>(legacy_tag::LEGACY_PMT_UNIFORM_VECTOR));
    write_u8(out, static_cast<uint8_t>(legacy_uniform_type_for<T>()));
    write_u32(out, static_cast<uint32_t>(vec.size()));
    write_u8(out, static_cast<uint8_t>(1));
    write_u8(out, static_cast<uint8_t>(0));

    for (const auto& val : vec) {
        serialize_to_big_endian(val, out);
    }

    return out;
}

static std::vector<uint8_t> serialize_string(std::string_view str) {
    std::vector<uint8_t> out;
    write_u8(out, static_cast<uint8_t>(legacy_tag::LEGACY_PMT_SYMBOL));
    write_u16(out, static_cast<uint32_t>(str.size()));
    out.insert(out.end(), str.begin(), str.end());
    return out;
}

static std::vector<uint8_t> serialize_tuple(const Tensor<Value>& tensor);

static std::vector<uint8_t> serialize_dict(const Value::Map& map) {
    std::vector<std::string> keys;
    keys.reserve(map.size());
    for (const auto& [k, _] : map) {
        keys.emplace_back(k);
    }
    std::sort(keys.begin(), keys.end());

    std::vector<uint8_t> out;
    if (keys.empty()) {
        write_u8(out, static_cast<uint8_t>(legacy_tag::LEGACY_PMT_NULL));
        return out;
    }

    auto write_tail = [&](auto&& self, std::size_t idx) -> void {
        if (idx >= keys.size()) {
            write_u8(out, static_cast<uint8_t>(legacy_tag::LEGACY_PMT_NULL));
            return;
        }
        write_u8(out, static_cast<uint8_t>(legacy_tag::LEGACY_PMT_DICT));
        write_u8(out, static_cast<uint8_t>(legacy_tag::LEGACY_PMT_PAIR));
        const auto& key = keys[idx];
        auto key_bytes = serialize_string(key);
        out.insert(out.end(), key_bytes.begin(), key_bytes.end());
        std::pmr::string key_pmr(key, std::pmr::get_default_resource());
        auto it = map.find(key_pmr);
        if (it == map.end()) {
            throw std::runtime_error("Legacy PMT dict missing key");
        }
        const auto& val = it->second;
        auto val_bytes = serialize_to_legacy(val);
        out.insert(out.end(), val_bytes.begin(), val_bytes.end());
        self(self, idx + 1);
    };

    write_tail(write_tail, 0);
    return out;
}

static std::vector<uint8_t> serialize_tuple(const Tensor<Value>& tensor) {
    std::vector<uint8_t> out;
    write_u8(out, static_cast<uint8_t>(legacy_tag::LEGACY_PMT_TUPLE));
    write_u32(out, static_cast<uint32_t>(tensor.size()));
    for (const auto& v : tensor) {
        auto bytes = serialize_to_legacy(v);
        out.insert(out.end(), bytes.begin(), bytes.end());
    }
    return out;
}

static std::vector<uint8_t> serialize_complex(std::complex<double> value) {
    std::vector<uint8_t> out;
    write_u8(out, static_cast<uint8_t>(legacy_tag::LEGACY_PMT_COMPLEX));
    write_double(out, value.real());
    write_double(out, value.imag());
    return out;
}

static bool try_serialize_tensor(const Value& obj, std::vector<uint8_t>& out) {
    if (auto t = obj.get_if<Tensor<uint8_t>>()) {
        out = serialize_uniform_vector(std::vector<uint8_t>(t->begin(), t->end()));
        return true;
    }
    if (auto t = obj.get_if<Tensor<int8_t>>()) {
        out = serialize_uniform_vector(std::vector<int8_t>(t->begin(), t->end()));
        return true;
    }
    if (auto t = obj.get_if<Tensor<uint16_t>>()) {
        out = serialize_uniform_vector(std::vector<uint16_t>(t->begin(), t->end()));
        return true;
    }
    if (auto t = obj.get_if<Tensor<int16_t>>()) {
        out = serialize_uniform_vector(std::vector<int16_t>(t->begin(), t->end()));
        return true;
    }
    if (auto t = obj.get_if<Tensor<uint32_t>>()) {
        out = serialize_uniform_vector(std::vector<uint32_t>(t->begin(), t->end()));
        return true;
    }
    if (auto t = obj.get_if<Tensor<int32_t>>()) {
        out = serialize_uniform_vector(std::vector<int32_t>(t->begin(), t->end()));
        return true;
    }
    if (auto t = obj.get_if<Tensor<uint64_t>>()) {
        out = serialize_uniform_vector(std::vector<uint64_t>(t->begin(), t->end()));
        return true;
    }
    if (auto t = obj.get_if<Tensor<int64_t>>()) {
        out = serialize_uniform_vector(std::vector<int64_t>(t->begin(), t->end()));
        return true;
    }
    if (auto t = obj.get_if<Tensor<float>>()) {
        out = serialize_uniform_vector(std::vector<float>(t->begin(), t->end()));
        return true;
    }
    if (auto t = obj.get_if<Tensor<double>>()) {
        out = serialize_uniform_vector(std::vector<double>(t->begin(), t->end()));
        return true;
    }
    if (auto t = obj.get_if<Tensor<std::complex<float>>>()) {
        out = serialize_uniform_vector(std::vector<std::complex<float>>(t->begin(), t->end()));
        return true;
    }
    if (auto t = obj.get_if<Tensor<std::complex<double>>>()) {
        out = serialize_uniform_vector(std::vector<std::complex<double>>(t->begin(), t->end()));
        return true;
    }
    if (auto t = obj.get_if<Tensor<Value>>()) {
        out = serialize_tuple(*t);
        return true;
    }
    return false;
}

std::vector<uint8_t> serialize_to_legacy(const Value& obj) {
    std::vector<uint8_t> out;

    if (obj.is_monostate()) {
        write_u8(out, static_cast<uint8_t>(legacy_tag::LEGACY_PMT_NULL));
        return out;
    }

    if (obj.holds<bool>()) {
        write_u8(out, static_cast<uint8_t>(obj.value_or<bool>(false) ? legacy_tag::LEGACY_PMT_TRUE : legacy_tag::LEGACY_PMT_FALSE));
        return out;
    }

    if (obj.holds<int8_t>()) {
        write_u8(out, static_cast<uint8_t>(legacy_tag::LEGACY_PMT_INT32));
        write_u32(out, static_cast<uint32_t>(static_cast<int32_t>(obj.value_or<int8_t>(0))));
        return out;
    }
    if (obj.holds<int16_t>()) {
        write_u8(out, static_cast<uint8_t>(legacy_tag::LEGACY_PMT_INT32));
        write_u32(out, static_cast<uint32_t>(static_cast<int32_t>(obj.value_or<int16_t>(0))));
        return out;
    }
    if (obj.holds<int32_t>()) {
        write_u8(out, static_cast<uint8_t>(legacy_tag::LEGACY_PMT_INT32));
        write_u32(out, static_cast<uint32_t>(obj.value_or<int32_t>(0)));
        return out;
    }
    if (obj.holds<int64_t>()) {
        write_u8(out, static_cast<uint8_t>(legacy_tag::LEGACY_PMT_INT64));
        write_u64(out, static_cast<uint64_t>(obj.value_or<int64_t>(0)));
        return out;
    }
    if (obj.holds<uint8_t>()) {
        write_u8(out, static_cast<uint8_t>(legacy_tag::LEGACY_PMT_UINT64));
        write_u64(out, static_cast<uint64_t>(obj.value_or<uint8_t>(0)));
        return out;
    }
    if (obj.holds<uint16_t>()) {
        write_u8(out, static_cast<uint8_t>(legacy_tag::LEGACY_PMT_UINT64));
        write_u64(out, static_cast<uint64_t>(obj.value_or<uint16_t>(0)));
        return out;
    }
    if (obj.holds<uint32_t>()) {
        write_u8(out, static_cast<uint8_t>(legacy_tag::LEGACY_PMT_UINT64));
        write_u64(out, static_cast<uint64_t>(obj.value_or<uint32_t>(0)));
        return out;
    }
    if (obj.holds<uint64_t>()) {
        write_u8(out, static_cast<uint8_t>(legacy_tag::LEGACY_PMT_UINT64));
        write_u64(out, obj.value_or<uint64_t>(0));
        return out;
    }

    if (obj.holds<float>()) {
        write_u8(out, static_cast<uint8_t>(legacy_tag::LEGACY_PMT_DOUBLE));
        write_double(out, static_cast<double>(obj.value_or<float>(0.0f)));
        return out;
    }
    if (obj.holds<double>()) {
        write_u8(out, static_cast<uint8_t>(legacy_tag::LEGACY_PMT_DOUBLE));
        write_double(out, obj.value_or<double>(0.0));
        return out;
    }

    if (obj.holds<std::complex<float>>()) {
        return serialize_complex(static_cast<std::complex<double>>(obj.value_or<std::complex<float>>({}))); 
    }
    if (obj.holds<std::complex<double>>()) {
        return serialize_complex(obj.value_or<std::complex<double>>({}));
    }

    if (obj.is_string()) {
        return serialize_string(obj.value_or(std::string_view{}));
    }

    if (obj.is_tensor()) {
        if (try_serialize_tensor(obj, out)) {
            return out;
        }
        throw std::runtime_error("Unsupported tensor type for legacy PMT serialization");
    }

    if (obj.is_map()) {
        if (auto map = obj.get_if<Value::Map>()) {
            return serialize_dict(*map);
        }
        throw std::runtime_error("Unsupported map type for legacy PMT serialization");
    }

    throw std::runtime_error("Unsupported PMT type for legacy serialization");
}

template <typename T>
requires std::is_integral_v<T> && (!std::is_same_v<T, bool>)
T from_big_endian_integral_to_native(T value) {
    if constexpr (std::endian::native == std::endian::little) {
        if constexpr (sizeof(T) == 1) {
            return value;
        } else if constexpr (sizeof(T) == 2) {
            return static_cast<T>(((static_cast<uint16_t>(value) & 0xFF00) >> 8) | ((static_cast<uint16_t>(value) & 0x00FF) << 8));
        } else if constexpr (sizeof(T) == 4) {
            return static_cast<T>(((static_cast<uint32_t>(value) & 0xFF000000) >> 24) | ((static_cast<uint32_t>(value) & 0x00FF0000) >> 8) |
                                  ((static_cast<uint32_t>(value) & 0x0000FF00) << 8) | ((static_cast<uint32_t>(value) & 0x000000FF) << 24));
        } else if constexpr (sizeof(T) == 8) {
            uint64_t u64_val = static_cast<uint64_t>(value);
            return static_cast<T>(((u64_val & 0xFF00000000000000ULL) >> 56) | ((u64_val & 0x00FF000000000000ULL) >> 40) |
                                  ((u64_val & 0x0000FF0000000000ULL) >> 24) | ((u64_val & 0x000000FF00000000ULL) >> 8) |
                                  ((u64_val & 0x00000000FF000000ULL) << 8) | ((u64_val & 0x0000000000FF0000ULL) << 24) |
                                  ((u64_val & 0x000000000000FF00ULL) << 40) | ((u64_val & 0x00000000000000FFULL) << 56));
        } else {
            std::vector<uint8_t> bytes(sizeof(T));
            std::memcpy(bytes.data(), &value, sizeof(T));
            std::reverse(bytes.begin(), bytes.end());
            T result;
            std::memcpy(&result, bytes.data(), sizeof(T));
            return result;
        }
    }
    return value;
}

template <typename T>
requires std::is_trivially_copyable_v<T>
T deserialize_from_big_endian(const uint8_t*& ptr, const uint8_t* end) {
    if (ptr + sizeof(T) > end) {
        throw std::runtime_error("Truncated legacy PMT buffer");
    }
    T result;

    if constexpr (std::is_integral_v<T> && !std::is_same_v<T, bool>) {
        T temp_val;
        std::memcpy(&temp_val, ptr, sizeof(T));
        result = from_big_endian_integral_to_native(temp_val);
        ptr += sizeof(T);
    } else if constexpr (std::is_floating_point_v<T>) {
        if constexpr (sizeof(T) == sizeof(uint32_t)) {
            uint32_t int_repr;
            std::memcpy(&int_repr, ptr, sizeof(uint32_t));
            uint32_t native_endian_int = from_big_endian_integral_to_native(int_repr);
            result = std::bit_cast<T>(native_endian_int);
            ptr += sizeof(uint32_t);
        } else if constexpr (sizeof(T) == sizeof(uint64_t)) {
            uint64_t int_repr;
            std::memcpy(&int_repr, ptr, sizeof(uint64_t));
            uint64_t native_endian_int = from_big_endian_integral_to_native(int_repr);
            result = std::bit_cast<T>(native_endian_int);
            ptr += sizeof(uint64_t);
        } else {
            std::memcpy(&result, ptr, sizeof(T));
            ptr += sizeof(T);
        }
    } else if constexpr (std::is_same_v<T, std::complex<float>>) {
        float real_part = deserialize_from_big_endian<float>(ptr, end);
        float imag_part = deserialize_from_big_endian<float>(ptr, end);
        result = std::complex<float>(real_part, imag_part);
    } else if constexpr (std::is_same_v<T, std::complex<double>>) {
        double real_part = deserialize_from_big_endian<double>(ptr, end);
        double imag_part = deserialize_from_big_endian<double>(ptr, end);
        result = std::complex<double>(real_part, imag_part);
    } else {
        std::memcpy(&result, ptr, sizeof(T));
        ptr += sizeof(T);
    }
    return result;
}

template <typename VTYPE>
std::vector<VTYPE> create_vector_from_big_endian(const uint8_t*& ptr, const uint8_t* end, size_t num_elements) {
    std::vector<VTYPE> vec;
    vec.reserve(num_elements);

    for (size_t i = 0; i < num_elements; ++i) {
        vec.push_back(deserialize_from_big_endian<VTYPE>(ptr, end));
    }
    return vec;
}

static Value deserialize_value(const uint8_t*& ptr, const uint8_t* end);

static Value deserialize_dict(const uint8_t*& ptr, const uint8_t* end) {
    if (ptr >= end) {
        throw std::runtime_error("Truncated legacy PMT buffer (dict)");
    }

    Value::Map map{std::pmr::get_default_resource()};

    if (ptr >= end) {
        return Value(std::move(map));
    }

    auto pair_tag = static_cast<legacy_tag>(*ptr++);
    if (pair_tag == legacy_tag::LEGACY_PMT_NULL) {
        return Value(std::move(map));
    }
    if (pair_tag != legacy_tag::LEGACY_PMT_PAIR) {
        throw std::runtime_error("Malformed legacy PMT dict (missing pair tag)");
    }

    Value key = deserialize_value(ptr, end);
    Value val = deserialize_value(ptr, end);

    if (!key.is_string()) {
        throw std::runtime_error("Legacy PMT dict key is not a string");
    }
    std::string key_str = std::string(key.value_or(std::string_view{}));
    map.emplace(std::pmr::string(key_str, std::pmr::get_default_resource()), std::move(val));

    Value tail = deserialize_value(ptr, end);
    if (tail.is_map()) {
        if (auto tail_map = tail.get_if<Value::Map>()) {
            for (auto& [k, v] : *tail_map) {
                map.emplace(std::pmr::string(k, std::pmr::get_default_resource()), std::move(v));
            }
        }
    } else if (!tail.is_monostate()) {
        throw std::runtime_error("Malformed legacy PMT dict tail");
    }

    return Value(std::move(map));
}

static Value deserialize_value(const uint8_t*& ptr, const uint8_t* end) {
    if (ptr >= end) {
        throw std::runtime_error("Empty legacy PMT buffer");
    }

    auto tag = static_cast<legacy_tag>(*ptr++);
    switch (tag) {
        case legacy_tag::LEGACY_PMT_NULL:
            return Value{};
        case legacy_tag::LEGACY_PMT_TRUE:
            return Value(true);
        case legacy_tag::LEGACY_PMT_FALSE:
            return Value(false);
        case legacy_tag::LEGACY_PMT_INT32:
            return Value(static_cast<int32_t>(read_u32(ptr, end)));
        case legacy_tag::LEGACY_PMT_INT64:
            return Value(static_cast<int64_t>(read_u64(ptr, end)));
        case legacy_tag::LEGACY_PMT_UINT64:
            return Value(static_cast<uint64_t>(read_u64(ptr, end)));
        case legacy_tag::LEGACY_PMT_DOUBLE:
            return Value(read_double(ptr, end));
        case legacy_tag::LEGACY_PMT_COMPLEX: {
            double real = read_double(ptr, end);
            double imag = read_double(ptr, end);
            return Value(std::complex<double>(real, imag));
        }
        case legacy_tag::LEGACY_PMT_SYMBOL: {
            uint16_t len = read_u16(ptr, end);
            if (ptr + len > end) {
                throw std::runtime_error("Truncated legacy PMT symbol");
            }
            std::string sym(reinterpret_cast<const char*>(ptr), len);
            ptr += len;
            return Value(std::move(sym));
        }
        case legacy_tag::LEGACY_PMT_UNIFORM_VECTOR: {
            if (ptr >= end) {
                throw std::runtime_error("Truncated legacy PMT uniform vector");
            }
            legacy_uniform_type dtype = static_cast<legacy_uniform_type>(*ptr++);
            uint32_t len = static_cast<uint32_t>(read_u32(ptr, end));
            if (ptr >= end) {
                throw std::runtime_error("Truncated legacy PMT uniform vector padding");
            }
            uint8_t npad = *ptr++;
            if (ptr + npad > end) {
                throw std::runtime_error("Truncated legacy PMT uniform vector padding");
            }
            ptr += npad;

            switch (dtype) {
                case legacy_uniform_type::U8: {
                    auto vec = create_vector_from_big_endian<uint8_t>(ptr, end, len);
                    return Value(Tensor<uint8_t>(gr::data_from, vec));
                }
                case legacy_uniform_type::S8: {
                    auto vec = create_vector_from_big_endian<int8_t>(ptr, end, len);
                    return Value(Tensor<int8_t>(gr::data_from, vec));
                }
                case legacy_uniform_type::U16: {
                    auto vec = create_vector_from_big_endian<uint16_t>(ptr, end, len);
                    return Value(Tensor<uint16_t>(gr::data_from, vec));
                }
                case legacy_uniform_type::S16: {
                    auto vec = create_vector_from_big_endian<int16_t>(ptr, end, len);
                    return Value(Tensor<int16_t>(gr::data_from, vec));
                }
                case legacy_uniform_type::U32: {
                    auto vec = create_vector_from_big_endian<uint32_t>(ptr, end, len);
                    return Value(Tensor<uint32_t>(gr::data_from, vec));
                }
                case legacy_uniform_type::S32: {
                    auto vec = create_vector_from_big_endian<int32_t>(ptr, end, len);
                    return Value(Tensor<int32_t>(gr::data_from, vec));
                }
                case legacy_uniform_type::U64: {
                    auto vec = create_vector_from_big_endian<uint64_t>(ptr, end, len);
                    return Value(Tensor<uint64_t>(gr::data_from, vec));
                }
                case legacy_uniform_type::S64: {
                    auto vec = create_vector_from_big_endian<int64_t>(ptr, end, len);
                    return Value(Tensor<int64_t>(gr::data_from, vec));
                }
                case legacy_uniform_type::F32: {
                    auto vec = create_vector_from_big_endian<float>(ptr, end, len);
                    return Value(Tensor<float>(gr::data_from, vec));
                }
                case legacy_uniform_type::F64: {
                    auto vec = create_vector_from_big_endian<double>(ptr, end, len);
                    return Value(Tensor<double>(gr::data_from, vec));
                }
                case legacy_uniform_type::C32: {
                    auto vec = create_vector_from_big_endian<std::complex<float>>(ptr, end, len);
                    return Value(Tensor<std::complex<float>>(gr::data_from, vec));
                }
                case legacy_uniform_type::C64: {
                    auto vec = create_vector_from_big_endian<std::complex<double>>(ptr, end, len);
                    return Value(Tensor<std::complex<double>>(gr::data_from, vec));
                }
                default:
                    throw std::runtime_error("Unsupported or unknown legacy PMT uniform vector tag");
            }
        }
        case legacy_tag::LEGACY_PMT_TUPLE: {
            uint32_t len = static_cast<uint32_t>(read_u32(ptr, end));
            std::vector<Value> values;
            values.reserve(len);
            for (uint32_t i = 0; i < len; ++i) {
                values.push_back(deserialize_value(ptr, end));
            }
            return Value(Tensor<Value>(values));
        }
        case legacy_tag::LEGACY_PMT_PAIR: {
            std::vector<Value> values;
            values.reserve(2);
            values.push_back(deserialize_value(ptr, end));
            values.push_back(deserialize_value(ptr, end));
            return Value(Tensor<Value>(values));
        }
        case legacy_tag::LEGACY_PMT_DICT:
            return deserialize_dict(ptr, end);
        case legacy_tag::LEGACY_PMT_VECTOR:
        default:
            throw std::runtime_error("Unsupported or unknown legacy PMT tag");
    }
}

gr::pmt::Value deserialize_from_legacy(const uint8_t* data, size_t size) {
    if (!data || size == 0) {
        throw std::runtime_error("Empty legacy PMT buffer");
    }
    const uint8_t* ptr = data;
    const uint8_t* end = data + size;
    return deserialize_value(ptr, end);
}

} // namespace legacy_pmt
