#include <cstddef>
#include <cstdint>
#include <random>
#include <span>
#include <vector>

namespace scitec {
struct Target {
    double r0, c0, r_rate, c_rate, amplitude, sigma;
    double row(double t) const { return r0 + r_rate * t; }
    double col(double t) const { return c0 + c_rate * t; }
};

struct SceneParams {
    double mean;
    double fpn_sigma;
    double read_sigma;
    double dc_level;
    double row_gradient;
};

class SceneSimulator {
  public:
    ///
    ///  Construct a new Scene Simulator object
    ///
    /// \param rows
    /// \param columns
    /// \param params
    /// \param seed
    ///
    SceneSimulator(std::size_t rows, std::size_t columns, SceneParams params,
                   uint64_t seed);
    ///
    ///  Adds a target to the simulator
    ///
    /// \param target
    ///
    void add_target(Target target);
    ///
    /// Renders image to a given spannable collection at a given time
    /// Applies noise, a row gradient, background level and targets to the
    /// outputed image.
    ///
    /// \param t
    /// \param out
    ///
    void render(double t, std::span<uint16_t> out);

  private:
    size_t rows_;
    size_t columns_;
    std::mt19937_64 rng_;
    std::vector<double> fixed_pattern_;
    SceneParams params_;
    std::normal_distribution<double> read_noise_;
    std::vector<Target> targets_;
};
} // namespace scitec