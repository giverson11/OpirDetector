#include <cstdio>
#include <exception>
#include <print>
#include <format>
#include <string_view>
#include <sys/types.h>
#include <vector>
#include "sim/TruthWriter.hpp"
#include "frame/FrameReader.hpp"
#include "detect/Cfar.hpp"
#include "detect/Cluster.hpp"
#include "core/Types.hpp"

#ifndef SCENE_DATA_FILE
#error "Scene file must be defined by the build system"
#endif

#ifndef TRUTH_CSV_FILE
#error "Truth csv file must be defined by the build system"
#endif

namespace opir {
namespace {

int run(std::vector<std::string_view> arguments) {

    FrameReader frameData{SCENE_DATA_FILE};

    auto data = frameData.next();
    if(!data){
        std::println(stderr, "{}", data.error());
        return 2;
    }

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
