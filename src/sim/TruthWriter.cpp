
#include "sim/TruthWriter.hpp"
#include "core/Error.hpp"
#include "sim/SceneSimulator.hpp"
#include <cstddef>
#include <format>

namespace opir {

TruthWriter::TruthWriter(std::filesystem::path path) : out_{path} {
    if (!out_) {
        throw Error("Truthwriter is cannot be initialized with null filepath");
    }
}

void TruthWriter::write_truth(std::span<const TruthRecord> records) {
    if (!out_) {
        throw Error("Trying to write with null filepath.");
    }
    for (const auto &record : records) {
        out_ << std::format("{}, {}, {}, {}, {}\n", record.frame_id,
                            record.target_id, record.row, record.col,
                            record.amplitude);
    }
}
} // namespace opir
