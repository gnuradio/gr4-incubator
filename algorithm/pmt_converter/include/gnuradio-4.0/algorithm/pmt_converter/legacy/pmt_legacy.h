// legacy/pmt_legacy.h
#pragma once

#include <variant>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <stdexcept>
#include <cstdint>
#include <iostream>

namespace legacy {

struct pmt_t;  // forward declaration

using pmt_pair = std::pair<std::shared_ptr<pmt_t>, std::shared_ptr<pmt_t>>;
using pmt_vector = std::vector<std::shared_ptr<pmt_t>>;
using pmt_dict = std::map<std::shared_ptr<pmt_t>, std::shared_ptr<pmt_t>>;

class pmt_t {
public:
    using variant_t = std::variant<
        bool,
        int64_t,
        std::string,     // for symbols
        pmt_pair,
        pmt_vector,
        pmt_dict>;

    pmt_t() = default;
    explicit pmt_t(const variant_t& val) : _val(val) {}
    explicit pmt_t(variant_t&& val) : _val(std::move(val)) {}

    // Factory functions
    static std::shared_ptr<pmt_t> make_bool(bool b) {
        return std::make_shared<pmt_t>(b);
    }

    static std::shared_ptr<pmt_t> make_int(int64_t i) {
        return std::make_shared<pmt_t>(i);
    }

    static std::shared_ptr<pmt_t> make_symbol(const std::string& s) {
        return std::make_shared<pmt_t>(s);
    }

    static std::shared_ptr<pmt_t> make_pair(std::shared_ptr<pmt_t> car, std::shared_ptr<pmt_t> cdr) {
        return std::make_shared<pmt_t>(pmt_pair{car, cdr});
    }

    static std::shared_ptr<pmt_t> make_vector(const pmt_vector& vec) {
        return std::make_shared<pmt_t>(vec);
    }

    static std::shared_ptr<pmt_t> make_dict(const pmt_dict& d) {
        return std::make_shared<pmt_t>(d);
    }

    // Type checkers
    bool is_bool() const    { return std::holds_alternative<bool>(_val); }
    bool is_int() const     { return std::holds_alternative<int64_t>(_val); }
    bool is_symbol() const  { return std::holds_alternative<std::string>(_val); }
    bool is_pair() const    { return std::holds_alternative<pmt_pair>(_val); }
    bool is_vector() const  { return std::holds_alternative<pmt_vector>(_val); }
    bool is_dict() const    { return std::holds_alternative<pmt_dict>(_val); }

    // Accessors
    bool to_bool() const    { return std::get<bool>(_val); }
    int64_t to_int() const  { return std::get<int64_t>(_val); }
    std::string to_symbol() const { return std::get<std::string>(_val); }

    std::shared_ptr<pmt_t> car() const { return std::get<pmt_pair>(_val).first; }
    std::shared_ptr<pmt_t> cdr() const { return std::get<pmt_pair>(_val).second; }

    const pmt_vector& to_vector() const { return std::get<pmt_vector>(_val); }
    const pmt_dict& to_dict() const     { return std::get<pmt_dict>(_val); }

    // Optional: equality check
    bool operator==(const pmt_t& other) const {
        return _val == other._val;
    }

private:
    variant_t _val;
};

inline std::ostream& operator<<(std::ostream& os, const std::shared_ptr<pmt_t>& pmt) {
    if (!pmt) return os << "<null>";
    if (pmt->is_bool())    return os << (pmt->to_bool() ? "true" : "false");
    if (pmt->is_int())     return os << pmt->to_int();
    if (pmt->is_symbol())  return os << "\"" << pmt->to_symbol() << "\"";
    if (pmt->is_pair())    return os << "(" << pmt->car() << " . " << pmt->cdr() << ")";
    if (pmt->is_vector()) {
        os << "[";
        for (const auto& item : pmt->to_vector())
            os << item << " ";
        return os << "]";
    }
    if (pmt->is_dict()) {
        os << "{";
        for (const auto& [k, v] : pmt->to_dict())
            os << k << ": " << v << ", ";
        return os << "}";
    }
    return os << "<?>";
}

} // namespace legacy
