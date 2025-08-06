#pragma once
#include <type_traits>
#include <complex>
#include <vector>

namespace gr::zeromq {

template <typename T>
struct is_arithmetic_or_complex
    : std::bool_constant<
        std::is_arithmetic_v<T> ||
        std::is_same_v<T, std::complex<float>> ||
        std::is_same_v<T, std::complex<double>> 
    > {};

template <typename T>
inline constexpr bool is_arithmetic_or_complex_v = is_arithmetic_or_complex<T>::value;

template <typename T>
struct is_vector_of_arithmetic_or_complex : std::false_type {};

template <typename Elem, typename Alloc>
struct is_vector_of_arithmetic_or_complex<std::vector<Elem, Alloc>>
    : is_arithmetic_or_complex<Elem> {};

template <typename T>
inline constexpr bool is_vector_of_arithmetic_or_complex_v = is_vector_of_arithmetic_or_complex<T>::value;


template <typename T>
concept ArithmeticOrComplex = is_arithmetic_or_complex_v<T>;

template <typename T>
concept VectorOfArithmeticOrComplex = is_vector_of_arithmetic_or_complex_v<T>;


}