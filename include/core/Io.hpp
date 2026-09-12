#pragma once

#include "core/Error.hpp"
#include "core/ParseError.hpp"

#include <cstddef>
#include <expected>
#include <filesystem>
#include <format>
#include <fstream>
#include <ios>
#include <istream>
#include <ostream>
#include <span>
#include <string_view>

namespace opir {

///
/// \brief Opens a file for writing, or throws naming the path that failed.
///
/// \param path
/// \param mode
/// \return
///
[[nodiscard]] inline std::ofstream
open_for_write(const std::filesystem::path &path,
               std::ios::openmode mode = {}) {
    std::ofstream out{path, mode | std::ios::out};
    if (!out) {
        throw Error(std::format("cannot open '{}' for writing", path.string()));
    }
    return out;
}

///
/// \brief Opens a file for reading, or throws naming the path that failed.
///
/// Without this a missing file yields a stream that reads as an empty one, so
/// a typo'd path looks identical to a zero-frame capture.
///
/// \param path
/// \param mode
/// \return
///
[[nodiscard]] inline std::ifstream
open_for_read(const std::filesystem::path &path, std::ios::openmode mode = {}) {
    std::ifstream in{path, mode | std::ios::in};
    if (!in) {
        throw Error(std::format("cannot open '{}' for reading", path.string()));
    }
    return in;
}

///
/// \brief Throws if a stream has failed since it was last checked.
///
/// Streams swallow their errors: a full disk sets badbit inside write() and
/// every later call becomes a silent no-op. Needed for the operator<< path,
/// which has no return value to inspect.
///
/// \param out
/// \param what
///
inline void throw_if_failed(const std::ostream &out, std::string_view what) {
    if (!out) {
        throw Error(std::format("failed while writing {}", what));
    }
}

///
/// Writes bytes to a buffer in binary, now error logic incase of exception use
///
/// \param out
/// \param bytes
///
inline void write_bytes(std::ostream &out, std::span<const std::byte> bytes) {
    out.write(reinterpret_cast<const char *>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
}

///
/// \brief Reads up to bytes.size() bytes, returning how many actually arrived.
///
/// Unlike write_bytes this never throws: reaching the end of a stream is normal
/// control flow for a reader, not a failure, so the caller decides what it
/// means. The count is the only way to tell a clean end from a truncated one --
/// istream::read sets failbit for both, so the stream's own state cannot.
///
/// \param in
/// \param bytes
/// \return bytes read: 0 at a clean end, less than requested if truncated
///
[[nodiscard]] inline std::size_t read_bytes(std::istream &in,
                                            std::span<std::byte> bytes) {
    in.read(reinterpret_cast<char *>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
    return static_cast<std::size_t>(in.gcount());
}

///
/// \brief Fills `bytes` exactly, or says why it could not.
///
/// \param in
/// \param bytes
/// \return nothing on success, EndOfStream at a clean end, ShortRead if the
///         stream ended part way through
///
[[nodiscard]] inline std::expected<void, ParseError>
read_exact(std::istream &in, std::span<std::byte> bytes) {
    const std::size_t got = read_bytes(in, bytes);
    if (got == bytes.size()) {
        return {};
    }
    if (got == 0) {
        return std::unexpected(ParseError::EndOfStream);
    }
    return std::unexpected(ParseError::ShortRead);
}

} // namespace opir
