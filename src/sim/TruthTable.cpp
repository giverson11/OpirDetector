#include "sim/TruthTable.hpp"
#include "core/Io.hpp"
#include "core/ParseError.hpp"
#include "core/Types.hpp"
#include "sim/SceneSimulator.hpp"

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <expected>
#include <filesystem>
#include <fstream>
#include <numeric>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace opir {
namespace {

// '\r' is in here so a file written on Windows loses its line ending along
// with the padding, rather than carrying it into the last field.
constexpr std::string_view kBlank = " \t\r";

std::string_view lstrip(std::string_view text) {
    const std::size_t i = text.find_first_not_of(kBlank);
    return i == std::string_view::npos ? std::string_view{} : text.substr(i);
}

std::string_view rstrip(std::string_view text) {
    const std::size_t i = text.find_last_not_of(kBlank);
    return i == std::string_view::npos ? std::string_view{}
                                       : text.substr(0, i + 1);
}

///
/// Reads one field and hands back what follows it.
///
/// \param rest
/// \param out written only on success
/// \return the remainder of the line, or nothing if no field was there
///
template <class T>
std::optional<std::string_view> take_field(std::string_view rest, T &out) {
    rest = lstrip(rest);
    const char *const first = rest.data();
    const auto [ptr, ec] = std::from_chars(first, first + rest.size(), out);
    if (ec != std::errc{}) {
        return std::nullopt;
    }
    return rest.substr(static_cast<std::size_t>(ptr - first));
}

std::optional<std::string_view> take_comma(std::string_view rest) {
    rest = lstrip(rest);
    if (rest.empty() || rest.front() != ',') {
        return std::nullopt;
    }
    return rest.substr(1);
}

std::expected<TruthRecord, ParseError> parse_line(std::string_view line) {
    TruthRecord record{};
    std::string_view rest = line;

    const auto field = [&rest](auto &out) {
        const auto next = take_field(rest, out);
        return next ? (rest = *next, true) : false;
    };
    const auto comma = [&rest] {
        const auto next = take_comma(rest);
        return next ? (rest = *next, true) : false;
    };

    // Reads into record while checking if each line fits expected formatting
    if (!field(record.frame_id) || !comma() || !field(record.target_id) ||
        !comma() || !field(record.row) || !comma() || !field(record.col) ||
        !comma() || !field(record.amplitude)) {
        return std::unexpected(ParseError::MalformedRecord);
    }
    // Checks for leftover garbage that doesnt fit formatting
    if (!lstrip(rest).empty()) {
        return std::unexpected(ParseError::MalformedRecord);
    }
    return record;
}

} // namespace

std::expected<TruthTable, ParseError>
TruthTable::load(const std::filesystem::path &path) {
    std::ifstream in = open_for_read(path);

    TruthTable table;
    std::string line;
    while (std::getline(in, line)) {
        const std::string_view text = rstrip(lstrip(line));
        // A file that ends on a newline is normal, and so is the blank line a
        // hand-edited table picks up; neither is a record.
        if (text.empty()) {
            continue;
        }

        const auto record = parse_line(text);
        if (!record) {
            return std::unexpected(record.error());
        }
        if (record->frame_id > kMaxTruthFrameId) {
            return std::unexpected(ParseError::MalformedRecord);
        }
        table.records_.push_back(*record);
    }

    if (table.records_.empty()) {
        return table;
    }

    // Nothing promises the file arrives in order, and the index below counts
    // on it: a frame's records have to be one contiguous run.
    std::ranges::sort(table.records_, {}, [](const TruthRecord &r) {
        return std::pair{r.frame_id, r.target_id};
    });

    // Counting sort into a prefix sum. Frames the file never mentions are
    // simply never counted, so their two offsets come out equal.
    const std::size_t frames =
        static_cast<std::size_t>(table.records_.back().frame_id) + 2;
    table.frame_offsets_.assign(frames, 0);
    for (const TruthRecord &record : table.records_) {
        ++table.frame_offsets_[static_cast<std::size_t>(record.frame_id) + 1];
    }
    std::inclusive_scan(table.frame_offsets_.begin(),
                        table.frame_offsets_.end(),
                        table.frame_offsets_.begin());

    return table;
}

std::span<const TruthRecord> TruthTable::view_at(FrameId frame) const {
    const std::size_t f = static_cast<std::size_t>(frame);
    if (f + 1 >= frame_offsets_.size()) {
        return {};
    }
    return std::span{records_}.subspan(
        frame_offsets_[f], frame_offsets_[f + 1] - frame_offsets_[f]);
}

} // namespace opir
