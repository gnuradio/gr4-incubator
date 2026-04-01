#pragma once

#include <algorithm>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

#include <gnuradio-4.0/Message.hpp>
#include <gnuradio-4.0/Tag.hpp>

#include <nlohmann/json.hpp>

namespace gr::incubator::sigmf::detail {

using Json = nlohmann::json;

[[nodiscard]] inline pmt::Value jsonToValue(const Json& value);

[[nodiscard]] inline property_map jsonToPropertyMap(const Json& object) {
    property_map map{};
    for (const auto& [key, value] : object.items()) {
        map[std::pmr::string(key)] = jsonToValue(value);
    }
    return map;
}

[[nodiscard]] inline pmt::Value jsonToValue(const Json& value) {
    if (value.is_null()) {
        return {};
    }
    if (value.is_boolean()) {
        return value.get<bool>();
    }
    if (value.is_number_integer()) {
        return value.get<std::int64_t>();
    }
    if (value.is_number_unsigned()) {
        return value.get<std::uint64_t>();
    }
    if (value.is_number_float()) {
        return value.get<double>();
    }
    if (value.is_string()) {
        return value.get<std::string>();
    }
    if (value.is_array()) {
        std::vector<pmt::Value> items;
        items.reserve(value.size());
        for (const auto& item : value) {
            items.push_back(jsonToValue(item));
        }
        gr::Tensor<pmt::Value> tensor;
        tensor.resize({items.size()});
        std::copy(items.begin(), items.end(), tensor.begin());
        return pmt::Value(std::move(tensor));
    }
    if (value.is_object()) {
        return pmt::Value(jsonToPropertyMap(value));
    }
    return {};
}

[[nodiscard]] inline property_map toTagMetaInfo(const Json& object, std::initializer_list<std::string_view> excludedKeys = {}) {
    auto meta = jsonToPropertyMap(object);
    for (const auto key : excludedKeys) {
        meta.erase(std::pmr::string(key));
    }
    return meta;
}

} // namespace gr::incubator::sigmf::detail
