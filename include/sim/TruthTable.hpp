#pragma once

#include "core/ParseError.hpp"
#include "core/Types.hpp"
#include "sim/SceneSimulator.hpp"

#include <cstddef>
#include <expected>
#include <filesystem>
#include <span>
#include <vector>

namespace opir {

/// The largest frame id a truth file may name. The index built here is one
/// slot per frame, so without a ceiling a corrupt id would size an allocation
/// from the file's own contents.
inline constexpr FrameId kMaxTruthFrameId = 1'000'000;

///
/// A truth table, grouped by frame. Everthing is read at once to get proper
/// indexing and the nature of csv content will be less memory intensive then
/// storing frame data
///
///
class TruthTable {
    std::vector<TruthRecord> records_;
    std::vector<std::size_t> frame_offsets_;

    /// Only load() builds one, so TruthTable is read fully on load.
    TruthTable() = default;

  public:
    ///
    /// Reads and indexes an entire truth file.
    ///
    /// \param path
    /// \throws Error if the path cannot be opened, matching the other readers:
    ///         a typo'd path is a caller's mistake, not a parse result
    /// \return MalformedRecord if any line is not five parseable fields, or
    ///         names a frame beyond kMaxTruthFrameId
    ///
    [[nodiscard]] static std::expected<TruthTable, ParseError>
    load(const std::filesystem::path &path);

    ///
    /// The records for one frame, in target id order.
    ///
    /// \param frame
    /// \return empty for a frame the file does not mention, which is what a
    ///         frame with every target out of view looks like
    ///
    [[nodiscard]] std::span<const TruthRecord> view_at(FrameId frame) const;

    /// Every record, ordered by frame and then by target.
    [[nodiscard]] std::span<const TruthRecord> view_all() const {
        return records_;
    }

    ///
    /// One past the highest frame id present, so a `for (FrameId f = 0; f <
    /// frame_count(); ++f)` walks the whole table.
    ///
    /// \return 0 for an empty file
    ///
    [[nodiscard]] FrameId frame_count() const {
        return frame_offsets_.empty()
                   ? 0
                   : static_cast<FrameId>(frame_offsets_.size() - 1);
    }
};

} // namespace opir
