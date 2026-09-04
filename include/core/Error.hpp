#pragma once

#include <stdexcept>

namespace opir {

/// The single exception type this program throws: an unreadable file, a row
/// that will not parse, or a time outside the data all surface as an Error.
/// Inherits std::runtime_error's constructors unchanged.
struct Error : std::runtime_error {
    using std::runtime_error::runtime_error;
};

} // namespace opir
