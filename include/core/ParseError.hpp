#pragma once

#include <string_view>
#include <format>
namespace opir {

enum class ParseError {
    EndOfStream, // clean EOF — normal loop termination, not a failure
    ShortRead,   // file ended mid-header or mid-payload — truncated
    BadMagic,    // magic didn't match — wrong format or desynced
    UnsupportedVersion,
    BadDimensions, // rows/cols zero or implausibly large

};

constexpr std::string_view to_string(ParseError e) {
    switch (e) {
    case ParseError::EndOfStream:
        return "end of stream";
    case ParseError::ShortRead:
        return "file ended mid payload";
    case ParseError::BadMagic:
        return "bad magic number";
    case ParseError::UnsupportedVersion:
        return "Invalid version";
    case ParseError::BadDimensions:
        return "Dimension of frame are invalid";
        // ...
    }
    return "unknown";
}

}; // namespace opir

template <>
struct std::formatter<opir::ParseError> : std::formatter<std::string_view> {
    auto format(opir::ParseError e, auto& ctx) const {
        return std::formatter<std::string_view>::format(to_string(e), ctx);
    }
};
