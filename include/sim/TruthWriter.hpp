#pragma once

#include "sim/SceneSimulator.hpp"
#include <filesystem>
#include <fstream>
#include <span>

namespace opir {

class TruthWriter {
    std::ofstream out_;

  public:
    TruthWriter(std::filesystem::path path);
    void write_truth(std::span<const TruthRecord> records);
};

} // namespace opir
