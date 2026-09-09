
#include "sim/TruthReader.hpp"
#include "core/Io.hpp"
#include <charconv>
namespace opir {

namespace {
std::string_view trim(std::string_view sv) {
    constexpr std::string_view ws = " \t\r\n";
    const auto b = sv.find_first_not_of(ws);
    if (b == std::string_view::npos)
        return {};
    return sv.substr(b, sv.find_last_not_of(ws) - b + 1);
}

// Pops the next comma-separated field off the front of `rest`.
std::string_view next_field(std::string_view &rest) {
    const auto comma = rest.find(',');
    const auto field = rest.substr(0, comma);
    rest = (comma == std::string_view::npos) ? std::string_view{}
                                             : rest.substr(comma + 1);
    return field;
}
} // namespace
TruthReader::TruthReader(std::filesystem::path path)
    : in_{open_for_read(path)} {}

///
/// Reads the next frame, or reports why it could not.
///
/// \return EndOfStream once the file is exhausted, which is the normal way
///         a read loop finishes rather than a failure.
///
std::expected<std::span<const TruthRecord>, ParseError> TruthReader::next() {}
} // namespace opir
