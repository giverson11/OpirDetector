#include "frame/FrameReader.hpp"
#include "core/Io.hpp"
#include "core/ParseError.hpp"
#include "core/Types.hpp"
#include "frame/Frame.hpp"

#include <cstddef>
#include <expected>
#include <ios>
#include <span>

namespace opir {
namespace {

std::expected<void, ParseError> validate(const FrameHeader &header, size_t rows,
                                         size_t cols) {
    if (header.magic != kFrameMagic) {
        return std::unexpected(ParseError::BadMagic);
    }
    if (header.version != kFrameVersion) {
        return std::unexpected(ParseError::UnsupportedVersion);
    }
    if (header.rows == 0 || header.cols == 0 || header.rows > kMaxFrameDim ||
        header.cols > kMaxFrameDim) {
        return std::unexpected(ParseError::BadDimensions);
    }
    // Every frame in a file comes off the same detector, so a shape that
    // changes mid-stream means the file is spliced, not that it is interesting.
    if (rows != 0 && (header.rows != rows || header.cols != cols)) {
        return std::unexpected(ParseError::BadDimensions);
    }
    return {};
}

} // namespace

FrameReader::FrameReader(std::filesystem::path path)
    : in_{open_for_read(path, std::ios::binary)} {}

std::expected<FrameView, ParseError> FrameReader::next() {
    FrameHeader header;
    if (const auto got =
            read_exact(in_, std::as_writable_bytes(std::span{&header, 1}));
        !got) {
        return std::unexpected(got.error());
    }

    if (const auto ok = validate(header, rows_, cols_); !ok) {
        return std::unexpected(ok.error());
    }

    rows_ = header.rows;
    cols_ = header.cols;
    buffer_.resize(rows_ * cols_);

    if (const auto got =
            read_exact(in_, std::as_writable_bytes(std::span{buffer_}));
        !got) {
        // A header with no pixels behind it is a truncated file, never a clean
        // end: the clean end is a stream that stops on a frame boundary.
        return std::unexpected(ParseError::ShortRead);
    }

    return FrameView{.id = header.frame_id,
                     .t = static_cast<double>(header.timestamp_us) * 1e-6,
                     .px = FrameSpan{buffer_.data(), rows_, cols_}};
}

} // namespace opir
