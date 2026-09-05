#include <exception>
#include <print>
#include <string_view>
#include <sys/types.h>
#include <vector>

namespace opir {
namespace {

int run(std::vector<std::string_view> arguments) { return 0; }
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
