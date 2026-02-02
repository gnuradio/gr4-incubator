#pragma once

#include <gnuradio-4.0/Value.hpp>
#include <memory_resource>
#include "legacy/pmt_legacy.h"

namespace gr_compat {

inline gr::pmt::Value to_new_pmt(const legacy::pmt_t& old) {
    if (old.is_bool()) {
        return gr::pmt::Value(old.to_bool());
    } else if (old.is_int()) {
        return gr::pmt::Value(static_cast<std::int64_t>(old.to_int()));
    } else if (old.is_symbol()) {
        return gr::pmt::Value(std::string_view(old.to_symbol()));
    } else if (old.is_pair()) {
        std::vector<gr::pmt::Value> items;
        items.reserve(2);
        items.emplace_back(to_new_pmt(*old.car()));
        items.emplace_back(to_new_pmt(*old.cdr()));
        return gr::pmt::Value(gr::Tensor<gr::pmt::Value>(items));
    } else if (old.is_vector()) {
        std::vector<gr::pmt::Value> vec;
        for (const auto& item : old.to_vector()) {
            vec.push_back(to_new_pmt(*item));
        }
        return gr::pmt::Value(gr::Tensor<gr::pmt::Value>(vec));
    } else if (old.is_dict()) {
        gr::pmt::Value::Map m{std::pmr::get_default_resource()};
        for (const auto& [k, v] : old.to_dict()) {
            if (!k || !k->is_symbol()) {
                throw std::runtime_error("Legacy PMT dict key is not a symbol");
            }
            m.emplace(std::pmr::string(k->to_symbol(), std::pmr::get_default_resource()), to_new_pmt(*v));
        }
        return gr::pmt::Value(std::move(m));
    } else {
        throw std::runtime_error("Unsupported legacy PMT type");
    }
}

inline std::shared_ptr<legacy::pmt_t> to_legacy_pmt(const gr::pmt::Value& obj) {
    if (obj.holds<bool>()) {
        return legacy::pmt_t::make_bool(obj.value_or<bool>(false));
    }
    if (obj.holds<std::int64_t>()) {
        return legacy::pmt_t::make_int(obj.value_or<std::int64_t>(0));
    }
    if (obj.holds<std::int32_t>()) {
        return legacy::pmt_t::make_int(static_cast<std::int64_t>(obj.value_or<std::int32_t>(0)));
    }
    if (obj.is_string()) {
        return legacy::pmt_t::make_symbol(std::string(obj.value_or(std::string_view{})));
    }
    if (auto tensor = obj.get_if<gr::Tensor<gr::pmt::Value>>()) {
        std::vector<std::shared_ptr<legacy::pmt_t>> vec;
        vec.reserve(tensor->size());
        for (const auto& v : *tensor) {
            vec.push_back(to_legacy_pmt(v));
        }
        return legacy::pmt_t::make_vector(vec);
    }
    if (auto map = obj.get_if<gr::pmt::Value::Map>()) {
        legacy::pmt_dict dict;
        for (const auto& [k, v] : *map) {
            dict.emplace(legacy::pmt_t::make_symbol(std::string(k)), to_legacy_pmt(v));
        }
        return legacy::pmt_t::make_dict(dict);
    }
    throw std::runtime_error("Unsupported PMT type for legacy conversion");
}

}
