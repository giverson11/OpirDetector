#include "sim/Frame.hpp"
#include "sim/SceneSimulator.hpp"
#include <exception>
#include <string_view>
#include <sys/types.h>
#include <vector>

namespace scitec {
namespace {

int run(std::vector<std::string_view> arguments) {
    SceneSimulator simulator(100, 100,
                             SceneParams{.mean = 0,
                                         .fpn_sigma = 15.0,
                                         .read_sigma = 8.0,
                                         .dc_level = 10000,
                                         .row_gradient = 3.0},
                             42);
    return 0;
}
} // namespace
} // namespace scitec
int main(int argc, char **argv) {
    const std::vector<std::string_view> arguments(argv + 1, argv + argc);
    try {
        return scitec::run(arguments);
    } catch (const std::exception &error) {
        return 1;
    }
    return 0;
}
