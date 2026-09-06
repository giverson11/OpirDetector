#include "frame/FrameReader.hpp"
#include "core/ParseError.hpp"
#include "frame/Frame.hpp"
#include <cstddef>
#include <expected>
#include <ios>

namespace opir {
FrameReader::FrameReader(std::filesystem::path path, size_t rows, size_t cols)
    : in_{path}, buffer_(rows * cols), rows_{rows}, cols_{cols} {}

std::expected<FrameView, ParseError> FrameReader::next() {
    FrameHeader h;
    if (in_.eof())
        return std::unexpected(ParseError::EndOfStream);
    if (!in_)
        return std::unexpected(ParseError::ShortRead);

    in_.read(reinterpret_cast<char *>(buffer_.data()),
             static_cast<std::streamsize>(buffer_.size()) *
                 static_cast<std::streamsize>(sizeof(Pixel)));
    if (!in_)
        return std::unexpected(ParseError::ShortRead);

    return FrameView{.id = h.frame_id,
                     .t = h.timestamp_us * 1e-6,
                     .px = std::mdspan<const Pixel, std::dextents<size_t, 2>>{
                         buffer_.data(), h.rows, h.cols}};
}
} // namespace opir
