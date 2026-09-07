#include "sim/TruthWriter.hpp"
#include "core/Io.hpp"
#include "sim/SceneSimulator.hpp"

#include <cstddef>
#include <format>
#include <span>

namespace opir {

TruthWriter::TruthWriter(std::filesystem::path path)
    : out_{open_for_write(path)} {}

void TruthWriter::write_truth(std::span<const TruthRecord> records) {
    for (const auto &record : records) {
        out_ << std::format("{}, {}, {}, {}, {}\n", record.frame_id,
                            record.target_id, record.row, record.col,
                            record.amplitude);
    }
    throw_if_failed(out_, "truth records");
}

} // namespace opir
