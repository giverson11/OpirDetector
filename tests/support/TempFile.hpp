#pragma once

#include "core/Types.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <string_view>
#include <vector>

namespace opir::test {

/// A unique path under the system temp directory that deletes itself again when
/// the object goes out of scope. The file is deliberately *not* created here:
/// creating it is the writer's job, so a test can tell "never opened" apart
/// from "opened and left empty".
class TempFile {
    std::filesystem::path path_;

  public:
    explicit TempFile(std::string_view extension) {
        // Seeded once per process so two test binaries running side by side in
        // the same temp directory cannot collide.
        static std::atomic<unsigned long long> counter{std::random_device{}()};
        path_ = std::filesystem::temp_directory_path() /
                ("opir_test_" + std::to_string(counter++) +
                 std::string(extension));
        std::error_code ec;
        std::filesystem::remove(path_, ec);
    }

    ~TempFile() {
        std::error_code ec;
        std::filesystem::remove(path_, ec);
    }

    TempFile(const TempFile &) = delete;
    TempFile &operator=(const TempFile &) = delete;

    const std::filesystem::path &path() const { return path_; }

    bool exists() const { return std::filesystem::exists(path_); }

    /// Size in bytes, or 0 if the file was never created.
    std::uintmax_t size() const {
        std::error_code ec;
        const auto n = std::filesystem::file_size(path_, ec);
        return ec ? 0u : n;
    }

    /// The whole file, verbatim.
    std::vector<std::byte> bytes() const {
        std::ifstream in{path_, std::ios::binary};
        if (!in)
            return {};
        std::vector<std::byte> out(static_cast<std::size_t>(size()));
        in.read(reinterpret_cast<char *>(out.data()),
                static_cast<std::streamsize>(out.size()));
        out.resize(static_cast<std::size_t>(in.gcount()));
        return out;
    }

    /// The whole file as text, for the line-oriented formats.
    std::string text() const {
        std::ifstream in{path_};
        if (!in)
            return {};
        return std::string{std::istreambuf_iterator<char>{in},
                           std::istreambuf_iterator<char>{}};
    }

    /// The whole file reinterpreted as a run of Pixels. Any trailing bytes that
    /// do not fill a whole Pixel are dropped, and the caller is expected to
    /// check the raw size when that matters.
    std::vector<Pixel> pixels() const {
        const std::vector<std::byte> raw = bytes();
        std::vector<Pixel> out(raw.size() / sizeof(Pixel));
        if (!out.empty())
            std::memcpy(out.data(), raw.data(), out.size() * sizeof(Pixel));
        return out;
    }
};

/// A path whose parent directory does not exist, so every attempt to open it
/// for writing must fail. This is what "a bad path" means in these tests.
inline std::filesystem::path unopenable_path() {
    return std::filesystem::temp_directory_path() /
           "opir_no_such_directory_9d3f1a" / "out.dat";
}

} // namespace opir::test
