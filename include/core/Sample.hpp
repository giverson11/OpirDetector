#pragma once

namespace opir {

/// Pairs a value with the time it applies to.
///
/// Templated in order to be reusable across multiple data types and contexts.
///
/// \tparam T The value type carried alongside the timestamp.
template <typename T> struct Sample {
    double timeSeconds{0.0}; ///< Seconds.
    T value{};               ///< The value observed or computed at that time.
};

} // namespace opir
