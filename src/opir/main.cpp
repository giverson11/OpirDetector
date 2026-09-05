#include "sim/Frame.hpp"
#include "sim/SceneSimulator.hpp"
#include <exception>
#include <print>
#include <string_view>
#include <sys/types.h>
#include <vector>

namespace opir {
namespace {
const size_t ROWS = 100;
const size_t COLUMNS = 100;

int run(std::vector<std::string_view> arguments) {
    std::vector<uint16_t> buffer(ROWS * COLUMNS);
    SceneSimulator simulator(ROWS, COLUMNS,
                             SceneParams{.mean = 0,
                                         .fpn_sigma = 15.0,
                                         .read_sigma = 8.0,
                                         .dc_level = 10000,
                                         .row_gradient = 3.0},
                             42);
    simulator.render(0.0, buffer);
    return 0;
}
} // namespace
} // namespace opir
int main(int argc, char **argv) {
    const std::vector<std::string_view> arguments(argv + 1, argv + argc);
    try {
        return opir::run(arguments);
    } catch (const std::exception &error) {
        return 1;
    }
    return 0;
}
